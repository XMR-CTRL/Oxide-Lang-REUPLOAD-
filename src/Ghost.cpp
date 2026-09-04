// ox:proof Ghost.cpp  -  T1/T2/T3 Ghost encoder for Oxide formal verification.
//
// This is the outside-the-box piece that makes verified *complex* apps
// tractable. The contract walker in Driver.cpp (Tier A/B/C) can already
// discharge requires/ensures/invariant/assert over a single function's
// concrete state. But pointing that prover at a GUI's full widget tree or a
// TCP stack's full packet history is intractable  -  the state space is
// combinatorial. The three design decisions implemented here let the user
// lift the proof to a SMALL ABSTRACT layer and discharge there:
//
//   T1  -  spec fn + refines  (Verification Modulo Abstraction)
//     `spec fn abs(n: i64) -> i64 = n * (n + 1) / 2` becomes a real SMT
//     `(define-fun abs ((n Int)) Int ...)` (or an uninterpreted `declare-fun`
//     if the body has calls/array/field  -  honest, with `; note:`). Contract
//     clauses may name the spec fn directly. `refines concrete <= spec` then
//     emits a per-relation discharge query proving the concrete impl implies
//     the abstract spec for all args. IronFleet-style refinement: the prover
//     works on the abstraction, never the combinatorial concrete state.
//
//   T2  -  ghost let / ghost fn  (Ghost state)
//     `ghost let focus: i64;` becomes `(declare-const ghost_<scope>_<focus>
//     Int)`  -  a symbol the prover can constrain via invariants but that has
//     NO runtime slot, NO codegen. `ghost fn` ensures/requires are reflected
//     like any concrete fn's contracts, letting spec contexts call them.
//     This is the load-bearing mechanism: invariants over a handful of ghost
//     ints summarize megabytes of concrete state.
//
//   T3  -  region + modifies  (Modular frame conditions)
//     `region FocusGroup = { widget_tree, ghost_focus };` names a union.
//     `fn grab_focus modifies FocusGroup { ... }` emits a frame axiom: every
//     NON-modified declared symbol equals its `old()` value, so a caller can
//     reason that the function leaves everything outside `FocusGroup` alone
//     even though `focus` is computed from a hundred concrete fields. Empty
//     `modifies` => conservative (no frame claim). This is what makes a
//     million-line proof compose: each function proves its effect on a tiny
//     abstract region; the regions don't interfere.
//
// All output appends to the SAME `.smt2` stream the contract walker in
// Driver.cpp produces, so a single Z3 run sees the abstract layer alongside
// the concrete contract terms. The whole section is a NO-OP if the program
// declares none of the T1/T2/T3 constructs  -  existing contract output stays
// byte-identical (verified by regression on examples/contract_*.ox).
//
// Honesty contract (same as the rest of the SMT pipeline): anything we can't
// soundly encode gets a fresh uninterpreted symbol + a trailing `; note:`,
// so a clause that uses a placeholder honestly fails to reach `unsat`. We
// NEVER fabricate a `unsat`  -  an unresolved `refines` name or a shape
// mismatch skips emission with a `; note: refines <a> <= <b> skipped` line.

#include "Smt.h"
#include "AST.h"
#include "Sema.h"     // mangleMethod()

#include <sstream>
#include <string>
#include <set>
#include <map>
#include <vector>
#include <utility>

namespace ox_smt {

// Missing-#6 cross-TU helpers
//
// `smtConcreteCallResult` (defined in src/Driver.cpp's Mini-WP #2 walker) is
// promoted to external linkage so `emitPreserves` (below) can drive the SAME
// inlining path the call-site WP uses, getting a real symbolic `result` term
// for the handler's body  -  not a fresh uninterpreted const. Forward-declared
// here because the definition lives in Driver.cpp; both TUs share the
// `ox_smt` namespace so the unqualified lookup in `emitPreserves` resolves.
// Keeping a single inlining path is load-bearing  -  divergent mini-walkers in
// Driver and Ghost would let the preserves query prove a false thing the call
// path would refuse, which is exactly the soundness gap Missing #2 closed.
std::string smtConcreteCallResult(SmtCtx& c, const FuncDecl* callee,
                                         const std::string& labelBase,
                                         const std::vector<std::string>& args,
                                         const std::string& pathCond,
                                         const std::vector<std::string>& premises,
                                         std::map<std::string, std::string>* postStore);

// Local helpers

namespace {

std::map<std::string, long long> collectConstGlobals(const Program& prog) {
  std::map<std::string, long long> out;
  for (auto& g : prog.globals) {
    if (!g || !g->isConst || !g->init) continue;
    if (auto il = dynamic_cast<const IntLit*>(g->init.get())) {
      out[g->name] = (long long)il->v;
    } else if (auto u = dynamic_cast<const UnaryExpr*>(g->init.get())) {
      if (u->op == UnaryExpr::Op::neg) {
        if (auto il = dynamic_cast<const IntLit*>(u->base.get())) out[g->name] = -(long long)il->v;
      }
    }
  }
  return out;
}

std::map<std::string, const SpecFnDecl*> collectSpecFns(const Program& prog) {
  std::map<std::string, const SpecFnDecl*> out;
  for (auto& sf : prog.specFns) if (sf) out[sf->name] = sf.get();
  return out;
}

// ox:proof Is an expression "pure" for SMT `define-fun` purposes  -  i.e. only
// arithmetic/boolean over its params and literals, with no calls, array
// indexing, field access, or method calls? If not, we fall back to declaring
// the spec fn as an uninterpreted function (honest).
//
// Mirrors what smtExpr in Driver.cpp models: smtExpr already lowers these
// forms to real SMT and everything else to a placeholder. We re-derive the
// purity test here (rather than calling smtExpr and inspecting for `ph`)
// because it's cleaner and doesn't pollute the placeholder namespace of the
// enclosing SmtCtx (which would otherwise leak ph names into the contract
// scope). A false positive (we say "pure" but smtExpr still emits a `ph`)
// is harmless  -  Z3 just sees an extra uninterpreted const. A false negative
// (we say "impure" but it was pure) is ALSO harmless  -  we emit a
// `declare-fun` instead of `define-fun`, which is looser but still sound.
// So this helper can be a conservative over-approximation.
//
// Tier-3 fix (Missing #4): this USED to reject Call, MethodCall, and CastExpr
// outright, forcing spec fns that compose other spec fns OR do type casts
// (page-aligned masks, canonicality checks) to be uninterpreted. That was
// too blunt  -  `smtExpr` can inline spec-fn calls via `smtInlineSpecCall` and
// lowers CastExpr via int2bv/extract, so the bodies ARE lowerable. We now
// allow:
//   - CastExpr  -  pure (smtExpr's CastExpr arm emits a real extract/zext term)
//   - Call to a SPEC fn (by name, no fnPtr)  -  pure_recursive if the callee
//     is itself in `specFns` (closes the page_aligned/exit_ok composition).
// Anything else stays impure (method calls, array index, field, struct
// literal, asm, etc.)  -  honest, matching what smtExpr actually lowers.
bool specFnBodyIsPure(const Expr* e, const std::map<std::string, const SpecFnDecl*>& specFns);

bool specFnBodyIsPure(const Expr* e, const std::map<std::string, const SpecFnDecl*>& specFns) {
  if (!e) return true;
  if (dynamic_cast<const IntLit*>(e)) return true;
  if (dynamic_cast<const BoolLit*>(e)) return true;
  if (dynamic_cast<const VarRef*>(e)) return true;     // param or const global
  if (dynamic_cast<const OldExpr*>(e)) return false;    // no pre-state in a spec fn
  if (dynamic_cast<const QuantExpr*>(e)) return true;   // forall/exists are SMT-native
  if (dynamic_cast<const NullLit*>(e)) return true;
  // MISSING-#4 fix: CastExpr is lowerable by smtExpr (int2bv + extract/zext).
  if (dynamic_cast<const CastExpr*>(e)) {
    auto ce = dynamic_cast<const CastExpr*>(e);
    return specFnBodyIsPure(ce->e.get(), specFns);
  }
  if (auto u = dynamic_cast<const UnaryExpr*>(e)) {
    if (u->op == UnaryExpr::Op::addr || u->op == UnaryExpr::Op::deref) return false;
    return specFnBodyIsPure(u->base.get(), specFns);
  }
  if (auto b = dynamic_cast<const BinaryExpr*>(e)) {
    // Bitops lower through real 64-bit BitVec bridges in smtExpr, so mask/shift
    // heavy spec fns (page tables, VMCS/MSR control words) remain definitional.
    return specFnBodyIsPure(b->lhs.get(), specFns) &&
           specFnBodyIsPure(b->rhs.get(), specFns);
  }
  if (auto t = dynamic_cast<const TernaryExpr*>(e)) {
    return specFnBodyIsPure(t->cond.get(), specFns) &&
           specFnBodyIsPure(t->thenE.get(), specFns) &&
           specFnBodyIsPure(t->elseE.get(), specFns);
  }
  // ox:proof MISSING-#4 fix: a Call to a non-method spec fn (no fnPtr, callee names a
  // declared spec fn in this program) is lowerable because smtExpr inlines
  // it via smtInlineSpecCall. Mirrors what the purity gate for spec-fn body
  // emission needs to permit for HV-style `spec fn exit_ok = page_aligned(x) && ...`.
  // MethodCall is intentionally still impure (the receiver self-state isn't
  // pure in a top-level define-fun context).
  if (auto call = dynamic_cast<const Call*>(e)) {
    if (!call->fnPtr && !call->callee.empty()
        && specFns.count(call->callee)) {
      for (auto& a : call->args)
        if (!specFnBodyIsPure(a.get(), specFns)) return false;
      return true;
    }
    return false;
  }
  // MethodCall, AssocCall, Index, Field, ArrayLit, StructLit, RangeLit,
  // *_New, GenericTypeRef, AsmExpr, FloatLit, StrLit, CharLit, SizeofExpr  - 
  // none lowerable to a self-contained SMT define-fun body.
  return false;
}

} // namespace

// ox:proof T1  -  emit every `spec fn` as either a real SMT define-fun (body pure over
// params) or an uninterpreted declare-fun (body has calls/array/field  - 
// honest). The symbol lives at top level so any later clause that names the
// spec fn resolves to it.
static void emitSpecFns(const Program& prog, std::ostringstream& out) {
  if (prog.specFns.empty()) return;
  out << "\n; ============================================================\n";
  out << "; T1  -  spec fns (abstract layer; refines targets live here)\n";
  out << "; ============================================================\n";

  std::map<std::string, const SpecFnDecl*> specFns = collectSpecFns(prog);
  std::map<std::string, long long> constGlobals = collectConstGlobals(prog);

  for (auto& sf : prog.specFns) {
    if (!sf) continue;
    out << "; spec fn " << sf->name << " (source line " << sf->line << ")\n";

    // ox:proof Sanitize: SMT identifiers are alnum/underscore. Oxide identifiers already
    // match, but the spec fn name could in principle clash with a SMT keyword;
    // we prefix with `sf_` defensively (and to avoid colliding with concrete-fn
    // define-funs from the contract walker, which use bare names like
    // `fib_ensures_0`).
    std::string sym = "sf_" + sf->name;

    // Build a scratch SmtCtx whose `out` IS this stream so smtExpr can emit
    // any needed placeholder declares inline. curFn qualifies ph names. The
    // `declared` set scopes to THIS spec fn so two spec fns' params (both
    // maybe named `n`) don't collide across `define-fun`s.
    SmtCtx c{out, nullptr, 0, 0, {}};
    c.curFn = sym;
    c.specFns = specFns;
    c.constGlobals = constGlobals;

    // ox:proof Bind each param to a unique SMT symbol `sf_<fn>_<param>` so the body
    // resolves param references. declare-const so they exist as top-level
    // Int/Bool/Real for the define-fun's formal list too.
    std::vector<std::string> paramSyms;
    std::vector<std::string> paramSorts;
    for (auto& p : sf->params) {
      std::string psym = "p_" + sym + "_" + p.name;
      // ox:proof Don't re-declare if it somehow already exists; record in nameMap so a
      // body VarRef to `p.name` resolves to `psym`.
      c.nameMap[p.name] = psym;
      c.declared.insert(psym);
      paramSyms.push_back(psym);
      paramSorts.push_back(smtSort(p.type));
    }

    const char* retSort = smtSort(sf->retType);

    // ox:proof --- `asm spec fn`  -  emit the uninterpreted symbol + a universal axiom
    //     linking the spec's requires/ensures so the instruction's behaviour
    //     is available to EVERY discharge query (not just the implementing
    //     block's WP  -  that WP grounds it at the actual args separately).
    //     The symbol is declared uninterpreted (the spec has no concrete
    //     body), and `result` in the ensures is bound to `(sf_<name> params)`
    //     via a nameMap binding so ensures clauses like `ensures result == 5`
    //     lower to `(<sort> (= (sf_foo p_sf_foo_x) 5))`. With NO requires, the
    //     precondition is vacuously `true`; with NO ensures, no universal
    //     axiom is wired (just the uninterpreted symbol  -  the linked block's
    //     WP still emits the ground hypothesis from the spec's ensures via
    //     the implements clause, so an ensures-less spec is degenerate but
    //     useful as a pure declared-name). ---
    if (sf->isAsmSpec) {
      // 1. `(declare-fun sf_<name> (paramSorts) RetSort)`  -  uninterpreted
      //    function symbol modellling the hardware instruction's result.
      std::ostringstream decl;
      decl << "(declare-fun " << sym << " (";
      for (size_t i = 0; i < paramSorts.size(); ++i) {
        if (i) decl << " ";
        decl << paramSorts[i];
      }
      decl << ") " << retSort << ")\n";
      out << decl.str();
      out << "; note: asm spec fn " << sf->name
          << "  -  declared uninterpreted; the universal axiom below relates its "
             "(sf_-prefixed) result to the architectural ensures\n";

      // ox:proof 2. Universal axiom: `(assert (forall (params) (=> req ens)))` with
      //    `result` bound to `(sf_<name> params)` so ensures reference the
      //    instruction's symbolic output. We bind `result` in nameMap to the
      //    SCOPED applied term `(sf_<name> <paramSyms...>)` (a fresh term,
      //    legal inside the forall body since the symbol is declared above).
      //    Each requires lowers against the params only; each ensures lowers
      //    against params + the bound `result`. Multiple clauses conjoin.
      std::string resultTerm = "(" + sym;
      for (const auto& ps : paramSyms) resultTerm += " " + ps;
      resultTerm += ")";
      // Save + bind `result` so ensures VarRef("result") resolves to the
      // applied term. (Other fn-level result names are `<curFn>_result`; a
      // spec isn't a fn, and an `asm spec fn` never recurses into itself with
      // a different result name, so the bind is safe  -  restored below.)
      auto savedResult = c.nameMap.find("result");
      c.nameMap["result"] = resultTerm;

      // Lower requires into a conjunction (empty => `true`).
      std::ostringstream reqScratch;
      if (!sf->requires_.empty()) {
        std::vector<std::string> reqTerms;
        for (auto& req : sf->requires_) {
          if (!req) continue;
          reqTerms.push_back(smtExpr(c, req.get()));
        }
        if (reqTerms.empty()) reqScratch << "true";
        else if (reqTerms.size() == 1) reqScratch << reqTerms[0];
        else {
          reqScratch << "(and";
          for (const auto& t : reqTerms) reqScratch << " " << t;
          reqScratch << ")";
        }
      } else {
        reqScratch << "true";
      }
      std::string reqTerm = reqScratch.str();

      // Lower ensures into a conjunction (empty => `true`).
      std::ostringstream ensScratch;
      if (!sf->ensures_.empty()) {
        std::vector<std::string> ensTerms;
        for (auto& ens : sf->ensures_) {
          if (!ens) continue;
          ensTerms.push_back(smtExpr(c, ens.get()));
        }
        if (ensTerms.empty()) ensScratch << "true";
        else if (ensTerms.size() == 1) ensScratch << ensTerms[0];
        else {
          ensScratch << "(and";
          for (const auto& t : ensTerms) ensScratch << " " << t;
          ensScratch << ")";
        }
      } else {
        ensScratch << "true";
      }
      std::string ensTerm = ensScratch.str();

      // ox:proof Restore the prior `result` binding so a later spec fn's ensures (or
      // contract clause that names `result`) doesn't see this spec's term.
      if (savedResult != c.nameMap.end()) c.nameMap["result"] = savedResult->second;
      else c.nameMap.erase("result");

      // ox:proof Emit the universal axiom ONLY if there's at least one requires or one
      // ensures clause; an ensures-less / requires-less asm spec contributes
      // only its uninterpreted symbol (the implementing block's hypothesis is
      // the only place its clauses get grounded). A `true ==> true` axiom is
      // honest but noise, so we skip it for the degenerate case.
      if (!sf->requires_.empty() || !sf->ensures_.empty()) {
        out << "; asm spec fn universal axiom  -  instruction behaviour contract\n";
        out << "(assert (forall (";
        for (size_t i = 0; i < paramSyms.size(); ++i) {
          if (i) out << " ";
          out << "(" << paramSyms[i] << " " << paramSorts[i] << ")";
        }
        out << ") (=> " << reqTerm << " " << ensTerm << ")))\n";
      }
      out << "\n";
      continue;   // skip the plain-spec-fn define-fun / declare-fun path below
    }

    if (sf->body && specFnBodyIsPure(sf->body.get(), specFns)) {
      // Emit any placeholder declares smtExpr needs (rare for a pure body),
      // then the define-fun with the lowered body. We render the body into a
      // scratch ostringstream FIRST, so the placeholder declares land ABOVE the
      // define-fun rather than inside its body (which would be ill-formed).
      std::ostringstream bodyScratch;
      SmtCtx bc{bodyScratch, nullptr, 0, 0, 0, 0, c.declared};
      bc.curFn = sym;
      bc.nameMap = c.nameMap;
      bc.specFns = specFns;
      bc.constGlobals = constGlobals;
      bc.expandingSpecFns.insert(sf->name);
      std::string bodyTerm = smtExpr(bc, sf->body.get());

      // Emit any declares the body scratch produced (placeholders), then the
      // define-fun. The declared set carries back so duplicate ph's aren't
      // re-emitted if a later clause references the same name.
      out << bodyScratch.str();
      c.declared = bc.declared;

      // Build `(define-fun sf_foo ((p_sf_foo_n Int) ...) <RetSort> <body>)`.
      std::ostringstream sig;
      sig << "(define-fun " << sym << " (";
      for (size_t i = 0; i < paramSyms.size(); ++i) {
        if (i) sig << " ";
        sig << "(" << paramSyms[i] << " " << paramSorts[i] << ")";
      }
      sig << ") " << retSort << " " << bodyTerm << ")\n";
      out << sig.str();
    } else {
      // Impure body (or no body): declare as an uninterpreted function. This
      // is honest  -  a spec fn whose body references calls/array/field is
      // better left uninterpreted than to emit a define-fun with placed-holders
      // inside (which would still be sound but harder to read in the .smt2).
      std::ostringstream sig;
      sig << "(declare-fun " << sym << " (";
      for (size_t i = 0; i < paramSorts.size(); ++i) {
        if (i) sig << " ";
        sig << paramSorts[i];
      }
      sig << ") " << retSort << ")\n";
      out << sig.str();
      if (!sf->body) {
        out << "; note: spec fn " << sf->name << " has no body  -  declared uninterpreted\n";
      } else {
        out << "; note: spec fn " << sf->name
            << " body references calls/array/field  -  declared uninterpreted\n";
      }
    }
    out << "\n";
  }
}

// ox:proof T2  -  collect every `ghost let` in every function body, emitting a
// `(declare-const ghost_<fn>_<name> <Sort>)` per binding. Contract clauses
// that reference a ghost let resolve to this symbol via the walker's nameMap
// (which the contract walker populates when it walks the body  -  for the ghost
// section we just pre-declare the symbols so they exist at top level before
// any clause discharge query). We also reflect `ghost fn` contracts onto the
// stream so a spec context can reference them.

// ox:proof Forward-declared here: a small recursive walker that finds ghost-let names.
// We DON'T use the existing smtWalkStmts because that one DISCHARGES  -  we just
// want to harvest symbol names. Implementing our own keeps the two walk passes
// independent (and lets the ghost-let namespace be function-qualified).
static void collectGhostLetNames(const std::vector<StmtPtr>& stmts,
                                 const std::string& fnName,
                                 std::ostringstream& out,
                                 std::set<std::string>& declared,
                                 const std::set<std::string>& alreadyEmitted) {
  for (auto& s : stmts) {
    if (!s) continue;
    if (auto gl = dynamic_cast<const GhostLetStmt*>(s.get())) {
      std::string sym = "ghost_" + fnName + "_" + gl->name;
      // Skip if the contract walker (Tier B WP encoder in Driver.cpp) already
      // emitted this exact declare-const inline while walking the function
      // body. Re-declaring would be a fatal Z3 error that aborts the whole
      // run (every later query reports `sat`), masking real proofs.
      if (alreadyEmitted.count(sym)) continue;
      if (!declared.count(sym)) {
        out << "(declare-const " << sym << " " << smtSort(gl->type) << ")\n";
        declared.insert(sym);
      }
    }
    // Recurse into nested blocks (if/while/for bodies, defer, blocks).
    if (auto is = dynamic_cast<const IfStmt*>(s.get())) {
      collectGhostLetNames(is->then, fnName, out, declared, alreadyEmitted);
      collectGhostLetNames(is->else_, fnName, out, declared, alreadyEmitted);
      continue;
    }
    if (auto ws = dynamic_cast<const WhileStmt*>(s.get())) {
      collectGhostLetNames(ws->body, fnName, out, declared, alreadyEmitted);
      continue;
    }
    if (auto fs = dynamic_cast<const ForStmt*>(s.get())) {
      collectGhostLetNames(fs->body, fnName, out, declared, alreadyEmitted);
      continue;
    }
    if (auto bl = dynamic_cast<const Block*>(s.get())) {
      collectGhostLetNames(bl->stmts, fnName, out, declared, alreadyEmitted);
      continue;
    }
    if (auto ds = dynamic_cast<const DeferStmt*>(s.get())) {
      // ox:proof A DeferStmt holds a single deferred Stmt via `body`. A ghost let
      // appearing inside a defer block is unusual and already declared by the
      // enclosing-scope walk (defer re-runs in the same scope). Rather than
      // synthesise a one-element vector (which would require a Stmt clone we
      // don't have), we document the limitation honestly and skip  -  this is a
      // sound under-approximation: any ghost let reachable via the enclosing
      // fn-body walk is already declared; a ghost let only-theoretically visible
      // through a defer body and nowhere else is a degenerate case nobody writes.
      // (If that ever bites, the fix is to add a Stmt::clone() and recurse here.)
      (void)ds;
      continue;
    }
  }
}

static void emitGhostLetsAndGhostFns(const Program& prog, std::ostringstream& out) {
  bool anyGhost = false;
  for (auto& fn : prog.funcs) {
    if (fn && fn->isGhost) { anyGhost = true; break; }
    if (fn) {
      for (auto& s : fn->body) {
        if (s && dynamic_cast<const GhostLetStmt*>(s.get())) { anyGhost = true; break; }
      }
    }
    if (anyGhost) break;
  }
  // For impl methods too.
  if (!anyGhost) {
    for (auto& im : prog.impls) {
      if (!im) continue;
      for (auto& m : im->methods) {
        if (m && m->isGhost) { anyGhost = true; break; }
      }
      if (anyGhost) break;
    }
  }
  if (!anyGhost) return;

  out << "\n; ============================================================\n";
  out << "; T2  -  ghost state (ghost let / ghost fn; no codegen, SMT-only)\n";
  out << "; ============================================================\n";

  std::set<std::string> declared;
  // Scan the stream built SO FAR (by emitSmt's contract walker + WP encoder)
  // for any `(declare-const ghost_<fn>_<name> ...)` the walker already emitted
  // inline, so the T2 section doesn't re-emit them (a duplicate decl is a
  // fatal Z3 error that aborts the whole run). We do a cheap substring scan.
  std::set<std::string> alreadyEmitted;
  {
    const std::string text = out.str();
    size_t pos = 0;
    const std::string needle = "(declare-const ghost_";
    while ((pos = text.find(needle, pos)) != std::string::npos) {
      size_t start = pos + needle.length();
      size_t end = text.find(' ', start);
      if (end != std::string::npos) alreadyEmitted.insert(text.substr(start, end - start));
      pos = start;
    }
  }
  for (auto& fn : prog.funcs) {
    if (!fn) continue;
    collectGhostLetNames(fn->body, fn->name, out, declared, alreadyEmitted);
  }
  for (auto& im : prog.impls) {
    if (!im) continue;
    for (auto& m : im->methods) {
      if (!m) continue;
      collectGhostLetNames(m->body, mangleMethod(im->structName, m->name), out, declared, alreadyEmitted);
    }
  }

  // ox:proof ghost fn reflection: emit a header per ghost fn so the .smt2 documents that
  // its requires/ensures (already discharged by the contract walker, which
  // skips the body for ghost fns per Sema) are pure-spec. We don't re-discharge
  // here  -  the walker already did. We just label the section for readability.
  for (auto& fn : prog.funcs) {
    if (fn && fn->isGhost) {
      out << "; ghost fn " << fn->name << " (contracts discharged by walker, body skipped at runtime)\n";
    }
  }
  for (auto& im : prog.impls) {
    if (!im) continue;
    for (auto& m : im->methods) {
      if (m && m->isGhost) {
        out << "; ghost fn " << mangleMethod(im->structName, m->name) << " (method, spec-only)\n";
      }
    }
  }
}

// T3  -  expand regions and emit `modifies` frame axioms. For each function
// with a non-empty `modifies` list, every declared (top-level) symbol that is
// NOT in the expanded modifies set gets a frame axiom: it equals its `old()`
// value after the call. We declare fresh `old_<sym>` consts per function to
// represent the pre-state, then assert `(= sym old_<sym>)` as a hypothesis a
// discharge query can assume. Frame axioms make verification compose: a
// caller proving a property on `widget_tree` can ignore a `modifies focus`
// function.
//
// Pattern (per function with modifies):
//   (declare-const <fn>_old_<sym> <Sort>)      ; pre-state snapshot
//   ; frame axiom: <sym> unchanged by <fn> (not in modifies <fn>)
//   (define-fun <fn>_frame_<sym> () Bool (= <sym> <fn>_old_<sym>))
//   ; discharge: can the frame be violated? unsat => frame holds.
//   (push) (assert (not <fn>_frame_<sym>)) (check-sat) (pop)
//
// We expand region names in the modifies list to their member names first.
// Empty modifies => conservative (no frame claim, function may mutate anything)
//  -  matches Dafny/SPARK default.

// Expand a modifies list by resolving region names to their members. Returns
// the fully-expanded set of bare names the function may modify.
static std::set<std::string> expandModifies(
    const std::vector<std::string>& modifies,
    const Program& prog) {
  std::set<std::string> out;
  // Index regions by name for O(1) lookup.
  std::map<std::string, const RegionDecl*> regionsByName;
  for (auto& r : prog.regions) {
    if (r) regionsByName[r->name] = r.get();
  }
  for (const auto& m : modifies) {
    auto it = regionsByName.find(m);
    if (it != regionsByName.end()) {
      // m is a region name  -  expand to its members.
      for (const auto& mem : it->second->members) out.insert(mem);
    } else {
      // ox:proof m is a bare array/global/ghost-let name.
      out.insert(m);
    }
  }
  return out;
}

// Collect candidate "mutable symbols"  -  the universe over which frame axioms
// range. We consider:
//   - every top-level `let` global (prog.globals)  -  Int/Bool/Real/ptr consts
//   - every ghost-let declared in any function (already declared in the SMT
//     stream by emitGhostLetsAndGhostFns as `ghost_<fn>_<name>`)
//   - every array typed const we know about (we don't have an interpreter for
//     concrete array contents; arrays get a single `declare-const` summarising
//     symbol  -  honest, since array theory would need (select/store) and we're
//     operating on the abstraction layer here)
// To stay sound and simple, we declare one symbol per global: `<gname>` of the
// global's sort. A function's modifies that lists a global name removes it
// from the frame; a modifies that lists a region expands and removes members.
static void collectMutableGlobals(const Program& prog,
                                  std::vector<std::pair<std::string, const char*>>& out) {
  for (auto& g : prog.globals) {
    if (!g || g->isConst) continue;       // const globals are immutable; skip
    out.push_back({g->name, smtSort(g->type)});
  }
}

static void emitRegionsAndModifies(const Program& prog, std::ostringstream& out,
                                   const PostStateMap& postIdents) {
  bool anyRegion = !prog.regions.empty();
  bool anyModifies = false;
  bool anyPureFrame = false;   // T3.5  -  explicitly-pure fns need no modifies to
                               // get the blanket "all memory unchanged" frame
  for (auto& fn : prog.funcs) {
    if (!fn) continue;
    if (!fn->modifies.empty()) anyModifies = true;
    if (fn->effectsExplicit && fn->effects.empty()) anyPureFrame = true;
  }
  for (auto& im : prog.impls) if (im)
    for (auto& m : im->methods) {
      if (!m) continue;
      if (!m->modifies.empty()) anyModifies = true;
      if (m->effectsExplicit && m->effects.empty()) anyPureFrame = true;
    }
  if (!anyRegion && !anyModifies && !anyPureFrame) return;

  out << "\n; ============================================================\n";
  out << "; T3  -  regions + modifies frame axioms (modular verification)\n";
  out << "; ============================================================\n";

  // Document each region's expansion (readability).
  for (auto& r : prog.regions) {
    if (!r) continue;
    out << "; region " << r->name << " = {";
    for (size_t i = 0; i < r->members.size(); ++i) {
      if (i) out << ", ";
      out << r->members[i];
    }
    out << "}  (source line " << r->line << ")\n";
  }

  // Gather the universe of mutable globals once.
  std::vector<std::pair<std::string, const char*>> mutables;
  collectMutableGlobals(prog, mutables);

  // ox:proof For each function with a non-empty modifies list, emit a frame axiom per
  // global NOT in the expanded modifies set.
  //
  // Tier 1 (post-state threading): the frame axiom traditionally reads
  //   (define-fun <fn>_frame_<g> () Bool (= <g> <fn>_old_<g>))
  // where BOTH <g> and <fn>_old_<g> are fresh top-level uninterpreted
  // constants  -  making the axiom vacuous: it asserts the equality of two
  // unrelated unconstrained symbols, so its negation is satisfiable (sat)
  // BY CONSTRUCTION regardless of what the body does. That reported `sat`
  // proves nothing.
  //
  // Tier 1 fix: `emitFnContracts` (Driver.cpp) now seeds the WP body walk
  // with `store[g] = <fn>_old_<g>` for every non-const global `g` BEFORE
  // walking the body, and after the walk records the final `store[g]`
  // (the body's actual post-state term) into `postIdents[fnName][g]`.
  // An unmodified global keeps `store[g] = old_<g>`  -  so `postIdents`'s
  // entry for that (fn, g) IS the pre-state symbol `<fn>_old_<g>`. The
  // frame axiom becomes `(= <fn>_old_<g> <fn>_old_<g>)`  -  trivially unsat
  // to negate, honestly discharging as "provably unchanged by the body".
  // A global the body reassigned has a different post term in `postIdents`,
  // so the frame becomes `(= <post> <old>)` which is sat in general  - 
  // honestly flagging that the body touched a global it didn't declare in
  // `modifies`. That's the real Dafny-modifies semantics.
  //
  // The `old_<g>` declaration lives at top level  -  it was emitted in the
  // per-function contracts block (by `emitFnContracts`'s Tier-1 seeding),
  // which writes to the SAME `ostringstream` (just earlier in the stream).
  // So the symbol is in scope when the frame axiom references it at top
  // level here. We do NOT re-declare it (avoids a duplicate-decl Z3 error).
  //
  // Fallback: if `postIdents` does NOT have an entry for this (fn, g)  - 
  // e.g. the fn is extern (early-returned in `emitFnContracts`), or the WP
  // encoder couldn't track the body  -  we fall back to the pre-Tier-1
  // pattern: declare both `<g>` and `<fn>_old_<g>` as fresh top-level uninterp
  // consts and emit `(= <g> <fn>_old_<g>)`. Strictly weaker (vacuous) but
  // never unsound: it's identical to today's behaviour for those fns.
  std::set<std::string> topDeclared;

  // Effect-system purity frame (T3.5): a function declaring `effects { }`
  // with NO hardware-state effects (mmio, vmcs_read, vmcs_write) promises
  // ALL mutable memory is unchanged  -  strictly stronger than `modifies`,
  // which only frames globals NOT appearing in its list. For a pure fn we
  // therefore emit the frame axiom for EVERY mutable global, ignoring the
  // modifies set entirely. If the fn lists a hardware-state effect we
  // suppress the blanket claim (such effects touch non-modeled state).
  static const std::set<std::string> kHwEffects = {
    "mmio", "vmcs_read", "vmcs_write"
  };
  auto emitForFn = [&](const std::string& fnName,
                       const std::vector<std::string>& modifies,
                       bool effectsExplicit,
                       const std::vector<std::string>& effects) {
    bool pureFrame = false;
    if (effectsExplicit && effects.empty()) {
      // An explicitly-pure fn: claim all memory unchanged  -  unless we can't
      // (no mutable globals) or the blanket claim is suppressed by a known
      // hardware-state effect (there won't be any in the empty-effects case,
      // but the guard keeps the reasoning explicit if the set is ever widened).
      pureFrame = !mutables.empty();
      (void)kHwEffects; // documented above; the empty-effects branch needs no
                       // suppression check since there are no effects to check
    }
    if (modifies.empty() && !pureFrame) return;   // conservative: no frame claim
    std::set<std::string> modset = pureFrame ? std::set<std::string>{}
                                             : expandModifies(modifies, prog);
    auto postIt = postIdents.find(fnName);
    bool havePost = (postIt != postIdents.end());
    if (pureFrame) {
      out << "\n; ---- purity frame axioms for " << fnName
          << " (effects {}  -  explicitly pure: ALL mutable memory unchanged) ----\n";
    } else {
      out << "\n; ---- frame axioms for " << fnName
          << " (modifies: " << modifies.size() << " entr" << (modifies.size()==1?"y":"ies") << ") ----\n";
    }
    for (auto& [gname, gsort] : mutables) {
      if (!pureFrame && modset.count(gname)) continue;   // in modifies set: no frame axiom
      // Look up the body's actual post-state term for this (fn, g).
      std::string postTerm;
      bool tracked = false;
      if (havePost) {
        auto gt = postIt->second.find(gname);
        if (gt != postIt->second.end()) { postTerm = gt->second; tracked = true; }
      }
      std::string oldSym = fnName + "_old_" + gname;
      if (tracked) {
        // Tier 1  -  real post-state term. The `old_<g>` declaration was
        // emitted by `emitFnContracts`'s pre-state seeding block, in the
        // SAME .smt2 stream above  -  so it's in scope here. Don't re-decl.
        // The post term may reference body placeholders (e.g. `ph0`),
        // which are also already declared above (smtDeclareConst wrote
        // them to `c.out` == same `out` stream).
        std::string frameLabel = fnName + "_frame_" + gname;
        out << "; " << frameLabel << "  (frame: " << gname
            << " unchanged; not in modifies; body post-state threaded)\n";
        out << "(define-fun " << frameLabel << " () Bool (= "
            << postTerm << " " << oldSym << "))\n";
        out << "(push)\n";
        out << "(assert (not " << frameLabel << "))\n";
        // ox:proof A1: use `check-sat-using (then simplify smt)` instead of bare
        // `check-sat` so Z3's MBQI quantifier-instantiation cache does NOT
        // leak across obligations. The preserve-unsat result here is the
        // load-bearing one for the verify-table row; bare `(check-sat)` let
        // earlier-frame `sat` models poison this query. Mirrors the same
        // change in `smtDischarge` (Driver.cpp).
        out << "(check-sat-using (then simplify smt))\n";
        out << "(pop)\n\n";
      } else {
        // Fallback  -  vacuous pattern (pre-Tier-1). Both symbols are fresh
        // top-level uninterpreted consts; the axiom asserts equality of
        // two unrelated symbols. Honest: `; note: post-state not tracked`.
        if (!topDeclared.count(gname)) {
          out << "(declare-const " << gname << " " << gsort << ")\n";
          topDeclared.insert(gname);
        }
        if (!topDeclared.count(oldSym)) {
          out << "(declare-const " << oldSym << " " << gsort << ")\n";
          topDeclared.insert(oldSym);
        }
        std::string frameLabel = fnName + "_frame_" + gname;
        out << "; " << frameLabel
            << "  (frame: " << gname
            << " unchanged; not in modifies; ; note: post-state not tracked  -  vacuous)\n";
        out << "(define-fun " << frameLabel << " () Bool (= "
            << gname << " " << oldSym << "))\n";
        out << "(push)\n";
        out << "(assert (not " << frameLabel << "))\n";
        // A1: same rationale as the tracked-frame path above.
        out << "(check-sat-using (then simplify smt))\n";
        out << "(pop)\n\n";
      }
    }
  };

  for (auto& fn : prog.funcs) {
    if (!fn) continue;
    // Pure fns have no modifies but still need the blanket purity frame;
    // impure-but-nomodifies fns get nothing (as before).
    if (fn->modifies.empty() && !(fn->effectsExplicit && fn->effects.empty())) continue;
    emitForFn(fn->name, fn->modifies, fn->effectsExplicit, fn->effects);
  }
  for (auto& im : prog.impls) {
    if (!im) continue;
    for (auto& m : im->methods) {
      if (!m) continue;
      if (m->modifies.empty() && !(m->effectsExplicit && m->effects.empty())) continue;
      emitForFn(mangleMethod(im->structName, m->name), m->modifies,
                m->effectsExplicit, m->effects);
    }
  }
}

// ox:proof T1  -  emit a discharge query per `refines <concrete> <= <spec>` declaration.
//
// The refinement obligation (IronFleet-style): for all argument tuples a
// matching the signatures,
//     (requires_spec(a) ==> requires_concrete(a))  AND
//     (ensures_concrete(a) ==> ensures_spec(a))
// i.e. the concrete implementation respects the abstract spec's preconditions
// AND establishes at least the abstract spec's postconditions. We emit ONE
// discharge query per refines that conjoins both directions, scoped under a
// push/pop so it doesn't pollute later queries. If either name is unresolved
// or the signatures don't match (we approximate "match" by param count +
// the two having contracts at all), we skip emission with a `; note: ...`
// honesty line  -  NEVER fabricate an `unsat`.
//
// For arguments, we declare fresh `arg_<i>` consts of each param's sort and
// universally quantify them via `(assert (forall (...) ...))` inside the push.
// The ensures direction references `result`  -  we declare `arg_result` and
// let the spec/concrete ensures clauses over it unify by name.

static const FuncDecl* findFuncByName(const Program& prog, const std::string& name) {
  for (auto& fn : prog.funcs) if (fn && fn->name == name) return fn.get();
  for (auto& im : prog.impls) if (im)
    for (auto& m : im->methods) if (m && mangleMethod(im->structName, m->name) == name) return m.get();
  return nullptr;
}

static const SpecFnDecl* findSpecFnByName(const Program& prog, const std::string& name) {
  for (auto& sf : prog.specFns) if (sf && sf->name == name) return sf.get();
  return nullptr;
}

static void emitRefines(const Program& prog, std::ostringstream& out) {
  if (prog.refines_.empty()) return;
  out << "\n; ============================================================\n";
  out << "; T1  -  refines (concrete-implies-abstract discharge queries)\n";
  out << "; ============================================================\n";

  // ox:unsafe MISSING-#4 fix: the per-refines `SmtCtx` below must be able to lower
  // spec-fn CALLS in the spec body (e.g. `exit_ok = page_aligned(gpa) && ...`).
  // `smtExpr`'s Call arm falls through `smtInlineSpecCall`, which looks up the
  // callee in `c.specFns`. Without seeding that map (and `constGlobals` for
  // any const-int cross-references in the body), the call decays to a
  // placeholder  -  visibly `refines_..._ph0`/`_ph1`  -  and the discharge query
  // stays `sat` regardless of the body's actual meaning. We index the
  // program's spec fns + const globals once here and replay them per refines.
  std::map<std::string, const SpecFnDecl*> specFns = collectSpecFns(prog);
  std::map<std::string, long long> constGlobals = collectConstGlobals(prog);
  // Also seed the concrete-fn decls map so `smtFindDirectCallee` can resolve
  // any concrete fn referenced by name from a requires/ensures clause body.
  std::map<std::string, const FuncDecl*> funcDecls;
  for (auto& fn : prog.funcs) if (fn && !fn->isExtern) funcDecls[fn->name] = fn.get();
  std::map<std::string, const FuncDecl*> methodDecls;
  for (auto& im : prog.impls) {
    if (!im) continue;
    for (auto& m : im->methods) {
      if (!m || m->isExtern) continue;
      methodDecls[mangleMethod(im->structName, m->name)] = m.get();
    }
  }

  for (auto& rf : prog.refines_) {
    if (!rf) continue;
    const FuncDecl* concrete = findFuncByName(prog, rf->concreteName);
    const SpecFnDecl* spec = findSpecFnByName(prog, rf->specName);
    out << "\n; refines " << rf->concreteName << " <= " << rf->specName
        << "  (source line " << rf->line << ")\n";

    if (!concrete) {
      out << "; note: refines " << rf->concreteName << " <= " << rf->specName
          << " skipped  -  concrete fn '" << rf->concreteName << "' not found\n";
      continue;
    }
    if (!spec) {
      out << "; note: refines " << rf->concreteName << " <= " << rf->specName
          << " skipped  -  spec fn '" << rf->specName << "' not found\n";
      continue;
    }
    if (concrete->params.size() != spec->params.size()) {
      out << "; note: refines " << rf->concreteName << " <= " << rf->specName
          << " skipped  -  arity mismatch (concrete " << concrete->params.size()
          << " vs spec " << spec->params.size() << ")\n";
      continue;
    }
    // ox:proof Both must have ensures (the postcondition direction). Spec fn's body IS
    // its spec  -  we treat its body as a single ensures over its params. If the
    // concrete has no ensures, we can't discharge the ensures-implies direction
    // soundly  -  skip with a note.
    if (concrete->ensures_.empty()) {
      out << "; note: refines " << rf->concreteName << " <= " << rf->specName
          << " skipped  -  concrete fn has no ensures to imply the spec from\n";
      continue;
    }
    if (!spec->body) {
      out << "; note: refines " << rf->concreteName << " <= " << rf->specName
          << " skipped  -  spec fn has no body to discharge against\n";
      continue;
    }

    // ox:proof Build a discharge query. We negate the refinement obligation:
    //   NOT (forall args. (reqSpec(args) ==> reqConc(args)) AND
    //                     (ensConc(args) ==> ensSpec(args)))
    // and `(check-sat)`. `unsat` => the refinement holds.
    //
    // We construct it as a single `(assert (not (forall ...)))` so one Z3
    // check-sat decides both directions.

    // Declare arg consts; bind BOTH the concrete's and spec's param nameMaps
    // to a shared `arg_<i>` Int/Bool so clauses written with either side's
    // param names resolve to the same SMT symbol. (We assume Int sort for
    // simplicity  -  most refinement targets are integer-valued; mismatches at
    // the SMT sort level for bool/real params are tolerated by overloading
    // the arg symbol to Int, which is a known looser-than-ideal encoding
    // matching the contract walker's Int-default policy. Documented below.)
    std::ostringstream argDecls;
    std::ostringstream argList;        // for the forall binder list
    SmtCtx c{out, nullptr, 0, 0, {}};
    c.curFn = "refines_" + rf->concreteName + "_" + rf->specName;
    // MISSING-#4 fix  -  seed the maps so spec-fn calls + const ints in the
    // spec/concrete clause bodies lower to real terms (not placeholders).
    c.specFns = specFns;
    c.constGlobals = constGlobals;
    c.funcDecls = funcDecls;
    c.methodDecls = methodDecls;

    // Declare arg_<i> consts (one per spec param; concrete params are aligned
    // by position).
    //
    // Missing-#6 fix (also affects #6's `emitPreserves` below): Z3's
    // `declare-const`s are GLOBAL  -  `(push)`/`(pop)` only saves the assertion
    // stack, NOT declarations. A program with multiple refines (and/or
    // preserves) would otherwise emit multiple `(declare-const arg_0 Int)`
    // lines, every block after the first aborting with `(error "constant
    // 'arg_0' ... already declared")` and leaving the prior `sat` in the log
    //  -  silently masking refines proofs as sat. So we prefix every per-block
    // symbol with `c.curFn` (which is `"refines_<conc>_<spec>"`  -  unique
    // per refines decl) so two refines over the same arity never collide.
    std::string argPref = c.curFn + "_arg_";
    for (size_t i = 0; i < spec->params.size(); ++i) {
      std::string arg = argPref + std::to_string(i);
      // We use smtSort of the spec param; if concrete's differs, we still bind
      // the SPEC param name (and the concrete param name) to the same arg sym.
      const char* sort = smtSort(spec->params[i].type);
      argDecls << "(declare-const " << arg << " " << sort << ")\n";
      argList << (i ? " " : "") << "(" << arg << " " << sort << ")";
      // Bind both nameMaps so clause terms with either side's param names resolve.
      c.nameMap[spec->params[i].name] = arg;
      if (i < concrete->params.size())
        c.nameMap[concrete->params[i].name] = arg;
    }
    // result for the ensures direction (concrete ensures references `result`).
    if (concrete->retType != BType::void_) {
      std::string r = argPref + "result";
      argDecls << "(declare-const " << r << " " << smtSort(concrete->retType) << ")\n";
      c.nameMap["result"] = r;
    }

    // ox:proof Lower spec body (the ensures-spec direction) into a term. The spec fn's
    // RETTYPE decides how it participates in the refinement obligation.
    //   - bool-spec:  the body IS a postcondition (a Bool). The obligation is
    //     `ensures_concrete(args) ==> spec_body(args)` directly.
    //   - value-spec: the body is a VALUE (Int/Real/...). The refinement says
    //     "the concrete's `result` equals the abstract spec's value", so the
    //     obligation is `ensures_concrete(args,result) ==> (result == spec_body(args))`
    //      -  `result` is declared as arg_result below. Treating the value body
    //     directly as a Bool postcondition would be ill-sorted (`=> ... <Int>`).
    std::string specBodyTerm = smtExpr(c, spec->body.get());
    std::string specPostcond;
    bool specReturnsBool = (spec->retType.tag == BType::Tag::bool_);
    if (specReturnsBool) {
      specPostcond = specBodyTerm;
    } else {
      // ox:unsafe `result` must be declared for the equality. If the concrete fn is void
      // (no result) and the spec returns a value, the refinement is ill-formed
      //  -  we skip with a note earlier would be ideal, but here we just emit
      // the equality against arg_result regardless; missing arg_result would
      // be a (now-validated) SMT-level symptom the user reads from the file.
      if (concrete->retType == BType::void_) {
        // Already-skipped path above requires ensures; we ensure arg_result
        // exists here unconditionally (declared below if the spec needs it),
        // and the SMT will report the void-mismatch if it ever happens.
      }
      specPostcond = "(= arg_result " + specBodyTerm + ")";
    }
    // Lower each concrete ensures clause and conjoin them.
    std::string concEnsTerm = "true";
    for (auto& e : concrete->ensures_) {
      std::string t = smtExpr(c, e.get());
      concEnsTerm = (concEnsTerm == "true") ? t : ("(and " + concEnsTerm + " " + t + ")");
    }
    // Lower each concrete requires clause and conjoin (premise direction).
    std::string concReqTerm = "true";
    for (auto& r : concrete->requires_) {
      std::string t = smtExpr(c, r.get());
      concReqTerm = (concReqTerm == "true") ? t : ("(and " + concReqTerm + " " + t + ")");
    }
    // ox:proof The spec fn has no requires (the body IS the spec / postcondition); the
    // refinement's precondition direction is just: the concrete's requires
    // must be satisfiable by some args (we don't discharge this  -  it's the
    // caller's job, same as the contract walker treats requires). The
    // postcondition direction: concrete's ensures ==> spec's postcondition
    // (either the spec body itself for bool-spec, or `result == spec_body`
    // for value-spec), under the concrete's requires as premise.
    //
    // Discharge query:
    //   (push)
    //   <argDecls>
    //   (assert (not (forall (<binders>)
    //                  (=> <concReqTerm> (=> <concEnsTerm> <specPostcond>)))))
    //   (check-sat)
    //   (pop)
    //
    // unsat => for all args, under the concrete's preconditions, the concrete's
    // ensures imply the spec's postcondition  -  the refinement holds.
    out << argDecls.str();
    out << "; refinement obligation: forall args, reqConc ==> (ensConc ==> specPost)\n";
    out << ";   specPost = ";
    if (specReturnsBool) out << "spec_body (bool-spec direct postcondition)\n";
    else                 out << "(= result spec_body) (value-spec: result equals abstract)\n";
    out << "(push)\n";
    out << "(assert (not (forall (" << argList.str() << ") "
        << "(=> " << concReqTerm << " (=> " << concEnsTerm << " " << specPostcond << ")))))\n";
    // A1: per-query tactic reset to prevent MBQI cache pollution from
    // earlier discharges leaking into this refines obligation.
    out << "(check-sat-using (then simplify smt))\n";
    out << "(pop)\n";
    out << "; note: refines discharge  -  unsat => abstract refinement holds\n\n";
  }
}

// Missing-#6  -  `preserves <handler_fn> <= <invariant_spec_fn>`
//
// Per-handler modular-composition discharge. Mirrors `emitRefines` in shape
// (one `check-sat` per decl, `unsat` ⇒ the property holds for all args), but
// WHERE THE POST-STATE COMES FROM is the soundness-critical difference:
//
//   * `refines` declares a fresh `arg_result` const and ASSUMES the concrete
//     fn's `ensures result == ...` clauses against it, then asks whether the
//     abstract spec follows. That's fine for the refines obligation because
//     the concrete fn's OWN per-fn discharge has already proven its ensures
//     against its body  -  we're free to take them as fact here.
//
//   * `preserves` cannot do that: the point of this query is to verify the
//     handler's BODY preserves the invariant, not to verify the handler's
//     ensures. So instead of a fresh `arg_result` const + assumed ensures, we
//     drive `smtConcreteCallResult`  -  Missing #2's WP mini-walker  -  to inline
//     the handler body at the top-level discharge site and produce a REAL
//     symbolic `result` term that captures the body's actual computation
//     (branches folded through `ite`, lets threaded through the store). The
//     invariant is then evaluated against THAT term. If the body genuinely
//     computes a value that violates the invariant, the query is `sat`
//     (honest); if the body preserves it, `unsat`.
//
// This is exactly the load-bearing distinction the Missing-#2 soundness test
// exercised (`liar_entry` lying in its ensures): `preserves` would refuse to
// discharge even if the handler's ensures SAID `result == idx`, because we
// use the body's actual value, not the assumed ensures. Per-handler
// preservation composes into "the machine preserves I" by induction over
// call sequences  -  each `preserves` decl IS the induction step for its
// handler, so discharging every handler independently IS the composition
// proof. No separate induction emission is needed (and emitting one would
// require a 2nd-order quantifier over sequences, which Oxide's SMT-LIB
// encoding deliberately avoids  -  the per-handler approach is sound AND
// first-order).
//
// G2b  -  finite-domain unrolling for a `forall k: T in lo..hi implies P` that
// appears as a preserves invariant body (the form `preserves h <= I` where
// `I(args) = forall k. bound(k) ==> P(args, k)`). Z3's MBQI over Int coupled
// with `(select (store arr idx v) k))` inside the quantifier body is unreliable
// (times out or returns `unknown`). The Dafny/Why3 playbook for small
// fixed-size arrays is to UNROLL the quantifier into a finite ground
// conjunction  -  `(and P(0) P(1) ... P(N-1))` where each P(i) is the body with
// the binder substituted by literal `i`. The result is a quantifier-free,
// ground formula that Z3's `simplify` tactic concretizes the select-over-store
// for each fixed index trivially.
//
// SOUNDNESS: the unrolled conjunction is EQUIVALENT to the original `forall`
// *only under the explicit bound `0 <= k < N`*. The substitution is sound iff:
//   1. `q->isForall == true` (exists would need a finite disjunction  -  separate
//      case, NOT handled here; we fall back to the recursive `smtExpr` forall
//      form to keep sound).
//   2. `q->lo` and `q->hi` are BOTH `IntLit` (the bound is statically known).
//      A dynamic/generic bound (a VarRef, an expression) is NOT unrollable  - 
//      we fall back to the original `forall` (sound failure, marked with a
//      `; note: unroll failed` line).
//   3. `q->binderType` is integral (Int sort) so a literal integer substitution
//      typechecks.
//   4. The unrolled magnitude `(|hi - lo|)` is bounded by a sanity cap (1024)
//       -  beyond which unrolling would blow the SMT file size into pathological
//      territory; fall back in that case too.
//
// Returns the unrolled SMT term, or an empty string when any soundness guard
// fails (caller falls back to the recursive `smtExpr` forall form). `note` is
// populated with a human-readable explanation for the `; note:` line.
static std::string smtUnrollQuantForall(SmtCtx& c, const QuantExpr* q,
                                        std::string& note) {
  note.clear();
  if (!q) return "";
  if (!q->isForall) {
    // ox:proof `exists k. ...` would need a finite DISJUNCTION, not a conjunction  -  a
    // different rewrite. Bail and let the recursive `smtExpr` QuantExpr arm
    // emit the real `exists`.
    note = "unroll skipped: invariant is `exists` (only `forall` is unrolled)";
    return "";
  }
  auto loLit = dynamic_cast<const IntLit*>(q->lo.get());
  auto hiLit = dynamic_cast<const IntLit*>(q->hi.get());
  if (!loLit || !hiLit) {
    // Dynamic bound (a VarRef, an expression referencing args/const-globals)  - 
    // the finite domain is NOT statically known. Unrolling would emit a
    // soundness-critical wrong number of conjuncts, so fall back to the real
    // `forall`. This is the documented sound-failure path.
    note = "unroll skipped: range bound is not a literal (dynamic/generic array)";
    return "";
  }
  if (q->binderType.tag != BType::Tag::i64 &&
      q->binderType.tag != BType::Tag::i32 &&
      q->binderType.tag != BType::Tag::u64 &&
      q->binderType.tag != BType::Tag::u32) {
    // ox:proof Only integral binders lower to SMT Int  -  a literal integer substitution
    // is unsound for anything else (bool, f32/f64, enum). Fall back.
    note = "unroll skipped: binder type is not integral";
    return "";
  }
  long long lo = (long long)loLit->v;
  long long hi = (long long)hiLit->v;
  long long loEff = lo;
  long long hiEff = q->inclusive ? hi + 1 : hi;  // exclusive upper for the loop
  if (hiEff < loEff) {
    // ox:proof Empty range  -  the `forall` is vacuously `true`. The unrolled form is the
    // SMT-true literal; downstream `(=> reqPremise true)` lets the
    // preservation obligation reduce to `reqPremise`-as-specified, matching the
    // quantifier's vacuous-discharge semantics exactly.
    note = "unrolled: empty range (vacuously true)";
    return "true";
  }
  long long count = hiEff - loEff;
  // ox:proof Sanity cap  -  beyond this, fall back to the quantifier (avoids pathological
  // blowup in SMT file size). 1024 is generous for fixed-size Oxide arrays;
  // the test shape uses N=4.
  const long long kMaxUnroll = 1024;
  if (count > kMaxUnroll) {
    note = "unroll skipped: range magnitude " + std::to_string(count) +
           " exceeds cap " + std::to_string(kMaxUnroll) +
           " (fall back to forall)";
    return "";
  }
  // Pin `c.nameMap[binder]` to each literal in turn, lower the body, and
  // collect the conjuncts. The pin works because the body's references to the
  // binder are `VarRef{binder}` which `smtExpr`'s VarRef arm resolves through
  // `c.nameMap`  -  same path the recursive forall arm takes. A literal
  // integer in the map emits bare (e.g. `"3"`) which Z3 accepts as an Int
  // ground term, so `(select arr 3)` and `page_aligned(...)` lower to their
  // concretized forms.
  std::vector<std::string> conj;
  auto mit = c.nameMap.find(q->binder);
  bool hadBind = (mit != c.nameMap.end());
  std::string savedBind = hadBind ? mit->second : "";
  for (long long k = loEff; k < hiEff; ++k) {
    c.nameMap[q->binder] = std::to_string(k);
    std::string t = smtExpr(c, q->body.get());
    if (t.empty()) t = "true";
    conj.push_back(t);
  }
  if (hadBind) c.nameMap[q->binder] = savedBind;
  else c.nameMap.erase(q->binder);
  // Assemble the conjunction. A single conjunct lowers to itself (no need to
  // wrap); many form `(and t0 t1 ... tn-1)`. Z3's `simplify` tactic handles
  // both shapes.
  note = "unrolled forall k: " + std::to_string(lo) +
         (q->inclusive ? "..=" : "..") + std::to_string(hi) +
         " -> finite conjunction of " + std::to_string(count) + " conjuncts";
  if (conj.size() == 1) return conj[0];
  std::string acc = conj[0];
  for (size_t i = 1; i < conj.size(); ++i)
    acc = "(and " + acc + " " + conj[i] + ")";
  return acc;
}

// `preserves <handler_fn> <= <invariant_spec_fn>`  -  Missing-#6 per-handler
// invariant-preservation discharge.
//
// Emits one discharge block per `preserves` decl. Mirror of `emitRefines` but
// for the per-handler induction-step form: query is
//   (assert (not (forall (args) (=> reqHandler(args) I(args, result)))))
// where `result` is the #2 WP mini-walker's inlined body terminal term (NOT a
// fresh const + assumed ensures  -  soundness-critical, see Missing #2).
//
// Bindings (mirrors `emitRefines`):
//   - One `(declare-const arg_<i> <sort>)` per handler param (positional).
//   - Handler param names AND invariant spec fn param names both bound to
//     `arg_<i>` in `c.nameMap`  -  so a clause written with either side's param
//     names resolves to the same SMT symbol (we ASSUME the invariant's params
//     align positionally with the handler's; an arity mismatch skips with a
//     `; note:` honesty line, never a false `unsat`).
//   - `c.nameMap["result"] = <inlined body term>`  -  so an invariant that
//     references `result` (the canonical name for the post-state value, same
//     convention as `ensures`) resolves to the body's computation.
//   - The invariant must be a Bool spec fn (its body IS a postcondition). A
//     non-Bool invariant is unsound to discharge this way  -  we skip with a
//     `; note:`.
//
// Skip honesty (mirrors `emitRefines`):
//   - unresolved handler or invariant spec fn
//   - arity mismatch (handler params vs invariant params)
//   - invariant spec fn has no body OR is non-Bool
//   - handler has no requires (degenerate: an unconstrained handler can do
//     anything, so the query is `sat` trivially  -  we still emit it honestly
//     so the user sees the discharge, BUT we ALSO `; note:` it so they know
//     the proof is vacuous without a precondition to constrain the body).
static void emitPreserves(const Program& prog, std::ostringstream& out) {
  if (prog.preserves_.empty()) return;
  out << "\n; ============================================================\n";
  out << "; Missing-#6  -  preserves (per-handler invariant-preservation)\n";
  out << "; ============================================================\n";

  // Same map-seeding as `emitRefines`  -  spec fns (so an invariant body that
  // composes other spec fns resolves), const-globals (so const int
  // cross-refs resolve), and concrete fn/method decls (so a handler requires
  // clause naming another fn resolves).
  std::map<std::string, const SpecFnDecl*> specFns = collectSpecFns(prog);
  std::map<std::string, long long> constGlobals = collectConstGlobals(prog);
  std::map<std::string, const FuncDecl*> funcDecls;
  for (auto& fn : prog.funcs) if (fn && !fn->isExtern) funcDecls[fn->name] = fn.get();
  std::map<std::string, const FuncDecl*> methodDecls;
  for (auto& im : prog.impls) {
    if (!im) continue;
    for (auto& m : im->methods) {
      if (!m || m->isExtern) continue;
      methodDecls[mangleMethod(im->structName, m->name)] = m.get();
    }
  }

  for (auto& pv : prog.preserves_) {
    if (!pv) continue;
    const FuncDecl* handler = findFuncByName(prog, pv->concreteName);
    const SpecFnDecl* inv   = findSpecFnByName(prog, pv->specName);
    out << "\n; preserves " << pv->concreteName << " <= " << pv->specName
        << "  (source line " << pv->line << ")\n";

    if (!handler) {
      out << "; note: preserves " << pv->concreteName << " <= " << pv->specName
          << " skipped  -  handler fn '" << pv->concreteName << "' not found\n";
      continue;
    }
    if (!inv) {
      out << "; note: preserves " << pv->concreteName << " <= " << pv->specName
          << " skipped  -  invariant spec fn '" << pv->specName << "' not found\n";
      continue;
    }
    if (!inv->body) {
      out << "; note: preserves " << pv->concreteName << " <= " << pv->specName
          << " skipped  -  invariant spec fn has no body\n";
      continue;
    }
    if (inv->retType.tag != BType::Tag::bool_) {
      out << "; note: preserves " << pv->concreteName << " <= " << pv->specName
          << " skipped  -  invariant spec fn must be Bool (got "
          << smtSort(inv->retType) << ")\n";
      continue;
    }
    // The invariant's params align positionally with the handler's params.
    // An arity mismatch means the invariant has a different view of the state
    // than the handler operates on  -  unsafe to discharge, skip honestly.
    if (handler->params.size() != inv->params.size()) {
      out << "; note: preserves " << pv->concreteName << " <= " << pv->specName
          << " skipped  -  arity mismatch (handler " << handler->params.size()
          << " vs invariant " << inv->params.size() << ")\n";
      continue;
    }

    // ox:proof -- Build the discharge query. --
    //
    // We negate the preservation obligation:
    //   NOT (forall args.  requires_handler(args)  ==>  I(args, result))
    // and `(check-sat)`. `unsat` ⇒ for all args satisfying the handler's
    // requires, the inlined body's result satisfies the invariant.
    //
    // The result term comes from `smtConcreteCallResult` (Missing-#2 WP mini-
    // walker, defined in src/Driver.cpp)  -  NOT a fresh const. This is the
    // soundness-critical bit: we evaluate the invariant against the body's
    // ACTUAL computation, not against an assumed `result == ...` from the
    // handler's own ensures. (If we did the latter, a handler that lies in
    // its ensures would falsely discharge its `preserves`.)
    std::ostringstream argList;        // for the forall binder list
    SmtCtx c{out, nullptr, 0, 0, {}};
    c.curFn = "preserves_" + pv->concreteName + "_" + pv->specName;
    c.specFns = specFns;
    c.constGlobals = constGlobals;
    c.funcDecls = funcDecls;
    c.methodDecls = methodDecls;

    // Declare arg_<i> consts and bind BOTH the handler's and the invariant's
    // param names to the same `arg_<i>` symbol  -  same convention as
    // `emitRefines`, so an invariant body written with the invariant's own
    // param names AND a handler requires clause written with the handler's
    // param names resolve to the same SMT symbol under one SmtCtx.
    //
    // Per-block prefix (`<curFn>_arg_<i>`): Z3 declare-consts are GLOBAL  -  a
    // preserves block over the same arity as another preserves (or as the
    // refines block) would otherwise re-declare `arg_0` and abort that
    // check-sat, masking the proof as sat. Same fix `emitRefines` now uses.
    // `c.curFn` is `"preserves_<handler>_<invariant>"`  -  unique per decl.
    std::vector<std::string> argSyms;   // <curFn>_arg_<i> per handler param, in order
    std::string argPref = c.curFn + "_arg_";
    // ── EMIT THE arg_<i> DECLARE-CONSTS IMMEDIATELY ─────────────────────────
    // This is load-bearing: `smtConcreteCallResult` (called just below) emits
    // define-funs referencing the arg symbols via `c.nameMap`  -  those define-
    // funs MUST appear in the file AFTER the declare-consts or Z3 reports
    // `(error "unknown constant preserves_..._arg_0")` and aborts that
    // check-sat, masking the proof as sat. So we declare-const in-line (no
    // buffering), then proceed to bind + walk. (`emitRefines` doesn't hit
    // this because it never invokes the mini-walker  -  its `arg_result` const
    // is referenced only by its OWN emitted clauses which all follow the
    // declare-const in the same buffered `argDecls` block.)
    for (size_t i = 0; i < handler->params.size(); ++i) {
      std::string arg = argPref + std::to_string(i);
      const char* sort = smtSort(handler->params[i].type);
      out << "(declare-const " << arg << " " << sort << ")\n";
      argList << (i ? " " : "") << "(" << arg << " " << sort << ")";
      c.nameMap[handler->params[i].name] = arg;
      if (i < inv->params.size())
        c.nameMap[inv->params[i].name] = arg;
      argSyms.push_back(arg);
    }
    // `result`  -  the load-bearing piece. We DO NOT declare a fresh const and
    // assume the handler's ensures against it (that's the unsound shortcut
    // Missing #2 soundness test rejects). Instead, drive `smtConcreteCallResult`
    // to inline the handler body under the just-established arg bindings.
    // The returned term IS the symbolic value the body computes (branches
    // folded through `(ite cond then else)` by mergeStores, lets threaded
    // through the WP store). Bind `c.nameMap["result"]` to it so an
    // invariant that names `result` resolves to the body's computation.
    //
    // We pass `pathCond = ""` and `premises = {}`  -  the path-condition and
    // caller-side premises are empty at the top-level discharge site (there
    // IS no caller here; the `forall` quantifier is what stands in for "any
    // call"). The callee's own requires are folded in BELOW as the premise
    // of the implication, not passed to the mini-walker, so the inlined
    // `assert`s in the body discharge against the unbounded args (i.e. the
    // walker sees all args, with the requires applied only via the implication
    // premise  -  exactly the right shape for "forall args satisfying req").
    //
    // Unique label prefix: `preserves_<handler>_<invariant>` (with
    // `c.assertSeq`-disambiguated `_inline_<n>` suffix inside the walker).
    // Same load-bearing reason as the call-site path: multiple `preserves`
    // decls against the same handler would otherwise mint duplicate
    // `_invariant_d0_0` define-fun names and Z3 aborts that check-sat.
    //
    // Gap 1a  -  capture the post-state store. For handlers that mutate arrays
    // (via `ept[i] = e`), the WP mini-walker's thread of `store[arrName]`
    // (the Gap 1b `(store arr i v)` update) lands in here. We then (Gap 1c,
    // just below) rebind the array-typed handler+invariant param names to
    // the post-state term, so the invariant body's `ept[k]` lowers through
    // the Index arm to `(select <post-state-array> k)`  -  seeing the array
    // AFTER the handler's mutation, which is what a `preserves` obligation
    // over mutable state means.
    std::map<std::string, std::string> postStore;
    std::string resultTerm;
    if (handler->retType != BType::void_) {
      // The Missing-#2 mini-walker. `smtConcreteCallResult` already seeds a
      // fresh `result` const internally for fall-through paths and threads
      // the WP store through every assignment + branch, returning the
      // terminal `store["result"]` term. See the long comment block at its
      // definition (src/Driver.cpp ~2516) for the full contract.
      resultTerm = smtConcreteCallResult(c, handler,
                                          /*labelBase=*/c.curFn,
                                          /*args=*/argSyms,
                                          /*pathCond=*/"",
                                          /*premises=*/{},
                                          /*postStore=*/&postStore);
      if (resultTerm.empty()) {
        // Defensive: the walker always returns the seed const for void callees
        // or bodies it can't analyze, so an empty term means we couldn't even
        // mint the seed  -  emit honestly.
        out << "; note: preserves " << pv->concreteName << " <= " << pv->specName
            << " skipped  -  body inlining returned no result term\n";
        continue;
      }
      c.nameMap["result"] = resultTerm;
    } else {
      // Void handler  -  still drive the walker so mutation post-state gets
      // captured (a void handler can mutate an array and the invariant holds
      // in the post-state without a result term). Empty result is fine here.
      resultTerm = smtConcreteCallResult(c, handler,
                                          /*labelBase=*/c.curFn,
                                          /*args=*/argSyms,
                                          /*pathCond=*/"",
                                          /*premises=*/{},
                                          /*postStore=*/&postStore);
    }

    // Gap 1c  -  thread post-state for mutable array params. For each handler
    // param of array type, the mini-walker may have updated `postStore[name]`
    // (via Gap 1b's `(store arr i v)` update). When present, rebind the
    // handler's AND the invariant's view of that param to the post-state
    // term  -  so the invariant body's `arr[k]` lowers through the Index arm
    // to `(select <post-state-array> k)`, seeing the array AFTER the
    // mutation. We ALSO declare `old_<arrName>` symbols for array params so
    // an invariant that writes `old(arr)` resolves to the PRE-state `arg_<i>`
    //  -  this is what lets an invariant prove that mutation is "in-bounds" by
    // comparing post- against pre-state.
    //
    // Param-name keys live in `postStore` under the SOURCE name (the walker
    // threads names not arg_<i> separated symbols  -  it sees the callee's
    // real param names from `callee->params[i].name`, then seeds
    // `calleeStore[<srcName>] = args[i]`). So the lookup is by
    // `handler->params[i].name`.
    for (size_t i = 0; i < handler->params.size(); ++i) {
      if (handler->params[i].type.tag != BType::Tag::array) continue;
      const std::string& srcName = handler->params[i].name;
      auto pit = postStore.find(srcName);
      if (pit == postStore.end() || pit->second.empty()) continue;
      // Only rebind when the post term actually differs from the pre-state
      // `arg_<i>` (mutating handlers differ; pure handlers leave the seed).
      // This is just an optimization  -  re-binding to the same term is a
      // no-op for Z3  -  but it keeps the discharge honest about "did this
      // handler mutate this array at all" via the emitted bindings.
      std::string preTerm = argSyms[i];
      if (pit->second == preTerm) continue;
      // ox:proof Bind `old(arr)` in the invariant body directly to the BOUND forall
      // variable `arg_<i>`  -  the pre-state of a preserves is the incoming
      // array, which IS the quantified variable. Previously we declared a
      // SEPARATE top-level `old_<arg>` const and asserted `(= old_<arg> arg)`
      // at top level  -  but that top-level `arg` was a FREE const, not the
      // forall-bound `arg`, so inside `(forall ((... arg)) ...)` the old_
      // symbol tracked an UNCONSTRAINED array instead of the bound pre-state.
      // Mapping old_srcName directly to `argSyms[i]` makes the invariant body
      // emit `(select <bound-arg> k)` for old(arr)[k]  -  correct, quantifier-
      // clean, no extraneous declare-const or assert to confuse Z3.
      c.nameMap["old_" + srcName] = preTerm;
      // Rebind the handler's AND the invariant's view of this param to the
      // POST-state term. (Both name maps point at the same memory cell  - 
      // positional alignment guarantees they're the same slot.)
      c.nameMap[srcName] = pit->second;
      if (i < inv->params.size() && inv->params[i].name != srcName) {
        c.nameMap[inv->params[i].name] = pit->second;
      }
    }

    // ox:proof Lower the invariant body (a Bool spec fn expression). Under the
    // nameMap we just built, every param name (handler's or invariant's)
    // resolves to `arg_<i>` and `result` resolves to the inlined body term.
    // For array params the handler mutated, the nameMap now points to the
    // POST-state array term (Gap 1c above); `(select post_array k)` reads
    // the post-state element.
    //
    // -- G2b  -  finite-domain unrolling for a top-level `forall k: T in lo..hi
    //    implies P` invariant body. When the invariant spec fn body IS itself a
    //    forall over a LITERAL finite bound (e.g. `forall k: i64 in 0..4
    //    implies page_aligned(ept[k])`), Z3's MBQI over Int + array select-store
    //    inside the embedded quantifier body is unreliable (times out / returns
    //    `unknown`). The Dafny/Why3 playbook is to UNROLL the quantifier into a
    //    finite ground conjunction `(and P(0) P(1) ... P(N-1))` (each P(i) is
    //    the body with the binder substituted by literal i). The result is
    //    quantifier-free and Z3's `simplify` tactic concretizes the
    //    select-over-store for each fixed index trivially  -  discharging `unsat`.
    //
    //    SOUND: the unrolled conjunction is EQUIVALENT to the original `forall`
    //    UNDER the explicit literal bound `0 <= k < N` (see
    //    `smtUnrollQuantForall` header). When any soundness guard fails (non-
    //    forall, non-literal bound, non-integral binder, magnitude > 1024), we
    //    keep the recursive `smtExpr` forall form and emit a `; note:` so the
    //    user knows the unroll failed. This is a SOUND failure  -  no false unsat.
    //
    //    ORDER: when unrolling applies, we skip the recursive `smtExpr`
    //    lowering ENTIRELY for the forall body (the recursive arm would lower
    //    to `(forall ((k Int)) ...)`, which is exactly the unreliable shape
    //    we're avoiding). Only on sound fallback do we lower via `smtExpr`.
    std::string invTerm;
    // ox:proof `invTermIsQuant`: true iff the invariant body reaches Z3 as a
    // REAL `(forall ((k Int)) ...)`  -  i.e. the sound-fallback path. Set
    // only when `smtUnrollQuantForall` could NOT unroll the body (the only
    // path that emits a genuine quantifier from this preserves obligation).
    // The unrolled branch leaves it `false` (the body is a ground
    // conjunction  -  no quantifier to instantiate). Used below to choose a
    // tactics hint targeted at the pathological
    // `(forall ((k Int)) (=> range (=> (select old ...) (select (store ...) k))))`
    // shape, where Z3's default MBQI is fragile over Int + Array
    // select/store (see the §A1 comment block at the check-sat site below).
    bool invTermIsQuant = false;
    const QuantExpr* quantBody =
        dynamic_cast<const QuantExpr*>(inv->body.get());
    if (quantBody) {
      std::string note;
      std::string unrolled = smtUnrollQuantForall(c, quantBody, note);
      if (!unrolled.empty()) {
        out << "; G2b: preserves " << pv->concreteName << " <= " << pv->specName
            << "  -  unrolled invariant body (forall -> finite conjunction)\n";
        out << ";   " << note << "\n";
        out << ";   original (forall ((k Int)) (=> (and (>= k lo) (< k hi)) "
               "BODY)) replaced with the unrolled conjunction above\n";
        invTerm = unrolled;
      } else if (!note.empty()) {
        out << "; G2b: preserves " << pv->concreteName << " <= " << pv->specName
            << "  -  " << note << " (sound fallback to original forall)\n";
        // ox:unsafe Sound fallback: lower the body via the recursive QuantExpr arm.
        // A REAL `(forall ((k Int)) (=> range body))` now reaches Z3  -  mark
        // it so the check-sat site below picks the quantifier-targeted tactic.
        invTerm = smtExpr(c, inv->body.get());
        invTermIsQuant = true;
      } else {
        // ox:note Defensive: empty note AND empty result  -  fall back to the ordinary
        // lowering; this branch is unreachable given the helper's contract.
        invTerm = smtExpr(c, inv->body.get());
        invTermIsQuant = true;
      }
    } else {
      // Non-quantified invariant body  -  ordinary lowering (no unroll applies).
      invTerm = smtExpr(c, inv->body.get());
    }

    // Lower each handler requires clause and conjoin  -  the premise of the
    // preservation implication. (The invariant spec fn has no requires  -  its
    // body IS the spec.)
    std::string reqPremise = "true";
    for (auto& r : handler->requires_) {
      std::string t = smtExpr(c, r.get());
      reqPremise = (reqPremise == "true") ? t : ("(and " + reqPremise + " " + t + ")");
    }
    if (handler->requires_.empty()) {
      // No requires ⇒ the premise is vacuously `true` ⇒ the query is just
      // `forall args. I(args, result)`. Honest, but emit a `; note:` so the
      // user knows the proof is vacuous (an unconstrained handler can do
      // anything, so proving it preserves anything is a strong claim they
      // should know they're making).
      out << "; note: preserves " << pv->concreteName << " <= " << pv->specName
          << "  -  handler has no requires; discharge is over UNBOUNDED args\n";
    }

    // ox:proof Discharge query:
    //   (push)
    //   <argDecls>
    //   (assert (not (forall (<binders>)
    //                (=> <reqPremise> <invTerm>))))
    //   (check-sat)
    //   (pop)
    //
    // unsat ⇒ for all args, under the handler's preconditions, the inlined
    // body's result makes the invariant hold  -  the handler preserves I.
    // (The arg-const declare-consts were already emitted to `out` above,
    // before the mini-walker ran  -  see comment in the binding loop.)
    out << "; preservation obligation: forall args, reqHandler ==> I(args, result)\n";
    out << ";   reqHandler = conjunction of handler's requires clauses\n";
    out << ";   I(args, result) = invariant spec fn body, with `result` bound to\n";
    out << ";     the #2 WP mini-walker's inlined body terminal term\n";
    out << ";     (NOT a fresh const + assumed ensures  -  soundness-critical)\n";
    out << "(push)\n";
    out << "(assert (not (forall (" << argList.str() << ") "
        << "(=> " << reqPremise << " " << invTerm << "))))\n";
    // ox:proof A1: per-query tactic reset  -  prevents earlier `sat`/`unknown` MBQI
    // state from leaking into this preserves discharge (the load-bearing
    // query for the verify-table preserves row).
    //
    // §A1  -  quantifier-targeted tactic for the sound-fallback path.
    // When `invTermIsQuant` (the invariant body is a REAL
    //   (forall ((k Int)) (=> (and (>= k lo) (< k hi))
    //                          (=> (select old_arr k) (select (store ...) k))))
    // shaped obligation), Z3's DEFAULT model-based quantifier instantiation
    // (MBQI) is empirically fragile over Int-sorted array `select`/`store`
    // inside the quantifier body  -  it can return `sat` (false counter-
    // example) or `unknown` where the obligation is actually `unsat`.
    // Z3's e-matching-based SMT instantiation (`(using-params smt :mbqi
    // false)`) is the targeted alternative: it pre-computes instantiation
    // patterns from `select`/`store`/`mod` positions in the body and
    // relies on the SMT core's congruence closure over the array theory,
    // which is exactly the structure that makes `select arr k` vs
    // `select (store arr idx v) k` decidable (reads-over-writes axioms).
    // We DISABLE MBQI scopped to this single check-sat ONLY  -  the
    // `(set-option :smt.mbqi ...)` calls sit BETWEEN `(push)`...`(pop)`,
    // but Z3 treats `smt.mbqi` as a GLOBAL option, so we restore `true`
    // AFTER the check-sat (and the trailing `(pop)` resets scope). This
    // keeps subsequent obligations on the fast default-MBQI path.
    //
    // We keep `(then simplify smt)` as the tactic (proven sound/fast on
    // the unrolled-ground path above); `simplify` normalises the store-
    // select chain and `smt` (now with MBQI off) does the deliberative
    // work. NB: this is a TACTIC hint, not a soundness change  -  the Body
    // term reaching Z3 is identical; only Z3's search STRATEGY changes,
    // and a TACTIC can only delay or fail (`unknown`), never answer `unsat`
    // for an obligation that is actually `sat` (Z3 tactics are refutation-
    // complete: an `unsat` from `smt` is always a true `unsat`).
    if (invTermIsQuant) {
      out << "; A1-quant: real (forall ((k Int)) ...) body  -  targeting with "
             "MBQI-off SMT instantiation (array select/store reads-over-writes)\n";
      out << "(set-option :smt.mbqi false)\n";
      out << "(check-sat-using (then simplify smt))\n";
      // Restore the default fast path for subsequent obligations.
      out << "(set-option :smt.mbqi true)\n";
    } else {
      out << "(check-sat-using (then simplify smt))\n";
    }
    out << "(pop)\n";
    out << "; note: preserves discharge  -  unsat => handler preserves invariant\n\n";
  }
}

// ox:proof D8  -  `noninterference <h1>, <h2>, ... <= <invariant_spec_fn>;`
// Owicki-Gries non-interference (stability) discharge.
//
// For each ORDERED pair (hA, hB) of handlers drawn from the decl's handler
// list with hA ≠ hB, emits one discharge query:
//
//   NOT (forall (hA_args, hB_args).  req_hA(hA_args)
//                              ∧ I(hB_args, shared_pre)
//                              ∧ (shared_post == step_hA(hA_args, shared_pre))
//                              ==> I(hB_args, shared_post))
//
// where:
//   - `step_hA` is hA's inlined body (via smtConcreteCallResult  -  the same
//     #2 WP mini-walker preserves uses), producing the post-state of hA's
//     atomic step over the shared state.
//   - `I(hB_args, state)` is the invariant spec fn body lowered against
//     hB's params bound to fresh arg consts, and the shared state bound to
//     `state` (pre or post).
//   - `req_hA` is the conjunction of hA's requires clauses (the premise
//     constraining hA's step  -  the hardware guarantees these for a trap
//     handler, and the caller guarantees them for a regular fn).
//
// `unsat` ⇒ for every possible pre-state and hA step, the invariant that
// hB relies on remains valid after hA runs. This is the cross-handler
// stability obligation that completes the Owicki-Gries proof:
//   preserves (per-handler sequential correctness) +
//   noninterference (cross-handler stability)
//   ⇒ the full system preserves I under every interleaving of steps.
//
// SHARED STATE MODEL: The invariant spec fn's params model the shared state.
// We model the interleaving by declaring the invariant's params as fresh SMT
// consts for hB's view (`inv_B_arg_<i>`), and hA's params as fresh consts
// (`hA_arg_<i>`). The step's effect is captured by driving the mini-walker
// on hA's body with hA's args, producing post-state terms (the `postStore`
// out-param threads array mutations). We then re-evaluate the invariant
// against the post-state by rebinding hA's mutated array params to their
// post-state terms before lowering the invariant body a second time.
//
// For handlers WITHOUT array mutations (pure-value handlers), the pre and
// post invariant terms are identical (the shared state doesn't change), so
// the discharge is trivially unsat  -  a sound no-op. The interesting case is
// a handler that mutates a shared array: the post-state store(select chain)
// flows into the invariant's second evaluation.
//
// Skip honesty (mirrors preserves/refines): unresolved handler or invariant,
// arity mismatch, non-Bool invariant, body-inlining failure, and a handler
// list with < 2 entries all emit a `; note:` and skip  -  NEVER a false unsat.
static void emitNoninterference(const Program& prog, std::ostringstream& out) {
  if (prog.noninterference_.empty()) return;
  out << "\n; ============================================================\n";
  out << "; D8  -  noninterference (Owicki-Gries cross-handler stability)\n";
  out << "; ============================================================\n";

  // Same map-seeding as emitPreserves/emitRefines.
  std::map<std::string, const SpecFnDecl*> specFns = collectSpecFns(prog);
  std::map<std::string, long long> constGlobals = collectConstGlobals(prog);
  std::map<std::string, const FuncDecl*> funcDecls;
  for (auto& fn : prog.funcs) if (fn && !fn->isExtern) funcDecls[fn->name] = fn.get();
  // ox:unsafe Also include trap handlers (they ARE FuncDecls, but live in
  // prog->trapHandlers, not prog->funcs).
  for (auto& fn : prog.trapHandlers) if (fn) funcDecls[fn->name] = fn.get();
  std::map<std::string, const FuncDecl*> methodDecls;
  for (auto& im : prog.impls) {
    if (!im) continue;
    for (auto& m : im->methods) {
      if (!m || m->isExtern) continue;
      methodDecls[mangleMethod(im->structName, m->name)] = m.get();
    }
  }

  for (auto& ni : prog.noninterference_) {
    if (!ni) continue;

    // ox:proof Resolve the invariant spec fn (shared across all pairs in this decl).
    const SpecFnDecl* inv = findSpecFnByName(prog, ni->specName);
    out << "\n; noninterference";
    for (auto& h : ni->handlers) out << " " << h;
    out << " <= " << ni->specName
        << "  (source line " << ni->line << ")\n";

    if (!inv) {
      out << "; note: noninterference … <= " << ni->specName
          << " skipped  -  invariant spec fn '" << ni->specName << "' not found\n";
      continue;
    }
    if (!inv->body) {
      out << "; note: noninterference … <= " << ni->specName
          << " skipped  -  invariant spec fn has no body\n";
      continue;
    }
    if (inv->retType.tag != BType::Tag::bool_) {
      out << "; note: noninterference … <= " << ni->specName
          << " skipped  -  invariant spec fn must be Bool (got "
          << smtSort(inv->retType) << ")\n";
      continue;
    }
    if (ni->handlers.size() < 2) {
      out << "; note: noninterference with < 2 handlers  -  no pairs to check "
             "(trivially stable, nothing discharged)\n";
      continue;
    }

    // Enumerate all ordered pairs (hA, hB) with hA ≠ hB.
    int pairCount = 0;
    for (size_t a = 0; a < ni->handlers.size(); ++a) {
      for (size_t b = 0; b < ni->handlers.size(); ++b) {
        if (a == b) continue;  // a handler can't interfere with itself
        const FuncDecl* hA = findFuncByName(prog, ni->handlers[a]);
        const FuncDecl* hB = findFuncByName(prog, ni->handlers[b]);

        out << "\n; --- pair: " << ni->handlers[a] << " step vs "
            << ni->handlers[b] << " assertion ---\n";

        if (!hA) {
          out << "; note: skipped  -  step handler '" << ni->handlers[a]
              << "' not found\n";
          continue;
        }
        if (!hB) {
          out << "; note: skipped  -  assertion handler '" << ni->handlers[b]
              << "' not found\n";
          continue;
        }

        // Arity check: both handlers' params must align positionally with
        // the invariant's params (same convention as preserves/refines).
        // hA and hB may have different arities from each other (if they take
        // different subsets of shared state), but each must individually
        // align with the invariant's params.
        if (hA->params.size() != inv->params.size()) {
          out << "; note: skipped  -  step handler '" << ni->handlers[a]
              << "' arity (" << hA->params.size()
              << ") ≠ invariant arity (" << inv->params.size() << ")\n";
          continue;
        }
        if (hB->params.size() != inv->params.size()) {
          out << "; note: skipped  -  assertion handler '" << ni->handlers[b]
              << "' arity (" << hB->params.size()
              << ") ≠ invariant arity (" << inv->params.size() << ")\n";
          continue;
        }

        // ox:proof ── Build the stability discharge query. ──
        //
        // We negate the stability obligation:
        //   NOT (forall (shared_args).
        //          req_hA(shared_args) ∧ I(shared_args)
        //          ==> I(shared_args_post))
        // where `shared_args` are the SHARED state variables (hA and hB
        // operate on the SAME shared state  -  they're concurrent threads
        // accessing the same EPT tables, VMCS fields, etc.), and
        // `shared_args_post` is the post-state after hA's atomic step.
        //
        // The post-state is obtained by driving smtConcreteCallResult on
        // hA's body  -  the mini-walker produces post-state terms via
        // postStore (for array mutations) or the result term (for pure
        // value handlers). We lower the invariant TWICE:
        //   - I_pre: invariant body with the shared args (pre-state)
        //   - I_post: invariant body with the post-state terms
        // And discharge: req_hA ∧ I_pre ==> I_post.
        //
        // KEY INSIGHT (Owicki-Gries): hB's pending assertion is the
        // invariant evaluated over the SAME shared state hA operates on.
        // hA and hB don't have independent args  -  they share state. So
        // we declare ONE set of arg consts (the shared state), drive hA's
        // body to get the post-state, and check I(pre) ==> I(post).

        std::string curFnA = "noninterference_" + ni->handlers[a] + "_vs_" + ni->handlers[b];
        std::ostringstream argList;

        // One SmtCtx for the pair. We declare one set of shared args
        // (matching hA's params positionally  -  hA and hB must have the
        // same arity for them to share state, which we already checked
        // above since both align with the invariant).
        SmtCtx c{out, nullptr, 0, 0, {}};
        c.curFn = curFnA;
        c.specFns = specFns;
        c.constGlobals = constGlobals;
        c.funcDecls = funcDecls;
        c.methodDecls = methodDecls;

        std::vector<std::string> sharedSyms;  // the shared state args

        // Declare the shared args (one per hA param = one per invariant param).
        for (size_t i = 0; i < hA->params.size(); ++i) {
          std::string sym = curFnA + "_arg_" + std::to_string(i);
          const char* sort = smtSort(hA->params[i].type);
          out << "(declare-const " << sym << " " << sort << ")\n";
          argList << " (" << sym << " " << sort << ")";
          sharedSyms.push_back(sym);
        }

        // ── Lower I_pre: invariant body with shared args as pre-state ──
        // Bind the invariant's param names AND hB's param names to the
        // shared args (they're the same state  -  Owicki-Gries).
        SmtCtx cPre{out, nullptr, 0, 0, {}};
        cPre.curFn = curFnA + "_pre";
        cPre.specFns = specFns;
        cPre.constGlobals = constGlobals;
        cPre.funcDecls = funcDecls;
        cPre.methodDecls = methodDecls;
        for (size_t i = 0; i < hA->params.size() && i < inv->params.size(); ++i) {
          cPre.nameMap[inv->params[i].name] = sharedSyms[i];
          cPre.nameMap[hA->params[i].name] = sharedSyms[i];
          cPre.nameMap[hB->params[i].name] = sharedSyms[i];
          cPre.declared.insert(sharedSyms[i]);
        }

        std::string invPreTerm = smtExpr(cPre, inv->body.get());

        // ── Drive the mini-walker on hA's body to get the post-state ──
        // Bind hA's params to the shared args in c.nameMap, then drive walker.
        for (size_t i = 0; i < hA->params.size(); ++i) {
          c.nameMap[hA->params[i].name] = sharedSyms[i];
          if (i < inv->params.size())
            c.nameMap[inv->params[i].name] = sharedSyms[i];
          c.declared.insert(sharedSyms[i]);
        }

        std::map<std::string, std::string> postStore;
        std::string resultTerm;
        // D8  -  suppress the call-requires sub-discharges that
        // smtConcreteCallResult would emit under smtWithCalleeBindings. We
        // only need hA's post-state (the inlined result term + postStore
        // mutations); the call-requires queries are hA's preconditions
        // discharged as standalone theorems  -  they trivially come back `sat`
        // (a precondition isn't a tautology) and would pollute the verify
        // report with spurious `sat` rows, shadowing the actual noninterference
        // stability check-sat and getting the parent `noninterference ...`
        // row misattributed. The suppression guard is local: emitsRespects
        // and downstream code still run normally; the flag is only set for
        // this one mini-walker drive.
        c.suppressCallRequires = true;
        if (hA->retType != BType::void_) {
          resultTerm = smtConcreteCallResult(c, hA,
                                             /*labelBase=*/c.curFn,
                                             /*args=*/sharedSyms,
                                             /*pathCond=*/"",
                                             /*premises=*/{},
                                             /*postStore=*/&postStore);
          if (resultTerm.empty()) {
            c.suppressCallRequires = false;  // restore before skip
            out << "; note: skipped  -  hA body inlining returned no result term\n";
            continue;
          }
        } else {
          // Void handler  -  still walk for mutation post-state.
          resultTerm = smtConcreteCallResult(c, hA,
                                             /*labelBase=*/c.curFn,
                                             /*args=*/sharedSyms,
                                             /*pathCond=*/"",
                                             /*premises=*/{},
                                             /*postStore=*/&postStore);
        }
        c.suppressCallRequires = false;  // restore  -  invariant lowering below
                                         // is plain smtExpr, not a call site

        // ── Lower I_post: invariant body with post-state terms ──
        // For array params that hA mutated, replace the shared arg with
        // the post-state term. For non-mutated params, the shared state
        // is unchanged (same shared symbol). For the result, bind it to
        // hA's inlined result term.
        SmtCtx cPost{out, nullptr, 0, 0, {}};
        cPost.curFn = curFnA + "_post";
        cPost.specFns = specFns;
        cPost.constGlobals = constGlobals;
        cPost.funcDecls = funcDecls;
        cPost.methodDecls = methodDecls;
        for (size_t i = 0; i < hA->params.size() && i < inv->params.size(); ++i) {
          // Default: shared arg (unchanged state)
          std::string sym = sharedSyms[i];
          // If hA mutated an array param at this slot, use post-state term.
          if (hA->params[i].type.tag == BType::Tag::array) {
            const std::string& hAName = hA->params[i].name;
            auto pit = postStore.find(hAName);
            if (pit != postStore.end() && !pit->second.empty()
                && pit->second != sharedSyms[i]) {
              sym = pit->second;  // post-state array term
              out << "; note: " << ni->handlers[a] << " mutated shared array '"
                  << hAName << "'  -  I_post uses post-state term " << sym << "\n";
            }
          }
          cPost.nameMap[inv->params[i].name] = sym;
          cPost.nameMap[hA->params[i].name] = sym;
          cPost.nameMap[hB->params[i].name] = sym;
          cPost.declared.insert(sym);
        }
        // If the handler returns a value and the invariant uses `result`,
        // bind it to the inlined result term.
        if (!resultTerm.empty() && hA->retType != BType::void_) {
          cPost.nameMap["result"] = resultTerm;
        }

        std::string invPostTerm = smtExpr(cPost, inv->body.get());

        // ── Lower hA's requires clauses (the premise for the step) ──
        // These are lowered under c (which has hA's params bound to shared args).
        std::string reqPremise = "true";
        for (auto& r : hA->requires_) {
          std::string t = smtExpr(c, r.get());
          reqPremise = (reqPremise == "true") ? t : ("(and " + reqPremise + " " + t + ")");
        }
        if (hA->requires_.empty()) {
          out << "; note: step handler '" << ni->handlers[a]
              << "' has no requires  -  step is unconstrained\n";
        }

        // ── Discharge ──
        // (push)
        // (assert (not (forall (shared_args)
        //   (=> (and req_hA I_pre) I_post))))
        // (check-sat)
        // (pop)
        //
        // unsat ⇒ for every shared pre-state and hA step satisfying hA's
        // requires, if the invariant held BEFORE hA's step (I_pre), it
        // still holds AFTER (I_post)  -  hA doesn't interfere with hB.
        out << "; stability obligation: forall shared_args,\n";
        out << ";   req_hA(shared) ∧ I(shared) ==> I(shared_post)\n";
        out << ";   I_pre  = " << invPreTerm << "\n";
        out << ";   I_post = " << invPostTerm << "\n";
        out << "(push)\n";
        std::string body;
        if (invPreTerm == invPostTerm) {
          // Trivially stable: I_pre == I_post (no mutation or same term).
          out << "; note: I_pre == I_post  -  trivially stable (no mutation detected)\n";
          body = "(=> " + reqPremise + " " + invPostTerm + ")";
        } else {
          body = "(=> (and " + reqPremise + " " + invPreTerm + ") " + invPostTerm + ")";
        }
        out << "(assert (not (forall (" << argList.str() << ") " << body << ")))\n";
        out << "(check-sat-using (then simplify smt))\n";
        out << "(pop)\n";
        out << "; note: noninterference discharge  -  unsat => "
            << ni->handlers[a] << " does not interfere with "
            << ni->handlers[b] << "\n\n";
        ++pairCount;
      }
    }
    if (pairCount == 0) {
      out << "; note: no valid handler pairs discharged (see skip notes above)\n";
    }
  }
}

// D9 (gap #6)  -  `cycle_preserves <handler>, <handler>, ... <= <invariant>;`
// VM-exit-cycle refinement discharge (per-handler across the trap cycle).
//
// For EACH handler in the decl's handler list, emits ONE discharge query:
//
//   NOT (forall (cycle_args).  req_handler(cycle_args) ∧ I_pre(cycle_args)
//                              ==> I_post(cycle_args_post_handler))
//
// where:
//   - `I_pre(cycle_args)` is the invariant spec fn body lowered against the
//     cycle args (the pre-`vmlaunch` guest state  -  a vector of consts sized to
//     the invariant's params, same convention as preserves/noninterference).
//   - `I_post(cycle_args_post_handler)` is the invariant body lowered against
//     the post-`vmresume` state, which EQUALS the post-handler state (the
//     handler ran, mutated shared state, then vmresume re-entered the guest;
//     the invariant must hold at the NEXT VM exit).
//   - the post-handler state is obtained by driving `smtConcreteCallResult`
//     (the same #2 WP mini-walker preserves/noninterference use) on the
//     handler's body with the cycle args, producing post-state terms via the
//     `postStore` out-param (for array mutations) or the result term (for pure
//     value handlers).
//   - `req_handler` is the conjunction of the handler's requires clauses (the
//     hardware guarantees these for a trap handler; the caller guarantees
//     them for a regular fn).
//
// `unsat` ⇒ for every pre-launch state matching the handler's preconditions,
// the handler's inlined body yields a post-state in which the cycle invariant
// still holds at the next VM exit  -  the handler re-establishes the invariant
// across the trap cycle.
//
// ── THE VM ENTRY→EXIT→RESUME IS MODELLED AS IDENTITY ON THE CYCLE INVARIANT ──
// The KEY DIFFERENCE from `preserves`: `cycle_preserves` EXPLICITLY
// acknowledges the vmlaunch/vmresume transition. This is modelled as IDENTITY
// at the invariant level  -  vmlaunch/vmresume do NOT touch the INVARIANT's
// state (only the handler does). So `shared_pre` (the pre-vmlaunch guest
// state) and `shared_post` (the post-vmresume state) are the SAME SMT symbol
// for the invariant's UNCHANGED slots, and only the handler-mutated slots
// flow through the mini-walker's post-state. Emitting a `; note: VM entry->
// exit->resume modelled as identity on cycle invariant ...` line documents the
// trust boundary for every discharge.
//
// ── WHY THIS IS THE OWICKI-GRY obligation #1+successor W.R.T. THE TRAP CYCLE ──
// The Owicki-Gries method proves concurrent correctness as:
//   (1) each handler individually preserves the invariant (sequential
//       correctness)  -  discharged by `preserves` (Missing-#6);
//   (2) one handler's step doesn't falsify the invariant another relies on
//       (non-interference / stability)  -  discharged by `noninterference` (D8);
//   (3) the invariant is re-established across the FULL trap cycle
//       (vmlaunch → guest runs → VM exit → handler runs → vmresume → guest
//       runs → …) as a single induction step per handler  -  discharged HERE by
//       `cycle_preserves` (D9 / gap #6).
// `cycle_preserves` is obligation #1+successor w.r.t. the cycle: each handler's
// step is the induction step (it mutates the state), and the cycle invariant
// is the induction hypothesis that must be re-established after each handler
// run before the next vmresume re-enters the guest. The "successor" is the
// next VM exit; the proof obligation is exactly `I_pre ∧ step ==> I_post`,
// which is the shape SMT discharges below.
//
// ── TRUST BOUNDARY ──
//   - handler-body preservation is discharged by `preserves` (per-handler);
//   - cross-handler pair overlap is discharged by `noninterference`;
//   - the VM transitions (vmlaunch/vmresume) are TRUSTED (modelled as identity
//     on the invariant's state)  -  `cycle_preserves` does NOT re-prove them; it
//     ties together the handler-body and cross-handler obligations across the
//     trap cycle. The trust boundary sits at vmresume/vmlaunch: the hardware
//     guarantees these transitions don't corrupt the invariant's abstract state
//     (the VMCS fields the invariant ranges over are saved/restored by the
//     CPU on exit/entry), so modelling them as identity is sound.
//
// `suppressCallRequires = true` is set around the mini-walker drive (same
// pattern as emitNoninterference, Ghost.cpp ~1680 - 1707) so the call-requires
// sub-discharges the walker would otherwise emit don't leak into the verify
// report as spurious `sat` rows shadowing the actual cycle_preserves
// check-sat. The result term + post-state store (the only things this emitter
// actually needs) are still produced.
//
// Skip honesty (mirrors preserves/noninterference/refines): unresolved handler
// or invariant, arity mismatch, non-Bool invariant, a handler list with < 1
// entry, or body-inlining failure all emit a `; note:` and skip  -  NEVER a
// false `unsat`.
static void emitCyclePreserves(const Program& prog, std::ostringstream& out) {
  if (prog.cyclePreserves_.empty()) return;
  out << "\n; ============================================================\n";
  out << "; D9 (gap #6)  -  cycle_preserves (VM-exit-cycle refinement)\n";
  out << "; ============================================================\n";

  // Same map-seeding as emitPreserves/emitNoninterference/emitRefines  -  spec
  // fns (so an invariant body that composes other spec fns resolves),
  // const-globals (so const int cross-refs resolve), and concrete fn/method
  // decls (so a handler requires clause naming another fn resolves).
  std::map<std::string, const SpecFnDecl*> specFns = collectSpecFns(prog);
  std::map<std::string, long long> constGlobals = collectConstGlobals(prog);
  std::map<std::string, const FuncDecl*> funcDecls;
  for (auto& fn : prog.funcs) if (fn && !fn->isExtern) funcDecls[fn->name] = fn.get();
  // ox:unsafe Also include trap handlers (they ARE FuncDecls, but live in
  // prog->trapHandlers, not prog->funcs)  -  mirrors the D8 noninterference
  // seeding. findFuncByName doesn't search trapHandlers, so seed the funcDecls
  // map so smtConcreteCallResult's callee resolution can find them.
  for (auto& fn : prog.trapHandlers) if (fn) funcDecls[fn->name] = fn.get();
  std::map<std::string, const FuncDecl*> methodDecls;
  for (auto& im : prog.impls) {
    if (!im) continue;
    for (auto& m : im->methods) {
      if (!m || m->isExtern) continue;
      methodDecls[mangleMethod(im->structName, m->name)] = m.get();
    }
  }

  // findFuncByName doesn't search trapHandlers (see its definition), so a
  // cycle_preserves over a `trap handler` would silently skip. Mirror the
  // emitNoninterference fix: wrap findFuncByName with a trapHandlers fallback.
  auto findHandlerByName = [&](const std::string& name) -> const FuncDecl* {
    const FuncDecl* f = findFuncByName(prog, name);
    if (f) return f;
    for (auto& th : prog.trapHandlers) if (th && th->name == name) return th.get();
    return nullptr;
  };

  for (auto& cp : prog.cyclePreserves_) {
    if (!cp) continue;

    // ox:proof Resolve the invariant spec fn (shared across all handlers in this decl).
    const SpecFnDecl* inv = findSpecFnByName(prog, cp->specName);
    out << "\n; cycle_preserves";
    for (auto& h : cp->handlers) out << " " << h;
    out << " <= " << cp->specName
        << "  (source line " << cp->line << ")\n";

    if (!inv) {
      out << "; note: cycle_preserves … <= " << cp->specName
          << " skipped  -  invariant spec fn '" << cp->specName << "' not found\n";
      continue;
    }
    if (!inv->body) {
      out << "; note: cycle_preserves … <= " << cp->specName
          << " skipped  -  invariant spec fn has no body\n";
      continue;
    }
    if (inv->retType.tag != BType::Tag::bool_) {
      out << "; note: cycle_preserves … <= " << cp->specName
          << " skipped  -  invariant spec fn must be Bool (got "
          << smtSort(inv->retType) << ")\n";
      continue;
    }
    if (cp->handlers.empty()) {
      out << "; note: cycle_preserves with empty handler list  -  nothing to check\n";
      continue;
    }

    // ox:proof Emit the per-handler discharge. This is per-handler sequential
    // correctness ACROSS the cycle, NOT cross-handler (that's noninterference's
    // job)  -  so no pair cross-product, just a straight loop over the handlers.
    int handlerCount = 0;
    for (size_t hi = 0; hi < cp->handlers.size(); ++hi) {
      const FuncDecl* handler = findHandlerByName(cp->handlers[hi]);

      out << "\n; --- handler: " << cp->handlers[hi]
          << " (cycle refinement step) ---\n";

      if (!handler) {
        out << "; note: skipped  -  handler '" << cp->handlers[hi]
            << "' not found\n";
        continue;
      }
      // Arity check: the handler's params must align positionally with the
      // invariant's params (same convention as preserves/noninterference).
      if (handler->params.size() != inv->params.size()) {
        out << "; note: skipped  -  handler '" << cp->handlers[hi]
            << "' arity (" << handler->params.size()
            << ") ≠ invariant arity (" << inv->params.size() << ")\n";
        continue;
      }

      // ox:proof ── Build the cycle-refinement discharge query. ──
      //
      // We negate the cycle refinement obligation:
      //   NOT (forall (cycle_args).
      //          req_handler(cycle_args) ∧ I_pre(cycle_args)
      //          ==> I_post(cycle_args_post_handler))
      // where `cycle_args` are the SHARED cycle-state variables (the pre-
      // vmlaunch guest state  -  sized to the invariant's params), and
      // `cycle_args_post_handler` is the post-vmresume state, which EQUALS the
      // post-handler state (the handler ran, mutated shared state, then
      // vmresume re-entered the guest  -  the invariant must hold at the NEXT
      // VM exit).
      //
      // The post-state is obtained by driving smtConcreteCallResult on the
      // handler's body  -  the mini-walker produces post-state terms via
      // postStore (for array mutations) or the result term (for pure value
      // handlers). We lower the invariant TWICE:
      //   - I_pre: invariant body with the cycle args (pre-/vmlaunch state)
      //   - I_post: invariant body with the post-state terms (post-vmresume)
      // And discharge: req_handler ∧ I_pre ==> I_post.
      //
      // VM entry->exit->resume is modelled as IDENTITY on the cycle invariant:
      // for UNCHANGED slots, the pre and post SMT symbols are the SAME (the VM
      // transitions don't touch the invariant's state); only the handler-
      // mutated slots flow through the mini-walker's post-state. This is the
      // trust boundary at vmresume/vmlaunch: the hardware guarantees these
      // transitions don't corrupt the invariant's abstract state.
      //
      // This mirrors emitNoninterference's structure (cPre / mini-walker /
      // cPost), but per-handler (no pair cross-product): each handler's step
      // is an independent induction step over the trap cycle.

      // Per-handler unique label base  -  `cycle_preserves_<handler>_<invariant>`.
      // Same load-bearing reason as preserves/noninterference: Z3 declare-
      // consts are GLOBAL, so two cycle_preserves over the same handler+arity
      // would otherwise re-declare `arg_0` and abort the second check-sat,
      // masking the proof as sat. The label base is unique per decl.
      std::string curFn = "cycle_preserves_" + cp->handlers[hi] + "_" + cp->specName;
      std::ostringstream argList;

      // One SmtCtx for the handler's mini-walker drive. We declare one set of
      // cycle args (matching the handler's params positionally  -  which also
      // align with the invariant's params per the arity check above).
      SmtCtx c{out, nullptr, 0, 0, {}};
      c.curFn = curFn;
      c.specFns = specFns;
      c.constGlobals = constGlobals;
      c.funcDecls = funcDecls;
      c.methodDecls = methodDecls;

      std::vector<std::string> cycleSyms;  // the cycle-state args (pre-vmresume)

      // Declare the cycle args (one per handler param = one per invariant param).
      // ── EMIT THE arg_<i> DECLARE-CONSTS IMMEDIATELY ───────────────────────
      // This is load-bearing (mirrors emitPreserves ~1160): `smtConcreteCallResult`
      // emits define-funs referencing the arg symbols via `c.nameMap`  -  those
      // define-funs MUST appear in the file AFTER the declare-consts or Z3
      // reports `(error "unknown constant cycle_preserves_..._arg_0")` and
      // aborts the check-sat, masking the proof as sat.
      for (size_t i = 0; i < handler->params.size(); ++i) {
        std::string sym = curFn + "_arg_" + std::to_string(i);
        const char* sort = smtSort(handler->params[i].type);
        out << "(declare-const " << sym << " " << sort << ")\n";
        argList << " (" << sym << " " << sort << ")";
        cycleSyms.push_back(sym);
      }

      // ── Lower I_pre: invariant body with cycle args as pre-/vmlaunch state ──
      // Bind the invariant's param names AND the handler's param names to the
      // cycle args (they're the same state  -  the cycle invariant ranges over
      // the same shared state the handler operates on).
      SmtCtx cPre{out, nullptr, 0, 0, {}};
      cPre.curFn = curFn + "_pre";
      cPre.specFns = specFns;
      cPre.constGlobals = constGlobals;
      cPre.funcDecls = funcDecls;
      cPre.methodDecls = methodDecls;
      for (size_t i = 0; i < handler->params.size() && i < inv->params.size(); ++i) {
        cPre.nameMap[inv->params[i].name] = cycleSyms[i];
        cPre.nameMap[handler->params[i].name] = cycleSyms[i];
        cPre.declared.insert(cycleSyms[i]);
      }

      // ox:proof G2b  -  finite-domain unrolling for a top-level `forall k: i64 in lo..hi
      // implies P` cycle-invariant body. This is the SAME unroll
      // `emitPreserves` uses (Ghost.cpp ~1302-1365); applying it here is
      // SOUND-CRITICAL for `cycle_preserves` over an array-valued invariant
      // such as `forall k in 0..4 implies page_aligned(ept[k])`: Z3's MBQI
      // over Int + array `select`/`store` inside the embedded quantifier body
      // is unreliable (times out / returns `unknown`) where the obligation is
      // actually `unsat`. The Dafny/Why3 fix is to unroll the `forall` into a
      // finite GROUND conjunction `(and P(0) P(1) ... P(N-1))` (each P(i) is
      // the body with the binder substituted by the literal i). Both I_pre
      // (lowered against the PRE-state cycle args) and I_post (lowered against
      // the handler's POST-state terms below) MUST go through the SAME unroll
      // so the obligation is a quantifier-free `(=> (and req I_pre) I_post)`
      // Z3's `simplify` tactic closes trivially.
      //
      // SOUND: the unrolled conjunction is EQUIVALENT to the original `forall`
      // under the explicit literal bound `0 <= k < N` (see `smtUnrollQuantForall`
      // header). When any soundness guard fails (non-forall, non-literal bound,
      // non-integral binder, magnitude > 1024), we keep the recursive `smtExpr`
      // forall form and emit a `; note:` so the user knows the unroll failed  - 
      // a SOUND failure (no false `unsat`). We track `invPreIsQuant` so the
      // discharge site below can pick the A1 quantifier-targeted tactic on the
      // sound-fallback path (where a REAL `(forall ((k Int)) ...)` reaches Z3).
      bool invPreIsQuant = false;
      std::string invPreTerm;
      {
        const QuantExpr* quantBody =
            dynamic_cast<const QuantExpr*>(inv->body.get());
        if (quantBody) {
          std::string note;
          std::string unrolled = smtUnrollQuantForall(cPre, quantBody, note);
          if (!unrolled.empty()) {
            out << "; G2b: cycle_preserves " << cp->handlers[hi] << " <= "
                << cp->specName << "  -  unrolled I_pre (forall -> conjunction)\n";
            out << ";   " << note << "\n";
            out << ";   I_pre original (forall ((k Int)) (=> range BODY)) "
                   "replaced with the unrolled conjunction above\n";
            invPreTerm = unrolled;
          } else if (!note.empty()) {
            out << "; G2b: cycle_preserves " << cp->handlers[hi] << " <= "
                << cp->specName << "  -  " << note
                << " (sound fallback to original forall for I_pre)\\n";
            invPreTerm = smtExpr(cPre, inv->body.get());
            invPreIsQuant = true;
          } else {
            // ox:note Defensive: empty note AND empty result  -  ordinary lowering.
            invPreTerm = smtExpr(cPre, inv->body.get());
            invPreIsQuant = true;
          }
        } else {
          // Non-quantified invariant body  -  ordinary lowering (no unroll).
          invPreTerm = smtExpr(cPre, inv->body.get());
        }
      }

      // ── Drive the mini-walker on the handler's body to get the post-state ──
      // Bind the handler's params (and the invariant's, for the same slots)
      // to the cycle args in c.nameMap, then drive the walker.
      for (size_t i = 0; i < handler->params.size(); ++i) {
        c.nameMap[handler->params[i].name] = cycleSyms[i];
        if (i < inv->params.size())
          c.nameMap[inv->params[i].name] = cycleSyms[i];
        c.declared.insert(cycleSyms[i]);
      }

      std::map<std::string, std::string> postStore;
      std::string resultTerm;
      // D9  -  suppress the call-requires sub-discharges that
      // smtConcreteCallResult would emit under smtWithCalleeBindings (same
      // pattern as emitNoninterference ~1680). We only need the handler's
      // post-state (the inlined result term + postStore mutations); the call-
      // requires queries are the handler's preconditions discharged as
      // standalone theorems  -  they trivially come back `sat` (a precondition
      // isn't a tautology) and would pollute the verify report with spurious
      // `sat` rows, shadowing the actual cycle_preserves check-sat and getting
      // the parent `cycle_preserves ...` row misattributed. The suppression
      // guard is local to this one mini-walker drive.
      c.suppressCallRequires = true;
      if (handler->retType != BType::void_) {
        resultTerm = smtConcreteCallResult(c, handler,
                                           /*labelBase=*/c.curFn,
                                           /*args=*/cycleSyms,
                                           /*pathCond=*/"",
                                           /*premises=*/{},
                                           /*postStore=*/&postStore);
        if (resultTerm.empty()) {
          c.suppressCallRequires = false;  // restore before skip
          // ox:why Also declare the result const so the verify harness's tally
          // stays consistent (mirrors emitPreserves's defensive skip).
          out << "(declare-const " << curFn << "_result Int)\n";
          out << "; note: skipped  -  handler body inlining returned no result term\n";
          continue;
        }
      } else {
        // Void handler  -  still walk for mutation post-state.
        resultTerm = smtConcreteCallResult(c, handler,
                                           /*labelBase=*/c.curFn,
                                           /*args=*/cycleSyms,
                                           /*pathCond=*/"",
                                           /*premises=*/{},
                                           /*postStore=*/&postStore);
      }
      c.suppressCallRequires = false;  // restore  -  invariant lowering below
                                       // is plain smtExpr, not a call site

      // The task spec asks us to declare `cycle_preserves_<handler>_<spec>_result`
      // as an Int. The mini-walker already threads the result term via c.nameMap
      // and the postStore; declaring it explicitly keeps the emitted SMT
      // self-documenting and gives the verify harness a stable handle. We
      // declare it as Int (matching the conventions in emitPreserves ~1180  - 
      // the result const is Int-typed regardless of the handler's return sort,
      // since the inlined term is concretised by the walker).
      out << "(declare-const " << curFn << "_result Int)\n";

      // ── Lower I_post: invariant body with post-state terms ──
      // For array params the handler mutated, replace the cycle arg with the
      // post-state term. For non-mutated params, the cycle state is unchanged
      // (same cycle symbol  -  the VM entry->exit->resume is identity on the
      // invariant's state). For the result, bind it to the handler's inlined
      // result term.
      SmtCtx cPost{out, nullptr, 0, 0, {}};
      cPost.curFn = curFn + "_post";
      cPost.specFns = specFns;
      cPost.constGlobals = constGlobals;
      cPost.funcDecls = funcDecls;
      cPost.methodDecls = methodDecls;
      for (size_t i = 0; i < handler->params.size() && i < inv->params.size(); ++i) {
        // Default: cycle arg (unchanged state  -  VM transitions are identity)
        std::string sym = cycleSyms[i];
        // If the handler mutated an array param at this slot, use post-state term.
        if (handler->params[i].type.tag == BType::Tag::array) {
          const std::string& hName = handler->params[i].name;
          auto pit = postStore.find(hName);
          if (pit != postStore.end() && !pit->second.empty()
              && pit->second != cycleSyms[i]) {
            sym = pit->second;  // post-state array term (post-handler state)
            out << "; note: " << cp->handlers[hi] << " mutated shared array '"
                << hName << "'  -  I_post uses post-state term " << sym << "\n";
          }
        }
        cPost.nameMap[inv->params[i].name] = sym;
        cPost.nameMap[handler->params[i].name] = sym;
        cPost.declared.insert(sym);
      }
      // If the handler returns a value and the invariant uses `result`,
      // bind it to the inlined result term.
      if (!resultTerm.empty() && handler->retType != BType::void_) {
        cPost.nameMap["result"] = resultTerm;
      }

      std::string invPostTerm = smtExpr(cPost, inv->body.get());

      // ── Lower the handler's requires clauses (the premise for the step) ──
      // These are lowered under c (which has the handler's params bound to the
      // cycle args). The hardware guarantees these for a trap handler; the
      // caller guarantees them for a regular fn.
      std::string reqPremise = "true";
      for (auto& r : handler->requires_) {
        std::string t = smtExpr(c, r.get());
        reqPremise = (reqPremise == "true") ? t : ("(and " + reqPremise + " " + t + ")");
      }
      if (handler->requires_.empty()) {
        out << "; note: handler '" << cp->handlers[hi]
            << "' has no requires  -  step is unconstrained\n";
      }

      // ── Discharge ──
      // (push)
      // (assert (not (forall (cycle_args)
      //   (=> (and req_handler I_pre) I_post))))
      // (check-sat-using (then simplify smt))
      // (pop)
      //
      // unsat ⇒ for every pre-/vmlaunch state and handler step satisfying the
      // handler's requires, if the cycle invariant held BEFORE the handler's
      // step (I_pre), it still holds AFTER (I_post)  -  the handler re-establishes
      // the invariant across the trap cycle, and the VM entry->exit->resume
      // transition (modelled as identity on the invariant) preserves it.
      out << "; note: VM entry->exit->resume modelled as identity on cycle invariant "
             "(trust boundary at vmresume/vmlaunch; handler-body preserves via "
             "the invariant spec path; cross-handler pair overlap via "
             "noninterference)\n";
      out << "; cycle refinement obligation: forall cycle_args,\n";
      out << ";   req_handler(cycle) ∧ I_pre(cycle) ==> I_post(cycle_post_handler)\n";
      out << ";   I_pre  = " << invPreTerm << "\n";
      out << ";   I_post = " << invPostTerm << "\n";
      out << "(push)\n";
      std::string body;
      if (invPreTerm == invPostTerm) {
        // Trivially established: I_pre == I_post (no mutation detected  -  the VM
        // entry->exit->resume is identity and the handler didn't mutate the
        // invariant's state). SMT still must discharge req_handler ==> I_post
        // (the handler's requires constrain the pre-state; the invariant must
        // hold over the constrained pre-state). Same shape as emitNoninterference
        // ~1777.
        out << "; note: I_pre == I_post  -  trivially established (no mutation "
               "detected; VM transitions are identity on the cycle invariant)\n";
        body = "(=> " + reqPremise + " " + invPostTerm + ")";
      } else {
        body = "(=> (and " + reqPremise + " " + invPreTerm + ") " + invPostTerm + ")";
      }
      out << "(assert (not (forall (" << argList.str() << ") " << body << ")))\n";
      out << "(check-sat-using (then simplify smt))\n";
      out << "(pop)\n";
      out << "; note: cycle_preserves discharge  -  unsat => "
          << cp->handlers[hi] << " re-establishes " << cp->specName
          << " across the trap cycle\n\n";
      ++handlerCount;
    }
    if (handlerCount == 0) {
      out << "; note: no valid handlers discharged (see skip notes above)\n";
    }
  }
}

// ox:proof D3  -  `axiom <expr>;` top-level SMT axioms.
//
// Emits each axiom as `(assert <lowered-body>)` at the TOP of the ghost
// section (before spec fns), so every later discharge query (refines/
// preserves/contract walker) sees it as a global premise. The body is lowered
// via `smtExpr` on a fresh `SmtCtx` seeded with the SAME `specFns` and
// `constGlobals` maps a spec fn body uses, so a body that names an uninterpreted
// `spec fn is_ram(gpa) -> bool` (declared without a body, hence `declare-fun`
// by `emitSpecFns`) resolves to `sf_is_ram` and a `forall` over it lowers to a
// real SMT-LIB `(forall ((gpa Int)) (=> (sf_is_ram gpa) (>= gpa 0)))`.
//
// Mirrors `emitSpecFns`'s SmtCtx construction (line ~220): we seed the maps
// from `collectSpecFns`/`collectConstGlobals` so spec fn names resolve, and we
// render the body into a scratch stream FIRST so any placeholder declares
// `smtExpr` needs land ABOVE the `(assert ...)` rather than inside it (which
// would be ill-formed). The optional `name` label is surfaced as a `; note:`
// line for readability; it carries no SMT semantics.
//
// Axioms have NO params of their own  -  a `forall gpa. ...` binds `gpa` locally
// inside the quantifier (handled by `smtExpr`'s QuantExpr arm), so we do NOT
// declare-const the binder at top level (that would shadow the quantifier's
// bound var and break the forall).
static void emitAxioms(const Program& prog, std::ostringstream& out) {
  if (prog.axioms.empty()) return;
  out << "\n; ============================================================\n";
  out << "; D3  -  top-level axioms (asserted, available to all queries)\n";
  out << "; ============================================================\n";

  std::map<std::string, const SpecFnDecl*> specFns = collectSpecFns(prog);
  std::map<std::string, long long> constGlobals = collectConstGlobals(prog);

  for (auto& ax : prog.axioms) {
    if (!ax) continue;
    // Documentation comment: surfaces the fully-qualified name (Namespace::Name
    // or just Name), the trusted marker, and the source citation when present.
    // All metadata  -  the (assert <body>) SMT semantics are unchanged. The
    // `; note:` line below is what the Driver's audit walkers key on.
    std::string qname = ax->qualifiedName();
    if (ax->isTrusted) {
      out << "; note: trusted axiom " << qname;
      if (!ax->sourceCitation.empty())
        out << " source: \"" << ax->sourceCitation << "\"";
      else
        out << " (no source cited)";
      out << " (source line " << ax->line << ")\n";
      if (!qname.empty())
        out << "; axiom " << qname << " (source line " << ax->line << ")\n";
      else
        out << "; axiom (source line " << ax->line << ")\n";
    } else if (!qname.empty()) {
      out << "; note: axiom " << qname << " (source line " << ax->line << ")\n";
      out << "; axiom " << qname << " (source line " << ax->line << ")\n";
    } else {
      out << "; axiom (source line " << ax->line << ")\n";
    }

    // Render the body into a scratch stream so placeholder declares land
    // ABOVE the (assert ...), mirroring emitSpecFns' pure-body path.
    std::ostringstream bodyScratch;
    SmtCtx bc{bodyScratch, nullptr, 0, 0, {}};
    bc.curFn = "axiom";
    bc.specFns = specFns;
    bc.constGlobals = constGlobals;
    std::string bodyTerm = smtExpr(bc, ax->body.get());

    out << bodyScratch.str();
    out << "(assert " << bodyTerm << ")\n";
  }
  out << "\n";
}

// ox:proof Public entry  -  append the whole T1/T2/T3 ghost section. No-op if none of
// the constructs are declared (keeps existing-contract output byte-identical).
//
// 2-arg overload  -  backward-compat: delegates to the 3-arg Tier-1 form with
// an empty PostStateMap. With no post-state threads, `emitRegionsAndModifies`
// falls back to the pre-Tier-1 vacuous-frame pattern (two fresh top-level
// uninterpreted consts per non-modified global). Identical behaviour for any
// hypothetical external caller; `emitSmt` uses the 3-arg overload directly.
void emitGhostSection(const Program& prog, std::ostringstream& out) {
  PostStateMap empty;
  emitGhostSection(prog, out, empty);
}

// 3-arg Tier-1 overload  -  threads the per-function post-state map of mutable
// globals (see `PostStateMap` in Smt.h) into `emitRegionsAndModifies` so the
// frame axioms compare the body's actual post-state term against the
// pre-state snapshot, making `modifies`-clause frame conditions non-vacuous.
// Behaviour for programs without any `region`/`modifies`/`spec fn`/`refines`/
// `ghost let`/`preserves` declarations is byte-identical to the 2-arg
// overload: the early-return guard below skips emission entirely. The
// `postIdents` map is only consulted inside `emitRegionsAndModifies`, and a
// no-modifies program triggers that function's own early-return before any
// map access.
void emitGhostSection(const Program& prog, std::ostringstream& out,
                      const PostStateMap& postIdents) {
  bool any =
      !prog.specFns.empty() || !prog.refines_.empty() || !prog.regions.empty() ||
      !prog.preserves_.empty() ||   // Missing-#6
      !prog.noninterference_.empty() ||  // D8
      !prog.cyclePreserves_.empty() ||   // D9 (gap #6)  -  cycle_preserves
      !prog.axioms.empty();          // D3
  if (!any) {
    for (auto& fn : prog.funcs) {
      if (fn && (fn->isGhost || !fn->modifies.empty())) { any = true; break; }
      if (fn) for (auto& s : fn->body)
        if (s && dynamic_cast<const GhostLetStmt*>(s.get())) { any = true; break; }
    }
  }
  if (!any) {
    for (auto& im : prog.impls) if (im)
      for (auto& m : im->methods)
        if (m && (m->isGhost || !m->modifies.empty())) { any = true; break; }
  }
  if (!any) return;        // byte-identical to pre-T1/T2/T3/Missing-#6 output

  out << "\n; ############################################################\n";
  out << "; # Ghost encoder section (T1/T2/T3 + Missing-#6)  -  appended by src/Ghost.cpp\n";
  out << "; # These constructs carry no runtime state; they exist so the SMT\n";
  out << "; # prover can reason about the ABSTRACT layer (spec fns, refines),\n";
  out << "; # ghost state (ghost let/fn), modular frame conditions\n";
  out << "; # (regions + modifies), AND per-handler invariant preservation\n";
  out << "; # (preserves, Missing-#6). No-op discharge is `sat` (honest); `unsat`\n";
  out << "; # on a refinement/frame/preservation query means it holds for all args.\n";
  out << "; ############################################################\n";

  emitAxioms(prog, out);   // D3  -  top-level axioms FIRST, so every later query sees them
  emitSpecFns(prog, out);
  emitGhostLetsAndGhostFns(prog, out);
  emitRefines(prog, out);
  emitPreserves(prog, out);   // Missing-#6  -  per-handler invariant preservation
  emitNoninterference(prog, out);  // D8  -  Owicki-Gries cross-handler stability
  // ox:proof D9 (gap #6)  -  cycle_preserves: VM-exit-cycle refinement (per-handler across
  // the trap cycle). Sits AFTER noninterference (which discharges cross-HANDLER
  // pair stability) so the cycle layer ties together the per-handler preserves
  // and cross-handler stability obligations over the full trap cycle, and BEFORE
  // emitRegionsAndModifies so the frame axioms (which close the SMT section) see
  // the cycle discharge queries above them in the emitted file.
  emitCyclePreserves(prog, out);  // D9 (gap #6)  -  VM-exit-cycle refinement
  emitRegionsAndModifies(prog, out, postIdents);
}

} // namespace ox_smt
