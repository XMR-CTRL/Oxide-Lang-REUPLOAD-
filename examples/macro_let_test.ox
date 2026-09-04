// Focused test for the macro let-bindings bug fix.
// max3 uses a leading `let ab = imax($a, $b)` binding that must be declared in
// scope before the trailing result `imax(ab, $c)` is type-checked and codegen'd.

macro max3(a, b, c) {
  let ab = imax($a, $b);
  imax(ab, $c)
}

// Two lets chained: the second refers to the first.
macro max4(a, b, c, d) {
  let ab = imax($a, $b);
  let cd = imax($c, $d);
  imax(ab, cd)
}

// A let that re-uses a parameter twice in its init.
macro sumsqadd(a, b) {
  let ab = ($a) + ($b);
  (ab) * (ab)
}

fn main() -> i64 {
  // max3: imax(imax(10,7),30) = imax(10,30) = 30
  let mx3 = expand max3(10, 7, 30);
  print(mx3);            // expect 30

  // max3 with negatives: imax(imax(-5, 3), -2) = imax(3, -2) = 3
  let mx3n = expand max3(-5, 3, -2);
  print(mx3n);           // expect 3

  // max4: imax(imax(1,9), imax(4,7)) = imax(9,7) = 9
  let mx4 = expand max4(1, 9, 4, 7);
  print(mx4);            // expect 9

  // sumsqadd: ((3)+(4)) * ((3)+(4)) = 7*7 = 49
  let ss = expand sumsqadd(3, 4);
  print(ss);             // expect 49

  // max3 inside a larger expression: max3(2,8,5)=8, plus 1 = 9
  let e = expand max3(2, 8, 5) + 1;
  print(e);              // expect 9

  return 0;
}
