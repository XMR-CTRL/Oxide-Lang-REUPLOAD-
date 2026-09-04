// ProofSplitter.cpp  -  goal splitting + domain-aware tactic selection.
//
// Implementation of features 1 (splitGoal) and 2 (selectTactic) from the
// verification-tactic spec. See ProofSplitter.h for the design and the
// soundness argument for each of the 5 split strategies.
//
// The splitter operates at the AST (Expr*) level BEFORE SMT lowering. The
// emitted SMT shape per sub-goal is the legacy (push/assert-premises/assert-
// (not term)/check-sat-using <tactic>/pop), but each sub-goal is emitted with
// its OWN `\; <sublabel> (source line N)` header so the verify-report parser
// turns each sub-goal into its own VerifyRow. The parent clause is then proven
// iff ALL its sub-rows are `unsat`  -  exactly the required "report unsat only
// if ALL sub-goals unsat" semantics, with no report-aggregator change needed:
// any non-unsat sub-row fails the parent, visible in the report.

#include "ProofSplitter.h"
#include "AST.h"
#include "Smt.h"

#include <string>
#include <vector>
#include <memory>

namespace ox_smt {

// Small AST-only helpers.

// Does `e` contain a QuantExpr anywhere in its sub-tree? If not, the goal is
// GROUND (quantifier-free)  -  the decidable fragments (QF_BV, ground Simplify)
// can decide it.
static bool exprContainsQuantifier(const Expr* e) {
  if (!e) return false;
  if (dynamic_cast<const QuantExpr*>(e)) return true;
  if (auto b = dynamic_cast<const BinaryExpr*>(e))
    return exprContainsQuantifier(b->lhs.get()) ||
           exprContainsQuantifier(b->rhs.get());
  if (auto u = dynamic_cast<const UnaryExpr*>(e))
    return exprContainsQuantifier(u->base.get());
  if (auto t = dynamic_cast<const TernaryExpr*>(e))
    return exprContainsQuantifier(t->cond.get()) ||
           exprContainsQuantifier(t->thenE.get()) ||
           exprContainsQuantifier(t->elseE.get());
  if (auto c2 = dynamic_cast<const CastExpr*>(e))
    return exprContainsQuantifier(c2->e.get());
  if (auto o = dynamic_cast<const OldExpr*>(e))
    return exprContainsQuantifier(o->sub.get());
  if (auto idx = dynamic_cast<const Index*>(e))
    return exprContainsQuantifier(idx->base.get()) ||
           exprContainsQuantifier(idx->index.get());
  if (auto f = dynamic_cast<const Field*>(e))
    return exprContainsQuantifier(f->base.get());
  if (auto call = dynamic_cast<const Call*>(e)) {
    for (auto& a : call->args)
      if (exprContainsQuantifier(a.get())) return true;
  }
  if (auto m = dynamic_cast<const MethodCall*>(e)) {
    if (exprContainsQuantifier(m->receiver.get())) return true;
    for (auto& a : m->args)
      if (exprContainsQuantifier(a.get())) return true;
  }
  return false;
}

// Does `e` perform integer mod or div? Routes the goal to NIA when not ground.
static bool exprHasModOrDiv(const Expr* e) {
  if (!e) return false;
  if (auto b = dynamic_cast<const BinaryExpr*>(e)) {
    if (b->op == BinaryExpr::Op::mod || b->op == BinaryExpr::Op::div) return true;
    return exprHasModOrDiv(b->lhs.get()) || exprHasModOrDiv(b->rhs.get());
  }
  if (auto u = dynamic_cast<const UnaryExpr*>(e))
    return exprHasModOrDiv(u->base.get());
  if (auto t = dynamic_cast<const TernaryExpr*>(e))
    return exprHasModOrDiv(t->cond.get()) ||
           exprHasModOrDiv(t->thenE.get()) ||
           exprHasModOrDiv(t->elseE.get());
  if (auto c2 = dynamic_cast<const CastExpr*>(e))
    return exprHasModOrDiv(c2->e.get());
  return false;
}

// ox:proof Does the lowered SMT term string contain BitVec operations? The goal AST may
// reference BV-typed symbols only via VarRef; after lowering we can spot the
// bv* ops the emitter produces. Used as the `hasBV` signal for tactic selection.
static bool smtTermHasBV(const std::string& s) {
  static const char* kBvOps[] = {
    "bvand", "bvor", "bvxor", "bvshl", "bvlshr", "bvashr",
    "bvadd", "bvsub", "bvmul", "bvult", "bvule", "bvslt", "bvsle",
    "bvnot", "bvneg", "(_ bv", "(_ BitVec"
  };
  for (const char* op : kBvOps)
    if (s.find(op) != std::string::npos) return true;
  return false;
}

// Deep-clone `e` substituting every VarRef whose name == `binder` with a fresh
// IntLit{value}. Used by the unroll split to produce a GROUND body instance at
// k=value. The returned tree is self-owned (ExprPtr) and must outlive any
// ProofGoal that borrows its .get().
static ExprPtr substBinder(const Expr* e, const std::string& binder, long long value) {
  if (!e) return nullptr;
  if (auto v = dynamic_cast<const VarRef*>(e)) {
    if (v->name == binder) {
      auto lit = std::make_unique<IntLit>();
      lit->v = (uint64_t)value; lit->line = v->line; lit->col = v->col;
      return lit;
    }
    auto c = std::make_unique<VarRef>(); c->name = v->name;
    c->line = v->line; c->col = v->col; return c;
  }
  if (auto i = dynamic_cast<const IntLit*>(e)) {
    auto c = std::make_unique<IntLit>(); c->v = i->v; c->line = i->line; c->col = i->col; return c;
  }
  if (auto fl = dynamic_cast<const FloatLit*>(e)) {
    auto c = std::make_unique<FloatLit>(); c->v = fl->v; c->isF32 = fl->isF32;
    c->line = fl->line; c->col = fl->col; return c;
  }
  if (auto bl = dynamic_cast<const BoolLit*>(e)) {
    auto c = std::make_unique<BoolLit>(); c->v = bl->v; c->line = bl->line; c->col = bl->col; return c;
  }
  if (auto s = dynamic_cast<const StrLit*>(e)) {
    auto c = std::make_unique<StrLit>(); c->v = s->v; c->line = s->line; c->col = s->col; return c;
  }
  if (auto ch = dynamic_cast<const CharLit*>(e)) {
    auto c = std::make_unique<CharLit>(); c->v = ch->v; c->line = ch->line; c->col = ch->col; return c;
  }
  if (auto u = dynamic_cast<const UnaryExpr*>(e)) {
    auto c = std::make_unique<UnaryExpr>(); c->op = u->op;
    c->base = substBinder(u->base.get(), binder, value);
    c->line = u->line; c->col = u->col;
    c->methodOverload = u->methodOverload; c->overloadStruct = u->overloadStruct;
    c->overloadMethod = u->overloadMethod; c->overloadRecvType = u->overloadRecvType;
    c->recvByRef = u->recvByRef;
    return c;
  }
  if (auto b = dynamic_cast<const BinaryExpr*>(e)) {
    auto c = std::make_unique<BinaryExpr>(); c->op = b->op;
    c->lhs = substBinder(b->lhs.get(), binder, value);
    c->rhs = substBinder(b->rhs.get(), binder, value);
    c->line = b->line; c->col = b->col;
    c->methodOverload = b->methodOverload; c->overloadStruct = b->overloadStruct;
    c->overloadMethod = b->overloadMethod; c->overloadRecvType = b->overloadRecvType;
    c->recvByRef = b->recvByRef; c->isPtrArith = b->isPtrArith;
    c->ptrArithPointee = b->ptrArithPointee;
    return c;
  }
  if (auto t = dynamic_cast<const TernaryExpr*>(e)) {
    auto c = std::make_unique<TernaryExpr>();
    c->cond = substBinder(t->cond.get(), binder, value);
    c->thenE = substBinder(t->thenE.get(), binder, value);
    c->elseE = substBinder(t->elseE.get(), binder, value);
    c->resultTy = t->resultTy; c->line = t->line; c->col = t->col;
    return c;
  }
  if (auto c2 = dynamic_cast<const CastExpr*>(e)) {
    auto c = std::make_unique<CastExpr>();
    c->e = substBinder(c2->e.get(), binder, value);
    c->target = c2->target; c->line = c2->line; c->col = c2->col; return c;
  }
  if (auto o = dynamic_cast<const OldExpr*>(e)) {
    auto c = std::make_unique<OldExpr>();
    c->sub = substBinder(o->sub.get(), binder, value);
    c->line = o->line; c->col = o->col; return c;
  }
  if (auto idx = dynamic_cast<const Index*>(e)) {
    auto c = std::make_unique<Index>();
    c->base = substBinder(idx->base.get(), binder, value);
    c->index = substBinder(idx->index.get(), binder, value);
    c->line = idx->line; c->col = idx->col;
    c->methodOverload = idx->methodOverload; c->overloadStruct = idx->overloadStruct;
    c->overloadMethod = idx->overloadMethod; c->overloadRecvType = idx->overloadRecvType;
    c->recvByRef = idx->recvByRef;
    return c;
  }
  if (auto f = dynamic_cast<const Field*>(e)) {
    auto c = std::make_unique<Field>();
    c->base = substBinder(f->base.get(), binder, value);
    c->field = f->field; c->line = f->line; c->col = f->col; return c;
  }
  // Fall back to a deep clone for unhandled forms (Call/MethodCall/Sizeof/
  // Asm/Range/StructLit/...). The binder cannot appear free inside those in a
  // well-typed quantifier body, so an unchanged copy is correct here.
  return cloneExpr(e);
}

// ox:proof Recognise the parse-time desugar of `A implies B` -> `(or (not A) B)`, i.e.
// a BinaryExpr::lor whose lhs is a UnaryExpr::not_. Fills `ante` (= A) and
// `conseq` (= B) on match.
static bool matchDesugaredImplies(const Expr* e, const Expr*& ante, const Expr*& conseq) {
  auto lor = dynamic_cast<const BinaryExpr*>(e);
  if (!lor || lor->op != BinaryExpr::Op::lor) return false;
  auto neg = dynamic_cast<const UnaryExpr*>(lor->lhs.get());
  if (!neg || neg->op != UnaryExpr::Op::not_) return false;
  ante = neg->base.get();
  conseq = lor->rhs.get();
  return true;
}

// ox:proof Parse a literal integer range out of a forall QuantExpr; fills loEff/hiEff
// (exclusive upper for the loop) and returns true iff both bounds are IntLit
// and the binder is integral (mirrors smtUnrollQuantForall's soundness guards).
static bool quantLiteralRange(const QuantExpr* q, long long& loEff, long long& hiEff) {
  if (!q || !q->isForall) return false;
  auto loLit = dynamic_cast<const IntLit*>(q->lo.get());
  auto hiLit = dynamic_cast<const IntLit*>(q->hi.get());
  if (!loLit || !hiLit) return false;
  if (q->binderType.tag != BType::Tag::i64 &&
      q->binderType.tag != BType::Tag::i32 &&
      q->binderType.tag != BType::Tag::u64 &&
      q->binderType.tag != BType::Tag::u32) return false;
  long long lo = (long long)loLit->v;
  long long hi = (long long)hiLit->v;
  loEff = lo;
  hiEff = q->inclusive ? hi + 1 : hi;
  return true;
}

// Sub-label for sub-goal index `i` under parent `label`. The `#s` separator is
// chosen NOT to collide with existing label markers (`_requires_`, `_ensures_`,
// etc.) so the verify-report parser keys each sub-goal as its OWN row.
static std::string subLabel(const std::string& label, int i) {
  return label + "#s" + std::to_string(i);
}

// splitGoal  -  decompose one goal into independently-dischargeable sub-goals.
// 5 strategies, applied in order; the first that matches wins. Each generated
// sub-goal is recursively split (so the splits compose: e.g. an unrolled
// quantifier whose instance body is a land becomes a conjunct split next).
static SplitResult splitGoalImpl(SmtCtx& c, const ProofGoal& g) {
  SplitResult result;
  if (!g.term) {
    result.goals.push_back(g);
    return result;
  }

  // (1) Conjunct split  -  BinaryExpr::Op::land (A && B) -> [A, B].
  if (auto b = dynamic_cast<const BinaryExpr*>(g.term)) {
    if (b->op == BinaryExpr::Op::land) {
      ProofGoal L; L.label = subLabel(g.label, 0); L.term = b->lhs.get();
      L.premises = g.premises; L.isGround = !exprContainsQuantifier(b->lhs.get());
      ProofGoal R; R.label = subLabel(g.label, 1); R.term = b->rhs.get();
      R.premises = g.premises; R.isGround = !exprContainsQuantifier(b->rhs.get());
      auto rl = splitGoalImpl(c, L);
      auto rr = splitGoalImpl(c, R);
      result.goals.insert(result.goals.end(), rl.goals.begin(), rl.goals.end());
      result.goals.insert(result.goals.end(), rr.goals.begin(), rr.goals.end());
      result.owners.insert(result.owners.end(),
                          std::make_move_iterator(rl.owners.begin()),
                          std::make_move_iterator(rl.owners.end()));
      result.owners.insert(result.owners.end(),
                          std::make_move_iterator(rr.owners.begin()),
                          std::make_move_iterator(rr.owners.end()));
      return result;
    }
  }

  // ox:proof (2) Implication split  -  QuantExpr whose body is `A implies (C && D)`:
  // split into two forall goals, each carrying the antecedent and ONE conjunct.
  if (auto q = dynamic_cast<const QuantExpr*>(g.term)) {
    const Expr* ante = nullptr; const Expr* conseq = nullptr;
    if (matchDesugaredImplies(q->body.get(), ante, conseq)) {
      if (auto cand = dynamic_cast<const BinaryExpr*>(conseq)) {
        if (cand->op == BinaryExpr::Op::land) {
          for (int half = 0; half < 2; ++half) {
            auto neg = std::make_unique<UnaryExpr>();
            neg->op = UnaryExpr::Op::not_;
            neg->base = cloneExpr(ante);
            auto lor = std::make_unique<BinaryExpr>();
            lor->op = BinaryExpr::Op::lor;
            lor->lhs = std::move(neg);
            lor->rhs = cloneExpr(half == 0 ? cand->lhs.get() : cand->rhs.get());
            auto nq = std::make_unique<QuantExpr>();
            nq->isForall = true; nq->binder = q->binder; nq->binderType = q->binderType;
            nq->lo = cloneExpr(q->lo.get()); nq->hi = cloneExpr(q->hi.get());
            nq->inclusive = q->inclusive; nq->body = std::move(lor);
            nq->line = q->line; nq->col = q->col;
            const Expr* np = nq.get();
            result.owners.push_back(std::move(nq));
            ProofGoal ng; ng.label = subLabel(g.label, half); ng.term = np;
            ng.premises = g.premises; ng.isGround = false;
            result.goals.push_back(ng);
          }
          return result;
        }
      }
    }
  }

  // ox:proof (3) Quantifier unroll  -  forall QuantExpr with a LITERAL range whose
  // magnitude <= 1024 unrolls into per-instance GROUND goals. Each instance is
  // a synthesised body with the binder substituted by IntLit{k}. Mirrors
  // smtUnrollQuantForall's soundness guards (literal bounds, integral binder,
  // <=1024 cap; empty range = vacuously true).
  if (auto q = dynamic_cast<const QuantExpr*>(g.term)) {
    long long loEff = 0, hiEff = 0;
    if (quantLiteralRange(q, loEff, hiEff)) {
      if (hiEff < loEff) {
        // ox:proof Empty range  -  forall vacuously true; emit a `true` goal.
        auto tru = std::make_unique<BoolLit>(); tru->v = true;
        const Expr* tp = tru.get();
        result.owners.push_back(std::move(tru));
        ProofGoal ng; ng.label = subLabel(g.label, 0); ng.term = tp;
        ng.premises = g.premises; ng.isGround = true;
        result.goals.push_back(ng);
        return result;
      }
      long long count = hiEff - loEff;
      const long long kMaxUnroll = 1024;
      if (count <= kMaxUnroll) {
        int idx = 0;
        for (long long k = loEff; k < hiEff; ++k) {
          auto inst = substBinder(q->body.get(), q->binder, k);
          const Expr* ip = inst.get();
          result.owners.push_back(std::move(inst));
          ProofGoal ng; ng.label = subLabel(g.label, idx++); ng.term = ip;
          ng.premises = g.premises; ng.isGround = true;  // ground instance
          result.goals.push_back(ng);
        }
        return result;
      }
      // ox:proof count > cap: fall through (leave as a leaf quantifier goal).
    }
  }

  // (4) Branch split  -  TernaryExpr `c ? T : E` -> [T under c, E under !c].
  // Borrows the then/else sub-Exprs; only the premise SMT strings are added.
  if (auto t = dynamic_cast<const TernaryExpr*>(g.term)) {
    std::string condPos = smtExpr(c, t->cond.get());
    std::string condNeg = "(not " + condPos + ")";
    ProofGoal T; T.label = subLabel(g.label, 0); T.term = t->thenE.get();
    T.premises = g.premises; T.premises.push_back(condPos);
    T.isGround = !exprContainsQuantifier(t->thenE.get());
    ProofGoal E; E.label = subLabel(g.label, 1); E.term = t->elseE.get();
    E.premises = g.premises; E.premises.push_back(condNeg);
    E.isGround = !exprContainsQuantifier(t->elseE.get());
    result.goals.push_back(T);
    result.goals.push_back(E);
    return result;
  }

  // (5) Premise isolation  -  when the goal carries 2+ premises, try each premise
  // ALONE first (cheap, often QF_BV) then keep the full premise set as a final
  // fallback. Soundness: an isolated premise that proves the goal implies the
  // full premise set proves it (monotonic), so reporting `unsat` from any
  // isolated attempt is honest; the full attempt remains as backup otherwise.
  if (g.premises.size() >= 2) {
    for (size_t i = 0; i < g.premises.size(); ++i) {
      ProofGoal ng; ng.label = g.label + "#p" + std::to_string(i);
      ng.term = g.term; ng.premises = { g.premises[i] }; ng.isGround = g.isGround;
      result.goals.push_back(ng);
    }
    // Full-premise fallback as its own row.
    ProofGoal full = g; full.label = g.label + "#pfull";
    result.goals.push_back(full);
    return result;
  }

  // No strategy matched  -  return the goal as a leaf.
  result.goals.push_back(g);
  return result;
}

SplitResult splitGoal(SmtCtx& c, const ProofGoal& g) {
  return splitGoalImpl(c, g);
}

// ox:proof selectTactic  -  pick the cheapest Z3 tactic that decides `g`.
// Inspection order: isGround&&hasBV -> QF_BV, isGround -> Simplify,
// hasMod||hasDiv -> NIA, else Cascade.
ProofTactic selectTactic(const SmtCtx& c, const ProofGoal& g) {
  bool isGround = g.isGround;
  if (g.term) isGround = !exprContainsQuantifier(g.term);
  bool hasModOrDiv = exprHasModOrDiv(g.term);

  // `hasBV`: scan the premise strings for any symbol known to be BV-typed
  // (c.bvVars holds declared BV symbols). At the ProofGoal level the term is
  // an AST Expr  -  the lowered BV ops are detected post-lowering in
  // smtDischargeGoal, which re-routes to QF_BV when a previously-AST goal
  // lowers to a BV term.
  bool hasBV = false;
  for (const std::string& prem : g.premises)
    for (const std::string& bv : c.bvVars)
      if (prem.find(bv) != std::string::npos) { hasBV = true; break; }

  if (isGround && hasBV) return ProofTactic::QF_BV;
  if (isGround)           return ProofTactic::Simplify;
  if (hasModOrDiv)        return ProofTactic::NIA;
  return ProofTactic::Cascade;
}

// ox:proof tacticStrategy  -  render a tactic to its (check-sat-using ...) string.
std::string tacticStrategy(ProofTactic t) {
  switch (t) {
    case ProofTactic::QF_BV:
      // Bit-blast after simplification  -  fully decidable for fixed-width words.
      // `solve-eqs` cheaply eliminates ground equalities first, then `smt`
      // with the BV theory decides the rest.
      return "(check-sat-using (then simplify solve-eqs smt))";
    case ProofTactic::Simplify:
      // ox:proof Ground, no quantifiers  -  `(then simplify smt)` decides it.
      return "(check-sat-using (then simplify smt))";
    case ProofTactic::NIA:
      // ox:proof Nonlinear Int arithmetic (mod/div inside a quantifier)  -  MBQI on.
      return "(check-sat-using (then simplify (using-params smt :mbqi true)))";
    case ProofTactic::Cascade:
    default:
      // Original 3-then cascade kept as the last-resort path.
      return "(check-sat-using (then simplify smt))";
  }
}

// smtDischargeGoal  -  split, select tactic per sub-goal, emit one labelled
// discharge clause per sub-goal. Returns the number of sub-goals discharged.
// Each sub-goal is emitted with a `\; <label> (source line N)` header so the
// verify-report parser turns it into its own VerifyRow (the parent clause is
// proven iff ALL sub-rows are unsat  -  the AND semantics the spec requires).
int smtDischargeGoal(SmtCtx& c, const ProofGoal& goal, int srcLine) {
  SplitResult sr = splitGoal(c, goal);
  if (sr.goals.empty()) sr.goals.push_back(goal);

  // `owners` keeps synthesised Expr trees alive for the lowering loop below.
  // It is destroyed at function return  -  after the SMT text is emitted.
  int n = 0;
  for (const ProofGoal& sub : sr.goals) {
    const std::string& label = sub.label;
    // Row-forming header: `; <label> (source line N)`.
    c.out << "; " << label << " (source line " << srcLine << ")\n";
    c.out << "; --- discharge (" << label << ") ---\n"
          << "(push)\n";
    for (const auto& p : sub.premises)
      c.out << "(assert " << p << ")\n";
    std::string term = sub.term ? smtExpr(c, sub.term) : std::string("true");
    // ox:why Emit the define-fun with a SUFFIX to avoid a "named expression already
    // defined" Z3 error when the caller (smtClause) already emitted a
    // define-fun with the bare `<label>`. The verify-report parser keys rows
    // by the `; <label> (source line N)` header, NOT by the define-fun name,
    // so the suffixed name does not fragment the report.
    c.out << "(define-fun " << label << "_ds () Bool " << term << ")\n";
    c.out << "(assert (not " << label << "_ds))\n";

    // selectTactic inspects the AST + premises; also re-check the lowered
    // term for BV ops and upgrade to QF_BV when a ground goal lowers to BV.
    ProofTactic tac = selectTactic(c, sub);
    if (sub.isGround && tac == ProofTactic::Simplify && smtTermHasBV(term))
      tac = ProofTactic::QF_BV;
    c.out << tacticStrategy(tac) << "\n";
    // For non-decidable tactics (NIA/Cascade), also emit the cheap Simplify
    // strategy as a second check-sat so a mis-classified ground goal still
    // gets a decisive attempt (parser keeps the FIRST decisive result).
    if (tac != ProofTactic::Simplify && tac != ProofTactic::QF_BV)
      c.out << tacticStrategy(ProofTactic::Simplify) << "\n";
    c.out << "(pop)\n\n";
    ++n;
  }
  return n;
}

}  // namespace ox_smt
