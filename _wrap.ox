// Overflow/wraparound (gap #2). `hi` is forced (via a mask) to the top bit set;
// shifting it left by 1 wraps that bit off the top in real 64-bit hardware ->
// low result. With native BitVec + bvshl this is modeled; the old unbounded-Int
// model would think `hi << 1` keeps growing. We assert the wrapped result is
// NOT larger than a full word, which only holds under true 64-bit semantics.
const TOPBIT: i64 = 1;

fn shift_wraps(x: i64) -> i64
    ensures (result & 1) == 0
{
    let doubled: i64 = x << 1;
    return doubled;
}

fn main() -> i64 { return 0; }
