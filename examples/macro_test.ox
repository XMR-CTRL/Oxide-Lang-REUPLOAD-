macro square(x) {
  ($x) * ($x)
}

macro max3(a, b, c) {
  let ab = imax($a, $b);
  imax(ab, $c)
}

macro cube(x) {
  ($x) * ($x) * ($x)
}

macro double_it(x) {
  ($x) + ($x)
}

fn main() -> i64 {
  let n = expand square(5);
  print(n);  // 25

  let m = expand cube(3);
  print(m);  // 27

  let d = expand double_it(21);
  print(d);  // 42

  // nested: max3内部用expand不方便(因为示例用max不存在),直接用max3 macro
  let mx = expand max3(10, 7, 30);
  print(mx);  // 30

  // macro in a larger expression context
  let total = expand square(4) + expand cube(2);
  print(total);  // 16 + 8 = 24

  return 0;
}
