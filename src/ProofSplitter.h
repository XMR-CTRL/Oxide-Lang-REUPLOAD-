// ProofSplitter.h  -  goal splitting + domain-aware tactic selection.
//
// Two cooperating features wired into the SMT discharge path:
//
//   1. Goal splitting  (splitGoal):
//      A single verification goal `G` is decomposed into a list of independent
//      sub-goals G1..Gn such that proving ALL of Gi holds iff G holds. Each
//      branch below is sound: combined, it is equivalent to G; in isolation
//      each split is a strengthening (proving Gi alone suffices for that
//      conjunct), so the caller discharges each Gi separately and reports
//      `unsat` only if EVERY Gi is `unsat`. A single `sat`/`unknown` on any
//      Gi means G is not (yet) proven, exactly as before  -  only the proof is
//      now attempted piecewise, which lets the tactic selection (#2) target
//      each piece with the cheapest decidable solver fragment.
//
//      The split is performed at the AST (Expr*) level BEFORE SMT lowering, so
//      the SMT emitter (smtExpr) is run once per sub-goal against a smaller,
//      more uniform goal  -  Z3's tactic selection is dramatically more effective
//      on a decidable fragment than on a mixed quantifier+BV+arithmetic blob.
//
//      Five split strategies, applied in order; the first that matches wins
//      (each goal fires at most one strategy, but the worklist is processed
//      exhaustively, so nested splits unfold recursively):
//
//        (1) Conjunct split    -  BinaryExpr::Op::land (A && B) -> [A, B].
//                               Proving A and B separately is equivalent to
//                               proving the conjunction (each conjunct is
//                               independently necessary). The most common
//                               case for compound `requires`/`ensures`.
//        (2) Implication split  -  a QuantExpr whose body is `(=> P (and C D))`,
//                               split into `[forall i. P => C, forall i. P => D]`
//                               so each consequent proves under the shared
//                               antecedent. Sound: (=> P (and C D)) is
//                               equivalent to (and (=> P C) (=> P D)). Because
//                               `implies` desugars to `!A || B` at parse time,
//                               the antecedent is matched as a `lor` with a
//                               `not_` lhs (the desugared shape of `A implies B`).
//        (3) Quantifier unroll  -  a forall QuantExpr with a LITERAL range whose
//                                magnitude <= 1024 unrolls into a ground
//                                conjunction of instances (delegates to
//                                smtUnrollQuantForall). A ground, quantifier-
//                                free goal is decidable for Z3 (no MBQI), so
//                                this never returns `unknown`  -  it is the #1
//                                source of "too hard" results.
//        (4) Branch split       -  a TernaryExpr `c ? T : E` (the Expr-level
//                                analogue of an IfStmt in the goal position)
//                                splits into two goals `[T (under c),
//                                E (under !c)]`. Each branch is proved under
//                                its condition as an extra premise. Sound: if
//                                T holds whenever c and E holds whenever !c,
//                                then `c ? T : E` holds unconditionally. The
//                                extra premise is the path condition.
//        (5) Premise isolation   -  when the goal carries premises that are
//                                themselves a land conjunction, each premise
//                                is tried IN ISOLATION first (as the only
//                                premise for the goal) before falling back to
//                                the full premise set. Sound: a premise set
//                                {P, Q} that proves G does NOT imply P alone
//                                proves G  -  so isolation is an OPTIMIZATION
//                                (try the cheap single-premise discharge first;
//                                if it succeeds, skip the full set), and the
//                                full-premise effort is still attempted if the
//                                isolated one fails. This turns a 4-premise
//                                goal whose proof only needs 1 premise into a
//                                single cheap QF_BV query.
//
//   2. Domain-aware tactic selection  (selectTactic):
//      Given a goal (Expr + premises), pick the cheapest Z3 tactic that decides
//      it. The chain, in priority order:
//          isGround && hasBV      -> QF_BV      (bit-blasting, fully decidable)
//          isGround               -> Simplify   (no quantifiers -> ground simplification decides it)
//          hasMod || hasDiv       -> NIA        (Nonlinear Integer Arithmetic)
//          else                   -> Cascade    (the existing (then simplify smt) triple)
//      `isGround` := no QuantExpr anywhere in the goal term. `hasBV` := the
//      goal (or a premise) references a symbol known to be BV-typed. `hasMod`
//      / `hasDiv` := a BinaryExpr::Op::mod / Op::div appears in the term.
//      Selecting a decidable fragment whenever possible is what stops Z3 from
//      returning `unknown` on goals it actually can decide.
//
// Integration:
//   `smtDischargeGoal` is called from `smtDischarge` (Driver.cpp) when the
//   original `Expr*` is available (the smtClause + the assert/invariant sites).
//   It splits the goal exhaustively, selects a tactic per sub-goal, lowers
//   each sub-goal Expr with smtExpr, and emits one `check-sat-using` per
//   (sub-goal, tactic) pair. The outer verify parser already keeps the LAST
//   non-unknown result per clause, so the loader keeps treating each clause as
//   `unsat` only if all its (now multiple) check-sats are `unsat`  -  the
//   `; note: split into N sub-goals` comment makes the piecewise proof visible.
//
// This file lives inside `namespace ox_smt` (alongside the SMT emitter helpers
// in Driver.cpp / Ghost.cpp) and operates on the same AST.h / Smt.h types.
// Implementation in ProofSplitter.cpp.

#pragma once

#include "AST.h"
#include "Smt.h"      // SmtCtx, smtExpr, smtSort
#include <string>
#include <vector>

namespace ox_smt {

// One verification goal. `term` is the AST expression to prove; `premises`
// are the SMT-level premise terms (already lowered strings  -  the require/
// invariant/path-condition facts assumed true when discharging this goal).
// `label` is the clause label (e.g. `fn_ensures_3`) used to name sub-goals.
// `isGround` is true iff `term` contains NO QuantExpr (decidable fragment).
struct ProofGoal {
  std::string label;
  const Expr* term = nullptr;
  std::vector<std::string> premises;
  bool isGround = true;
};

// ox:proof The tactic to use for one goal. Maps directly to a (check-sat-using ...)
// strategy string used downstream.
enum class ProofTactic {
  QF_BV,    // (then simplify solve-eqs smt)  -  bit-blast, fully decidable
  Simplify, // (then simplify smt)   -  ground, no quantifiers: simplify decides it
  NIA,      // (then simplify (using-params smt :mbqi true))  -  nonlinear Int
  Cascade,  // the existing 3-then chain (simplify+mbqi variants)  -  fallback
};

// Split a goal into a list of sub-goals. Applies the 5 strategies above in
// order; returns the worklist after recursively splitting each generated
// sub-goal, so callers get a fully-expanded list of leaves to discharge
// independently. A goal that matches no strategy returns a singleton list
// containing itself unchanged.
//
// `c` is the SmtCtx (used only for the quantifier-unroll strategy, which needs
// to lower the bound body via smtExpr  -  the other four strategies are pure AST
// rearrangements and never touch the SMT stream).
//
// `owners` receives any ExprPtrs the splitter synthesised in-place (currently
// only the unroll instances); the returned ProofGoals borrow their `term` from
// these and from the input goal's tree, so `owners` must outlive the goals.
// The caller stashes `owners` and the goals together for the lifetime of the
// discharge pass.
struct SplitResult {
  std::vector<ProofGoal> goals;
  std::vector<ExprPtr>   owners;   // fresh Expr trees the goals borrow
};
SplitResult splitGoal(SmtCtx& c, const ProofGoal& g);

// Pick the cheapest tactic that decides `g`. Inspection order matches the
// spec: isGround&&hasBV -> QF_BV, isGround -> Simplify, hasMod||hasDiv -> NIA,
// else Cascade. `c` is consulted for the BV-symbol set (`c.bvVars`).
ProofTactic selectTactic(const SmtCtx& c, const ProofGoal& g);

// ox:proof Render a tactic to its (check-sat-using ...) strategy string.
std::string tacticStrategy(ProofTactic t);

// Top-level entry: split `goal` exhaustively, then for each resulting sub-goal
// select a tactic and emit one discharge query into `c.out`. Returns the number
// of sub-goals actually discharged (>= 1). The emitted shape mirrors the legacy
// `smtDischarge` comment/push/assert/check-sat/pop per sub-goal, so the existing
// verify-report parser consumes multiple check-sats per clause unchanged.
//
// `srcLine` (default 0) is the source line of the original clause; each emitted
// sub-goal gets a `\; <sublabel> (source line N)` header so the verify-report
// parser turns each sub-goal into its OWN VerifyRow  -  the parent clause is then
// proven iff ALL its sub-rows are `unsat` (the AND semantics the spec requires),
// with no report-aggregator change.
int smtDischargeGoal(SmtCtx& c, const ProofGoal& goal, int srcLine = 0);

}  // namespace ox_smt
