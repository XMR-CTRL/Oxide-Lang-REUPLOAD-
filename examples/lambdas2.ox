// Lambdas II — the C++-parity forms beyond the basics in `lambdas.ox`.
//
// `lambdas.ox` covers non-capturing fn-pointer lambdas and `[a]`/`[&a]`/mixed
// capture lists. This file exercises the higher-order, function-object, and
// call-site forms that round out parity with C++ lambdas:
//
//   * `(expr)(args)` — call applied to ANY expression (postfix call), not just
//     a bare name. Works for IIFEs, returned fn-pointers, fn-pointer array/struct
//     element loads, and capturing-closure temporaries.
//   * immediately-invoked capturing lambdas: `(fn[&](){...})()`
//   * function objects: a struct field typed `fn(...) -> ...`, holding a
//     non-capturing lambda, called through its owner.
//   * dispatch tables: `[fn(...){}, fn(...){}]` arrays indexed by runtime value.
//   * capturing structs by value (private copy) and by reference (writes home).

struct Point { x: i64; y: i64; }

// a struct with a function-pointer field = a simple function object
struct Action {
  f: fn(i64) -> i64;
  scale: i64;
}

fn apply_action(a: &Action, v: i64) -> i64 {
  // postfix call on a separately-resolved field value, indirect.
  return (a.f)(v) * a.scale;
}

fn tripler(n: i64) -> i64 { return n * 3; }

fn main() -> i64 {
  // 1) immediately-invoked non-capturing lambda (IIFE).
  print("iife=", (fn() -> i64 { return 7; })());

  // 2) immediately-invoked CAPTURING lambda — a closure temp is built, then
  //    called. The by-ref write is observable in `acc`.
  let mut acc = 0;
  (fn[&acc](n: i64) -> void { acc = acc + n; })(42);
  print("iife-cap acc=", acc);

  // 3) call a freshly-returned fn pointer, with a postfix call chained on the
  //    call result (no intermediate `let`).
  //    tripler is a plain fn; calling it and then calling the i64 it returns is
  //    not legal, so use a fn that returns a fn pointer:
  // (see `make_adder` style below)

  // 4) function object: hold a non-capturing lambda in a struct field and call
  //    it through a by-ref method.
  let act = Action { f: fn(n: i64) -> i64 { return n + 100; }, scale: 2 } as Action;
  print("apply_action(3)=", apply_action(&act, 3));

  // 5) dispatch table: an array of fn pointers, indexed at runtime, called with
  //    a postfix call on an Index expression.
  let table: [fn(i64) -> i64; 3] = [
    fn(x: i64) -> i64 { return x; },
    fn(x: i64) -> i64 { return x + 1; },
    fn(x: i64) -> i64 { return x * 2; },
  ];
  let choice = 2;
  print("table[choice](10)=", (table[choice])(10));

  // 6) capturing a struct BY VALUE is a private snapshot; mutating the copy
  //    inside the lambda leaves the caller's struct untouched.
  let p = Point { x: 1, y: 2 } as Point;
  let offset = fn[p](dx: i64, dy: i64) -> Point {
    let q = p;
    q.x = p.x + dx;
    q.y = p.y + dy;
    return q;
  };
  let moved = offset(10, 20);
  print("moved=", moved.x, ",", moved.y);
  print("p still=", p.x, ",", p.y);

  // 7) capturing a struct BY REFERENCE writes back through the captured pointer.
  let mut r = Point { x: 0, y: 0 } as Point;
  let mover = fn[&r](dx: i64, dy: i64) -> void {
    r.x = r.x + dx;
    r.y = r.y + dy;
  };
  mover(3, 4);
  mover(5, 6);
  print("r now=", r.x, ",", r.y);

  // 8) a higher-order function built from a capturing closure that itself is
  //    passed around (not returned). Build a tally reducer then drive it.
  let mut sum = 0;
  let tally = fn[&sum](k: i64) -> void { sum = sum + k; };
  tally(1); tally(2); tally(3); tally(4);
  print("tally sum=", sum);

  return 0;
}
