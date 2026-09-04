// Virtual dispatch (vtables). Base declares `virtual fn`;
// `struct Derived: Base { ... override fn ... }` replaces the slot. A
// polymorphic call through a `&Base` receiver resolves to the most-derived
// implementation — the whole point of C++ virtual. Compare with
// inheritance.ox (non-virtual, static dispatch shadows).

struct Animal {
  name: i64,        // stand-in "name" id
}

impl Animal {
  fn new(n: i64) -> Animal { return Animal { name: n }; }
  virtual fn speak(&self) -> i64 { return 0; }   // default: silence
  fn name_of(&self) -> i64 { return self.name; } // non-virtual (static)
}

struct Dog: Animal {
  breed: i64,
}

impl Dog {
  fn new(n: i64, b: i64) -> Dog {
    return Dog { name: n, breed: b };
  }
  override fn speak(&self) -> i64 { return 7; } // DOGS BARK
}

struct Cat: Animal {}

impl Cat {
  fn new(n: i64) -> Cat { return Cat { name: n }; }
  override fn speak(&self) -> i64 { return 9; } // CATS MEOW
}

// Polymorphic helper: takes a BASE reference. `speak` is virtual, so it
// dispatches to the concrete object's override at runtime.
fn chorus(a: &Animal) -> i64 {
  return a.speak();
}

fn main() {
  let d = Dog::new(1, 2);
  let c = Cat::new(3);
  let a = Animal::new(4);   // a base instance — uses the default virtual

  print("d.speak()=", d.speak());   // 7  (Dog override)
  print("c.speak()=", c.speak());   // 9  (Cat override)
  print("a.speak()=", a.speak());   // 0  (Animal default)

  // Calls through &Base — the polymorphic proof.
  print("chorus(d)=", chorus(&d));   // 7
  print("chorus(c)=", chorus(&c));   // 9
  print("chorus(a)=", chorus(&a));   // 0

  // A non-virtual base method is NOT dispatched: always the base impl, even
  // on a derived receiver (contrast with `speak` above).
  print("d.name_of()=", d.name_of());   // 1 (inherited, static)

  // Base fields + own fields both accessible on the derived.
  print("d.name=", d.name);
  print("d.breed=", d.breed);
}
