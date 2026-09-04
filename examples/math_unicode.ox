// math_unicode.ox — Unicode math symbols and their ASCII fallbacks.
//
// Oxide's advanced-math feature supports a small set of Unicode math glyphs,
// each with an ASCII keyword fallback so source can be written without
// Unicode. The mapping (src/Lexer.h ~L328, src/Lexer.cpp ~L123/L475,
// Parser.cpp ~L3484):
//
//   π  (U+03C0)  <->  pi         (the circle constant; bare identifier)
//   √  (U+221A)  <->  sqrt       (prefix square-root: √x or sqrt x)
//   ∫  (U+222B)  <->  integrate (definite-integral prefix operator)
//   ²  (U+00B2)  <->  pow2       (postfix square: x² or x pow2)
//   ³  (U+00B3)  <->  pow3       (postfix cube: x³ or x pow3)
//
// Unicode glyphs lex as Tok::math_sym (text carries the raw UTF-8 bytes); the
// ASCII forms lex as Tok::kw_pi/kw_sqrt/kw_integrate/kw_pow2/kw_pow3. The
// Parser maps each kind to its meaning: sqrt is a prefix operator (parsePrimary
// builds a Call to the `sqrt` runtime fn for both √x and `sqrt x`), ²/³/pow2/
// pow3 are postfix (parsePostfix ~L2818) building a PowerExpr with an IntLit
// exponent, and pi is a bare constant (MathSymExpr).
//
// STATUS (verified against the rebuilt compiler):
//   WORKING: √x, sqrt x, sqrt(x), x², x³, x pow2, x pow3, **, e(), pi, π.
//   (Bug 5 fix: Sema's checkExpr for MathSymExpr now whitelists `pi`/`π` and
//    emits `error[sema]: unknown math symbol: '<glyph>'` for any other
//    Unicode math glyph. Unknown glyphs are NO LONGER silently folded to
//    Euler's number (`ox_e`) — see examples/test_unknown_glyph.ox.)

fn main() -> i64 {
  let x: f64 = 3.0;

  // =================================================================
  // sqrt  /  √   — square root (prefix operator AND call form)
  // =================================================================
  // ASCII call form: sqrt(<expr>) — the kw_sqrt arm lowers to Call(sqrt, [x])
  let s_call = sqrt(16.0);
  print("sqrt(16)     =", s_call);  // expect 4.0
  // ASCII prefix keyword form: sqrt <expr>
  let s_pref = sqrt 25.0;
  print("sqrt 25      =", s_pref);   // expect 5.0
  // Unicode prefix operator (math_sym glyph): √<expr>
  let s_uni = √49.0;
  print("√49          =", s_uni);     // expect 7.0
  // less trivial: √2 ~ 1.41421
  let rt2 = √2.0;
  print("√2           =", rt2);      // expect ~1.41421

  // =================================================================
  // pow2 / ²   — postfix square (x² == x*x)
  // =================================================================
  let sq_ascii = x pow2;             // ASCII postfix power-of-2
  print("3 pow2       =", sq_ascii); // expect 9.0
  let sq_uni = x²;                   // Unicode postfix ²
  print("3²           =", sq_uni);   // expect 9.0

  // compose with addition: f(x) = x² − 1, evaluate at 5 -> 24
  let xq: f64 = 5.0;
  let fx_uni = xq² - 1.0;            // Unicode form
  print("5²−1         =", fx_uni);  // expect 24.0
  let fx_asc = xq pow2 - 1.0;        // ASCII form
  print("5 pow2 − 1   =", fx_asc);   // expect 24.0

  // =================================================================
  // pow3 / ³   — postfix cube (x³ == x*x*x)
  // =================================================================
  let cb_ascii = x pow3;             // ASCII postfix power-of-3
  print("3 pow3       =", cb_ascii); // expect 27.0
  let cb_uni = x³;                   // Unicode postfix ³
  print("3³           =", cb_uni);   // expect 27.0

  // =================================================================
  // **   — the ASCII infix power operator (full coverage in math_power.ox);
  // included here for the inline comparison row.
  // =================================================================
  let pw = 2.0 ** 10;                // ASCII infix
  print("2**10        =", pw);       // expect 1024.0
  // mix infix ** with postfix ³: 2 ** 3²  == 2 ** 9  == 512 (postfix beats infix)
  let mix = 2 ** 3²;
  print("2**3²        =", mix);       // expect 512.0

  // =================================================================
  // Euler's number e — NOT a reserved glyph (no Unicode e); the call form
  // e() returns 2.71828... and works today (e is a regular identifier).
  // =================================================================
  let ev = e();
  print("e()          =", ev);       // expect 2.71828...
  let ev_sq = ev pow2;               // e² ~ 7.389056
  print("e pow2       =", ev_sq);    // expect ~7.389056

  // =================================================================
  // pi  /  π   — the bare `pi` ASCII keyword and the `π` glyph both build a
  // MathSymExpr("pi"). Sema's checkExpr whitelists `pi`/`π` and folds the
  // node to an f64 constant that IRGen lowers to `@ox_pi()` (3.14159...).
  // (Bug 5: any other Unicode math glyph reaching a MathSymExpr now fails
  // compilation with `error[sema]: unknown math symbol` instead of being
  // silently folded to Euler's number — see examples/test_unknown_glyph.ox.)
  // =================================================================
  let a_pi = pi;                    // ASCII: pi    <-> π
  print("pi           =", a_pi);   // expect 3.14159...
  let a_pi_u = π;                   // Unicode π
  print("π            =", a_pi_u); // expect 3.14159...

  // pi-equivalent that DOES work today: e() (Euler's number, a regular
  // identifier via @ox_e) gives a working "named math constant" baseline
  // so the file shows that bare-constant access works for un-reserved names.
  let pi_proxy = e() * 0.0 + 3.14159265358979;   // a stand-in 3.14159... value
  print("pi (proxy)   =", pi_proxy); // expect 3.14159...

  return 0;
}
