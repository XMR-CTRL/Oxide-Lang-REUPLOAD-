> **OXIDE** · Hardware Conformance
> The trust boundary between Oxide and the machine it runs on.

# Hardware Conformance and the Trust Boundary

**For**: Oxide compiler SMT verification path (`src/Smt.h`, `src/Driver.cpp` asm! encoder, `src/Ghost.cpp` spec-fn / axiom emission)
**Scope**: What the `asm!` SMT axiomatisation actually proves, what it assumes, and why the spec-fn-as-hardware-model is sound *by design*, not a gap to be closed.
**Status**: Reference document. Pinned to the code in `src/Smt.h:143-179` (memory model axiom families), `src/Smt.h:209-246` (asm! encoder state), and `examples/hv_vmlaunch_contracts.ox` / `examples/d3_axiom_test.ox` / `examples/fix3_memory_model_test.ox`.

---

## 0. The one-sentence claim

> The spec fn `asm_read_cr3_phys` says `= PAGE_SIZE`, that's a **user assertion**, not derived from the hardware semantics. The trust boundary **is the spec fn**.

Everything below is the long form of that sentence, what it implies for soundness, what it deliberately does *not* prove, and how to audit the asserted specs against the Intel SDM.

---

## 1. What the trust boundary IS

In Oxide's verification, the **spec fn is the hardware model**. Concretely, for every `asm!(...)` block the compiler:

1. Mints a fresh per-block **uninterpreted function** `asm_<curFn>_<seq>` (`seq` is the block's index within the enclosing function, 0 for the first block). See `src/Smt.h:209-214`.
2. Looks for a **user-supplied `spec fn`** named `asm_<curFn>` (or `asm_<curFn>_<seq>`) whose parameters bind positionally to the asm *input* operands and whose return type matches the single *output* operand's type.
3. Asserts that spec fn's body as a **top-level universal axiom**

   ```
   (assert (forall ((in_0 S_0) ...) (= (asm_<curFn>_<seq> in_0 ...) <body>)))
   ```

   plus a ground instance pinned to the call-site input terms.
4. Hands Z3 the rest of the program and asks it to discharge each `requires` / `ensures` / `invariant` clause, with the asm axiom available as a premise.

The spec fn is therefore the **contract the architecture is claimed to honour**. The downstream `ensures result == PAGE_SIZE` on `read_cr3_phys` folds *because* the spec fn asserted `= PAGE_SIZE`, not because Z3 modelled `mov %cr3, %rax`. This is exactly the seL4 / Dafny trust model:

- **seL4** trusts its Haskell specification of the kernel; the proof shows the C implementation refines the Haskell model, *not* that the Haskell model matches the ARM hardware. The hardware model is an assumption.
- **Dafny** trusts `axiom` and uninterpreted-function declarations; each method is verified against them. The axioms are not themselves derived, they are stated.

Oxide plays the same role: the spec fn is the assumed architectural contract, and Z3 verifies the *software* against it.

## 2. What the trust boundary is NOT

It is **not** a derivation from hardware semantics. Three concrete consequences:

- **The `asm!` block's hardware semantics are never modelled in Z3.** The inline-asm string (`"mov %cr3, %rax"`, `"cpuid"`, …) is opaque to the SMT encoder; it is not parsed, not lifted to a memory model, and not run through an instruction semantics. The block's *only* presence in the SMT query is the uninterpreted function symbol plus the spec-fn axiom linking that symbol to a value.
- **`asm_read_cr3_phys(...) = PAGE_SIZE` is an assertion, not a theorem.** Z3 does not (and cannot) prove that `mov %cr3, %rax` yields a page-aligned value. If the spec fn lied, asserted `= 0`, Z3 would happily discharge `ensures result == 0`, and the negative control in `hv_vmlaunch_contracts.ox` (`read_cr3_phys_wrong`, expected `sat`) exists precisely to prove the encoder is *not* silently substituting a different contract of its own.
- **The spec fn is not validated against a golden hardware model.** There is no second SMT encoding of the x86 semantics against which the spec fn is checked for consistency. The spec fn is the golden model.

In short: the compiler proves *software-against-spec*; it does not (and structurally cannot, in this design) prove *spec-against-hardware*.

## 3. How the axioms chain

The full proof chain for a function containing one `asm!` block:

```
   ┌─────────────────────────────────────────────────────────────────────┐
   │  asm!("mov %cr3, %rax", in("{rdx}") 0, out("{rax}") v)              │  (source)
   │      │  llvm-ir : an opaque inline-asm node; NOT modelled.         │
   │      ▼                                                              │
   │  (declare-fun asm_read_cr3_phys_0 (Int) Int)                         │  (Smt.h:209-214)
   │      │  a fresh uninterpreted function.                              │
   │      ▼                                                              │
   │  spec fn asm_read_cr3_phys(_ignored: i64) -> i64 = PAGE_SIZE;       │  (user)
   │      │  the architectural contract; the assertion.                 │
   │      ▼                                                              │
   │  (assert (forall ((r Int)) (= (asm_read_cr3_phys_0 r) PAGE_SIZE)))   │  (Smt.h:215-219)
   │  + (assert (= (asm_read_cr3_phys_0 0) PAGE_SIZE))                    │  ground instance
   │      │  links the uninterpreted symbol to the asserted value.       │
   │      ▼                                                              │
   │  ensures result == PAGE_SIZE  on read_cr3_phys                       │
   │      │  Z3 folds:  result := asm_read_cr3_phys_0(0) := PAGE_SIZE    │
   │      ▼                                                              │
   │  (assert (not (= result PAGE_SIZE)))  ; (check-sat)                 │
   │  ──► unsat  ⇒  clause holds for all inputs (discharged)              │
   └─────────────────────────────────────────────────────────────────────┘
```

The chain is **sound** in the SMT sense: Z3's `unsat` means *there is no model of the axioms in which the negated goal holds*. So every value Z3 can assign to the uninterpreted `asm_read_cr3_phys_0` is forced to equal `PAGE_SIZE` by the spec-fn axiom, and the `ensures` folds only because of that forcing. The negative control (`read_cr3_phys_wrong`, with a lying `ensures result == 0`) reports `sat`, proving the discharge is real and not a placeholder pass-through.

Two encoder rules that gate this chain (confirmed against `examples/c5_asm_test.ox` and `hv_vmlaunch_contracts.ox`):

- The spec fn name **must** be `asm_` + the *exact* function name. A mismatch leaves the uninterpreted symbol unconnected and the discharge silently goes `sat`.
- The axiom binds only for a **single-output** block with at least one input. Multi-output blocks (e.g. the real `cpuid` with eax/ebx/ecx/edx) are left uninterpreted unless remodelled as a single-output block; see `asm_emulate_cpuid_leaf` in `hv_vmlaunch_contracts.ox`.

## 4. The five memory-model axiom families

Beyond per-asm-block contracts, Oxide carries a **compiler-managed baseline** of memory-model axioms so that EPT/TLB-coherence reasoning does not have to be re-asserted in every program. The families are enumerated in `src/Smt.h:151-171`. They are keyed on **uninterpreted** SMT symbols, `cpu`/`addr`/`event`/`tick` ids modelled as `Int`, predicates as `Bool`, that are *distinct* from any spec-fn symbol the source program declares, so the auto baseline and the manual `axiom` surface coexist without a symbol clash.

| # | Family | Axiom shape (sketch) | Source pointer |
|---|--------|----------------------|----------------|
| 1 | **INVEPT flush** | `invept(cpu) ⇒ ∀addr. ¬stale_tlb(cpu, addr)`, after an INVEPT, no TLB entry is stale. | `Smt.h:156`, `fix3_memory_model_test.ox:21` (`invept_flush`) |
| 2 | **happens_before** | irreflexive **and** transitive partial order over events, the program-order / commit skeleton. | `Smt.h:157`, `fix3_memory_model_test.ox:28` (`hb_transitive`), `:35` (`hb_irreflexive`) |
| 3 | **TSO store buffer** | per-cpu store-buffer + `commit_order(i,j) ⇒ i < j`, stores enter the buffer in program order and commit in insertion order (x86 TSO: no store-store reordering). Liveness ("eventually commits") is left to the uninterpreted `committed` predicate, *no liveness axiom*, to keep the safety slice decidable. | `Smt.h:158-164` |
| 4 | **Cache coherence** | `invalidate(addr, cpu)`: when a write to `addr` by any cpu commits, every *other* cpu's cached copy is invalidated, `¬cached(cpu, addr)`. | `Smt.h:165-167` |
| 5 | **stale_tlb post** | folded into (1): if `invept(cpu)` holds, the TLB is not stale for any addr. Provided as its own axiom for programs that reference `stale_tlb` symbolically. | `Smt.h:168-171` |

> **Implementation status note.** The `memModelAxioms` vector is declared in `Smt.h:179` and the comment at `Smt.h:143-149` describes the intended *auto-emitted* baseline (populated once in `emitSmt`, written verbatim after `(set-logic ALL)`). In the current tree those families are surfaced through the **manual `axiom NAME: …`** path (D3 feature, see `examples/d3_axiom_test.ox` and `examples/fix3_memory_model_test.ox`), the user-authored axiom is emitted as `(assert (forall …))` at the top of the SMT ghost section. Whether the compiler-managed auto-baseline or the manual path supplies them, the **trust posture is identical**: these axioms are *asserted architectural facts*, not derived ones, and belong to the same trust boundary as the per-asm spec fns.

This is additive to the per-asm spec-fn axioms: a program that declares its own axioms continues to lower them through the spec-fn + bounded-forall path, and an empty `memModelAxioms` vector (the default for any freshly-constructed `SmtCtx` that is not the one populated in `emitSmt`) is a no-op, programs that don't touch the memory-model symbols simply carry the harmless unused `declare-fun`s.

## 5. Why this is sound

The proof obligation Z3 discharges for each clause is:

```
axioms ∧ preconditions   ⊨   (negated_goal is false)
```

Z3 returning `unsat` means: **there is no assignment to the free symbols, including the uninterpreted `asm_*` function and the memory-model predicates, under which the axioms hold and the negated goal holds.** Two consequences:

- You **cannot prove a false thing via the spec fn unless the spec fn is false.** The spec fn is an *assumption*. If `asm_read_cr3_phys` asserted a value the hardware never produces, Z3 would prove `ensures` clauses that are *false on the real hardware*. The soundness of the whole pipeline therefore reduces to the soundness of the asserted specs, exactly the reduction seL4 and Dafny make.
- The negative control bears witness to the encoder's honesty. `read_cr3_phys_wrong` carries the *same* asm body as the positive `read_cr3_phys` but a *lying* `ensures result == 0`. Under the spec axiom the asm result is `PAGE_SIZE`, so the negated goal `result != 0` is satisfiable and the discharge reports `sat`. If the encoder ever silently treated the asm result as unconstrained, *both* cases would discharge and the soundness bug would be invisible. The single `sat` row is the negative control doing its one job.

So the verification is sound **relative to the spec fns and memory-model axioms**, which is the strongest claim any deductive verification of software makes. There is no deduction in this design that proves a property of the hardware; there is only deduction of software properties given the hardware contract.

## 6. What is NOT verified

Be explicit about the assumption boundary so auditors know where their review has to land:

1. **The spec fn itself.** `asm_read_cr3_phys(...) = PAGE_SIZE` is *assumed*. Z3 never asks whether `mov %cr3, %rax` actually yields a page-aligned value. A spec fn that mis-states the architecture will produce a *sound-but-wrong* verification: every discharge is an honest `unsat`, against a contract that does not match the silicon.
2. **The `asm!` block's machine encoding.** The inline-asm string is opaque to the SMT encoder; whether the emitted LLVM inline-asm node is textually and semantically what the spec fn claims is **not** in the Z3 proof.
3. **The memory-model axioms.** Families 1, 5 (§4) are asserted architectural facts. INVEPT's flush semantics, x86 TSO, and cache-coherence invalidation are not derived from a microarchitectural model, they are stipulated.
4. **The multi-output asm boundary.** Blocks with more than one output are left uninterpreted rather than axiomatised (the encoder requires a single output for the forall-axiom to bind). `cpuid` is remodelled as single-output in the example; the real 4-output `cpuid` is **not** covered by a spec-fn axiom unless similarly remodelled.
5. **Spec-fn name alignment.** A spec fn whose name is not `asm_<exactFnName>` (or `asm_<exactFnName>_<seq>`) leaves the uninterpreted symbol unconnected; the discharge silently reports `sat`. Name conformance is checked by manual review and the negative controls, not by the encoder.

## 7. How to audit the spec fns

Because the spec fns are the trust boundary, auditing them is the load-bearing review step. The audit maps each `spec fn asm_*` (and each user-authored memory-model `axiom`) to the Intel® 64 and IA-32 Architectures Software Developer's Manual (SDM) section that states the architectural contract, then checks the emitted `asm!` machine code against that same section.

Recommended procedure:

1. **Enumerate every `spec fn asm_*`.** `grep -rn "spec fn asm_" examples/ hv/ src/`, each is one trust-boundary entry.
2. **For each spec fn, record six fields:**
   - `fn`, the Oxide function the spec fn axiomatises.
   - `asm!` string, the inline-asm text in that function's body.
   - `spec body`, the asserted contract (e.g. `= PAGE_SIZE`, `= 1`).
   - `SDM ref`, the volume/chapter that states this contract for this instruction/operand. Examples: `mov %cr3` ⇒ SDM Vol. 3, §2.5 (Control Registers, CR3) + §4.5 (page-aligned physical base); `cpuid` leaf 1 ⇒ SDM Vol. 2, `CPUID` instruction, Table 2-9 (`EAX[31:0] = <family/model/stepping>`, leaf 1 returns the feature flag set).
   - `disasm ref`, the emitted machine bytes for the `asm!` block (from `hv/build.sh` / objdump of the linked kernel) and confirmation they correspond to the asm string.
   - `negative control?`, whether a lying `ensures` against the same body exists and reports `sat` (the encoder-honesty witness for this contract).
3. **Spot-check the emitted SMT-LIB** for any spec fn in doubt:

   ```
   ./build/oxide.exe verify --emit-smt build/<file>.smt2 --verify-only examples/<file>.ox
   ```
   Confirm the `(declare-fun asm_<fn>_<seq> …)` and the matching `(assert (forall …))` appear, and that the ground instance is pinned to the call-site inputs (`Smt.h:215-219`). A missing forall-assert is the silent-`sat` failure mode of §6.5.
4. **Memory-model axioms get the same treatment.** For each user-authored `axiom NAME:` (D3 surface; `fix3_memory_model_test.ox`, `d3_axiom_test.ox`) and for each of the five compiler-managed families, record the SDM section: INVEPT ⇒ SDM Vol. 3, §28.3 (Invalidating Translations from EPT); TSO ⇒ SDM Vol. 3, §8.2.2 (Memory Ordering in P6 and More Recent Processors); cache coherence ⇒ SDM Vol. 3, §11.4 (Cache and TLB Coherency); TLB staleness ⇒ SDM Vol. 3, §4.10 (Caching the TLB).
5. **Re-run on every hardware-contract change.** Because the specs are assumptions, a silicon erratum or SDM clarification that changes the architectural contract must be reflected in the spec fn and re-audited. There is no compiler step that will flag a stale spec.

The audit table is the artefact that closes the gap the encoder leaves open: the spec fns are assumed inside Z3, and their conformance to the SDM is established *outside* Z3, by human review plus disassembly.

---

## 8. Known limitations and gap status

Six limitations were identified during the verification push. Five are genuine gaps being **addressed**; the sixth, hardware conformance, is the **by-design** posture this document formalises. It is not on the "fix" track; it is documented, audited, and accepted.

| # | Gap | Status | Note |
|---|-----|--------|------|
| 1 | **Pointer aliasing** | **Sound but incomplete (same-term only)** | The WP encoder folds memory via the array-`select`/`store` axiom `(select (store M a v) a) == v`, which fires only when the load's address term is *structurally identical* to the store's address term. Two syntactically distinct but aliasing addresses are not recognised as equal, so a roundtrip that should fold may discharge to `sat` instead. Sound, no false `unsat` is produced, but incomplete: some true facts about aliasing are not provable. Extending to a congruence-closing alias model is on the roadmap. |
| 2 | **VM-exit dispatch** | **Gap being addressed** | The VM-exit handler dispatches on the exit reason read via `vmread`. The vmread value is not currently axiomatised by a `spec fn asm_vmread`, so the dispatch table's branch conditions are over an unconstrained symbol and the per-reason handler contracts cannot be tied to the architectural exit-reason encoding. Adding a `spec fn asm_vmread` mirroring SDM Vol. 3, Appendix B (VMCS field encodings, exit reason = field 0x4402) closes this. |
| 3 | **512-entry EPT loop** | **Gap being addressed** | `setup_ept` writes 512 PTEs in a loop; verifying the loop boundary requires a `forall k.` quantifier over the index range `0..512` plus the array-`store`-monotonicity reasoning to show all entries satisfy the EPT-entry contract. The bounded-forall surface exists (`forall k: T in lo..hi implies body`) but the unbounded-array fold across the loop is not yet instantiated; the quantifier + array reasoning is being wired. |
| 4 | **Cross-function MMIO** | **WORKS (Feature 7 / `mmio_load`-`mmio_store` array)** | The compiler models `mmio_mem` as a global `(Array Int Int)` symbol; `mmio_store(p,v)` → `(store mmio_mem p v)`, `mmio_load(p)` → `(select mmio_mem p)`. Store-then-load roundtrips fold via `(select (store M a v) a) == v` *within one function body*; distinct-address non-aliasing holds under `requires a != b`; last-write-wins holds. This is verified, not a gap; see `examples/hv_vmlaunch_contracts.ox` (`ept_roundtrip`, `gpr_roundtrip`) and the c5b array-modelling feature. |
| 5 | **Boot path contracts** | **Gap being addressed** | The early boot sequence (`hv/boot/stub.S`, `hv/src/kernel.ox`, protected-to-long mode, GDT/TSS setup, page-table construction, `CR4.VMXE`, `IA32_VMX_BASIC` probe, `vmxon`) carries no `requires`/`ensures` contracts, so the boot path is outside the SMT verification surface. Adding contracts for the mode transition, paging setup, and VMX-enable steps (each pinned to an SDM section) is in progress. |
| 6 | **Hardware conformance** | **BY DESIGN (this document)** | The spec fns and memory-model axioms are *assertions*, not derivations from hardware semantics. The `asm!` block's machine encoding is not modelled in Z3; the user-supplied `spec fn asm_*` asserts the architectural contract; Z3 verifies the rest of the program against that spec; the spec fn *is* the trust boundary (seL4 Haskell model / Dafny axiom posture). Conformance of the spec fns to the Intel SDM is established by the **audit procedure** in §7 (spec → SDM section mapping + disassembly check), not by the compiler. This is the accepted trust model, not a defect to be closed. |

### The trust model, restated

1. The `asm!` block's hardware semantics are **not modelled in Z3**.
2. The **user supplies** a `spec fn asm_<fn>` that **asserts** the architectural contract.
3. Z3 verifies the **rest of the program** against that spec.
4. The spec fn **is** the trust boundary, like seL4's Haskell model or Dafny's axioms.
5. **Disassembly** verifies the `asm!` block matches the spec fn's claim, by manual review against the SDM (§7).

The verification is sound **relative to the spec fns and memory-model axioms**. That is the strongest claim deductive software verification makes, and it is the claim Oxide makes explicitly.
