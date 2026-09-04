
// Lambdas with C++-style capture lists.
//   fn[a, &b](params...) -> T { ... }   captures a BY VALUE (a copy made at the
//                                        lex site; mutating it inside is private)
//   fn[&a](...)                          captures a BY REFERENCE (the local's
//                                        address is passed in; mutation shows)
//   fn(...) { ... }                       non-capturing; lowers to a plain
//                                        function pointer, callable like any fn.
//
// NOTE: a function declared to return `fn(...) -> ...` (a plain fn-pointer
// type) may return a NON-capturing lambda, but NOT a capturing one — exactly
// as in C++, where a capturing closure is not convertible to a function
// pointer. Capturing closures are first-class struct values you can bind
// (`let f = fn[...](){}`) and call, but the closure struct type is distinct
// from a bare `fn` pointer type.


fn main() -> i64 {
  // non-capturing -> bare fn pointer
  let sq = fn(x: i64) -> i64 {
    return x * x;
  };
  print("sq(4)=", sq(4));

  // by-value capture is an independent copy: mutating it inside the lambda
  // does NOT touch the caller's variable.
  let mut v = 100;
  let bump_copy = fn[v]() -> void {
    v = v + 1;
  };
  bump_copy();
  print("after bump_copy, v=", v);

  // by-reference capture: the lambda writes through the address, so the
  // caller observes the change (like C++ [&v]).
  let bump_ref = fn[&v]() -> void {
    v = v + 1;
  };
  bump_ref();
  print("after bump_ref, v=", v);

  // mixed capture + a real parameter: by-value a, by-ref b, plus arg k.
  let a = 3;
  let mut b = 0;
  let combine = fn[a, &b](k: i64) -> i64 {
    b = a + k;
    return b;
  };
  print("combine(7)=", combine(7));
  print("b after combine=", b);

  return 0;
}
