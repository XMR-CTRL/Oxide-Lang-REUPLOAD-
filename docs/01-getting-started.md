> **OXIDE** · Getting Started
> Zero to a compiled, verified program in about five minutes.

# Getting Started

Oxide is a compiled systems language with C-style braces that emits LLVM IR and hands it to clang. Two things about it are unusual and you should know them before you build anything.

First it does not link LLVM. The compiler writes the IR as text and shells out to clang to optimize and link it. You need clang on PATH and that is the only hard dependency. There is no LLVM SDK to install.

Second it's two languages in one. There is a hosted mode for normal programs and a `--freestanding` mode for kernels and hypervisors. Everything in this page assumes hosted mode. Freestanding is its own doc.

## Build the compiler

Any C++17 compiler works. The repo ships a CMake project.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

On Windows the LLVM install at `C:\Program Files\LLVM` already ships a `clang++` that can build it.

```
clang++ -std=c++17 -O2 src/*.cpp -o oxide
```

That single command is the whole supported build path `(no configure step no extra dependencies)`.

### Why the build is this simple

Most compilers of this size vendor LLVM and link against its object model. Oxide deliberately does not. The internals doc has the full reasoning. Textual IR plus an out of process clang keeps the compiler small and keeps the build graph minimal. The cost is that clang must be reachable at run time for hosted builds.

## First program

```oxide
fn main() -> i64 {
  print("hello from oxide");
  return 0;
}
```

Save as `hello.ox` and run it.

```
oxide run hello.ox
```

`run` compiles the file links it against the embedded runtime and executes it. If you just want to type check without producing artifacts use `check`.

```
oxide check hello.ox
```

A clean check produces no output. Any error is printed on stderr with a source line.

## The commands

| Command | What it does |
|---------|--------------|
| `oxide run file.ox` | compile link execute |
| `oxide emit file.ox` | print the generated LLVM IR to stdout |
| `oxide build file.ox` | emit an object file `.o` |
| `oxide exe file.ox` | emit a native executable |
| `oxide check file.ox` | lex parse type check without codegen |
| `oxide verify file.ox` | emit SMT obligations and run a solver against them |
| `oxide rt` | print the embedded C runtime |
| `oxide bindgen header.h` | generate `extern fn` bindings from a C header |

Useful flags:

```
-o PATH          output path for build exe and rt
-O0              disable the default -O2 optimization pass
--target TRIPLE  put a target triple into the IR default is host
--freestanding   drop the C runtime and the @main wrapper
--entry NAME     program entry symbol in freestanding mode
--link NAME      link a native library repeatable e.g. --link user32
--emit-smt PATH  write an extra .smt2 with every contract in the file
--verify-only    run the verifier and skip codegen
--solver-timeout N  seconds per solver invocation
--import-path DIR  extra directory for `import "..."` lookups
--prover NAME    pick z3 cvc5 or why3 when multiple are installed
```

## The smallest interesting program

```oxide
fn fib(n: i64) -> i64
  requires n >= 0
  ensures result >= 0
{
  if n < 2 { return n; }
  let mut a: i64 = 0;
  let mut b: i64 = 1;
  let mut i: i64 = 1;
  while i < n
    invariant 1 <= i && i <= n
  {
    let c = a + b; a = b; b = c; i = i + 1;
  }
  return b;
}

fn main() -> i64 {
  let mut sum = 0;
  for let mut i = 0; i < 10; i = i + 1 {
    sum = sum + fib(i);
  }
  print("fib sum 0..9 =", sum);
  return 0;
}
```

Two things to notice.

`requires` and `ensures` are real contracts not comments. On a hosted build they become runtime trap gates inserted into the IR. Pass `--emit-smt` and the same clauses are emitted as SMT-LIB proof obligations. On the current build `fib` above comes back with 4 proven 0 undischarged 1 assumed `(verified 2026-09-02 with build/oxide-hardened.exe)`.

`print` takes any number of arguments of any supported type and prints them space separated on one line followed by a newline. Arrays print as `[1, 2, 3]`. Structs print as `Name{field: v, ...}` recursively.

## What to read next

- The language reference for every type every operator and every statement.
- The verification doc for contracts quantifiers ghost code and the SMT encoding.
- The freestanding doc if you are here for the hypervisor.
- The compiler internals doc if you want to hack on the compiler itself.

Every example file referenced in the docs lives in `examples/` and compiles with the current binary.
