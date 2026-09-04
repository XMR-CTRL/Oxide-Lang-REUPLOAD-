// Native map iteration: `for k in map` binds the key, `for k, v in map` binds
// both key and value. This replaces the `map_keys` + `map_get` ceremony for a
// read-only walk — the compiler lowers it to the per-entry @ox_map_key_ptr +
// @ox_map_get runtime API directly, no temporary keys vec allocated.

fn main() -> i64 {
  let mut m: map[str, i64] = map[str, i64];
  map_set(m, "banana", 3);
  map_set(m, "apple", 5);
  map_set(m, "cherry", 2);

  print("--- keys only ---");
  for k in m { print("key:", k); }

  print("--- key + value ---");
  for name, count in m {
    print(name, "->", count);
  }

  // an integer-keyed map
  let mut idx: map[i64, str] = map[i64, str];
  map_set(idx, 1, "one");
  map_set(idx, 2, "two");
  map_set(idx, 3, "three");
  let mut total = 0;
  for n, word in idx { total = total + n; print(n, "=", word); }
  print("sum keys:", total);

  // sum only the values (key bound but unused is fine)
  let mut sum = 0;
  for k, c in m { sum = sum + c; }
  print("total count:", sum);
  return 0;
}
