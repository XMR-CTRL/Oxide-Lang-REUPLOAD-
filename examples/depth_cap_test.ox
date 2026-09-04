// depth_cap_test.ox — MAX_INLINE_DEPTH cap for non-recursive call chains.
//
// smtConcreteCallResult (Driver.cpp ~line 2945) defines MAX_INLINE_DEPTH = 4.
// When the inline stack reaches that depth, a NON-recursive callee switches
// to the assume-ensures arm instead of inlining its body. This bounds SMT
// size on deep non-recursive chains (handler -> dispatcher -> walk -> map
// -> store) which would otherwise inline exponentially.
//
// Chain: a -> b -> c -> d -> e -> f   (depth 6, cap kicks in at the e->f call).
// Top-level verification of `a` walks the whole chain. When `e`'s body calls
// `f`, inlineStack == [b,c,d,e] (size 4), so the depth cap fires and `f`'s
// ensures is ASSUMED (not inlined). The assumption is sound: `f(x)` promises
// `result == x`, so assuming it lets `e`'s `ensures result == x+1` discharge.
//
// Expected in the emitted SMT (--emit-smt):
//   ; note: callee 'f' hit max inline depth (4) — assuming ensures (recursive
//          or max depth) at call site instead of inlining body
// and each discharge should be `unsat` (proven).
//
// Run:
//   oxide.exe verify --emit-smt examples/depth_cap_test.smt2 examples/depth_cap_test.ox
//   grep "max inline depth" examples/depth_cap_test.smt2     # should show 1+ hit
//   grep "sat"        examples/depth_cap_test.smt2          # all unsat = proven

fn f(x: i64) -> i64
    requires x >= 0
    ensures result == x
{
    return x;
}

fn e(x: i64) -> i64
    requires x >= 0
    ensures result == x + 1
{
    return f(x + 1);
}

fn d(x: i64) -> i64
    requires x >= 0
    ensures result == x + 2
{
    return e(x + 1);
}

fn c(x: i64) -> i64
    requires x >= 0
    ensures result == x + 3
{
    return d(x + 1);
}

fn b(x: i64) -> i64
    requires x >= 0
    ensures result == x + 4
{
    return c(x + 1);
}

fn a(x: i64) -> i64
    requires x >= 0
    ensures result == x + 5
{
    return b(x + 1);
}

fn main() -> i64 {
    let r: i64 = a(0);
    print("a(0)=", r);  // 5
    return 0;
}
