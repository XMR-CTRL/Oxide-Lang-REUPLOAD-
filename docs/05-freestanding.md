> **OXIDE** · Freestanding and the Hypervisor
> Bare metal mode, the working Type 1 hypervisor, and how the verification actually runs without hardware.

# Freestanding and the Hypervisor

Oxide has a freestanding mode for writing kernels hypervisors and firmware without a hosted runtime. This is where the language shows what it was designed for. The `hv/` directory is a real multiboot2 Type 1 hypervisor written in Oxide with 64-bit EPT and a VM exit dispatch loop. It compiles. It verifies by disassembly against the Intel SDM. It has not been booted on hardware.

## Freestanding mode

`--freestanding` changes the rules.

- No C runtime. `print` `ox_bounds_fail` and `ox_chan_*` do not exist.
- No `@oxide_main` wrapper. The entry symbol is whatever `--entry` names.
- Symbols are unmangled and stable so a linker script can find them.
- Only `asm!` `mmio_load` `mmio_store` `memset` `memcpy` and pointer arithmetic are guaranteed to work.

```
oxide exe --freestanding --entry kmain kernel.ox --entry kmain
```

### The four primitives

1. **`asm!(code, ...)`** emits an inline LLVM asm node.

```oxide
let flags: u64 = asm!("pushf\npopq $0", "=r" (flags));
```

2. **`mmio_load(addr)` and `mmio_store(addr, val)`** emit LLVM `load volatile` and `store volatile`. The address is a raw pointer. There is no bounds check and no retry.

3. **`memset(dst, val, n)` and `memcpy(dst, src, n)`** lower to the LLVM intrinsics.

4. **Pointer arithmetic** `p + n` moves by elements of `sizeof(*p)` not bytes. Byte level arithmetic requires `p as *u8`.

Everything else is just Oxide. Integer arithmetic comparisons control flow structs and enums all work normally.

### What freestanding gives you that hosted does not

Hosted Oxide gives you segments and paging already set up by the OS loader. Freestanding does not. You get a stack you get a load address and you write the rest. The hypervisor does page tables GDT TSS and then vmxon. That is the full starting state.

## The hypervisor

`hv/` builds a multiboot2 kernel. The entry is `_start` and lives in `hv/boot/stub.S`.

```
hv/
  boot/
    stub.S          32 bit pm -> long mode, GDT, TSS, page tables
    vmcs_syms.S     symbol address helpers for assembly to find Oxide globals
    linker.ld       multiboot2 load layout, load at 1 MiB
  src/
    vmx.ox          VMCS field encodings + ept pointer bits + ctl bit constants
    vmxops.ox       vmclear vmptrld vmwrite vmread vmlaunch vmresume vmxoff vmcall
    vmlaunch.ox     builds a 64 bit guest VMCS with EPT, launches, handles exits
    kernel.ox       serial COM1, VT-x cpuid probe, IA32_VMX_BASIC, vmxon
    freestanding_runtime.ox  minimal ox_bounds_fail and ox_contract_fail
  build.sh          concat sources, emit IR, clang assembler, ld.lld link
  build/            mb2check.py validates the multiboot2 header
  tools/            qemu_check.py static analysis against the Intel SDM
```

Build it.

```
bash hv/build.sh
```

It produces `hv/build_out/kernel.elf`.

### The guest

The guest code is a small payload that runs under the hypervisor. It does these in order.

1. `cpuid` which is an unconditional VM exit.
2. Three `vmcall`s which exercise the VMCALL handler.
3. `hlt` which yields to the hypervisor.

Each exit routes through the `VM exit handler` which reads the exit reason field and dispatches. `VMCALL` goes to a serial echo service. `CPUID` becomes the vendor string plus the hypervisor present bit. `RDMSR` reads pass through to the host MSR. `WRMSR` becomes a safe no-op. `HLT` triggers clean hypervisor shutdown.

The full table is in `hv/src/vmlaunch.ox`.

### EPT setup

`setup_ept` builds a four level EPT:

```
PML4 → PDPT → PD → 2 MiB pages
```

It identity maps the first 1 GiB of guest physical memory. It writes `VMCS_EPT_POINTER` with memory type WB and a 4 level page walk. It enables `CPU_BASED2_ENABLE_EPT`. If any of this is wrong the VM entry fails immediately and the failure is loud.

### How it is verified without hardware

This is the part people usually ask about. Without qemu and without physical hardware you cannot boot test the hypervisor. What you can do instead is verify each slice by construction.

- **Per-handler proofs.** Individual handlers in `hv/src/vmlaunch.ox` have contracts that get emitted as SMT and checked.
- **Non-interference by construction.** The VMCS and EPT structures are built with explicit field encodings checked against the Intel SDM appendix.
- **Disassembly diffing.** The build pipes the disassembly through `hv/tools/qemu_check.py` and compares it against the Intel SDM encoding tables.
- **Cross-module contracts.** `hv/src/vmx.ox` defines the VMCS field layout. The callers of those definitions verify against them.

The slices verify. The composition does not yet. The EPT verification is limited to the first few pages. These are filed as concrete gaps in the roadmap not as something we pretend does not exist.

## The verified standard library

Oxide has three modules in `std/verified/` that are proven correct against their specifications.

### buffer.ox

A bounds checked byte buffer. Invariant is `len <= cap`. Every operation carries that invariant. The proof discharges at compile time. A wrong index fails not at runtime but in the solver.

### page_table.ox

A page table walker used by the hypervisor. It walks the 4 level structure with the proof obligations mechanical. Each level's invariant is stated once in the invariant field of a `forall` and discharged.

### ring_buffer.ox

A lock free ring buffer with a producer and consumer index. The `noninterference` proof applies here. One thread writes one thread reads. The prover confirms neither can corrupt the other's view.

Each of these has a Z3 discharge count in its doc header. The pattern is the same across all three. State the invariant as a contract use it as a loop invariant and let the solver do the case split.

## What you cannot do today

- **Boot the hypervisor.** qemu is not integrated into the build pipeline and this environment has no qemu install. Disassembly verification is what we have.
- **Memory map reasoning.** The SMT encoding does not yet model full page table contents. Small regions work. A full page table switch does not.
- **Concurrent execution.** `spawn` runs inline. The channels are real. The threads are not.
- **EPT verification to 512 entries.** Limited to small page counts. Scaling requires the array model below.

These are filed as issues with line numbers. They are not footnotes.

## Running it yourself

Build the compiler.

```
clang++ -std=c++17 -O2 src/*.cpp -o oxide
```

Build the hypervisor.

```
bash hv/build.sh
```

Run the verification slice.

```
oxide verify --verify-only --solver-timeout 15 hv/src/vmlaunch.ox
```

Review the discharge counts. Build the verified stdlib.

```
oxide check std/verified/buffer.ox
oxide verify std/verified/buffer.ox
```

If everything checks out the proof counts match the ones published in this doc.
