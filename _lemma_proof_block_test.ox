lemma fn add_one_gt(x: i64) -> i64
    requires x >= 0
    ensures x + 1 > x
{
    return 0;
}

fn uses_lemma(x: i64) -> i64
    requires x >= 0
    ensures result == 0
{
    proof {
        add_one_gt(x);
        assert x + 1 > x;
    }
    return 0;
}

fn main() -> i64 {
    return 0;
}
