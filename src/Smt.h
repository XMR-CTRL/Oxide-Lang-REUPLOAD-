// ox:proof Smt.h  -  shared SMT-LIB emission types + declarations.
//
// Factored out of src/Driver.cpp so the Ghost encoder (src/Ghost.cpp) can share
// the same SmtCtx, helpers (smtExpr / smtPlaceholder / smtDeclareConst /
// smtClause / smtDischarge / collectOldNames / smtSort), and entry point
// (emitSmt) without having to duplicate them or manufacture a second
// parallel walker. Everything here lives in namespace `ox_smt` and is defined
// either in Driver.cpp (the contract walker) or Ghost.cpp (the T1/T2/T3 ghost
// encoder section, appended by emitSmt).
//
// The contract walker is sound-but-loose: it encodes requires/ensures/
// invariant/assert clauses as SMT terms and emits the negated discharge query
// per clause. Unsupported subforms (calls, array index, field access, casts)
// are replaced by a fresh uninterpreted constant flagged with a trailing
// `; note:` so the file stays well-sorted and a clause using such a placeholder
// honestly fails to reach `unsat`. See the long header comment in Driver.cpp
// at the top of the `namespace ox_smt {` block for the full encoding notes.

#pragma once

#include <sstream>
#include <string>
#include <set>
#include <map>
#include <vector>
#include <utility>   // std::pair for SmtCtx::mmioWriteEffects entries

struct BType;        // defined in AST.h (fwd-declared here to keep Smt.h light)
struct Expr;        // AST.h
struct Program;     // AST.h
struct SpecFnDecl;  // AST.h  -  spec fn bodies used for contract-call inlining
struct FuncDecl;    // AST.h  -  concrete fn bodies used for WP call summaries
struct StructDecl;  // AST.h  -  struct decls, for G1e struct-array field flattening
class  Sema;        // Sema.h

namespace ox_smt {

// ox:proof SmtCtx  -  per-function SMT emission scope. The walker carries one of these
// through every clause; declarations accumulated here persist across the
// function's clauses (params, result, old_ snapshots, quantifier binders,
// ghost-let names) so clause terms resolve against a function-unique symbol
// space (`p_<fn>_<param>`, `<fn>_result`, `<fn>_old_<x>`, etc.).
//
// `curFn` qualifies placeholder names so two functions' `ph0` can't collide.
// `nameMap` maps a bare Oxide source name to its fully-qualified SMT symbol so
// clause terms written with the source-level name (`n`, `result`, `old(x)`'s
// `x`) resolve to the function-unique decl. `declared` tracks every name
// already declared as a constant in the current scope, so we don't redeclare
// and unknown VarRefs fall to a placeholder cleanly.
// `constGlobals` carries compile-time integer `const` globals substituted as
// literals (so SDM cross-reference asserts like
// `assert VMCS_EPT_POINTER == 0x001A` can discharge to `unsat` instead of
// decaying to an uninterpreted placeholder).
// `specFns` indexes `spec fn` declarations by bare source name so contract
// clauses can lower `spec_predicate(args...)` as a real inlined SMT term rather
// than as an opaque call placeholder. `expandingSpecFns` prevents recursive spec
// definitions from blowing the encoder stack; recursion falls back to an honest
// placeholder.
struct SmtCtx {
  std::ostringstream& out;
  Sema* sema = nullptr;                // unused; reserved for type resolution
  int placeholderSeq = 0;              // fresh uninterpreted-constant counter
  int assertSeq = 0;                   // per-function assert counter, for naming
  // ox:proof `assume <expr>;` / `trusted assume <expr>;` per-function hypothesis counter,
  // for naming the emitted `(define-fun <fn>_assume_<n> () Bool ...)`. Mirrors
  // assertSeq; a separate counter keeps assume labels distinct from assert
  // labels in the SMT witness output (the report can distinguish "assumed" vs
  // "discharged" clauses by the label prefix). See Driver.cpp's assumeStmt arm.
  int assumeSeq = 0;
  int calcStepSeq = 0;                 // calcD  -  per-function calc-step counter, for naming
  std::set<std::string> declared;
  std::string curFn;
  std::map<std::string, std::string> nameMap;
  std::map<std::string, long long> constGlobals;
  // D4  -  const globals declared WITHOUT an initializer (`const NAME: T;` or
  // `extern const NAME: T;`). These are symbolic: they must NOT be substituted
  // as a literal. Instead `smtExpr`'s VarRef arm emits a `(declare-const NAME
  // Sort)` and returns the bare NAME as the SMT term, so verification reasons
  // about symbolic state (e.g. symbolic VMCS fields, MMIO base addresses).
  // Maps the source-level const name to its SMT sort string (e.g. "Int").
  std::map<std::string, std::string> symConstGlobals;
  std::map<std::string, const SpecFnDecl*> specFns;
  std::map<std::string, const FuncDecl*> funcDecls;
  std::map<std::string, const FuncDecl*> methodDecls;
  // G1e  -  index of `prog.structs` by name. Populated once in `emitSmt` (and
  // threaded into every per-function `emitFnContracts` SmtCtx) so the SMT
  // encoder can resolve a struct's field list when flattening a
  // struct-element array param into one `(Array Int <fieldSort>)` per field  - 
  // see `emitFnContracts`'s struct-array seeding block and the Field arms in
  // `smtExpr` / `smtExprWp` / `smtEncodeStmt`'s AssignTarget.
  std::map<std::string, const StructDecl*> structDecls;
  // G1e  -  per-function mapping from a struct-array PARAM's bare name to its
  // flattened per-field SMT array symbol: param name → { field name → symbol }.
  // For `epts: [EptEntry; 4]` with `struct EptEntry { gpa, flags }`, the
  // seeding block declares `p_<fn>_epts__gpa` and `p_<fn>_epts__flags` and
  // records here `structArrayFields["epts"]["gpa"] = "p_<fn>_epts__gpa"`,
  // `structArrayFields["epts"]["flags"] = "p_<fn>_epts__flags"`. The Field
  // read/write arms consult this to lower `epts[idx].gpa` as
  // `(select <flatArrSym> <idx>)` and `epts[idx].gpa = x` as a WP store
  // rebinding of the flat array symbol. Keyed by the BARE param name so the
  // index arm's base-VarRef name lookup matches directly.
  std::map<std::string, std::map<std::string, std::string>> structArrayFields;
  std::set<std::string> expandingSpecFns;
  // D1  -  concrete-`fn` recursion guard. `expandingSpecFns` above guards
  // `spec fn` inlining only (smtInlineSpecCall); concrete `fn` callees go
  // through `smtConcreteCallResult`, which inlines the callee body to derive
  // its post-state. A recursive `fn walk(...){ return walk(...); }` makes the
  // inliner chase `walk -> walk -> walk -> ...` and blows the C++ stack.
  // `inlineStack` mirrors the spec guard: it carries the names of every
  // concrete `fn` currently being inlined, so `smtConcreteCallResult` can
  // detect that the callee is on the stack (direct OR indirect recursion)
  // and switch to Dafny/Why3 "assume callee's contract" mode  -  declare a
  // fresh `result` symbol + assert the callee's `ensures` clauses under the
  // callee's `requires` premise, instead of re-inlining the body.
  std::vector<std::string> inlineStack;
  std::set<std::string> lenSyms;
  // ox:proof Tier 3  -  fixed array length symbols. Maps an SMT base symbol (e.g.
  // `p_sumAll_a` or `ept_table`) to its declared compile-time count
  // (`BType::count`). `smtLenOf` consults this to emit an equality
  // `(assert (= len_<base> N))` instead of just `(>= len_<base> 0)` so a
  // `requires 0 <= idx < len(arr)` clause can fold the bound to a literal
  // and actually discharge. Populated by `emitFnContracts` for params
  // (after `nameMap[p.name] = q` we record `arrayLenSyms[q] = N`) and
  // for non-const mutable global arrays (same keying). Slices and dynarrays
  // aren't keyed here  -  they legitimately stay uninterpreted.
  std::map<std::string, int32_t> arrayLenSyms;
  std::vector<std::string> wpPremises;
  std::string wpPathCond;
  // D1 fix  -  pending call-site assume terms from recursive / depth-capped
  // callees encountered while lowering an expression via smtExprWp. These
  // are the callee's ensures clauses (bound to the call-site arg terms +
  // the fresh result symbol), accumulated as premise strings. Callers that
  // USE smtExprWp for a ReturnStmt value then fold these into the
  // discharge premises so `return recursive_fn(args)` can prove its own
  // ensures using the recursive callee's assumed ensures. Cleared by the
  // ReturnStmt arm after consuming them. This mirrors the
  // `__ox_call_assume__` store mechanism used by the ExprStmt Call-arm
  // path, but lives on the ctx (not the store) so smtExprWp  -  which only
  // has a CONST store ref  -  can still write these assumes.
  std::vector<std::string> pendingCallAssumes;
  // Tier 2a  -  when this function is an impl-block method with a `self` receiver,
  // `selfStructName` holds the owning struct's name ("" for free fns). Set by
  // `emitFnContracts` after looking up `fn.implStruct` in `prog.structs`. The
  // WP `Field` arm consults this so `self.x` resolves to the per-field SMT
  // const `self__<S>__<x>` that the seeding block declared at function entry.
  // Empty for free fns / associated fns  -  a `Field` on a non-self base still
  // punts to `smtPlaceholder` (Tier 2a scope).
  std::string selfStructName;
  // Fix 3  -  compiler-managed memory model axioms. Populated ONCE in `emitSmt`
  // (Driver.cpp) and emitted at the TOP of the .smt2 file right after the
  // `(set-logic ALL)` header, BEFORE any per-function contract section, so
  // every discharge query (free fn + impl method + ghost section) sees them
  // as premises. Each entry is a raw SMT-LIB line  -  `(declare-fun ...)`
  // symbol introductions AND `(assert ...)` axiom bodies  -  to be written
  // verbatim to the output stream.
  //
  // The compiler-managed baseline covers FIVE families, all keyed on
  // UNINTERPRETED SMT symbols (Int modelled cpu/addr/event/tick ids, Bool
  // for predicates) that are DISTINCT from any spec_fn symbol the source
  // program may declare, so the manual `axiom` path and the auto-emitted
  // path coexist without a symbol clash:
  //   1. INVEPT flush       -  invept(cpu) ⇒ ∀addr. ¬stale_tlb(cpu, addr)
  //   2. happens_before     -  irreflexive + transitive partial order
  //   3. TSO store buffer   -  per-cpu store-buffer + commit_order relation:
  //                          stores enter the buffer in program order, and
  //                          commit_order(i,j) ⇒ i<j (stores commit in
  //                          insertion order, x86 TSO: no store-store
  //                          reordering). Eventually-committed is left to the
  //                          uninterpreted `committed` predicate (no liveness
  //                          axiom  -  we keep the safety slice decidable).
  //   4. Cache coherence    -  invalidate(addr, cpu): when a write to `addr`
  //                          by any cpu commits, every OTHER cpu's cached copy
  //                          is invalidated (¬cached(cpu, addr)).
  //   5. stale_tlb post     -  already folded into (1): if invept(cpu) holds,
  //                          the TLB is not stale for any addr. (Provided as
  //                          its own axiom for programs that reference
  //                          `stale_tlb` symbolically.)
  //
  // This is ADDITIVE to the existing manual `axiom NAME: ...` surface: a
  // program that declares its own axioms (see examples/fix3_memory_model_test.ox)
  // continues to lower them through the spec_fn + bounded-forall path. An
  // empty vector (the default for any freshly-constructed SmtCtx that is not
  // the one populated in emitSmt) is a no-op  -  programs that don't touch the
  // memory-model symbols simply carry the (harmless) unused declare-funs.
  std::vector<std::string> memModelAxioms;
  // Part 1  -  Cross-function MMIO threading (named per-address model).
  //
  // The existing `Feature 7` machinery encodes `mmio_store(addr, val)` /
  // `mmio_load(addr)` as `store` / `select` on a synthetic WP-slot key
  // `mmio_mem` (an `(Array Int Int)` symbol threaded store-by-store across
  // callee→caller via `smtConcreteCallResult`'s `postStore`). That works
  // WITHIN a single function's body and a single inlined callee, but the
  // SMT file only ever sees the FINAL `(store ...)` chain on the function's
  // `mmio_mem` slot; a downstream clause written in terms of the BARE ADDRESS
  // (e.g. an `axiom` about `mmio_state_F0000000 = 1` alongside
  // `init()` that calls `configure_device(0xF0000000)` and writes `0x1`) has
  // no symbol to bind to. The three fields below close that gap with a
  // brighter, NAMED per-address model that COMPLEMENTS the array model (both
  // are maintained in parallel; the array remains the trusted incident
  // record, the named model surfaces the effect of each write on a stable
  // symbol the rest of the file  -  Ghost frame axioms, user-authored `axiom`
  // declarations about specific MMIO addresses  -  can cite).
  //
  //   `mmioWriteEffects`: maps a function name (the callee's `curFn`) to its
  //     full list of (addressTerm, valueTerm) MMIO writes, in source order.
  //     Populated by `smtEncodeStmt`'s `mmio_store` arm (in the WP path) and
  //     by `smtExpr`'s `mmio_store` arm (in the signature path) under the
  //     current function's name; consulted by the function-call arms'
  //     cross-boundary propagation so a callee's writes land in the caller's
  //     `mmioState`. Consumed by the Ghost encoder's frame-axiom emission so
  //     a `modifies <region>` including an MMIO region name still emits a
  //     PRECISE frame axiom on the unchanged neighbouring addresses (the
  //     function-local `mmioState` post-terms) instead of vacuously asserting
  //     `(= old_<addr> old_<addr>)` for every MMIO address while only naming
  //     the array-symbol slot.
  //   `mmioReadAddresses`: addresses read via `mmio_load(addr)` by the current
  //     function. Recorded so a `modifies`-clause frame axiom can include
  //     only WRITTEN addresses in the "modified" universe (a read doesn't
  //     change the value, so it is NOT a modification  -  the frame axiom
  //     correctly fires, asserting the address equals its old value).
  //   `mmioState`: current SMT model of each MMIO ADDRESS's VALUE, keyed by
  //     the address term string (NOT by the source address expression  -  we
  //     key off the LOWERED ptr-term, which is symbol-stable across reads of
  //     the same source expression in the same scope). `smtExpr`'s
  //     `mmio_load(addr)` arm consults this first; a HIT returns the stored
  //     value term, so the read discharges against the propagated write even
  //     across a function-call boundary (callee `configure_device(base)`
  //     writes `mmioState[base] = 1`; caller's `mmio_load(base)` then reads
  //     `1` and `assert enabled == 0x1` goes unsat). A MISS declares a
  //     fresh uninterpreted constant `<fn>_mmio_load_<seq>` of Int sort, so
  //     the read resolves honesty (the model is unconstrained about that
  //     address) and records the address in `mmioReadAddresses`.
  //   The cross-function propagation happens in the function-call arms of
  //   `smtExpr` and `smtEncodeStmt`: after `smtConcreteCallResult` returns,
  //   we look up the callee's `mmioWriteEffects` entries and apply each write
  //   to the CALLER's `mmioState`, emitting a frame axiom for unchanged
  //   addresses (every address the caller knew about that the callee did NOT
  //   write keeps its pre-state value).
  std::map<std::string, std::vector<std::pair<std::string, std::string>>> mmioWriteEffects;
  std::set<std::string> mmioReadAddresses;
  std::map<std::string, std::string> mmioState;
  // Symbols DECLARED as native `(_ BitVec 64)` (not Int). A source value that
  // flows into a bitwise/shift op is declared here as a real fixed-width word:
  // it is emitted bare in bit context (so the query stays in decidable QF_BV  - 
  // Z3 returns a decisive sat/unsat with a counterexample model instead of
  // `unknown`), models true 64-bit wraparound (bvadd/bvor/bvshl), and is
  // `bv2int`-wrapped only when pure-integer arithmetic needs it. `bv2int` of a
  // declared BV is the decidable direction  -  unlike `int2bv` of a free Int,
  // which is what made the old Int-bridged encoding punt to `unknown`.
  std::set<std::string> bvVars;
  // ox:proof D8  -  noninterference suppression guard. Set by emitNoninterference
  // (in Ghost.cpp) while driving smtConcreteCallResult on the step handler's
  // body to obtain post-state terms. When true, smtConcreteCallResult skips
  // emitting its call-site `requires` discharge queries  -  they would
  // otherwise show up as `sat` rows in the verify report (a precondition is
  // not a tautology when treated standalone) and shadow the actual
  // noninterference stability check-sat, leaving the parent
  // `noninterference ...` row misattributed. The result term + post-state
  // store (the only things emitNoninterference actually needs) are still
  // produced.
  bool suppressCallRequires = false;
  // Oxide SOURCE names that the WP walker has bound to a bitop-flowing RHS
  // (`let masked = w & ALL_ONES` => `masked` lands here). Distinct from
  // `bvVars`  -  that holds declared SMT *symbols* (params/result); `wpBvNames`
  // holds the source-level names whose *value* in the WP store is a bv-bridge
  // term. The arithmetic-operand helper consults this so a later
  // `wrapped = masked + 1` keeps the chain in `bvadd` (real 64-bit wrap)
  // rather than crossing back to unbounded Int `+` and losing overflow.
  std::set<std::string> wpBvNames;

  // ox:proof asm! SMT-encoder state (the contract-5 `asm!`-axiomatisation path).
  // The asm! block is modelled as a fresh per-block uninterpreted function
  // `asm_<curFn>_<seq>` whose arg sorts are the SMT sorts of the block's INPUT
  // AsmIO operands (isOutput=false) and whose result sort is smtSort(a->resultTy)
  // (the single output's sort for the common single-output case). The applied
  // term `(asm_<curFn>_<seq> <input_term_1> ...)` is the value term returned by
  // smtExpr / smtExprWp for the AsmExpr. If a user supplies a `spec fn` named
  // `asm_<curFn>` (or `asm_<curFn>_<seq>`), its body is bound positionally to
  // the asm inputs and asserted as a top-level `(assert (forall ...))` axiom
  // linking the uninterpreted symbol to the spec  -  so an `ensures result == V`
  // on a function whose body contains the asm! block can discharge to `unsat`.
  //
  // `asmSeq` is the per-function sequence counter incremented once per AsmExpr
  // visit (in declaration order). `asmDeclsEmitted` dedups so the
  // `(declare-fun asm_<fn>_<seq> ...)` line ships exactly once per block even
  // when smtExpr AND smtExprWp both visit the AsmExpr (e.g. the ExprStmt /
  // AsmExpr arm in smtEncodeStmt lowers the outputs via smtExprWp, while a
  // clause that names the asm result-symbol via the spec fn re-enters smtExpr).
  // `asmAxiomsEmitted` dedups the top-level `(assert (forall ...))` axiom
  // analogously (the axiom is universal, so asserting it twice is redundant but
  // not unsound; dedup keeps the .smt2 file readable).
  // `asmExprSeq` keys an AsmExpr pointer (opaque `const void*` to keep Smt.h
  // free of the AST.h include) to its assigned `seq`, so the SAME asm! block
  // gets the SAME symbol `asm_<curFn>_<seq>` whether it's first visited by
  // smtExpr or by smtExprWp. Both lowerers append to the per-Ctx dedup sets
  // above so the declare-fun + forall-assert emit only on the first visit.
  int asmSeq = 0;
  std::set<std::string> asmDeclsEmitted;
  std::set<std::string> asmAxiomsEmitted;
  // Verified-asm `implements` link dedup. smtAsmTerm stamps this with the
  // per-block symbol `asm_<curFn>_<seq>` once it has emitted the implements
  // hypothesis (asserted each of the asm spec's `ensures` clauses, guarded by
  // the `requires` conjunction, plus discharged each `requires` as a caller
  // proof obligation) so the SECOND visit (smtExpr after smtExprWp or vice-
  // versa) does not re-discharge / re-assert the hypothesis. The hypothesis is
  // idempotent (asserting a clause twice is redundant but sound)  -  dedup keeps
  // the .smt2 file readable and avoids duplicate check-sat for the requires
  // discharges (which would pollute the witness count).
  std::set<std::string> asmImplHypsEmitted;
  std::map<const void*, int> asmExprSeq;
  // When the WP path lowers the AsmExpr, the matching ground spec body for the
  // block's specific inputs (the spec-fn body with params bound to the asm
  // INPUT terms) is appended here so the caller (the ExprStmt-AsmExpr arm in
  // smtEncodeStmt OR smtExprWp's recursive caller) can push it into
  // `c.wpPremises` as a scoped discharge premise  -  exactly how `requires`
  // clauses and call-site assumes are threaded. Cleared by the immediate
  // caller after consumption.
  std::vector<std::string> asmSpecPremises;
  // MULTI-OUTPUT `asm!` side channel. smtAsmTerm fills this with the per-output
  // applied terms `(asm_<fn>_<seq>_out0 <inputs...>)`, `(asm_<fn>_<seq>_out1
  // <inputs...>)`, ... for blocks with outCount > 1 (empty for single/zero
  // outputs). The ExprStmt-AsmExpr arm consumes it to rebind each output
  // lvalue in the WP store to its OWN per-output symbol (without this, every
  // output of a multi-output block would wrongly bind to the same term).
  // Cleared-and-filled at every smtAsmTerm call  -  a prior block's vector never
  // leaks into the next.
  std::vector<std::string> asmOutputTerms;
};

// ox:proof SMT sort name for an Oxide type. Bool -> "Bool", f32/f64 -> "Real",
// everything else (i*/u*/ptr/enum/...) -> "Int".
// By-value, fully recursive variant: nested-array sorts (`[[i64; 2]; 2]` ->
// `(Array Int (Array Int Int))`) compose correctly (no shared static buffer).
std::string smtSortStr(const BType& t);
// Backward-compat const char* shim  -  materialises smtSortStr into a
// thread-local buffer ONCE per call. Callers that stash the pointer across
// multiple smtSort calls must copy to std::string themselves (see Ghost.cpp's
// std::vector<std::string> use).
const char* smtSort(const BType& t);

// ox:proof SMT-legal identifier for a function param / local / result / old-name.
// Prefixed to avoid clashes with SMT keywords and other Oxide names.
std::string sym(const std::string& kind, const std::string& name);

// Emit `(declare-const name sort)` once and remember it in `declared`.
void smtDeclareConst(SmtCtx& c, const std::string& name, const char* sort);

// ox:proof Recursively lower one spec expression to an SMT term (string). Unsupported
// subforms go to `smtPlaceholder` (honest uninterpreted const + `; note:`).
std::string smtExpr(SmtCtx& c, const Expr* e);

// Synthesize `ph<N>` (function-qualified) as a fresh uninterpreted constant of
// `sort`, declare + emit a `; note:` describing `why`, and return its name.
std::string smtPlaceholder(SmtCtx& c, const char* sort, const char* why);

// ox:why Collect every `old(<bareName>)` mention in a spec expr (so the function's
// entry can pre-declare the old_<x> snapshots once).
void collectOldNames(const Expr* e, std::set<std::string>& out);

// ox:proof Emit the discharge query for one clause:
//   (push) [(assert <premise>) ...] (assert (not <clause-term>)) (check-sat) (pop)
// `unsat` => clause holds for all inputs under the premises. `premises` is
// how `requires` is carried into the discharge of `ensures`/`assert`/
// `invariant` (Hoare-logic: prove Body ⊢ clause under Pre).
void smtDischarge(SmtCtx& c, const std::string& label, const std::string& term,
                  const std::vector<std::string>& premises = {});

// ox:proof Emit a clause as a named `(define-fun <label> () Bool <term>)` and discharge
// it. `premises` forwarded to smtDischarge. Caller pre-populates `c.declared`.
void smtClause(SmtCtx& c, const std::string& fnName, const char* kind, int idx,
               const Expr* e, const std::vector<std::string>& premises = {});

// ox:proof Entry point called from Driver::run for `--emit-smt PATH` (and by `verify`).
// Walks every non-extern function with contracts, writes a complete .smt2 file
// (contracts section + the ghost section from emitGhostSection + `(exit)`).
bool emitSmt(const Program& prog, const std::string& outPath);

// ox:proof T1/T2/T3 Ghost encoder  -  implemented in src/Ghost.cpp, invoked from
// emitSmt right before the trailing `(exit)`. Appends to the SAME ostringstream
// so everything lands in one .smt2 file a single Z3 run can consume.
//
//   T1  -  `spec fn` declarations : each spec fn is either a real SMT
//        `(define-fun ...)` (if its body is pure arithmetic/boolean over its
//        params) or a `(declare-fun ...)` uninterpreted function (honest, with
//        a `; note:`) when the body contains calls/array/field  -  so contract
//        clauses that NAME a spec fn resolve to a real symbol.
//        `refines <concrete> <= <spec>` emits a discharge query per relation:
//        forall args (requires_spec(args) ==> requires_concrete(args)) AND
//        forall args (ensures_concrete(args) ==> ensures_spec(args)). If either
//        name is unresolved or signatures don't match, it emits nothing but a
//        `; note: refines <a> <= <b> skipped` honesty line  -  NEVER a false
//        `unsat`.
//   T2  -  ghost let / ghost fn : every `ghost let x: T` in the program becomes
//        a `(declare-const ghost_<fn>_<x> <Sort>)` so contract clauses that
//        reference it resolve to a real symbol (its value is unconstrained
//        Int/Bool  -  sound; the proof obligation is carried by the clause).
//        `ghost fn` ensures/requires are reflected like any concrete fn.
//   T3  -  region / modifies : each `region R = { a, b, ghost_c }` is expanded
//        to its member set. For each function with a `modifies` clause, a
//        frame axiom per NON-modified symbol is emitted: that symbol equals
//        its `old()` value. If `modifies` is empty (the conservative default),
//        no frame axiom is emitted  -  the function is treated as a potential
//        mutator of everything. The `; note:` mechanism documents each
//        emitted axiom so Z3 output stays readable.

// ox:proof The ghost section is a no-op (emits nothing) when the program declares none
// of the T1/T2/T3 constructs  -  so existing contract output stays
// byte-identical (this is what makes the feature additive).
void emitGhostSection(const Program& prog, std::ostringstream& out);

// Tier 1  -  per-function post-state terms for mutable globals.
//
// After `emitFnContracts` walks a function's body with the WP encoder
// (`smtEncodeStmts`), the symbolic `store` it threaded through the body
// holds, for every name the body touched, the final SMT term that name
// resolves to at function exit. By seeding `store[g]` at function entry
// with a fresh pre-state const `<fn>_old_<g>` for every non-const global `g`,
// we get:
//   - if the body NEVER assigns `g`: `store[g]` is still `<fn>_old_<g>` at
//     exit → the frame axiom `(= store[g] <fn>_old_<g>)` collapses to
//     `(= old old)` and discharges (unsat negation)  -  provably unchanged.
//   - if the body ASSIGNS `g`: `store[g]` is the new post term, generally
//     distinct from `<fn>_old_<g>` → the frame's negation is sat, honestly
//     flagging that the function touched a global it didn't claim to modify.
//
// `PostStateMap` is keyed by the fn's symbol prefix (free fn name OR
// `mangleMethod(structName, methodName)` for impl methods  -  matching the
// Ghost section's frame-axiom keying), then by the bare source-global name.
// A missing (fn, g) entry means the WP encoder could not symbolically track
// `g` for that fn (e.g. unsupported body form, or the post term escaped via
// an early `return` inside a branch the WP walker couldn't merge). The frame
// emitter falls back to today's vacuous axiom (two fresh top-level uninterp
// consts)  -  strictly weaker but never unsound.
using PostStateMap = std::map<std::string, std::map<std::string, std::string>>;

// Tier 1  -  overload of `emitGhostSection` that also threads the per-function
// post-state map so frame axioms are non-vacuous. The 2-arg overload above
// delegates here with an empty map (yields the pre-Tier-1 vacuous-frame
// behaviour for any hypothetical external caller).
void emitGhostSection(const Program& prog, std::ostringstream& out,
                      const PostStateMap& postIdents);

} // namespace ox_smt
