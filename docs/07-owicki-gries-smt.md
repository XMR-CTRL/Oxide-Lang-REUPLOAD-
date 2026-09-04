> **OXIDE** · Owicki-Gries SMT Encoding
> How concurrent non-interference obligations are lowered into SMT-LIB checks.

# Owicki-Gries Non-Interference: SMT-LIB Encoding Strategy

**For**: Oxide compiler (`src/Driver.cpp` SMT emitter, `src/Ghost.cpp` ghost section)
**Scope**: Obligation #3 (non-interference / stability) of the Owicki-Gries method
**Status**: Design document, implementable SMT emission plan

---

## 1. Problem Statement

Oxide's existing `preserves <handler> <= <invariant>` discharges per-handler
invariant preservation: "for all args satisfying the handler's requires, the
inlined body's post-state satisfies invariant I." This is Owicki-Gries
obligation #2 (sequential correctness), each handler alone preserves I.

**Missing**: Obligation #3, **non-interference (stability)**. When VCPU_A
executes one of its atomic blocks, the assertions that VCPU_B is currently
holding must remain valid under A's state transformation. If not, A's step can
falsify B's pending assertion, and the per-handler proofs don't compose.

The question: for every atomic step of handler H_A, does every assertion
P_B (from handler H_B's annotation set) remain valid after H_A's step?

---

## 2. Key Design Decisions

### A. Exact SMT-LIB Shape of a Non-Interference Check

One stability check per (assertion P, atomic-step S) pair. The shape:

```smt
(push)
; --- Shared state: pre-state symbols (the "global" state both VCPUs see) ---
(declare-const ept_pre (Array Int Int))
(declare-const vmcs_eptp_pre Int)
; ... one per shared mutable symbol ...

; --- Post-state: S_A's effect as state-transformer equalities ---
(declare-const ept_post (Array Int Int))
(declare-const vmcs_eptp_post Int)
; ... one per shared mutable symbol ...
; Step relation: equalities that DEFINE post-state in terms of pre-state
; + the step's own preconditions (asserted as premises).
(assert (= ept_post (store ept_pre idx_A val_A)))       ; step's write
(assert (= vmcs_eptp_post vmcs_eptp_pre))                ; step doesn't touch this
; ... frame: every untouched symbol equals its pre-state ...

; --- Assertion P_B evaluated in PRE-state (the assertion VCPU_B holds) ---
; P_B is lowered via smtExpr against the _pre symbols
; --- The SAME assertion P_B evaluated in POST-state ---
; P_B_post is the same expression text, but every shared symbol replaced by _post

; --- Premises: step's own preconditions (H_A's requires, scoped to this step) ---
; These are asserted as top-level (assert ...) so Z3 treats them as facts.
(assert req_A_premises)

; --- Goal: P_B(pre) ==> P_B(post)  ---
; Negate and check:
(assert (not (=> P_B_pre P_B_post)))
(check-sat)
(pop)
```

`unsat` ⇒ the assertion P_B is stable under step S_A.

### B. Shared State Modeling

**Decision**: Model each shared mutable global as a **separate top-level
declare-const** per field, using SMT `(Array Int <elemSort>)` for arrays (the
existing `smtSortStr` convention) and `Int`/`Bool`/`(_ BitVec 64)` for scalars.

This matches the existing Oxide SMT emission:
- Arrays already emit as `(declare-const <sym> (Array Int Int))` via `smtSortStr`.
- Struct-element arrays already flatten to per-field arrays via `structArrayFields`.
- BitVec symbols already flow through `c.bvVars`.

We do NOT use a single monolithic "state" record. Each shared symbol is
independent, this is what Z3's array theory congruence closure is built for
(`select`/`store` reads-over-writes is decidable). A monolithic record would
require uninterpreted `update_field` functions, losing decidability.

**Two copies per stability check**: `ept_pre` and `ept_post`. The step
relation links them via `store`/equality. The assertion is evaluated against
both copies and the goal is `P(pre) ⇒ P(post)`.

### C. Step Semantics, Atomic Block as State Transformer

Each `atomic { ... }` block in a handler becomes a **state transformer**: a
conjunction of equalities defining post-state symbols in terms of pre-state
symbols.

The existing WP mini-walker (`smtConcreteCallResult` in Driver.cpp) already
computes, for a handler body, the symbolic `postStore` map: for each mutated
array param/global, the `(store arr idx val)` term. We reuse this:
1. Run the WP mini-walker on the **atomic block's statement list** (not the
   whole handler body), seeded with pre-state symbols.
2. The resulting `postStore` gives us the post-state term for each symbol.
3. Emit `(assert (= <sym>_post <postStore[sym]>))` for each shared symbol.
4. For symbols not in `postStore` (the atomic block didn't touch them), emit
   `(assert (= <sym>_post <sym>_pre))`, the frame condition.

### D. Query Cardinality

For N handlers, each with K_i atomic blocks, and each atomic block carrying
M_j assertions (preconditions, invariants, postconditions that become
"pending assertions" other VCPUs must see), the total count is:

```
sum over all ordered pairs (H_A, H_B) where H_A != H_B:
    sum over each atomic block S in H_A:
        sum over each assertion P in H_B's annotation set:
            1 stability check
```

In practice:
- **2 VCPUs, 2 handlers each with 1 atomic block and 1 assertion**: 2×2×1×1 = 4 checks.
- **2 VCPUs, 2 handlers each with 2 atomic blocks and 3 assertions**: 2×2×2×3 = 24 checks.

This is **O(A × P)** where A = total atomic blocks across all handlers and
P = total assertions. For the Oxide hypervisor case (small handler count,
few atomic blocks), this is tens of queries, well within Z3's throughput.

**Key: assertions come from the invariant itself plus any `assert` and
`invariant` annotations in the handler body.** The simplest sound scheme: the
"assertion set" for handler H_B is { the global invariant I } plus every
`assert` clause in H_B's body. A richer scheme adds `while`/`for` loop
invariants to the pending set too.

### E. Worked Example

See Section 5 below.

---

## 3. Extending the AST

### 3a. New Module-Level Declaration: `noninterference`

```oxide
// Existing:
atomic { ept[gpa] = flags; }

// New module-level declaration:
noninterference handle_ept_map <= handle_ept_unmap;
```

**AST addition** (in `AST.h`, near `PreservesDecl`):

```cpp
// Owicki-Gries non-interference (stability) declaration.
//   noninterference <handler_A> <= <handler_B>;
// Emits stability checks: for every atomic block in handler_A,
// every assertion in handler_B's annotation set is stable under it.
struct NonInterferenceDecl {
  std::string stepHandler;    // the handler whose atomic steps we check
  std::string assertHandler;  // the handler whose assertions must be stable
  int line = 0;
};
```

Add to `Program`:
```cpp
std::vector<std::unique_ptr<NonInterferenceDecl>> noninterferences_;
```

### 3b. New Lexer Keyword

In `Lexer.h`:
```cpp
kw_noninterference,
```
In `Lexer.cpp` keyword table:
```cpp
{"noninterference", Tok::kw_noninterference},
```

### 3c. Parser

Parse `noninterference <id> <= <id> ;` and push a `NonInterferenceDecl`.
Mirror the `preserves` parse path exactly (both use `<=` = `Tok::lteq`).

---

## 4. SMT-LIB Encoding, Full Schematic

### 4a. Per-Check Structure

For each (step-handler H_A, assert-handler H_B) pair from a
`noninterference` declaration, and for each atomic block S in H_A and each
assertion P in H_B's set:

```smt
; =================== Owicki-Gries stability check ===================
; step-handler:  handle_ept_map  (atomic block at line 42)
; assert-handler: handle_ept_unmap (assertion: invariant ept_ok)
; ============================================================
(push)

; --- 1. Pre-state shared symbols (what both VCPUs see before the step) ---
(declare-const ni_ept_pre (Array Int Int))
(declare-const ni_vmcs_eptp_pre Int)

; --- 2. Step's OWN parameters (the atomic block's local inputs) ---
;    These are bound to the step-handler's param symbols. For a handler
;    `fn handle_ept_map(ept: [i64;4], gpa: i64, flags: i64)`, the atomic
;    block sees ept (the pre-state array), gpa, flags as free inputs.
(declare-const ni_step_gpa Int)
(declare-const ni_step_flags Int)

; --- 3. Post-state shared symbols (after H_A's atomic block runs) ---
(declare-const ni_ept_post (Array Int Int))
(declare-const ni_vmcs_eptp_post Int)

; --- 4. Step relation: post = transform(pre, step_params) ---
;    The WP mini-walker ran on the atomic block's statements, seeded with
;    pre-state symbols. postStore gives us the post term for each symbol.
;    Frame: untouched symbols equal their pre-state.
(assert (= ni_ept_post (store ni_ept_pre ni_step_gpa ni_step_flags)))
(assert (= ni_vmcs_eptp_post ni_vmcs_eptp_pre))

; --- 5. Step preconditions (H_A's requires clauses, which gate the step) ---
(assert (and (>= ni_step_gpa 0) (< ni_step_gpa 4)))

; --- 6. Assertion P_B evaluated in PRE-state ---
;    Lowered via smtExpr with a nameMap pointing shared symbols to *_pre.
;    For invariant `ept_ok(ept) = forall k in 0..4 implies ept[k] >= 0`:
;    (pre-state form, unrolled; same as preserves G2b path)
(assert (and
  (>= (select ni_ept_pre 0) 0)
  (>= (select ni_ept_pre 1) 0)
  (>= (select ni_ept_pre 2) 0)
  (>= (select ni_ept_pre 3) 0)))

; Wait; no. The assertion is NOT asserted as a fact. It's the LHS of the
; implication we're trying to prove. Let me re-state:

; --- 6. Assertion P_B in PRE-state (as a TERM, not asserted) ---
;    P_B_pre = (and (>= (select ni_ept_pre 0) 0) ... )

; --- 7. Assertion P_B in POST-state (same expression, _post symbols) ---
;    P_B_post = (and (>= (select ni_ept_post 0) 0) ... )

; --- 8. Non-interference obligation: P_B_pre ==> P_B_post ---
;    Negate and check-sat. unsat => P_B is stable under H_A's step.
(assert (not (=> P_B_pre P_B_post)))
(check-sat)
(pop)
```

**Critical**: P_B_pre and P_B_post are the **same assertion expression** lowered
twice, once with the nameMap mapping shared symbols to `*_pre`, once with
`*_post`. The step relation (section 4) and step preconditions (section 5) are
(top-level) `assert`ed facts. The obligation (section 8) is `(assert (not (=> ...)))`.

### 4b. Why This Shape Works

- **Z3 sees a quantifier-free array-theory problem** (when the assertion is
  unrolled via `smtUnrollQuantForall`, the existing G2b path). `select` over
  `store` is decidable, Z3's congruence closure handles reads-over-writes
  natively.

- **The step relation is conjunctive equalities**, no uninterpreted
  functions, no quantifiers. The post-state is fully defined by the pre-state
  and the step's parameters.

- **The assertion is ground** (after unrolling), `select` on `ept_pre` vs
  `select` on `ept_post` where `ept_post = (store ept_pre gpa flags)`. Z3
  folds `(select (store ept_pre gpa flags) k)` to `(ite (= gpa k) flags (select ept_pre k))`
 , a ground `ite` it decides instantly.

### 4c. Handling Quantified Invariants (foralls)

When the invariant I contains a `forall k in 0..N implies P(k)`:
- **Unroll** it via the existing `smtUnrollQuantForall` helper (G2b path).
  This produces `(and P(0) P(1) ... P(N-1))`, ground, decidable.
- The unrolled assertion is evaluated twice: once over `_pre`, once over `_post`.
- Sound: the unroll is equivalent to the forall under the literal bound (same
  soundness argument as the existing preserves path).

When the invariant has a non-literal bound (G2b sound fallback):
- Emit a **real `forall`** for both P_B_pre and P_B_post:
  ```smt
  (assert (not (=>
    (forall ((k Int)) (=> (and (>= k 0) (< k 4)) (>= (select ni_ept_pre k) 0)))
    (forall ((k Int)) (=> (and (>= k 0) (< k 4)) (>= (select ni_ept_post k) 0))))))
  ```
- Use the same MBQI-off tactic hint (`(set-option :smt.mbqi false)`) +
  `(check-sat-using (then simplify smt))` the preserves path already uses.
- Add `:pattern ((select ni_ept_pre k))` and `:pattern ((select ni_ept_post k))`
  via the existing pattern-collection logic.

---

## 5. Concrete Worked Example

### 5a. Oxide Source

```oxide
// Shared state: EPT (Extended Page Table); 4 entries, each an i64
// Global mutable array accessible by both VCPU handlers.
let mut ept: [i64; 4];

// Invariant: every EPT entry is non-negative (valid mapping flag bit clear)
spec fn ept_ok(ept: [i64; 4]) -> bool =
  forall k: i64 in 0..4 implies ept[k] >= 0;

// VCPU 0 handler: map a GPA to a flags value
fn handle_ept_map(gpa: i64, flags: i64): void
  requires gpa >= 0 && gpa < 4 && flags >= 0
{
  atomic {
    ept[gpa] = flags;
  }
}

// VCPU 1 handler: unmap a GPA (set to 0)
fn handle_ept_unmap(gpa: i64): void
  requires gpa >= 0 && gpa < 4
{
  atomic {
    ept[gpa] = 0;
  }
}

// Per-handler preservation (existing feature):
preserves handle_ept_map <= ept_ok;
preserves handle_ept_unmap <= ept_ok;

// NEW: non-interference declarations (both directions)
noninterference handle_ept_map <= handle_ept_unmap;
noninterference handle_ept_unmap <= handle_ept_map;
```

### 5b. Assertion Sets

For this example, each handler's "assertion set" is { the invariant ept_ok
applied to the shared state }. The global invariant is the predicate
`ept_ok(ept)`, it's the assertion every handler must preserve AND that must
be stable under every other handler's steps.

Simpler formulation: since `preserves` already proves each handler preserves
I individually, the non-interference checks only need to verify:
**I holds in pre-state ⇒ I holds in post-state, for each cross-handler step.**

### 5c. Emitted SMT-LIB (one of the four checks)

This is the check: "VCPU B holds `ept_ok(ept)`. VCPU A runs `handle_ept_map`'s
atomic block. Does `ept_ok` survive?"

```smt
; =================== Owicki-Gries stability check ===================
; step-handler:    handle_ept_map    (atomic block, source line 42)
; assert-handler:  handle_ept_unmap  (assertion: ept_ok)
; Check: ept_ok(ept_pre) ==> ept_ok(ept_post), where ept_post is the
;       result of handle_ept_map's atomic block running on ept_pre.
; ============================================================
(push)

; --- 1. Pre-state shared symbols ---
(declare-const ni_ept_pre (Array Int Int))

; --- 2. Step parameters (handle_ept_map's atomic block inputs) ---
(declare-const ni_step_gpa Int)
(declare-const ni_step_flags Int)

; --- 3. Post-state shared symbols ---
(declare-const ni_ept_post (Array Int Int))

; --- 4. Step relation: handle_ept_map's atomic block ---
;    `ept[gpa] = flags`  →  ept_post = (store ept_pre gpa flags)
(assert (= ni_ept_post (store ni_ept_pre ni_step_gpa ni_step_flags)))

; --- 5. Step preconditions (handle_ept_map's requires) ---
(assert (and
  (>= ni_step_gpa 0)
  (<  ni_step_gpa 4)
  (>= ni_step_flags 0)))

; --- 6. Assertion ept_ok in PRE-state (unrolled forall) ---
;    forall k in 0..4 implies ept[k] >= 0  →  unrolled:
;    (and (>= (select ni_ept_pre 0) 0) ... (>= (select ni_ept_pre 3) 0))
;    This is P_B_pre; a TERM, not a fact.

; --- 7. Assertion ept_ok in POST-state (same expression, _post) ---
;    P_B_post = (and (>= (select ni_ept_post 0) 0) ... (>= (select ni_ept_post 3) 0))

; --- 8. Non-interference obligation (negated for check-sat) ---
(assert (not (=>
  (and
    (>= (select ni_ept_pre 0) 0)
    (>= (select ni_ept_pre 1) 0)
    (>= (select ni_ept_pre 2) 0)
    (>= (select ni_ept_pre 3) 0))
  (and
    (>= (select ni_ept_post 0) 0)
    (>= (select ni_ept_post 1) 0)
    (>= (select ni_ept_post 2) 0)
    (>= (select ni_ept_post 3) 0)))))
(check-sat)
(pop)
```

**Z3 reasoning**:
- `ept_post[0] = (select (store ept_pre gpa flags) 0) = (ite (= gpa 0) flags (select ept_pre 0))`
- For index 0: if `gpa = 0`, post-0 = `flags` and we know `flags >= 0` (step precondition), so `post-0 >= 0` holds. If `gpa != 0`, post-0 = pre-0 and we assumed pre-0 >= 0.
- Same for all 4 indices. Z3 folds the `ite` and discharges each conjunct.
- **Result: `unsat`**, the assertion is stable.

### 5d. The Counter-Example (what would fail)

If `handle_ept_map` set `ept[gpa] = flags` WITHOUT the precondition
`flags >= 0`, the same check would be `sat`, Z3 finds `flags = -1, gpa = 2`
as a counterexample where pre-state is fine but post-state entry 2 is -1 (negative),
violating the invariant. This is the exact soundness guarantee we need.

---

## 6. Emission Algorithm, Pseudocode

### 6a. Collect Atomic Blocks

```
function collectAtomicBlocks(handler_fn: FuncDecl) -> list<Block>:
  """Walk the handler body, return all Blocks with isAtomic=true."""
  blocks = []
  for stmt in walkAllStmts(handler_fn.body):
    if stmt is Block && stmt.isAtomic:
      blocks.append(stmt)
  return blocks
```

### 6b. Collect Assertion Set

```
function collectAssertions(handler_fn: FuncDecl, invariant: SpecFnDecl) -> list<Expr>:
  """The assertion set for Owicki-Gries: the invariant plus every
     `assert` clause in the handler body. These are the predicates
     other VCPUs must not falsify when this handler is between steps."""
  asserts = []
  // The global invariant is always in the set (if the handler shares its state).
  if invariant != null:
    asserts.append(invariant.body)
  // Every `assert <expr>` in the body is a locally-proven fact that
  // must survive other VCPUs' steps.
  for stmt in walkAllStmts(handler_fn.body):
    if stmt is AssertStmt:
      asserts.append(stmt.cond)
    // Loop invariants are pending at the loop head; include them.
    if stmt is WhileStmt:
      asserts.append_all(stmt.invariants)
    if stmt is ForStmt:
      asserts.append_all(stmt.invariants)
  return asserts
```

### 6c. Emit Non-Interference Section

```
function emitNonInterference(prog: Program, out: ostringstream):
  if prog.noninterferences_.empty():
    return

  out << header_comment

  // Shared maps (same as emitPreserves)
  specFns  = collectSpecFns(prog)
  funcDecls = collectFuncDecls(prog)

  for ni in prog.noninterferences_:
    H_A = findFuncByName(prog, ni.stepHandler)
    H_B = findFuncByName(prog, ni.assertHandler)

    // Find the invariant both handlers share (look for a `preserves`
    // decl for H_B to identify which invariant applies, or use a
    // convention: the first Bool spec fn whose params match the
    // shared state).
    invariant = findSharedInvariant(prog, H_B)

    atomicBlocks = collectAtomicBlocks(H_A)
    assertions   = collectAssertions(H_B, invariant)

    for blockIdx, block in enumerate(atomicBlocks):
      for assertIdx, assertExpr in enumerate(assertions):
        emitOneStabilityCheck(
          prog, out, H_A, H_B, block, blockIdx,
          assertExpr, assertIdx, invariant, specFns, funcDecls)
```

### 6d. Emit One Stability Check (the core)

```
function emitOneStabilityCheck(prog, out, H_A, H_B, block, blockIdx,
                                assertExpr, assertIdx, invariant,
                                specFns, funcDecls):

  // Generate a unique label for this check
  label = "ni_" + H_A.name + "_step" + blockIdx +
          "_" + H_B.name + "_assert" + assertIdx

  out << comment("stability: " + H_A.name + " step#" + blockIdx +
                 " vs " + H_B.name + " assertion#" + assertIdx)

  // --- Identify shared mutable symbols ---
  // These are the non-const global arrays/vars that BOTH handlers
  // can read or write. We find them by scanning both handlers' bodies
  // for assignments to globals, plus the invariant's referenced symbols.
  sharedSyms = collectSharedMutableGlobals(prog, H_A, H_B, invariant)

  // --- 1. Pre-state symbols ---
  preSyms = {}  // map: sourceName -> smtSymbol
  out << "(push)\n"
  for sym in sharedSyms:
    preSym = label + "_pre_" + sym.name
    out << "(declare-const " << preSym << " " << smtSortStr(sym.type) << ")\n"
    preSyms[sym.name] = preSym

  // --- 2. Step parameters (H_A's handler params, as free inputs) ---
  stepParams = {}
  for param in H_A.params:
    stepParam = label + "_step_" + param.name
    out << "(declare-const " << stepParam << " " << smtSortStr(param.type) << ")\n"
    stepParams[param.name] = stepParam

  // --- 3. Post-state symbols ---
  postSyms = {}
  for sym in sharedSyms:
    postSym = label + "_post_" + sym.name
    out << "(declare-const " << postSym << " " << smtSortStr(sym.type) << ")\n"
    postSyms[sym.name] = postSym

  // --- 4. Step relation: run WP mini-walker on the atomic block ---
  // Create a fresh SmtCtx seeded with pre-state symbols.
  ctx_A = SmtCtx{out, ...}
  ctx_A.curFn = label + "_step"
  ctx_A.specFns = specFns
  ctx_A.funcDecls = funcDecls

  // Bind H_A's params to stepParams in nameMap
  for param in H_A.params:
    ctx_A.nameMap[param.name] = stepParams[param.name]

  // Bind shared symbols to pre-state symbols in nameMap + store
  store_A = {}
  for sym in sharedSyms:
    ctx_A.nameMap[sym.name] = preSyms[sym.name]
    store_A[sym.name] = preSyms[sym.name]
    // For struct-element arrays, also seed structArrayFields
    if sym.type is struct-element array:
      seedStructArrayFields(ctx_A, sym, preSyms[sym.name])

  // Run the WP mini-walker on the atomic block ONLY
  postStore = {}
  smtEncodeStmts(ctx_A, H_A.name, block.stmts, 0, {}, store_A, "", _, _)

  // --- 4b. Emit step relation equalities ---
  for sym in sharedSyms:
    postTerm = postStore.get(sym.name, preSyms[sym.name])  // default: unchanged
    out << "(assert (= " << postSyms[sym.name] << " " << postTerm << "))\n"

  // --- 5. Step preconditions (H_A's requires) ---
  reqPremise = "true"
  for req in H_A.requires_:
    term = smtExpr(ctx_A, req)  // uses nameMap with stepParams + preSyms
    reqPremise = andJoin(reqPremise, term)
  out << "(assert " << reqPremise << ")\n"

  // --- 6. Build assertion P_B in PRE-state ---
  ctx_pre = SmtCtx{out, ...}
  ctx_pre.curFn = label + "_assert_pre"
  ctx_pre.specFns = specFns

  // Bind shared symbols to pre-state symbols
  for sym in sharedSyms:
    ctx_pre.nameMap[sym.name] = preSyms[sym.name]
    if sym.type is struct-element array:
      seedStructArrayFields(ctx_pre, sym, preSyms[sym.name])

  // Bind H_B's params to stepParams (if H_B shares params with H_A; 
  // for shared-state invariants, the invariant sees the same symbols).
  // The invariant spec fn's params are bound to the shared symbols.
  if invariant != null:
    for i, param in enumerate(invariant.params):
      // Positional alignment with shared symbols or H_B's params
      if i < sharedSyms.size():
        ctx_pre.nameMap[param.name] = preSyms[sharedSyms[i].name]

  // Lower the assertion (with G2b unrolling if it's a forall)
  P_pre = lowerAssertion(ctx_pre, assertExpr, invariant)

  // --- 7. Build assertion P_B in POST-state ---
  ctx_post = SmtCtx{out, ...}
  ctx_post.curFn = label + "_assert_post"
  ctx_post.specFns = specFns

  // Same bindings but with _post symbols
  for sym in sharedSyms:
    ctx_post.nameMap[sym.name] = postSyms[sym.name]
    if sym.type is struct-element array:
      seedStructArrayFields(ctx_post, sym, postSyms[sym.name])

  if invariant != null:
    for i, param in enumerate(invariant.params):
      if i < sharedSyms.size():
        ctx_post.nameMap[param.name] = postSyms[sharedSyms[i].name]

  P_post = lowerAssertion(ctx_post, assertExpr, invariant)

  // --- 8. Emit the negated non-interference obligation ---
  out << "(assert (not (=> " << P_pre << " " << P_post << ")))\n"

  // Use the MBQI-off tactic if the assertion is quantified
  if isQuantified(P_pre):
    out << "(set-option :smt.mbqi false)\n"
    out << "(check-sat-using (then simplify smt))\n"
    out << "(set-option :smt.mbqi true)\n"
  else:
    out << "(check-sat-using (then simplify smt))\n"

  out << "(pop)\n"
  out << comment("stability discharge; unsat => assertion stable under step")
```

### 6e. Helper: lowerAssertion

```
function lowerAssertion(ctx, assertExpr, invariant) -> string:
  """Lower an assertion expression to an SMT term. If the expression
     is the invariant (a SpecFnDecl body that's a forall), try the
     G2b unroll path; fall back to recursive smtExpr with quantifier."""
  if assertExpr is QuantExpr (or invariant.body is QuantExpr):
    quantBody = (assertExpr as QuantExpr) or (invariant.body as QuantExpr)
    note = ""
    unrolled = smtUnrollQuantForall(ctx, quantBody, note)
    if unrolled is not empty:
      return unrolled
    else:
      // Sound fallback: real forall
      result = smtExpr(ctx, assertExpr or invariant.body)
      markAsQuantified(result)
      return result
  else:
    return smtExpr(ctx, assertExpr)
```

---

## 7. Implementation Notes for C++ Translation

### 7a. Where to Add in the Codebase

1. **`AST.h`**: Add `NonInterferenceDecl` struct and `Program::noninterferences_` vector (mirror `PreservesDecl`).

2. **`Lexer.h` / `Lexer.cpp`**: Add `kw_noninterference` keyword.

3. **`Parser.cpp`**: Add parse arm for `noninterference <id> <= <id> ;`, copy the `preserves` parse arm and rename.

4. **`Ghost.cpp`**: Add `emitNonInterference` function, call it from `emitGhostSection` after `emitPreserves`. This keeps all ghost/spec discharges in one Z3 run.

5. **`Driver.cpp`**: Export a helper `collectAtomicBlocks(fn)` and reuse `smtEncodeStmts` for the step relation. The WP mini-walker is already accessible.

6. **`Smt.h`**: No structural changes needed, the existing `SmtCtx` supports everything (nameMap, store, declared, structArrayFields, arrayLenSyms, specFns, funcDecls).

### 7b. Key Reuse from Existing Code

| Existing function | Reuse for |
|---|---|
| `smtEncodeStmts` | Running the WP mini-walker on an atomic block's statement list |
| `smtConcreteCallResult` (with `postStore`) | Getting post-state terms for the step relation, BUT we run on the atomic block only, not the whole handler |
| `smtUnrollQuantForall` | Unrolling the invariant's `forall k in 0..N` for ground discharge |
| `smtExpr` | Lowering the assertion expression to SMT terms |
| `smtDischarge` pattern (`push`/`assert`/`check-sat`/`pop`) | The standard negated-for-unsat query shape (but we build it manually for the `=>` obligation) |
| `collectOldNames` | Detecting `old(x)` references in assertions (rare for shared-state invariants, but supported) |
| `structArrayFields` seeding | Structural-array flattening for struct-element shared arrays |

### 7c. Important: Running WP on an Atomic Block (Not the Whole Handler)

The existing `smtConcreteCallResult` runs the WP mini-walker on a function's
entire body. For the step relation, we need to run it on JUST the atomic
block's `stmts` vector. This is a direct call to `smtEncodeStmts`:
```cpp
smtEncodeStmts(ctx_A, H_A->name, block->stmts, 0,
               /*premises=*/{}, store_A,
               /*pathCond=*/"", returned, ens);
```
The resulting `store_A` (after the call) has the post-state term for every
symbol the atomic block touched. We then emit `(assert (= post_sym postTerm))`
for each shared symbol.

### 7d. Frame Conditions (Untouched Symbols)

After running the WP walker on the atomic block, any shared symbol NOT in the
post-store was not touched by the block. Its post-state term equals its
pre-state term. Emit:
```smt
(assert (= ni_ept_post ni_ept_pre))   ; frame: untouched
```
This is automatically handled by the default: `postStore.get(sym, preSyms[sym])`.

### 7e. Handling `assert` Annotations in the Step Handler

When the atomic block contains an `assert` statement, the WP encoder already
discharges it as a fact (via `smtDischarge` inside `smtEncodeStmt`). These
discharges happen DURING the step-relation walk and may emit their own
`push`/`check-sat`/`pop`, which is fine: they're verifying the step's
internal correctness, not the non-interference obligation. The non-interference
`check-sat` is emitted AFTER the step relation, in its own `push`/`pop`.

**This is why the non-interference check gets its own `(push)`/`(pop)` wrapper
around the ENTIRE emission**, any stray `check-sat`s from within the step
encoding are scoped and don't interfere.

### 7f. When the Invariant References Handler-Specific State

The invariant `ept_ok` is a `spec fn` over the shared state (`ept`). Its
params align with the shared symbols. But if the invariant also references a
handler's local state (e.g. `result`), we handle it the same way `preserves`
does: bind `result` to the step handler's parameter or to a fresh symbol
(table-stake for the assertion, we don't prove it, we assume it as a premise).

---

## 8. Soundness Argument

The encoding is sound for the standard Owicki-Gries composition theorem:

1. **Initialization**: The invariant holds initially (discharged separately, a single check that the initial state satisfies I. This is obligation #1, not covered here but trivial: assert I against the init-state symbols, check-sat for `unsat`).

2. **Sequential correctness**: Each handler preserves I when run alone, this is what `preserves` already discharges (obligation #2).

3. **Non-interference**: For every step of H_A and every assertion P_B held by H_B, P_B is stable under H_A's step. This is what we emit here.

   **Why our encoding is sound**:
   - We model the step as a **total** state transformer (post = store/pre + frame). The step's preconditions are `assert`ed as facts, so Z3 only considers steps that respect H_A's contract.
   - The assertion P_B is evaluated identically in pre and post, the ONLY difference is shared symbols point to `pre` vs `post`.
   - The step relation fully defines `post` from `pre`, there's no nondeterminism left (the step params are free but constrained by requires).
   - `(assert (not (=> P_pre P_post)))` + `unsat` ⇒ no counterexample exists where P holds in pre and fails in post, under the step's preconditions.

4. **Composition**: If all three obligations discharge `unsat`, the Owicki-Gries theorem says the concurrent system preserves I, this is the proof-theoretic result, not something we encode in SMT.

---

## 9. Complexity and Practical Bounds

| Configuration | Atomic blocks | Assertions | Stability checks |
|---|---|---|---|
| 2 VCPUs, 1 handler each, 1 atomic block, 1 invariant | 2 | 1 | 2 × 1 × 1 = 2 |
| 2 VCPUs, 2 handlers each, 1 atomic block, 1 invariant + 2 asserts | 4 | 3 | 2 × 2 × 3 = 12 |
| 4 VCPUs, 1 handler each, 2 atomic blocks, 1 invariant + 3 asserts | 8 | 4 | 4 × 1 × 2 × 4 = 32 |

For the Oxide hypervisor use case (2-4 VCPUs, small handlers), this is 2-50
Z3 queries, well within the existing `verify` pipeline's throughput (the
`preserves` + contract walker already emits dozens per file).

Each check is **quantifier-free** (when unrolled) and uses only the **array
theory**, Z3 decides these in milliseconds.

---

## 10. Summary of Changes

| File | Change |
|---|---|
| `AST.h` | `NonInterferenceDecl` struct + `Program::noninterferences_` -->
| `Lexer.h/cpp` | `kw_noninterference` keyword -->
| `Parser.cpp` | Parse `noninterference <= ;` (mirror `preserves`) -->
| `Ghost.cpp` | `emitNonInterference()`, the main emission function (Section 6c) -->
| `Driver.cpp` | Export `collectAtomicBlocks(fn)` helper; reuse `smtEncodeStmts` for atomic block WP -->
| `Smt.h` | No structural changes (existing `SmtCtx` is sufficient) -->
