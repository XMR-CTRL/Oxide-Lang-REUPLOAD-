# Oxide, the manual

The full documentation set for the Oxide programming language and its compiler. Reads
best in the numbered order 01 through 06, but every file is written to stand alone. Pick
the one that matches what you want to do right now.

## What Oxide is

Oxide is a small compiled systems language. Looks like a trimmed down C, compiles through
textual LLVM IR with clang running as a subprocess, and does two things most languages
do not:

1. **Contracts that actually verify.** You write `requires` and `ensures`, the compiler
   emits SMT-LIB, an external solver discharges it at compile time. Or the same contract
   becomes a runtime trap. Or both.
2. **Freestanding mode for real bare metal.** Inline asm, volatile MMIO, no runtime, and
   a working multiboot2 Type 1 hypervisor kernel written in the language under `hv/`.

It is experimental, single-author, Windows-first, unreleased and unlicensed. Go in with
eyes open.

## Reading paths

- **I just landed here, show me the thing** -> `01-getting-started.md`
- **I want every type, statement, and operator** -> `02-language.md`
- **How do contracts and SMT actually work** -> `03-verification.md`
- **I want to hack on this compiler** -> `04-internals.md`
- **Bare metal, hypervisors, and the verified stdlib** -> `05-freestanding.md`
- **Everything at a glance** -> `feature` (in the repo root)

## Where things live

- `01-getting-started.md`   Build, first program, commands, what works, what does not.
- `02-language.md`          Every feature with a runnable example and the reasoning behind it.
- `03-verification.md`      requires / ensures / invariant / assert, the SMT encoding, what the
                            solver can see and what it cannot, quality gate.
- `04-internals.md`         Compiler architecture, ownership, diagnostics, why clang runs as a
                            subprocess.
- `05-freestanding.md`      Freestanding mode, MMIO, asm, the hv/ hypervisor, verified stdlib,
                            caveats.
- `06-competitive-analysis.md`  Oxide next to SPARK, Rust, Frama-C, C++.
- `07-owicki-gries-smt.md`  How the non-interference check for concurrent code is encoded.
- `08-hardware-conformance.md`  Trials and limits for hypervisor targets.
- `feature-reference-draft.md`  The one-page feature dump, moved here from the repo root.

## Honesty rules

- Numbers in these docs were re-run on the hardening build at time of writing. If you run
  them again and the numbers differ, the docs are stale. file an issue or just fix them.
- Nothing in this set says "Oxide is great". It says what Oxide does and where it falls
  short.
- No m dashes anywhere. Simple typographic rule, strictly enforced.
