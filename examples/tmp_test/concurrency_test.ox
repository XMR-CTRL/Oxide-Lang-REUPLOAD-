fn worker(n: i64) -> i64 {
  return n * n;
}

fn main() -> i64 {
  let ch = Channel<i64>::new();
  spawn worker(5);
  let val = <- ch;
  sync {
    print(val);
  }
  return 0;
}
