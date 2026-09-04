


struct Counter {
  private count: i64;
  private stepSize: i64;
}

impl Counter {
  fn new(start: i64, stepSize: i64) -> Counter {

    return Counter { count: start, stepSize: stepSize };
  }
  fn value(&self) -> i64 { return self.count; }
  fn inc(&self) {
    self.count = self.count + self.stepSize;
  }
  fn reset(&self) { self.count = 0; }
}

fn main() -> i64 {
  let mut c: Counter = Counter::new(10, 3);


  print(c.value());
  c.inc();
  c.inc();
  print(c.value());
  c.reset();
  print(c.value());


  let z: Counter = Counter {};
  print(z.value());
  return 0;
}
