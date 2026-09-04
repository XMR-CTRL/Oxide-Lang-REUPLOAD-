// Single inheritance (non-virtual). Base fields laid out first; inherited
// methods callable on a derived receiver; a derived value also usable as a
// Base (upcast). Virtual dispatch is in inheritance_virtual.ox.

struct Animal {
  name: i64,     // stand-in "name" id
}

impl Animal {
  fn new(n: i64) -> Animal { return Animal { name: n }; }
  fn legs(&self) -> i64 { return 4; }   // inherited unchanged
  fn speak(&self) -> i64 { return 0; }  // shadowed by derived below (non-virtual)
}

struct Dog: Animal {
  breed: i64,
}

impl Dog {
  fn new(n: i64, b: i64) -> Dog {
    return Dog { name: n, breed: b };   // base field `name` + own field `breed`
  }
  fn speak(&self) -> i64 { return 7; } // shadow the base method (static dispatch)
}

fn describe(a: &Animal) -> i64 {
  // Takes a Base; an inherited base method is reachable through the base type.
  return a.legs();
}

fn main() {
  let d = Dog::new(1, 2);
  print("d.name=", d.name);          // base field access through derived
  print("d.breed=", d.breed);
  print("d.legs()=", d.legs());      // inherited method (no override) -> 4
  print("d.speak()=", d.speak());     // Dog's own speak (static) -> 7

  // Upcast: pass &Dog where &Animal is expected (base sub-object at offset 0).
  print("describe(d)=", describe(&d)); // legs via base -> 4
}
