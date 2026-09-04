// math_solve.ox — MATLAB-style linear system solve: `A \ b`.
//
// Exercises SolveExpr (src/AST.h ~L689). The operator is a single backslash
// (Tok::backslash, src/Lexer.cpp ~L427) distinct from the division slash; it
// solves the linear system A*x = b for x. Sema (src/Sema.cpp ~L3753) requires
// the left operand to be a matrix (a 2D MatrixLit or array-of-array value) and
// the right operand to be a vector/numeric; the result element type is f64
// (f32 only when both sides carry f32). IRGen (src/IRGen.cpp ~L2869) lowers
// the expression to `@ox_mat_solve(A_handle, b_handle)`, which performs a
// Gauss-Jordan elimination with partial pivoting and returns a freshly-
// allocated solution-vector handle.
//
// STATUS: requires the Parser subagent to wire Tok::backslash -> SolveExpr.
//   Until then this file fails at parse time — that is the regression it guards.
fn main() -> i64 {
  // ---- trivial case: identity system, x = b ----
  let I = [[1.0, 0.0],
           [0.0, 1.0]];
  let b = [[5.0], [7.0]];
  let x = I \ b;                      // I*x = b  ==>  x = b = [5; 7]
  print("x0=", x[0][0]);             // expect 5.0
  print("x1=", x[1][0]);             // expect 7.0

  // ---- non-trivial 2x2 system ----
  //   2a +  b = 3
  //    a + 3b = 2
  // det = 2*3 - 1*1 = 5;  a = (3*3 - 1*2)/5 = 7/5 = 1.4
  //                          b = (2*2 - 1*3)/5 = 1/5 = 0.2
  let A = [[2.0, 1.0],
           [1.0, 3.0]];
  let c = [[3.0], [2.0]];
  let sol = A \ c;
  print("a=", sol[0][0]);             // expect 1.4  (7/5)
  print("b=", sol[1][0]);             // expect 0.2  (1/5)

  // verify the solution re-satisfies the original equations:
  //   2*a + 1*b  ==  2*1.4 + 0.2  ==  3.0
  //   1*a + 3*b  ==  1.4 + 0.6   ==  2.0
  let chk0 = 2.0 * sol[0][0] + 1.0 * sol[1][0];
  let chk1 = 1.0 * sol[0][0] + 3.0 * sol[1][0];
  print("eq0=", chk0);               // expect ~3.0
  print("eq1=", chk1);               // expect ~2.0

  // ---- 3x3 system with a known integer solution ----
  //   A = [[2,1,1],[1,3,2],[1,0,0]], b = [3, 9, 2]  -> x = [2; 1; 0]
  //   check: row0: 2*2+1*1+1*0=5? no — pick a cleaner one:
  //   A = [[1,1,1],[0,2,5],[0,0,1]], b = [6,-11,7]
  //   solve bottom-up: z=7; 2y+5*7=-11 -> y=-23; x+(-23)+7=6 -> x=22
  // (kept simpler below — a unit triangular system solving cleanly)
  let U = [[1.0, 0.0, 0.0],
           [1.0, 1.0, 0.0],
           [2.0, 1.0, 1.0]];
  let d = [[1.0], [3.0], [7.0]];
  //   z = row2: 2*1 + 1*3 + 1*z = 7  ->  z = 2  (wait, recompute below)
  // Manual substitution:
  //   row0: x = 1
  //   row1: x + y = 3  ->  y = 2
  //   row2: 2*x + 1*y + 1*z = 7  ->  2 + 2 + z = 7  ->  z = 3
  let xsol = U \ d;
  print("x=", xsol[0][0]);            // expect 1.0
  print("y=", xsol[1][0]);            // expect 2.0
  print("z=", xsol[2][0]);            // expect 3.0

  return 0;
}
