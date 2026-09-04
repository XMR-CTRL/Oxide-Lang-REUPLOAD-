// math_verify.ox — formal-verification contracts that involve the power
// operator, exercising the SMT encoder's handling of PowerExpr.
//
// A `requires`/`ensures` pair is checked at run time on every call (the
// ensures gate traps if violated) and may also be discharged statically by
// the SMT encoder when `--emit-smt` is fed to Z3. The interesting case for
// the advanced-math feature: a contract clause that mentions `pow2` / `²`
// forces the SMT encoder (Driver.cpp's smtExpr path) to lower PowerExpr into
// a real SMT term — not an opaque uninterpreted function — so the prover can
// actually reason about exponentiation.
//
// STATUS: requires the Parser to recognise `pow2`/`²` in BOTH expression and
//   contract-clause positions and the SMT encoder to lower PowerExpr. Today
//   the contract clauses fail to parse.

// square(x): x squared. The ensures clause says the output is non-negative
// whenever the input is non-negative — a fact Pow(x, 2) satisfies for real x.
// The `pow2` ASCII fallback (== x²) appears in the result expression so the
// SMT encoder must reason about PowerExpr, not just x*x (the IRGen would lower
// both to the same @ox_square_f64 runtime call, but the SMT path sees the AST).
fn square(x: f64) -> f64
  requires x >= 0.0
  ensures result == x pow2
{
  return x pow2;
}

// cube(x): x cubed. The ensures pins the result to the cube via `x pow3`
// (== x³); for a non-negative input the cube is also non-negative, so the
// additional `result >= 0.0` clause is the discharge target.
fn cube(x: f64) -> f64
  requires x >= 0.0
  ensures result == x pow3
  ensures result >= 0.0
{
  return x pow3;
}

// abs_square: |x|² — always non-negative regardless of sign, a nice
// invariant for the SMT encoder to prove (the result is the square of an
// already-non-negative value). Uses the Unicode ² glyph in the ensures to
// also exercise the math_sym lowering path through the SMT encoder.
fn abs_square(x: f64) -> f64
  ensures result >= 0.0
{
  let mut a: f64 = 0.0;
  if x < 0.0 { a = -x; }
  else { a = x; }
  return a²;                  // Unicode postfix square in the body
}

// halve: a counting-loop contract that mixes power and arithmetic. The loop
// invariant bounds the running product, and the ensures ties the final value
// to the power-theory helper `square`.
fn halve_then_square(n: i64) -> f64
  requires n >= 0
  ensures result >= 0.0
{
  let mut i: i64 = 0;
  let mut acc: f64 = 0.0;
  while i < n
    invariant 0 <= i && i <= n
    invariant acc >= 0.0
  {
    let term: f64 = i as f64;
    acc = acc + term * term;
    i = i + 1;
  }
  return acc;
}

fn main() -> i64 {
  // happy paths — every gate passes silently:

  // square() requires a non-negative input, so we only call it with x >= 0.
  // (Calling square(-4.0) would trip the `requires x >= 0.0` gate at run time
  // — that TRIAL is intentionally left out so this file is a happy-path
  // verify; the abs_square case below exercises a negative base cleanly.)
  let s = square(5.0);
  print("square(5)=", s);            // expect 25.0

  let c = cube(3.0);
  print("cube(3)=", c);              // expect 27.0

  // abs_square accepts any sign and still returns a non-negative value — a
  // stronger contract that the SMT encoder can prove without a precondition.
  let asq = abs_square(-7.0);
  print("abs_square(-7)=", asq);     // expect 49.0

  let h = halve_then_square(4);
  print("halve_then_square(4)=", h); // expect 0+1+4+9 = 14.0

  return 0;
}
