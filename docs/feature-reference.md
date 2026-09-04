# Oxide Language — Complete Feature Reference

A comprehensive catalog of every feature in the Oxide programming language and its compiler.
Organized by subsystem.

---

## 1. Type System

### Primitive Types
| Type | Description |
|------|-------------|
| `i8`, `i16`, `i32`, `i64` | Signed integers (8/16/32/64-bit) |
| `u8`, `u16`, `u32`, `u64` | Unsigned integers (8/16/32/64-bit) |
| `usize` | Pointer-sized unsigned integer |
| `f32` | 32-bit floating point |
| `f64` | 64-bit floating point |
| `bool` | Boolean (`true`/`false`) |
| `void` | Unit/void type |
| `str` | immutable UTF-8 string (heap-allocated, C-ABI compatible) |
| `char` | Single character |

### Composite Types
| Type | Description |
|------|-------------|
| Arrays `[T; N]` | Fixed-size stack arrays |
| `DynArray<T>` / `vec` | Heap-allocated growable vector (dynamic array) |
| `Map<K,V>` | Ordered key-value map (red-black tree backed) |
| `Set<T>` | Ordered set |
| `HMap<K,V>` | Hash map |
| `HSet<T>` | Hash set |
| Structs | Named record types with fields, methods, inheritance |
| Pointers `*T` | Raw pointers (nullable) |
| `&T` / `&mut T` | Borrow references (compile-time checked) |
| Function pointers `fn(A,B) -> C` | First-class function pointers |
| Enums | Tagged unions with named variants |
| Generics `T` | Parametric polymorphism with concept constraints |

### Type Features
- **Type inference** — local `let` infers type from initializer
- **Default type args** — `struct S<T = i32>` allows partial specification
- **`where` clauses** — additional constraints on generic parameters
- **Type casting** — explicit `as` casts between numeric types
- **Void-pointer decay** — `&u8` and `*void` interconvert at FFI boundary

---

## 2. Object-Oriented Programming

### Structs & Methods
- **Field declarations** — `struct Foo { x: i32; y: str; }`
- **`impl` blocks** — associate methods with a struct
- **`&self` methods** — shared/immutable receiver
- **`&mut self` methods** — exclusive/mutable receiver
- **Associated functions** — methods without `self` (like Rust's `Self::foo()`)
- **Constructor convention** — `fn new(...) -> Self` is idiomatic

### Inheritance
- **Single inheritance** — `struct Derived : Base`
- **Virtual dispatch** — `virtual fn` declares an overridable method
- **`override fn`** — explicitly overrides a virtual parent method
- **Vtable generation** — compiler emits LLVM vtable structs + stores vptr in objects
- **Method resolution** — walks inheritance chain, uses vtable for virtual calls

### Concepts (Compile-Time Interfaces)
- **`concept C<T> { fn ... }`** — declares required method signatures
- **Generic constraint** — `fn f<T: Concept>()` requires T to satisfy concept
- **Compile-time predicates** — concepts checked at instantiation, no runtime polymorphism
- **Constrained overloads** — different concepts can produce different specializations
- **Default type arguments** — `struct S<T = i32>` with concept-constrained defaults

---

## 3. Memory Management

### RAII
- **Destructor** — `fn drop(&mut self)` called automatically at scope exit
- **Clone** — `fn clone(&self) -> Self` for explicit deep copies
- **Move semantics** — values moved on assignment/pass, preventing aliasing
- **Use-after-move** — compile error: moved value cannot be used
- **Scope-exit drops** — compiler inserts drop calls at every exit path (return, break, end of scope)

### Defer
- **`defer { ... }`** — Go/Zig-style scope-exit blocks
- **LIFO ordering** — multiple defers execute in reverse order
- **Runs on all paths** — including panic/early return

### Ownership
- **Linear types** — every value has exactly one owner
- **Borrow checking** — `&T` (shared, multiple readers) vs `&mut T` (exclusive, one writer)
- **Move-into-function** — passing by value transfers ownership
- **Return-value lifetime** — returned values live in caller's scope

---

## 4. Control Flow

| Construct | Syntax |
|-----------|--------|
| If/else | `if cond { } else { }` |
| If-let | `if let Pat = expr { }` |
| While loop | `while cond { }` |
| For loop | `for x in iter { }` |
| Break | `break` / `break N` (labeled break) |
| Continue | `continue` |
| Return | `return expr` / `return` (void) |
| Match | `match val { Pat => body, ... }` |
| Block expressions | `{ stmt; expr }` yields `expr` |

---

## 5. Functions & Lambdas

### Functions
- **`fn name(params) -> ret`** — standard function declaration
- **`export fn`** — (planned) C-ABI export for interop
- **`extern fn`** — declare external C function to call from Oxide
- **Default parameter values** — `fn f(x: i32 = 0)`
- **Early returns** — `return` anywhere in function body

### Lambdas / Closures
- **`|params| { body }`** — closure syntax
- **Capture by value or reference** — inferred from usage
- **Fn-pointer decay** — non-capturing lambdas coerce to `fn(A) -> B`
- **Capturing closures as struct fields** — closure environment stored in struct

---

## 6. Modules & Imports

| Feature | Syntax | Description |
|---------|--------|-------------|
| Import | `import "path/to/file.ox"` | Include another Oxide module |
| Import resolution | compiler searches `--import-path` dirs | Search path for modules |
| Export (planned) | `export fn` | DSO-export for C interop |
| Namespacing | file-scoped | Each file is its own namespace |

---

## 7. Formal Verification (Contracts & SMT)

### Design-by-Contract
| Keyword | Purpose |
|---------|---------|
| `requires(cond)` | Precondition: caller must guarantee |
| `ensures(cond)` | Postcondition: callee must guarantee |
| `invariant(cond)` | Loop/struct invariant |
| `assert(cond)` | Runtime + static assertion |
| `old(expr)` | Refer to pre-state value in `ensures` |
| `decreases(expr)` | Termination measure (well-founded ordering) |

### Quantifiers & Logic
| Keyword | Purpose |
|---------|---------|
| `forall x in range. P` | Universal quantifier |
| `exists x in range. P` | Existential quantifier |
| `P implies Q` | Logical implication |
| `P iff Q` | Logical biconditional |

### Ghost Machinery
| Feature | Description |
|---------|---------|
| `ghost let x = expr` | Ghost variable (SMT-only, erased at runtime) |
| `ghost fn` | Ghost function (no runtime code) |
| `spec fn` | Specification function — SMT abstraction, no body emitted |
| `region R` | Named memory region for fine-grained aliasing control |
| `modifies R` | Frame axiom: declares which regions a function may write |
| `axiom NAME: body` | User-authored SMT axiom (live syntax, emitted to solver) |
| `instantiate` | Guided instantiation of quantified axioms |
| `refines` | Refinement obligation: concrete impl refines abstract spec |
| `preserves` | Preservation obligation (invariant maintenance) |
| `noninterference` | Owicki-Gries noninterference check for concurrent/handler ghost state |
| `cycle_preserves` | Preservation across a full scheduling cycle (e.g., VMCycle) |
| `proof that P by induction on k` | Proof statement with induction tactic on variable k |

### SMT Pipeline
- **Multi-prover dispatch** — Z3, CVC5, Why3 (auto-select or `--prover`)
- **Goal splitting (ProofSplitter)** — 5 splitting strategies:
  1. Per-ensures-clause split
  2. Per-quantifier-instance split
  3. Per-disjunct split
  4. Per-case-analysis split
  5. Per-modifies-region split
- **Tactic selection** — auto-selects SMT logic:
  - `QF_BV` — quantifier-free bit-vector
  - `Simplify` — simplification pass
  - `NIA` — non-linear integer arithmetic
  - `Cascade` — try multiple in sequence
- **Proof certificates (JSON)** — serializes every discharged goal, prover, tactic, timing
- **`oxide check` replay** — re-verify a proof certificate without re-running the full pipeline
- **BV bridging** — Oxide int/uint types mapped to SMT bit-vectors with proper sign extension
- **Memory model axioms** — built-in support for:
  - TSO (Total Store Order) for x86
  - Cache coherence protocols
  - TLB consistency
  - Custom user-authored axioms via `axiom NAME: body;`
- **asm! axiomatization** — `spec fn asm_<name>` provides SMT-level semantics for inline asm blocks
- **Noninterference emission (D8)** — `emitNoninterference` in Ghost.cpp (fully implemented)
- **Cycle preservation emission (D9)** — `emitCyclePreserves` in Ghost.cpp (fully implemented)
- **ProofSplitter wiring** — integrated into `smtClause` dispatch

---

## 8. Inline Assembly

### Syntax
```
asm!("instruction string",
     in(reg) expr,
     out(reg) target,
     clobber [reg1, reg2]);
```

### Features
- **Input operands** — `in(reg) expr` binds a value to a register
- **Output operands** — `out(reg) target` receives result
- **Clobber list** — `clobber [rax, rcx]` declares registers that may be modified
- **SMT axiomatization** — pair with `spec fn asm_<name>` to give the asm block formal semantics for the verifier
- **Spec-function abstraction** — the spec fn body is the SMT-level model; the asm! is the runtime implementation

---

## 9. Bare-Metal / Systems Programming

### Trap Handling
| Feature | Description |
|---------|-------------|
| `trap handler name(...)` | Declare an interrupt/exception handler |
| Trap vector table | Emitted in `.trap_table` ELF section |
| `--freestanding` | Compile without libc, no runtime |
| `--entry NAME` | Set custom entry point (default `main`) |

### MMIO
| Function | Description |
|----------|-------------|
| `mmio_load(ptr: *T) -> T` | Volatile load from memory-mapped I/O address |
| `mmio_store(ptr: *T, val: T)` | Volatile store to memory-mapped I/O address |

### Hypervisor Support
- **VMX operations** — `vmread`, `vmwrite`, VMCALL, VMLAUNCH, VMRESUME via asm!
- **EPT (Extended Page Tables)** — inductively verified page-table walks
- **Per-handler preservation** — each trap handler proven to preserve invariants
- **Cross-VCPU noninterference** — ghost state isolation between virtual CPUs
- **VM cycle preservation** — full scheduling cycle proven sound
- **SDM constants** — Intel SDM field encoding constants available in Oxide

---

## 10. Foreign Function Interface (C Interop)

| Feature | Syntax | Description |
|---------|--------|-------------|
| `extern fn` | `extern fn printf(fmt: *u8, ...) -> i32;` | Declare C function to call from Oxide |
| `extern struct` | `extern struct Foo;` | Opaque struct handle (size unknown, pointer-only) |
| `--link NAME` | CLI flag | Link a native library (e.g., `--link ws2_32`) |
| `&u8` / `*void` | Void pointer | Decays to `void*` at C boundary |
| `str_ptr(s)` | Builtin | Get raw `*u8` pointer to string data for C interop |
| `#include` interop | via clang | Oxide links through clang, system headers accessible |

---

## 11. Standard Library (Builtins)

### Math
`abs`, `min`, `max`, `pow`, `sqrt`, `sin`, `cos`, `tan`, `log`, `log2`, `log10`, `exp`, `floor`, `ceil`, `round`

### String Operations
| Function | Description |
|----------|-------------|
| `str.len()` | Length |
| `str.lower()` / `str.upper()` | Case conversion |
| `str.trim()` | Strip leading/trailing whitespace |
| `str.replace(old, new)` | Substring replacement |
| `str.split(delim)` | Split into `vec[str]` |
| `str.join(vec)` | Join strings with delimiter |
| `str.contains(sub)` | Substring test |
| `str.starts_with(prefix)` / `str.ends_with(suffix)` | Prefix/suffix tests |
| `str_slice(s, start, len)` | Substring slice (clamped) |
| `str_ptr(s)` | Raw pointer for FFI |
| `int_to_str(n)` / `float_to_str(f)` | Number-to-string conversion |
| `str_to_int(s)` / `str_to_float(s)` | String-to-number conversion |

### Collections (DynArray/vec)
| Function | Description |
|----------|-------------|
| `vec.push(val)` | Append element |
| `vec.pop()` | Remove last element (returns bool) |
| `vec.len()` | Current length |
| `vec.sort()` | In-place sort |
| `vec.contains(val)` | Membership test |
| `vec.get(i)` / `vec.set(i, val)` | Indexed access |
| `vec.clear()` | Remove all elements |
| `set_to_vec(s)` / `hset_to_vec(s)` | Convert set to vector |

### Map / Set / HMap / HSet
| Function | Description |
|----------|-------------|
| `map.insert(k, v)` / `map.remove(k)` | Insert/delete key |
| `map.get(k)` / `map.contains(k)` | Lookup |
| `map.keys()` / `map.values()` | Key/value iterators |
| `map.len()` / `map.clear()` | Size / clear |
| `hmap.insert(k, v)` / `hmap.get(k)` | Hash map variants |
| `set.insert(v)` / `set.contains(v)` | Set operations |
| `hset.insert(v)` / `hset.contains(v)` | Hash set variants |

### I/O
| Function | Description |
|----------|-------------|
| `read_line()` | Read line from stdin |
| `read_file(path) -> str` | Read entire file |
| `file_open(path, mode)` | Open file handle |
| `file_close(handle)` | Close file |
| `file_read(handle, n)` | Read n bytes |
| `file_write(handle, data)` | Write bytes |
| `print(...)` | Print to stdout (any number of args) |
| `println(...)` | Print with newline |

### Time & Random
| Function | Description |
|----------|-------------|
| `time_ns()` | Nanosecond timestamp |
| `clock_ms()` | Millisecond clock |
| `seed(n)` | Seed PRNG |
| `rand(max)` | Random integer in [0, max) |
| `rand_float()` | Random float in [0.0, 1.0) |

### Bit Operations
`bit_and`, `bit_or`, `bit_xor`, `bit_not`, `shl`, `shr` (native operator support: `&`, `|`, `^`, `~`, `<<`, `>>`)

---

## 12. Compiler CLI (Driver)

### Commands
| Command | Description |
|----------|-------------|
| `ox run file.ox` | Compile + link + execute |
| `ox emit file.ox` | Emit textual LLVM IR (`.ll`) |
| `ox build file.ox` | Compile to object file (`.o`) |
| `ox exe file.ox` | Compile to standalone executable |
| `ox verify file.ox` | Run formal verification (SMT pipeline) |
| `ox check cert.json` | Replay a proof certificate |

### Flags
| Flag | Description |
|------|-------------|
| `--freestanding` | No libc, no runtime (bare metal) |
| `--entry NAME` | Custom entry point |
| `--link NAME` | Link a native library |
| `--import-path DIR` | Add module search path |
| `--prover NAME` | Select SMT prover (z3/cvc5/why3) |
| `--emit-smt` | Also emit `.smt2` file alongside IR |
| `-O0` / `-O1` / `-O2` | Optimization levels (passed to clang) |
| `--verify-only` | Run verifier, skip code emission |

### Compilation Pipeline
1. **Lexer** — tokenize source (`src/Lexer.h`, `src/Lexer.cpp`)
2. **Parser** — build AST (`src/Parser.h`) — recursive descent, all keywords
3. **Sema** — semantic analysis (`src/Sema.h`, `src/Sema.cpp`) — type checking, borrow checking, concept resolution, builtin resolution
4. **IRGen** — emit textual LLVM IR (`src/IRGen.h`, `src/IRGen.cpp`) — RAII insertion, vtable generation, trap table emission, contract gates
5. **Ghost/SMT** — emit SMT-LIB (`src/Smt.h`, `src/Ghost.cpp`) — ghost state, memory model axioms, asm! axiomatization, BV bridging
6. **ProofSplitter** — split verification goals (`src/ProofSplitter.h`)
7. **ProofDispatch** — multi-prover dispatch + certificates (`src/ProofDispatch.h`)
8. **clang** — out-of-process: Oxide emits `.ll`, clang assembles + links

### Output Artifacts
- `.ll` — textual LLVM IR
- `.smt2` — SMT-LIB proof obligations
- `.json` — proof certificate
- `.o` — object file
- Executable (no extension)

---

## 13. Safety Guarantees

| Guarantee | Mechanism |
|-----------|-----------|
| No use-after-move | Compile error at Sema |
| No data races (single-threaded) | Borrow checker (`&mut` exclusivity) |
| No null dereference (in verified code) | SMT proves pointer safety under `requires` |
| Memory safety | RAII drops, no GC needed |
| Type safety | Static type checking with inference |
| Termination | `decreases` measure on recursive functions |
| Contract compliance | `requires`/`ensures` verified by SMT |
| Invariant preservation | `invariant` clauses checked at loop boundaries |
| Handler isolation | `noninterference` proof (Owicki-Gries) |
| Cycle soundness | `cycle_preserves` proof for scheduling loops |

---

## 14. Code Generation Features

| Feature | Description |
|---------|-------------|
| RAII drop insertion | Compiler auto-inserts `drop` calls at scope exits |
| Vtable generation | Virtual method tables emitted as LLVM constants |
| Trap table emission | `.trap_table` section with handler entries |
| Contract gate emission | `requires` checks emitted as runtime assertions (in non-verified builds) |
| Spec fn inlining | `spec fn` calls inlined into SMT terms (no runtime code) |
| Ghost erasure | `ghost let` / `ghost fn` produce no IR |
| Closure environment | Capturing lambdas compile to struct + fn-pointer pairs |
| Dead-code elimination | clang handles LTO/dead-code elimination at -O2 |

---

## 16. C++ Interoperability

Oxide provides bidirectional C/C++ interoperability through multiple mechanisms.

### Calling C from Oxide (`extern fn`)
```oxide
extern fn printf(fmt: *u8, ...) -> i32;
extern fn socket(af: i32, t: i32, proto: i32) -> i64;
```
- Declares C functions callable from Oxide
- Emits LLVM `declare` with C calling convention
- Type mapping: `i32`→`i32`, `i64`→`i64`, `*T`→`T*`, `str`→`i8*`, `f64`→`double`

### Calling Oxide from C (`export fn`)
```oxide
export fn multiply(a: i64, b: i64) -> i64 {
  return a * b;
}
```
- Emits `define dso_local` instead of `define` — symbol is visible to C linker
- C code calls it as: `extern int multiply(int a, int b);`
- Full C ABI compatibility through clang lowering

### Opaque Handle Types (`extern struct`)
```oxide
extern struct HWND_tag;
typedef HWND = *HWND_tag;
```
- Opaque C-handle tag types (no fields, pointer-only)
- Distinct, non-interchangeable handle types like C's `DECLARE_HANDLE(HWND)`

### Void-Pointer Decay
- Any addressable struct/array auto-decays to `&u8` at extern call sites
- C-style `T* → void*` implicit conversion
- No explicit `as &u8` cast needed

### Linking Native Libraries
```bash
oxide exe program.ox --link ws2_32 --link user32
```
- `--link NAME` adds `-l NAME` to the linker
- `-Wl,FLAG` passes raw flags to the linker
- Repeatable for multiple libraries

### Automatic C Header Binding (`oxide bindgen`)
```bash
oxide bindgen windows.h -o win32.ox
```
- Parses C headers via `clang -ast-dump=json`
- Auto-generates `extern fn` declarations for every C function
- Maps C types to Oxide types: `int`→`i32`, `long`→`i64`, `char*`→`*u8`, `void*`→`&u8`
- Emits `extern struct Name;` for opaque structs
- Handles function pointers, typedefs, struct pointers

### C++ Interop Notes
- C++ functions must be wrapped in `extern "C"` on the C++ side (standard practice)
- No C++ name mangling bridge — use `extern "C"` wrapper functions
- Struct ABI: clang handles platform struct return/arg passing correctly
- Function pointers: Oxide `fn(A) -> B` bitcasts to C `B (*)(A)`

---

## 17. Macros & Metaprogramming

Oxide provides a compile-time macro system for code generation.

### Macro Declaration
```oxide
macro square(x) {
  (x) * (x)
}

macro max3(a, b, c) {
  let ab = expand max(ab, a, b);
  expand max(ab, ab, c)
}
```

### Macro Expansion
```oxide
fn main() -> i64 {
  let n = expand square(5);      // expands to (5) * (5)
  print(n);                      // 25
  return 0;
}
```

### How It Works
- `macro name(params...) { body }` — declares a macro with parameter placeholders
- `expand name(args...)` — invokes the macro, substituting args for params
- Expansion happens at compile time — the expanded code is type-checked and compiled in place
- Macros can call other macros (recursive expansion)
- No runtime overhead — purely textual/AST-level substitution

---

## 18. Concurrency Primitives

Oxide provides CSP-style concurrency with threads and channels.

### Thread Spawning
```oxide
spawn {
  // runs on a new thread
  print("hello from thread");
};
```
- `spawn { body }` creates a new OS thread
- Returns a join handle (can be awaited with `sync`)

### Channels
```oxide
let ch = Channel<i64>;    // create a bounded channel
ch <- 42;                  // send value into channel
let msg = <- ch;           // receive value from channel
```
- `Channel<T>` — typed message-passing channel
- `chan <- val` — send operation (blocks if buffer full)
- `<- chan` — receive operation (blocks if empty)
- Runtime backed by `ox_chan_new` / `ox_chan_send` / `ox_chan_recv`

### Synchronization
```oxide
sync {
  // mutual exclusion / barrier section
  print("synchronized");
};
```
- `sync { ... }` — barrier-synchronized block
- Ensures all spawned threads reach the barrier before proceeding

### Runtime Support
- `ox_thread_create` — creates OS thread (Windows: CreateThread, POSIX: pthread_create)
- `ox_thread_join` — waits for thread completion
- `ox_chan_new` — allocates a buffered channel
- `ox_chan_send` / `ox_chan_recv` — channel operations
- `ox_sync_begin` / `ox_sync_end` — barrier primitives

### Example
```oxide
fn worker(ch: Channel<i64>) {
  let msg = <- ch;
  print("received:", msg);
  ch <- 42;
}

fn main() -> i64 {
  let ch = Channel<i64>;
  spawn { worker(ch); };
  ch <- 100;
  let reply = <- ch;
  print("reply:", reply);
  sync {
    print("synchronized section");
  };
  return 0;
}
```

---

## 19. Multi-Output Inline Assembly

Oxide supports inline assembly with multiple output operands.

### Syntax
```oxide
let mut aux: i32 = 0;
let tsc: i64 = asm!("rdtscp",
  out("{rax}") tsc_val,    // output 0 → i64
  out("{rcx}") aux,        // output 1 → i32
  in("{rdx}") 0,           // input
  clobber [memory]);
```

### Features
- **Multiple outputs** — `out(reg) target` for each output operand
- **In-out operands** — `inout(reg) target` reads and writes the same register
- **Aggregate return** — LLVM IR returns a struct, Each output extracted via `extractvalue`
- **SMT axiomatization** — each output modeled as a separate uninterpreted function:
  - `asm_<fn>_<seq>_out0(inputs...) -> T0`
  - `asm_<fn>_<seq>_out1(inputs...) -> T1`
- **Spec functions** — `spec fn asm_<fn>_out0(...) = body;` provides SMT semantics per output
- **Clobber lists** — `clobber [rax, rcx, memory]` declares modified registers/memory

### Verification
- Each output gets its own `spec fn` axiom: `spec fn asm_my_fn_out0(x: i64) -> i64 = 5;`
- The SMT encoder applies the correct uninterpreted function per output
- `ensures result == expected` discharges against the per-output spec

---

## 20. Cross-Function MMIO Threading (SMT)

MMIO (Memory-Mapped I/O) effects are tracked across function call boundaries in the SMT verifier.

### How It Works
- When function A calls function B, and B does `mmio_store(addr, val)`, the verifier propagates B's MMIO write effects to A's post-state
- `mmio_load(addr)` in A sees the value written by B (same SMT term)
- Frame axioms are emitted for unchanged MMIO addresses

### SMT Infrastructure
- `SmtCtx::mmioWriteEffects` — maps each function to its list of (address, value) MMIO writes
- `SmtCtx::mmioState` — current SMT model of each MMIO address's value
- `SmtCtx::mmioReadAddresses` — addresses read by the current function
- Frame axioms: addresses NOT written by a function equal their `old()` value

### Example
```oxide
fn configure_device(base: *u8) {
  mmio_store(base, 0x1);  // enable device
}

fn init() -> i64 {
  let base = 0xF0000000 as *u8;
  configure_device(base);  // mmio write threaded into init's post-state
  let enabled = mmio_load(base);  // SMT knows this is 0x1
  assert enabled == 0x1;   // discharges to unsat (proven)
  return 0;
}
```

---

## 21. Trap Handler Discharge (SMT)

Trap handler preconditions can optionally be discharged by the SMT solver instead of being assumed.

### Standard Trap Handlers (Assumed)
```oxide
trap handler handle_exit(exit_reason: i64, qual: i64)
  requires exit_reason >= 0 && exit_reason < 64   // ASSUMED (hardware guarantees)
  ensures qual >= 0
{
  // handler body
}
```

### Discharged Trap Handlers
```oxide
trap handler handle_exit(exit_reason: i64, qual: i64) discharge
  requires exit_reason >= 0 && exit_reason < 64   // DISCHARGED (proven from VM-exit axioms)
  ensures qual >= 0
{
  // handler body
}
```

### How `discharge` Works
- Without `discharge`: `requires` clauses are ASSUMED as premises (the hardware guarantees them on VM exit)
- With `discharge`: `requires` clauses are emitted as SMT discharge queries — the solver must prove they follow from:
  - The compiler-managed memory model axioms (TSO, cache coherence, TLB)
  - User-authored `axiom NAME: body;` declarations about the VMCS/exit state
  - The VM-exit hardware context (modelled by the memory model + user axioms)

### Trust Boundary
- Standard: trust that hardware delivers correct exit context → assume requires
- Discharge: prove that exit context implies requires from the axiomatic model → discharge requires
- The `ensures` and `invariant` clauses discharge identically in both modes

---

## 22. What Oxide Cannot Do (Remaining Limitations)

- **No C++ name mangling bridge** — use `extern "C"` on the C++ side (standard practice)
- **No full hypervisor verification** — can prove slices (per-handler, noninterference, EPT induction) but not full `hv/vmlaunch.ox` yet
- **No 512-entry EPT** — EPT verification limited to small page counts
- **Concurrency verification** — concurrency primitives exist but the SMT verification pipeline is sequential (no concurrent program logic yet)

---

*Generated from source analysis of `src/` (Lexer.h, Parser.h, Sema.h, Sema.cpp, IRGen.h, IRGen.cpp, Smt.h, Ghost.cpp, ProofSplitter.h, ProofDispatch.h, Driver.cpp, AST.h) and example files in `examples/` and `freestanding/`.*
