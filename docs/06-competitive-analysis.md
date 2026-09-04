> **OXIDE** · Competitive Analysis
> Oxide against SPARK, Rust, Frama-C, and C++ on verification features, with citations.

# Competitive Analysis: Oxide Formal Verification vs SPARK / Rust / Frama-C / C++

**Scope**: First-hand reading of the Oxide compiler source (`src/Driver.cpp`, `Ghost.cpp`, `ProofDispatch.cpp`, `ProofSplitter.cpp`, `Smt.h`, `AST.h`, `Sema.cpp`) + comparison against SPARK, Rust (Prusti/Creusot/Kani), Frama-C, and C++. No flattery, facts from the code.

**Correction to the task brief**: The prompt defines **OG = "Overflow Guard"**. That name does **not** appear anywhere in the Oxide source (`grep overflow_guard OG "Overflow Guard"` → no source/docs hits). In the actual codebase, **OG = Owicki-Gries**, the concurrent-verification method whose non-interference (stability) obligation is implemented as **`noninterference`** in `Ghost.cpp:1652` (`emitNoninterference`) and the lexer keyword `kw_noninterference` (`Lexer.cpp:41`). Overflow handling is a separate, ordinary matter: arithmetic is modelled as **unbounded `Int`** (sound for no-overflow Oxide semantics, `Driver.cpp:1495-1496`), with a `(_ BitVec 64)` bridge for bitops (`smtIntToBv`). This report uses the codebase's definition: **OG = Owicki-Gries non-interference**.

---

## 1. Verification features per language

| Feature | **Oxide** | **SPARK (Ada)** | **Rust** (Prusti/Creusot/Kani) | **Frama-C** (post-hoc C) | **C++** (basic) |
|---|---|---|---|---|---|
| Normal-by-design | YES, contracts are native syntax (`requires`/`ensures`/`invariant`/`assert`, lexer keywords, `Lexer.cpp`) | YES, `Pre`/`Post`/`Loop_Invariant`/`Assert` pragmas in Ada | Partial, Creusot uses `requires`/`ensures`/`invariant`/`proof_assert` annotations; Prusti similar | NO, post-hoc via **ACSL** annotation language bolted onto C | NO, `[[assume]]`/`[[assert]]` (C++23) are runtime UB-hints, not verification |
| Preconditions | `requires` | `Pre` | `requires` | `requires \\` | `[[expect]]` family (weak) |
| Postconditions | `ensures` (with `old(x)`) | `Post` (with `Old`) | `ensures` | `ensures` (with `\old`) | ❌ |
| Loop invariants | `invariant` (per-loop) | `Loop_Invariant` | `invariant` | `loop invariant` | ❌ |
| Loop variants (termination) | `decreases` (D6, single measure per fn; recursive calls assume callee `ensures`) | `Loop_Variant` ✓ | Creusot: `variant`; Kani: N/A | `loop variant` | ❌ |
| Assertions | `assert` (spec expr, SMT-discharged) | `Assert` | `proof_assert` / `assert` (runtime) | `assert` | `[[assert]]` (UB-hint) |
| Ghost code | `ghost let`, `ghost fn`, `spec fn` (no codegen) | `Ghost_Function`, `Global` in spec context | Creusot `ghost`/`pure` fn | `/*@ ghost @*/` code | ❌ |
| Quantifiers | `forall i: T in lo..hi implies P` / `exists ...` (real SMT `forall`/`exists`, `QuantExpr`, `AST.h:728`) | `for all`/`exists` in SPARK 2014 proof | Creusot `forall`/`exists` (Pearlite); Prusti limited | `\forall`/`\exists` (ACSL) | ❌ |
| Implication | `implies` keyword in spec context | `=>` | `==>` | `==>` | ❌ |
| Refinement (impl→spec) | `refines <fn> <= <spec_fn>` (`AST.h:1499`) | YES, `Refinement.lg`/proof units (mature) | Creusot: refinement via trait/spec | `refines` (ACSL) | ❌ |
| Spec functions | `spec fn` (inlined into SMT, `smtInlineSpecCall` `Driver.cpp:2229`) | `Ghost_Function`/`Expression_Function` | Creusot `predicate`/`logic` fn | `/*@ logic @*/` fns | ❌ |
| Lemmas | `lemma fn` (axiom-encoded: `forall params. R ==> E` as `(assert (forall …))`, body self-discharged, `AST.h:1215`) | YES, `Proof_Unit` lemmas, large library | Creusot `lemma` (Pearlite) | `Lemma`/`Coq`-style (via Wp-Coq) + ACSL `lemma` | ❌ |
| Proof blocks | `proof { ... }` block (lemma calls add `ensures` as hypotheses), `proof that forall k. P by induction` | YES, `Global`/`Local` proof boxes | Creusot `proof` blocks; Prusti via trusted items | `proof` clause (Wp) + interactive Coq | ❌ |
| Equational reasoning | `calc { e1; REL {hints} e2; ... }` (Dafny-style, per-step discharge + transitive composition, `Driver.cpp:5381`) | ❌ (via GNATprove tactics, not source syntax) | Creusot: `addr_of`/rewriting (partial) | ❌ (via tactics/Coq) | ❌ |
| Manual instantiation | `instantiate forall k. P on e;` (ground/pattern, `AST.h:1002`) | via `SPARK`/`Pragma` hints | limited | via `Coq`/`SSReflect` | ❌ |
| Frame clauses | `modifies` (region-expanded frame axioms), `effects { io, mmio, ... }` (effect system, `AST.h:1248`) | `Global`/`Depends`/`Contract_Cases` | Creusot `@`/`mut` tracks borrows; Kani no | ACSL `assigns` clause | ❌ (no effect system) |
| Range types | `type X = BaseTy where <bool>;` (constraint auto-emitted as premise, `AST.h:1318`) | `subtype` with predicates | Creusot `model` | ❌ | ❌ |
| Hardware/asm contracts | **`asm!` block → `spec fn asm_<fn>_<seq>` axiom** (seL4-style trust boundary, `docs/hardware-conformance.md`), uniquely mature | via `System`/`Multiply` (very limited) | Kani checks unsafe; **no asm-contract story** | ACSL `volatile`/`asm` (limited) | ❌ |
| Concurrency correctness | **Owicki-Gries: `preserves`, `noninterference`, `cycle_preserves` + `atomic { }`** (`Ghost.cpp:1604-2153`), **first-class, built-in** | Ravenscar profile (tasking restricted; no built-in non-interference like OG) | limited (loom/Chalkal research; Stacked Borrows via Miri) | **Aorai** (research plugin), no first-class OG | ❌ |
| Abstract interp (no annotations) | ❌ | ❌ | ❌ | **EVA plugin**, finds runtime errors **without contracts** by value analysis | ❌ |

---

## 2. SMT solvers used

| | **Oxide** | **SPARK** | **Rust** | **Frama-C** | **C++** |
|---|---|---|---|---|---|
| Z3 | ✓ (default) | ✓ via Why3 | ✓ (Prusti, Creusot, Kani all support Z3) | ✓ via WP | ❌ (no built-in) |
| CVC5 | ✓ (`ProofDispatch.cpp:97`) | ✓ via Why3 | ✓ (Creusot/Kani) | ✓ via WP | ❌ |
| Alt-Ergo | ✓ (`ProofDispatch.cpp:101`, `Valid`/`Invalid` parsing) | ✓ (native, often champion) | ❌ | ✓ via WP | ❌ |
| Why3 | ✓, invoked **as a solver** via `why3 prove <file>` (`ProofDispatch.cpp:95-96`) | **Why3 is the VC generator + dispatcher** (the whole point, SPARK goes through WhyML) | ❌ | **Why3 is the Wp backend** (Alt-Ergo/Z3/CVC5/Coq) | ❌ |
| Coq | ❌ | ✓ (via Why3, interactive) | ❌ | ✓ (Wp-Coq for hard goals) | ❌ |

**Oxide's dispatch architecture** (`ProofDispatch.cpp`, `ProofSplitter.cpp`):
- **Parallel threaded dispatch** to Z3/CVC5/Why3/Alt-Ergo, one `std::thread` per solver, with a `done` atomic + per-solver timeout (`ProofDispatch.cpp:161-220`).
- **Disagreement detection**, if one solver returns `unsat` and another `sat`, merged status is `"BUG"` (catches encoder soundness bugs), `mergeResults:226`.
- **Goal splitting**, 5 strategies (conjunct, implication split, quantifier unroll to 1024, branch, premise isolation) in `ProofSplitter.cpp`, the Why3 `split` transformation equivalent.
- **Proof certificate**, splits the combined `.smt2` into per-goal files + `proof_log` in `build/_proof/`, replayable via `oxide-check` against a different solver (`Driver.cpp:7179-7213`, `ProofDispatch.cpp:524-542`).

**Key architectural distinction** (matches the task brief): Oxide emits **SMT-LIB2 directly** (`Smt.h`, the `ox_smt` namespace; `emitSmt` walker). There is **no Why3 VC generator** and no WhyML logic language. Why3 is treated as just another back-end solver.

---

## 3. What Oxide can verify that the others CANNOT (or do awkwardly)

1. **First-class Owicki-Gries non-interference (the real OG).** `noninterference h1, h2, ... <= I;` discharges one stability query per `(atomic-step_S_A, pending-assertion_P_B)` pair: lower the invariant in pre- and post-state, negate `I(pre) ⇒ I(post)`, `(check-sat)` per pair (`Ghost.cpp:1652-1938`). Combined with `preserves` (per-handler I-preservation) and `cycle_preserves` (VM-exit-cycle refinement, D9), this gives a **complete Owicki-Gries proof obligation set as native source declarations**. SPARK, Rust, and Frama-C have no first-class `noninterference` keyword; SPARK's Ravenscar restricts concurrency but does not produce cross-task stability checks in this form; Frama-C's Aorai is a research plugin, not a keyword. **This is Oxide's single strongest differentiator.**

2. **Language-native contracts (not bolt-on).** `requires`/`ensures`/`invariant`/`assert`/`forall`/`exists`/`implies` are lexer keywords (`Lexer.cpp`), parsed by the same parser as runtime code and type-checked in `Sema.cpp`. SPARK's `Pre`/`Post` are Ada pragmas (close, but bolt-on-style); Frama-C's ACSL is a separate annotation grammar in comments; Creusot is comment-style too. Oxide contracts arguably sit **closer to the metal than even SPARK**.

3. **`asm!` spec-fn hardware trust boundary.** Each `asm!` mints an uninterpreted `asm_<fn>_<seq>` and binds it to a user `spec fn asm_<fn>` whose body is asserted as a top-level universal axiom (`Smt.h:209-214`, `docs/hardware-conformance.md`). This is exactly the seL4/Dafny trust model made into language syntax. SPARK and Frama-C can express similar trust boundaries but with more ceremony and no native keyword.

4. **`calc` equational blocks** (`Driver.cpp:5381`), Dafny-style step-by-step relational discharge directly in source. SPARK/Frama-C achieve this via tactic-side interactions, not source syntax.

5. **Proof certificate + replay** (`proof_log`, `oxide-check`) with **solver-disagreement detection** (`"BUG"` status). SPARK's GNATprove has proof sessions but the explicit "two solvers disagree ⇒ probable encoder bug" guard is a nice, honest touch.

---

## 4. What competitors can verify that Oxide CANNOT

1. **Mature lemma libraries.** Why3 ships large real/integer/floating libraries of pre-proven facts that discharge goals without re-proving. SPARK inherits these via Why3. Frama-C has the libc ACSL library + Coq stdlib via Wp-Coq. **Oxide has `lemma fn` machinery (axiom-encode + self-discharge, `AST.h:1215`) but no library**, every numeric fact must be re-derived.

2. **Interactive proofs and proof tactics.** SPARK → Coq; Frama-C → Wp-Coq / SSReflect for goals SMT can't close. Oxide is **batch SMT only**; when Z3 returns `unknown`, there is no interactive fallback. `proof that ... by induction` exists (`AST.h:1027`) but it is source-level, not an interactive tactic language.

3. **Proof automation quality.** GNATprove ships mature transformation libraries (case, induction, encoding smt, remove-term) far beyond Oxide's 5-strategy splitter. Why3's `transformations` are battle-tested; Oxide's `ProofSplitter` is a re-implementation.

4. **EVA-like abstract interpretation without contracts.** Frama-C's **EVA** finds runtime errors by value analysis **with zero annotations**. Oxide requires contracts for everything, there is no annotation-free value-analysis path.

5. **Verifying existing code (Frama-C uniquely).** Oxide verifies only Oxide. Frama-C verifies arbitrary C, including kernel modules and libc. This is structurally unavailable to Oxide.

6. **Industrial scale + certification.** SPARK has 20+ years, DO-178C cert, seL4. Frama-C has Airbus/Siemens use. Oxide is a solo project, months old, with a ~940-line hypervisor kernel as its largest verified artifact (`hv/`). It has **no third-party audit and no certification story**.

7. **Floating-point theory.** Why3 has mature float libraries; SPARK has limited float support with proof. Oxide encodes `f32`/`f64` as `Real` (`Driver.cpp:1490`), sound for reasoning about numeric ordering but not faithful to IEEE-754 float semantics (rounding, NaN, infinities).

---

## 5. Math syntax (`matrix`/`solve`/`integrate`) × SMT, the killer question

### What Oxide actually has
The lux new math syntax (`AST.h:678-711`):
- `MatrixLit` `[[a,b],[c,d]]` → encoded as **a real 2D SMT array** `(Array Int (Array Int <elemSort>))` built with nested `(store ...)` per cell (`Driver.cpp:2967-2995`). **Literal cells ARE concrete terms** the solver can read.
- `MatMulExpr` `A * B` → **opaque uninterpreted function** `matmul_<fn>_<seq>` returning a 2D array sym (`smtEmitMatmul`, `Driver.cpp:2113`).
- `SolveExpr` `A \ b` → **opaque uninterpreted** `solve_<fn>_<seq>` returning a vector (`smtEmitSolve`, `Driver.cpp:2100`).
- `IntegrateExpr` → **opaque uninterpreted** `integrate_<fn>_<seq>` (`smtEmitIntegrate`, `Driver.cpp:2139`).

### The honest verdict on the SMT integration
**The SMT solver cannot reason about the real algebra of matmul/solve/integrate.** Per the code comments (`Driver.cpp:2113-2146`):
> *"matmul: opaque uninterpreted `matmul_<fn>_<seq>` ... Sound: `ensures matmul(A,B) == matmul(A,B)` discharges by syntactic determinism of the same fresh symbol per call; further invariants must be stated as `requires/ensures` axioms."*

So:
- ✅ **`ensures matmul(A,B) == matmul(A,B)`** discharges trivially (same fresh symbol).
- ✅ If you **assert the algebra as axioms** (e.g. `(assert (forall A B C (= (matmul ...) (matmul ...)))`), the solver takes them on trust and can combine them.
- ❌ The solver **cannot prove** `matmul(A, matmul(B,C)) == matmul(matmul(A,B), C)` on its own, associativity is not derivable from an uninterpreted symbol.
- ❌ It **cannot prove** that `solve(A, b)` actually satisfies `A * solve(A,b) == b` (the defining property of linear solve) unless you axiomatise that fact yourself.
- ❌ It **cannot verify** numerical integration correctness, `integrate` is uninterpreted.

The encoder is **honest about this** (`Driver.cpp:2199-2208`): a contract that depends on real algebra floats honestly reports `sat` ("can't prove") rather than fabricating a counter-model. But that means **linear-algebra verification in Oxide today is "state it as axioms and the solver will use it", not "the solver will *derive* it."**

### Can SPARK or Rust verify linear algebra operations?
- **SPARK/Why3**: has a real-number library (Coq-compatible via Wp-Coq) and, more importantly, a **lemma library** for reals. But SPARK also models most numeric functions as uninterpreted + axiomatised, very like Oxide, so a *full* linear-algebra proof (e.g. proven Gauss elimination correct) would still require the user to supply most algebra as lemmas. SPARK is **marginally ahead** because it can lean on Coq and Why3's real library for the building blocks, and because total deterministic floating-point reasoning is a known-hard area even for them.
- **Rust** (Creusot/Kani): **cannot verify linear-algebra semantics**. Creusot's standard library logic functions give you nothing for `ndarray`/`nalgebra`. Kani can check panics/UB in a specific matrix implementation by concretising indices, but it cannot prove `A*(B*C) = (A*B)*C` symbolically. Rust is **the weakest** of the four for algebraic proof.
- **Frama-C**: ACSL can express `\let`/`\lambda` and the Wp plugin can express array updates, but there is **no built-in linear-algebra theory** either, same uninterpreted-function wall as Oxide.

### Bottom line on math syntax
**Oxide's `matrix`/`solve`/`integrate` is a runtime convenience plus a syntactic contract hook, NOT a built-in verified algebra theory.** The matrix *literal* is genuinely modelled (concrete 2D array); the *operations* are uninterpreted placeholders you axiomatise yourself. This puts Oxide on roughly equal footing with SPARK/Frama-C for *symbolic* algebra (everyone hits the uninterpreted-function wall), and **ahead of Rust/Kani** (no algebra story at all). But it's **behind** where a real-number lemma library + Coq back-end would help, and Oxide has neither. **No competitor can "verify linear algebra" out of the box either**; the honest statement is that Oxide has nicer **syntax** for stating the contracts but **no more actual proof power** for the algebra than anyone else.

---

## 6. Comparison of overall capability (honest ranking for *formal verification capability*)

| Rank | Tool | Why |
|---|---|---|
| #1 | **SPARK (Ada)** | 20+ years, seL4, DO-178C, mature lemma libraries, Why3 + Coq back-end, Ravenscar. The reference. |
| #2 | **Frama-C** | Verifies existing C; EVA abstract interp **without** contracts; rich plugin ecosystem; Wp-Coq for interactive. Industrial. |
| #3 | **Oxide** | First-class OG non-interference + language-native contracts + asm! spec-fn trust boundary are genuinely ahead of SPARK/Frama-C. BUT: immature, solo, no lemma library, no interactive path (Coq), no EVA-like inferencer, math syntax is uninterpreted. |
| #4 | **Rust** (Prusti/Creusot/Kani) | Notable tools but each covers a fraction. No first-class concurrency correctness, no linear algebra, borrows-theory verification is still research. Kani is good for UB/panic checks (Frama-C EVA-adjacent) but not full proof. |
| #5 | **C++ (standalone)** | `[[assume]]`/`[[assert]]` are UB-hints, not verification. Needs external tools (Frama-C can't even reach modern C++; you'd need VST/Coq or CBMC). |

---

## 7. Concrete recommendations, 3 features to surpass SPARK

To pass SPARK (not just be novel), Oxide needs to close the **maturity + library + interactivity** gap while keeping its OG-first-class advantage. Three highest-leverage additions:

### 🥇 Recommendation 1: A reusable verified lemma **library** + an **axiom audit index**
SPARK's biggest moat is Why3's standard lemma library. Oxide has the plumbing (`lemma fn`, axiom-encode + self-discharge, `AST.h:1215`) but no content.
- Build `std/verified/` out (currently only `buffer.ox`, `page_table.ox`, `ring_buffer.ox`) into a real catalogue:
  - **Integer arithmetic lemmas** (monotonicity, division/mod properties, inequalities, the bedrock that makes Z3 close the typical `ensures` without user hand-feeding). These let `decreases` and `invariant` discharge without manual `instantiate`.
  - **Array/sequence lemmas** (extensionality, `select`/`store` identities, range-fill facts). The SMT path already uses `(Array Int …)` heavily, give it the companion facts.
  - **OG cross-handler stability lemmas** specific to EPT/VMCS, Oxide's niche, where it can beat SPARK on relevance.
- Pair it with the **`--audit-axioms` / `--audit-trust`** machinery that already exists (`Driver.cpp:7223`, `hardware-conformance.md`) to produce a single trust-boundary index. SPARK has nothing like this integrated with the language, Oxide's `--audit-axioms` already surfaces every trusted fact and would make the library self-documenting. Lean into it: every lemma in the library ships with an audit entry, so users can see exactly what is machine-proven vs assumed.

### 🥈 Recommendation 2: An **escape hatch to Coq** (or an internal proof script language) for `unknown` goals
Today, when Z3/CVC5/Why3/Alt-Ergo all return `unknown`, the Oxide user is stuck, there is no interactive path. SPARK users punt the goal to Coq via Why3 and close it by hand.
- Add a `proof by coq "...% Local Coq vernacular %" { }` (or `proof by tactic`) escape hatch that emits the goal as a Coq-friendly obligation and accepts a Coq proof script as a string / sidecar file. Reuse the existing 4-solver proof_log record so the Coq result is logged alongside.
- This single feature would close the gap on SPARK/Why3's interactive back-end and turn Oxide's batch-SMT weakness into "SMT first, Coq when needed."
- Lower-effort variant if Coq integration is too heavy: a **Dafny/Lean-style proof script language** embedded in `proof { }` blocks (assert-by-assert hints with rewriting, `have`, `smt`-timeout bumps). The `calc` and `instantiate` infrastructure already exists, extend it.

### 🥉 Recommendation 3: A **math theory layer**, turn `matmul`/`solve`/`integrate` from uninterpreted symbols into an SMT-decidable theory (or at least a curated axiom set)
Oxide's `matrix`/`solve`/`integrate` syntax currently can only discharge `matmul(A,B) == matmul(A,B)`-style trivial facts. To genuinely verify linear-algebra properties, the thing no competitor can do well either, Oxide should become the one tool that *can*:
- **Option A (curated axioms):** ship a standard library that axiomatises `matmul`, `solve`, `integrate` with the ring/matrix axioms (associativity, distributivity, identity, `A*solve(A,b)=b` for invertible `A`, integral linearity). Then `ensures A*(B*C) == (A*B)*C` discharges from those axioms. This is cheap (Oxide already supports user axioms) and immediately beats SPARK/Frama-C/Rust, none of whom ship such a layer.
- **Option B (a custom Z3 theory), harder but the genuine win:** register a custom theory plugin (Z3 supports user-theories via `Z3_mk_func_decl` + `Z3_theory_assert_axioms`) that gives `matmul` real algebraic congruence so the solver propagates associativity/distributivity automatically. This is the kind of thing that would let you say "verified linear-algebra operations" and actually mean it.
- Either way, the math syntax should stop being **uninterpreted by default** and become **axiom-interpreted with an audit trail**, exactly the `--audit-axioms` story you'd build in Recommendation 1. Tightly couple these.

### Honourable mention (recommended, but not top-3)
- **Goal-level proof reuse / incremental caching** like Why3 sessions (partly there via `oxide-check` replay, extend to per-goal skip-on-unchanged).
- **Total-correctness `decreases` real enforcement**: the recursive-call arm currently *assumes* the callee's `ensures` (sound, standard) but the explicit `decreases` measure discharge should be a hard goal when present (`AST.h:1199` carries the field; wire the SMT discharge).
- **A teaching suite**: SPARK has John Barnes's books; Oxide's pedagogy is one README + docs/. A walkthrough of the OG proof on the hypervisor would convert the novelty into adoption.

---

## Summary table (the one-line answer)

| Axis | Oxide | SPARK | Frama-C | Rust | C++ |
|---|---|---|---|---|---|
| Contracts native | ★★★★★ | ★★★★ | ★★ (ACSL) | ★★ (Creusot) | ★ |
| SMT back-end breadth | ★★★★★ (Z3/CVC5/Why3/Alt-Ergo parallel) | ★★★★★ (via Why3) | ★★★★ (WP) | ★★★ | ☆ |
| First-class concurrency (OG) | ★★★★★ (unique) | ★★★☆ (Ravenscar) | ★☆ (Aorai) | ★☆ | ☆ |
| Lemma libraries | ★ (plumbing, no content) | ★★★★★ | ★★★ (Coq) | ★ (none for algebra) | ☆ |
| Interactive proof (Coq) | ☆ | ★★★★★ | ★★★★ | ☆ | ☆ |
| Verify existing code | ☆ | ☆ | ★★★★★ | ☆ | ☆ |
| Annotation-free inferencing (EVA) | ☆ | ☆ | ★★★★★ | ★ (Kani) | ☆ |
| Hardware/asm trust model | ★★★★★ (asm!+spec fn) | ★★ | ★★ | ★ (unsafe) | ☆ |
| Linear algebra verification | ★★ (syntax + uninterpreted) | ★★ (same wall + Coq help) | ★★ | ★ | ☆ |
| Maturity / scale proof | ★ (hypervisor ~940 LOC) | ★★★★★ (seL4) | ★★★★ (Airbus/Siemens) | ★★ | ☆ |

**The one-liner:** Oxide is a novel, legitimately-first-class non-interference language with honest-by-design SMT emission, but to **surpass SPARK** it needs (1) a real verified lemma library indexed by `--audit-axioms`, (2) an interactive Coq/script escape hatch for `unknown` goals, and (3) to turn the `matrix`/`solve`/`integrate` uninterpreted symbols into a curated (or user-theory) algebra, because right now the math syntax can't actually prove algebra, it just *states* it.
