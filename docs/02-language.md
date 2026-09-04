> **OXIDE** · Language Reference
> Every type every operator every statement. One example per feature and the reasoning behind it.

# Oxide Language Reference

This is the reference. It is written to be read in order but you can jump around. Every feature gets a runnable example and then a short note on why it works the way it does. Reasoning matters more than syntax dumps. If a decision looks arbitrary read the note next to it and it usually is not.

If you want the fastest possible tour look at `examples/` directly. This doc and that directory document the same language. The README stays smaller on purpose.

# 1. Types

Oxide has a small set of built in types and everything user defined is built from them.

| Type | Meaning | Size |
|------|---------|------|
| `i8` `i16` `i32` `i64` | signed integers | 1, 2, 4, 8 bytes |
| `u8` `u16` `u32` `u64` | unsigned integers | 1, 2, 4, 8 bytes |
| `usize` | pointer sized unsigned integer | target dependent |
| `bool` | `true` or `false` | 1 byte |
| `char` | a single byte character | 1 byte |
| `f32` | single precision float | 4 bytes |
| `f64` | double precision float | 8 bytes |
| `str` | immutable UTF-8 string | pointer plus length |
| `void` | absence of a value | 0 bytes |

`sizeof(T)` returns the byte size at compile time. It works inside expressions and inside type annotations.

```oxide
fn main() -> i64 {
  print(sizeof(i64));     // 8
  print(sizeof(f32));     // 4
  print(sizeof(bool));    // 1
  print(sizeof(usize));   // 8 on x86_64
  return 0;
}
```

### Why sized integers

Most languages let you say `int` and then silently mean a host specific thing. Oxide does not. Every integer type announces its width in its name and the width is honored down to the LLVM IR. That matters for FFI to C and for any code that reasons about bit patterns. `i32` is always `i32`. The consequence is that FFI calls to Win32 or a kernel ABI lower faithfully without manual padding.

Arrays are fixed size value types written `[T; N]` where `N` is a compile time constant.

```oxide
let buf: [u8; 16] = [0; 16];
buf[0] = 0xFF;
print(buf[0]);
print(buf);
```

`vec[T]` is the dynamic array. It grows at run time and bounds checks every index. Scalars take a fast path. Structs arrays and nested vecs go through a memcpy path keyed on the element byte width. `examples/vec_structs.ox` covers the struct case.

## Structs

A `struct` declares a record with named fields. Construction is positional in the declaration order or explicit by name.

```oxide
struct Point {
  x: i64;
  y: i64;
}

fn main() -> i64 {
  let p = Point { x: 1, y: 2 };
  print(p.x);
  print(p.y);
  return 0;
}
```

Structs are value types. Assignment copies. A struct may contain another struct.

```oxide
struct Rect {
  tl: Point;
  br: Point;
}

fn main() -> i64 {
  let r = Rect {
    tl: Point { x: 0, y: 0 },
    br: Point { x: 4, y: 5 }
  };
  let w = r.br.x - r.tl.x;
  let h = r.br.y - r.tl.y;
  print(w * h);
  return 0;
}
```

### Methods and impl blocks

`impl` attaches functions to a type. A method takes `&self` `&mut self` or `self` as its first parameter. An associated function takes no receiver and is called as `Type::name`.

```oxide
impl Point {
  fn new(x: i64, y: i64) -> Point {
    return Point { x: x, y: y };
  }
  fn dist2(&self) -> i64 {
    return self.x * self.x + self.y * self.y;
  }
  fn translate(&mut self, dx: i64, dy: i64) {
    self.x = self.x + dx;
    self.y = self.y + dy;
  }
}

fn main() -> i64 {
  let mut p = Point::new(3, 4);
  p.translate(1, 1);
  print(p.dist2());  // 4^2 + 5^2 = 41
  return 0;
}
```

A field can be marked `private` which hides it from other modules. The mechanism is intentionally simple because there is currently no module system beyond `import` inlining. Once real modules land the privacy semantics will tighten.

### Operator overloading

Operator overloading is by named method not by a special syntax. The method names are fixed.

| Method | Operator |
|--------|----------|
| `__add` `__sub` `__mul` `__div` `__mod` | `+` `-` `*` `/` `%` |
| `__eq` | `==` (`!=` is `!__eq`) |
| `__lt` `__le` `__gt` `__ge` | `<` `<=` `>` `>=` |
| `__band` `__bor` `__bxor` `__shl` `__shr` | `&` `\|` `^` `<<` `>>` |
| `__neg` | unary `-` |
| `__index` | `[i]` |
| `__iadd` through `__imod` | compound assignment |
| `__assign` | `=` on assignment into the type |

Load `examples/operator_overload.ox` to see the full set defined on a real type.

### Why operators are named methods

Two reasons. First it makes them first class functions you can pass around. Second it removes a whole category of magical syntax from the parser. `a + b` means `a.__add(b)`. Nothing else.

## Enums and match

An `enum` declares a fixed set of tags. A `match` dispatches on the tag and the `_` arm catches the rest.

```oxide
enum Color { Red, Green, Blue }

fn name(c: Color) -> i64 {
  match c {
    Red   => { print("red");   return 1; }
    Green => { print("green"); return 2; }
    Blue  => { print("blue");  return 3; }
  }
  return 0;
}
```

`match` is exhaustive. A missing arm is a compile error unless `_` catches it. You can also use enum values as integers directly.

```oxide
print(Green as i64);   // 1
print(c == Green);    // true
```

## Binding and mutability

`let` is immutable. `mut` on its own or `let mut` is mutable. The compiler rejects reassignment of an immutable binding.

```oxide
let x = 5;
x = 6;          // compile error: cannot mutate immutable binding
let mut y = 5;
y = 6;          // fine
```

Shadowing is allowed in nested scopes. `examples/_hv_shadow.ox` and `examples/_hv_redecl.ox` exist specifically to pin this down.

```oxide
let x = 1;
{
  let x = 2;    // shadows outer x inside this block
  print(x);     // 2
}
print(x);       // 1
```

A redeclaration in the same scope is a compile error.

## Control flow

`if` `else if` `else` `while` `for` `break` `continue` `return` all behave as in C. Parentheses around `if` `while` and `for` headers are optional.

```oxide
if x > 0 { print("pos"); }
while i < n { i = i + 1; }
for let mut i = 0; i < 10; i = i + 1 { print(i); }
```

`for x in <expr>` iterates arrays vecs and strings. Integer ranges use `a..b` for exclusive and `a..=b` for inclusive. A range is only legal as a `for` iterable it is not a first class value.

```oxide
for x in [1, 2, 3] { print(x); }
for i in 0..arr.len() { print(arr[i]); }
for i in 1..=10 { print(i); }
for k, v in my_map { print(k, "->", v); }
```

Map iteration is `for k, v in my_map`. A bare `for k in my_map` binds just the key. Asking for two bindings on a non map iterable is an error.

### Why ranges are iterable but not values

A range literal carries almost no runtime information. Making it a real value would require a heap allocation a destructor and ownership rules. As a `for` iterable the compiler can emit a tight loop with no allocation. That choice keeps `for i in 0..n` as cheap as a C `for`.

## Functions

Return type comes after an arrow. A function with no return type returns `void`.

```oxide
fn add(a: i64, b: i64) -> i64 {
  return a + b;
}

fn greet(name: str) {
  print("hi", name);
}
```

Functions may be recursive and may be called before they are defined in the same module. `main` is the required entry point for `run` and `exe`.

### Default arguments

A trailing parameter can carry a default. Defaults must be trailing meaning a non defaulted parameter may not follow a defaulted one.

```oxide
fn make_greeting(who: str, greeting: str = "Hello", punct: str = "!") -> str {
  return greeting + ", " + who + punct;
}

print(make_greeting("oxide"));        // Hello, oxide!
print(make_greeting("oxide", "Hi"));  // Hi, oxide!
```

A default may be a literal a const global or `null` for a pointer parameter. It may not reference the function's own parameters or locals because those do not exist at the call site.

### Variadic print

`print` is variadic in the sense that it takes any number of arguments. `print(a, b, c)` and `print(a)` are both fine. A `println` alias exists and behaves identically.

## Generics

Functions and structs can take type parameters in angle brackets.

```oxide
fn id<T>(x: T) -> T { return x; }

fn max<T>(a: T, b: T) -> T {
  if a > b { return a; }
  return b;
}

struct Pair<A, B> { a: A; b: B; }

fn first<A, B>(p: Pair<A, B>) -> A { return p.a; }

fn main() -> i64 {
  print(id<i64>(7));
  print(max(11, 4));            // type args inferred
  let p = Pair<i64, str> { a: 1, b: "hello" };
  print(first<i64, str>(p));
  return 0;
}
```

Generic functions are monomorphized. That means one copy of the body is emitted per concrete instantiation and there is no boxing runtime dispatch or trait object. The cost is code size. The win is zero runtime overhead.

### Where clauses and concepts

A generic type can be constrained with `where`. The constraint names a concept the type must satisfy.

```oxide
concept Printable<T> {
  fn to_str(&self) -> str;
}

struct Person { name: str; age: i64; }

impl Person {
  fn new(name: str, age: i64) -> Person { return Person { name: name, age: age }; }
  fn to_str(&self) -> str { return self.name + " (" + itos(self.age) + ")"; }
}

fn show<T: Printable>(x: T) {
  print(x.to_str());
}

fn show2<T>(x: T) where T: Printable {
  print(x.to_str());
}
```

An inline `T: Printable` and a `where T: Printable` constraint mean the same thing. `where` is preferred when there are multiple type params or the constraint list is long. A type satisfies a concept when some `impl` block supplies every required signature with a compatible shape. Satisfaction is checked at the call site at compile time.

### Why concepts instead of trait objects

Oxide has no trait objects and no vtable coercion for generics. Concepts are a static check on the caller not a runtime mechanism. This keeps generics monomorphic and matches the rest of the language which avoids hidden allocations and hidden dispatch.

## RAII drop clone and move

Two method names are recognized by the compiler.

```
impl File {
  fn new(path: i64) -> File { return File { path: path }; }
  fn drop(&mut self) { print("close", self.path); }
  fn clone(&self) -> File { return File { path: self.path }; }
}
```

`drop` makes the type move only. The compiler inserts `drop` calls at every scope exit including `return` `break` `continue` and panic paths. Drops run in reverse declaration order of the live locals.

A type with `drop` is never implicitly copied. `let b = a` is a move not a copy. After the move `a` is invalid and touching it is a compile time error. `clone(&self) -> T` borrows the receiver and returns a fresh independent copy. It is the only way to copy a move only type. This matches Rust exactly. There is no `Copy` trait because the compiler does not want silent deep copies.

```oxide
fn scope() -> i64 {
  let a = File::new(10);
  let b = File::new(11);
  let c = a;            // move: a invalid from here on
  let d = b.clone();    // explicit copy: b still usable
  return b.path;
}
```

Drops happen in reverse declaration order at the end of the function meaning `d` then `c` then `b`. A `return x` where `x` is a whole move only struct is a move into the caller and no drop runs in the callee. `return x.field` only reads the field and the containing struct is dropped before the return value is materialized.

### Why move only is the default

Most languages start with implicit copy and then add move as an escape hatch. Oxide does the reverse for RAII types. The reason is that a move only type cannot be silently copied into a context that would leak or double free its owned resource. The cost is ergonomics. You have to write `.clone()` where a C++ developer would not. The payoff is that resource bugs become compile errors instead of heisenbugs.

## Defer

`defer <stmt>` schedules a statement to run at the end of the enclosing scope. LIFO order interleaved with RAII drops.

```oxide
fn make_thing() -> i64 {
  let h = acquire_handle();
  defer release_handle(h);        // runs at scope exit
  let x = compute(h);
  if x < 0 { return -1; }         // release runs here too
  return x;
}
```

The deferred statement is type checked in the enclosing scope at the point of the `defer`. It cannot reference a name declared later in the same block and it cannot use `return` `break` or `continue` inside. A block form `defer { ... }` is allowed and has its own scope.

### Why defer exists alongside RAII

RAII requires a wrapper type. Defer does not. For an `extern` handle or a raw pointer where you would rather not write a `struct` plus an `impl` `defer` is the minimal honest solution. RAII is for libraries. Defer is for the day to day.

## Lambdas

`fn(params) -> T { ... }` is a lambda expression. A leading capture list mirrors C++.

```oxide
fn(a, &b, c)(k: i64) -> i64 { ... }   // a,c by value; b by reference
fn[=](k: i64) -> i64 { ... }           // capture every in scope local by value
fn[&](k: i64) -> void { ... }          // capture every in scope local by ref
fn(x: i64) -> i64 { return x * 2; }    // non capturing
```

A non capturing lambda decays to a plain function pointer. You can assign it to a `fn(params) -> T` typed local return it from a function store it in a struct field and call it like any other `fn`. A capturing lambda lowers to an anonymous closure struct `{ fnptr, cap0, cap1, ... }` built at the capture site. By value captures are private copies. By reference captures are the local's address.

A capturing closure is not convertible to a bare `fn` pointer. A function declared to return `fn(...) -> ...` may return a non capturing lambda and must not return a capturing one.

```oxide
let f = fn(x: i64) -> i64 { return x + 1; };
print(f(4));                    // 5

let mut acc = 0;
let add = fn[&acc](n: i64) { acc += n; };
add(3); add(4);
print(acc);                     // 7

let table = [fn(x: i64) -> i64 { return x + 1; }, fn(x: i64) -> i64 { return x * 2; }];
print(table[0](10));            // 11
print(table[1](10));            // 20
```

`examples/lambdas.ox` and `examples/lambdas2.ox` cover every form.

## Inheritance

Oxide has single inheritance.

```oxide
struct Animal {
  name: i64;
}

impl Animal {
  fn new(n: i64) -> Animal { return Animal { name: n }; }
  virtual fn speak(&self) -> i64 { return 0; }
  fn name_of(&self) -> i64 { return self.name; }
}

struct Dog: Animal {
  breed: i64;
}

impl Dog {
  fn new(n: i64, b: i64) -> Dog { return Dog { name: n, breed: b }; }
  override fn speak(&self) -> i64 { return 7; }
}

fn chorus(a: &Animal) -> i64 {
  return a.speak();
}
```

Base fields are laid out first. A derived value is usable as a base and an upcast `&Derived` to `&Base` works because the base sub object sits at offset 0. Non virtual methods shadow statically. Virtual methods dispatch through a vtable emitted by the compiler.

### Why single inheritance only

Single inheritance keeps the object layout trivial and the vtable format obvious. Multiple inheritance would require adjusting pointers on casts complicating the object model for a feature that shows up rarely in the systems code Oxide targets. Composition and generics cover the same ground.

## Macros

Oxide has a compile time macro system for code generation. This is not text substitution and not a preprocessor. A macro is a declared unit of code that is expanded at the call site.

```oxide
macro square(x) {
  ($x) * ($x)
}

macro cube(x) {
  ($x) * ($x) * ($x)
}

fn main() -> i64 {
  let n = expand square(5);
  print(n);   // 25

  let m = expand cube(3);
  print(m);   // 27
  return 0;
}
```

`$name` refers to an argument. `expand name(args)` instantiates the macro at the point of use. The expanded code then goes through the normal type checker. Macro expansion is purely a compile time mechanism and produces no runtime code.

### Why `expand` is explicit

Implicit macro invocation is how you end up with macros shadowing functions and functions shadowing macros and nobody knowing which is which. `expand` makes the instantiation visible at the call site. You always know when macro expansion is happening.

## Modules and imports

There is no package system. `import "name"` substitutes the body of `name.ox` at the point of the `import`. The search path is the current file's directory plus any `--import-path` directories.

```oxide
// helpers.ox
fn twice(x: i64) -> i64 { return x * 2; }

// main.ox
import "helpers"

fn main() -> i64 {
  print(twice(21));
  return 0;
}
```

There is no namespacing yet. Large programs are built by convention using file naming and disciplined prefixes. This is honest but not scalable. A real module system is on the roadmap.

## Concurrency (partial)

Oxide has channels and a `spawn` keyword. The channel machinery is complete and safe. The `spawn` path is not.

```oxide
fn main() -> i64 {
  let ch = Channel<i64>::new();
  ch <- 42;
  let v = <- ch;
  print(v);
  spawn { print(99); };
  sync { print(100); };
  return 0;
}
```

`Channel<T>::new()` makes a typed bounded queue in the runtime. `ch <- v` sends. `<- ch` receives. `spawn { ... }` parses and type checks as a thread spawn but at the IR lowering today it runs the body inline in the current thread. `sync { ... }` is a barrier block.

This is why the verification doc treats concurrent proofs as separate from the working runtime channel code. The pieces exist. The full parallelism is not wired up yet. Do not rely on `spawn` for actual parallelism in production code today. Channels are safe to use for async style message passing on a single executor.

### Why ship partial concurrency

Channels were built first because they are also useful for in process queues. `spawn` is the harder part because a real thread spawn requires a runtime scheduler and a memory model. Rather than leave a public syntax that silently misbehaves the compiler emits a warning if you call `spawn` on a code path where parallelism is load bearing. The current behavior is correct but not parallel and this is documented rather than hidden.

## FFI to C

`extern fn` declares a C function callable from Oxide.

```oxide
extern fn printf(fmt: &u8, ...) -> i32;
extern fn socket(af: i32, socktype: i32, proto: i32) -> i64;
```

`extern struct` declares an opaque C handle type.

```oxide
extern struct HWND_tag;
typedef HWND = &HWND_tag;
```

Passing any struct or array into a C void pointer parameter works. The Oxide type decays to `&u8` at the call site. `--link NAME` adds `-l NAME` to the clang link step. Host symbols like `MessageBoxA` `CreateWindowExA` `socket` `bind` `listen` `accept` work the same as they do from C.

`oxide bindgen header.h` reads a C header via clang's AST dump and writes `extern` declarations for the functions and opaque struct declarations for the opaque types. `i32` maps to `i32` `char*` maps to `&u8` `void*` maps to `&u8`. The result is a `.ox` file you can `import` straight into a program. `examples/extern/` has a working demo with a `lib.ox` on the C side calling into it.

### Why FFI is extern only

Oxide does not promise to be a drop in replacement for C. It promises to call C code without ceremony. That asymmetry is deliberate. `extern fn` requires the C function exist already. There is no Oxide function called from C in the public surface yet `export fn` is on the roadmap. This keeps the ABI story one directional and simple.

## Error handling

There are no exceptions and no `Result`/`Option` built in. A function returns a value or it traps. For recoverable errors the convention today is to return `-1` or a sentinel value and check at the call site. The `assert` and contract gates exist for internal invariants. This is a real limitation and it is known.

### Why no Result yet

A Result type needs either tagged unions or a low level enum plus a discriminant both of which Oxide can express but neither is standardized in the language yet. Once enums get payloads the stdlib will grow `Result` `Option` and `Iterator` on top of the existing `vec` `Map` and `Set`. This order of work keeps the language small while the type system settles.

## Operators

Full binary operator set.

| Operator | Meaning |
|----------|---------|
| `+` `-` `*` `/` `%` | arithmetic |
| `<<` `>>` `&` `\|` `^` `~` | bitwise |
| `==` `!=` `<` `<=` `>` `>=` | comparison |
| `&&` `\|\|` `!` | logical short circuit |
| `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | compound assignment |
| `++` `--` prefix and postfix | increment decrement |
| `cond ? a : b` | ternary |
| `typeof` `sizeof` | compile time type and size queries |

Prefix `++x` and postfix `x++` mutate any lvalue of int char usize bool or pointer type. For a pointer `p++` steps forward by one element not one byte. `(i % 2 == 0) ? "even" : "odd"` works exactly as in C. Comparison of two `str` values is lexicographic.

## What is intentionally not here

- No exceptions. Trap only.
- No null pointer values except behind `&T` and explicit `null`.
- No classes with member access control beyond `private` fields.
- No async await syntax. `spawn` and channels are the story.
- No operator overloading via `operator+` syntax. Named methods only.
- No autoderef chains. `&T` requires `(*x)` or `x.field` where a method takes `&self`.

Each of these is a deliberate omission not an accident. The language trades completeness for auditability. Verification works because the surface area is small.
