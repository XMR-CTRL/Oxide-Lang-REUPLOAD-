fn sumTo(n: i64) -> i64
    requires n >= 0
    ensures result == (n * (n + 1)) / 2
{
    let mut s: i64 = 0;
    let mut i: i64 = 0;
    while i < n
        invariant s == (i * (i + 1)) / 2
        invariant 0 <= i && i <= n
    {
        s = s + i + 1;
        i = i + 1;
    }
    return s;
}

fn proof_demo() -> i64
    ensures result == 0
{
    proof that forall k: i64 in 0..100 implies k + 1 > k
    by induction on k:
        base: 0 + 1 > 0
        step: assume k + 1 > k
              prove (k + 1) + 1 > (k + 1);
    return 0;
}

fn main() -> i64 {
    return 0;
}
