// _redecl_test.ox — regression test for the same-scope redeclaration fix.
//
// What this file exercises (run: ./build/oxide.exe emit _redecl_test.ox):
//   (1) SAME-SCOPE redeclaration  -> compile ERROR:
//         "redeclaration of 'x' in the same scope"
//   (2) NESTED-SCOPE shadow       -> OK (shadowing is permitted)
//   (3) GHOST let predeclare+redeclare -> OK (intentional re-declaration;
//         predeclareGhostLetsStmt and the body walker write the same slot)
//
// Expected outcome: exactly ONE `error[sema]: redeclaration of 'x' in the same
// scope` (pointing at the second `let x` in case (1), line ~35), and no other
// errors. The build stays sound: the diagnosed redeclaration still overwrites
// the binding (the check reports once and falls through — no error cascade),
// so any downstream code in case (1) sees the new binding as before.

// ----------------------------------------------------------------------------
// (2) NESTED-SCOPE SHADOW — must compile (no redeclaration error).
// A `{ }` block is a fresh `pushScope()`, so the inner `let x` is a legitimate
// shadow, not a same-scope redeclaration.
// ----------------------------------------------------------------------------
fn nested_shadow(initial: i64) -> i64 {
  let x = initial;
  {
    let x = x + 1;          // OK: new scope shadows the outer `x`
    if x != initial + 1 { return 0; }
  }
  return x;                // outer `x` untouched
}

// ----------------------------------------------------------------------------
// (3) GHOST LET — must compile (no redeclaration error).
// `predeclareGhostLetsStmt` pre-declares `g` into the fn param scope so the
// `ensures` clause can name it; the body walker then re-declares the same
// `g` (same name, same type, same scope-slot). That overlap is INTENTIONAL
// and is explicitly allowed via the `allowRedecl=true` declare() path. The
// `ensures result == g` proves the ghost name is visible to the contract.
// ----------------------------------------------------------------------------
fn with_ghost(g_in: i64) -> i64
  ensures result == g
{
  ghost let mut g: i64;
  g = g_in;
  return g_in;
}

// ----------------------------------------------------------------------------
// (1) SAME-SCOPE REDECLARATION — must error.
// Two `let x` in the SAME function-body scope. This is the bug the fix turns
// into a compile error: a silent overwrite here would break drop order,
// ownership/borrow tracking, ghost-variable meaning, and SMT variable mapping.
// ----------------------------------------------------------------------------
fn same_scope_redecl() -> i64 {
  let x = 1;
  let x = 2;               // ERROR: redeclaration of 'x' in the same scope
  return x;
}

fn main() -> i64 {
  let a: i64 = nested_shadow(7);
  let b: i64 = with_ghost(7);
  if a != 7 { return 0; }
  if b != 7 { return 0; }
  let s: i64 = same_scope_redecl();
  return 42;
}
