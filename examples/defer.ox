// `defer <stmt>` schedules a statement to run at the END of the enclosing
// scope, in LIFO order interleaved with RAII drops, on every exit path:
// fall-through, return, break, continue. It is Zig/Go-style cleanup that
// composes with RAII — usable for raw resources (extern handles, raw pointers)
// that have no destructor of their own.

// RAII drop type, to show defer interleaving with dtors in the right order.
struct Owning {
  id: i64,
}
impl Owning {
  fn new(id: i64) -> Owning { return Owning { id: id }; }
  fn drop(&mut self) { print("drop Owning", self.id); }
}

fn cleanup_basic() -> i64 {
  defer print("defer 1 (last scheduled, runs first at exit)");
  defer print("defer 2 (runs before defer 1)");
  print("body of cleanup_basic");
  return 0;
}

fn cleanup_on_return(x: i64) -> i64 {
  defer print("defer runs before return leaves the function");
  if x > 0 {
    print("taking early return");
    return x;
  }
  print("falling through");
  return 0;
}

fn defer_with_raii_interleave() -> i64 {
  defer print("defer A");
  let a = Owning::new(70);     // RAII local; drops at scope exit
  defer print("defer B");
  let b = Owning::new(71);
  print("defer_with_raii_interleave body");
  // At scope exit the reverse-declaration order is:
  //   b (drop), defer B, a (drop), defer A
  return 0;
}

fn defer_in_loop() -> i64 {
  let mut s = 0;
  for let mut i = 0; i < 3; i = i + 1 {
    defer print("loop defer i-body", i);
    s = s + i;
    if i == 1 {
      print("breaking at i=1");
      break;   // defer runs on the break (per-iteration scope unwind)
    }
  }
  return s;
}

fn defer_block_body() -> i64 {
  let mut acc = 0;
  // Defer may be a block; its own `let` is local to the deferred run.
  defer {
    let note = acc + 100;
    print("deferred block: note=", note);
  }
  acc = 42;
  print("before scope exit acc=", acc);   // acc is 42
  return acc;
}

fn main() -> i64 {
  print("== cleanup_basic ==");
  cleanup_basic();
  print("== cleanup_on_return(5) ==");
  cleanup_on_return(5);
  print("== defer_with_raii_interleave ==");
  defer_with_raii_interleave();
  print("== defer_in_loop ==");
  let s = defer_in_loop();
  print("defer_in_loop sum=", s);
  print("== defer_block_body ==");
  defer_block_body();
  return 0;
}
