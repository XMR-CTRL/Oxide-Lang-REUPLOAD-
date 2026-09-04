> **OXIDE** · Compiler Internals
> How the compiler is organized, how the seven stages fit together, and where the sharp edges are.

# Compiler Internals

This doc is for contributors. It describes the pipeline, the role of each file, the runtime that ships embedded in every hosted build, and the bits of the architecture that will surprise you.

The two rules this codebase runs on:

1. Emit textual LLVM IR. Never link LLVM.
2. Do the work in sema. Emit is a thin layer on top.

Almost everything else follows from those two.

## The pipeline

```
main.cpp → Lexer → Parser → Sema → [Ghost/ProofSplitter/ProofDispatch] → IRGen → clang subprocess
```

A file enters as text it exits as a native binary. Between those two points there is exactly one IR.

### main.cpp

The CLI entry. It parses arguments sets flags on `Driver` and calls `Driver::run`. No logic lives here by design. 120 lines.

### Lexer

`src/Lexer.h` and `src/Lexer.cpp`. Turns source text into a token stream. Holds the keyword table. Comments are attached to tokens as trivia so the AST can reference them if needed. One file one pass.

### Parser

`src/Parser.h` and `src/Parser.cpp`. Recursive descent. Builds the AST out of `AST.h`. Allocates nodes and hands ownership up via `std::unique_ptr`. The grammar is simple and the parser reflects that. There is no precedence climbing or Pratt parser here.

### AST

`src/AST.h`. Every syntactic form has a node. Nodes are value typed through `unique_ptr`. Visitor pattern is present but most passes use explicit `switch` or `dynamic_cast` for speed. The comment count in this file is where new contributors should spend their first hour.

### Sema

`src/Sema.h` and `src/Sema.cpp`. Type checking name resolution mutability checking borrow checking macro resolution concept satisfaction default argument validation move analysis drop insertion defer LIFO ordering and lambda capture analysis. This file is where an Oxide program becomes correct or gets rejected.

Sema owns the rule that move only types have `drop` and are non copyable. Sema decides whether a `let` is a move or a copy. Sema decides which `impl` satisfies a concept. Sema builds the table that says which function is the superclass of which.

### Ghost Smt ProofSplitter ProofDispatch

`src/Ghost.cpp` takes annotated contracts and emits SMT-LIB terms. `src/Smt.h` is the C++ SMT term constructor. `src/ProofSplitter.h/cpp` takes a set of obligations and splits them into prover sized chunks. `src/ProofDispatch.h/cpp` hands each chunk to Z3 CVC5 or Why3 manages timeouts and collects results. This directory of files exists only when there are contracts in the program. A contract free program never touches them.

### IRGen

`src/IRGen.h` and `src/IRGen.cpp`. Turns the typed AST into textual LLVM IR. This is where RAII drop calls are inserted where vtables are emitted where the `.trap_table` section is generated where closure environments are materialized. IRGen does not think about types it emits LLVM that was already fully checked by Sema.

### Driver

`src/Driver.h` and `src/Driver.cpp`. The part that actually compiles. Generates the final IR shell writes it to a temp file builds a clang command line runs clang streams output and reports errors. Driver also owns the embedded C runtime and the `bindgen` subcommand.

## The textual IR decision

Oxide does not link LLVM. It writes out a `.ll` file as a plain string and it shells out to `clang -x ir` to turn that into an object or an executable.

```
clang -x ir -c input.ll -o output.o
clang -x ir input.ll -o output.exe -luser32
```

This has two consequences.

**Build stays simple.** No LLVM build system. No `llvm-config`. The compiler builds in seconds on any machine with a C++17 compiler.

**Optimization is opaque.** clang runs in a subprocess with its own stdout and stderr. Errors from clang have to be forwarded or they vanish. Temp files must be cleaned up. In debug builds temp file names include the PID because parallel builds overlap without it.

### Why this works at all

LLVM's textual IR format is stable and well documented. Clang reads the exact same IR whether it comes from a file or from a LTO plugin. The gap between "we wrote to a file and clang read it" versus "we called LLVM via the API" is roughly zero in correctness and potentially enormous in correctness proof obligations. Proving what you emit is easier than proving what LLVM will do to it once you link against it.

## The embedded runtime

The Oxide runtime is not a separate library. It is a C string inside `Driver.cpp` compiled fresh into every hosted executable.

```
oxide rt
```

prints it out so you can look at it.

The runtime is where `print` lives. It is where `ox_bounds_fail` and `ox_contract_fail` live. It is where `vec` resize lives. It owns the fast paths for the collection types and the string allocator. It includes a full channel allocator `ox_chan_*` and a thread spawn shim `ox_thread_create` even though the compiler currently executes spawn bodies inline.

Why embed the runtime as a string? Because the compiler is otherwise just `src/*.cpp` and to add a dependency would be to require the user to install something. A single self contained binary with the runtime embedded stays no dependency.

### Runtime hot paths

The runtime uses tagged pointer free structs and a bump allocator for the vec expansion path. Allocation is checked and aborts on OOM. There is no gc. There is no reference counting. A value deallocates when its scope ends.

## The `@main` rename

In a hosted build the user's `fn main()` is renamed to `@oxide_main` in the IR and a compiler generated `main` is emitted that does:

```
call runtime init
call @oxide_main
return result
```

This is what lets the runtime run before user code. The runtime init allocates the string table the rng and the thread state. Without it `print` would not know where to write.

In freestanding mode there is no runtime. There is no `@oxide_main` wrapper. The entry symbol is whatever `--entry` names and it must match the linker.

### Why the rename

If the user writes `fn main()` in C the symbol is `main` and it is an entry point. If the user writes `fn main()` in Oxide and the compiler emits it as `main` there is no place to put runtime setup. Renaming the user entry and shadowing it is how hosted mode runs setup code without requiring the user to remember anything.

## Diagnostics

Error messages are formatted in `Sema` and `Parser` with source line and column. The format is stable enough for tooling to parse.

```
file.ox:12:34: error: cannot move from immutable binding
```

There is no color. There is no fancy diff. Error strings are of a quality someone fixing a deployment cares about.

## Building and testing

The compiler builds with no flags beyond `-std=c++17 -O2`.

```
clang++ -std=c++17 -O2 src/*.cpp -o oxide
```

CMake also works. MSVC also works. The build outputs go into `build/`.

The regression suite is `examples/`. Every language feature has at least one example. Contract features have positive and negative files. The naming convention is `<file>.ox` plus `<file>.err` or `<file>.out` for expected error or expected output.

`verify_quality.py build/oxide-hardened.exe` runs the formal verification quality gate. It checks the positive suite asserts verify and the negative cases all fail. This catches regressions in the SMT path that ordinary tests would miss.

## Known rough edges

- Parser is a monolithic recursive descent function set per construct. Adding a syntax form means touching the parser the lexer the AST and the type checker. There is no DSL for this.
- Sema is a 280KB file. It is not a single god object but it is the size of one.
- Templates and monomorphization are in Sema and IRGen and split across both. The CRTP between the two is not pretty.
- String interpolation is not yet a language feature. `"hello {name}"` is a story that ends with `String::format` which does not exist yet.
- The borrow checker is only as strong as the alias analysis Sema does today. It catches the obvious cases. Through functions it is conservative.

These are filed and known. The broad outline of the compiler is not in flux. The surface shifts in small ways.

## Where to start hacking

If you are adding a language feature:

1. Add the keyword to `src/Lexer.cpp`.
2. Add the AST node to `src/AST.h`.
3. Add the parse branch in `src/Parser.cpp`.
4. Add the type check in `src/Sema.cpp`.
5. Add the emit rule in `src/IRGen.cpp`.
6. Add an example in `examples/`.
7. If it affects contracts update the verification quality gate.

That order is the order that keeps you from breaking existing tests along the way.
