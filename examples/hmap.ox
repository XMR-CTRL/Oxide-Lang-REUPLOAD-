
// hmap[K, V]: a hash map (std::unordered_map analogue). Open-addressing
// table, O(1) average lookup/insert, iteration in INSERTION order (like a
// Python dict / ES Map — deterministic, unlike a raw hash walk). Same call
// surface as the ordered `map`, plus the `m[k]` / `m[k] = v` sugar.
fn main() -> i64 {
  let m: hmap[str, i64] = hmap[str, i64];

  // Explicit builtins (same names as the ordered map; routed by the operand
  // type to the hash-table runtime).
  map_set(m, "banana", 3);
  map_set(m, "apple", 5);
  map_set(m, "cherry", 2);
  print(m);                  // {banana: 3, apple: 5, cherry: 2}  (insertion order)
  print(map_get(m, "apple"));
  print(map_contains(m, "pear"));
  print(map_contains(m, "banana"));
  print(len(m));

  // The C++-grade `m[k]` / `m[k] = v` sugar — the natural map surface.
  m["banana"] = 30;
  m["date"] = 7;
  print(m["banana"]);
  print(m["date"]);
  print(m["missing"]);       // 0 — missing key reads as the zero value
  print(len(m));

  // Compound assignment on a map slot: read-modify-write via the runtime.
  m["count"] = 100;
  m["count"] += 5;
  m["count"] *= 2;
  print(m["count"]);         // 210

  // Replace + overwrite keep the original insertion position (date stays 4th).
  m["apple"] = 999;
  print(m);

  // Deletion preserves the relative insertion order of survivors.
  map_delete(m, "apple");
  print(m);
  print(len(m));

  // Integer keys + f64 values; iteration with `for k, v in hmap`.
  let nums: hmap[i64, f64] = hmap[i64, f64];
  nums[2] = 0.5;
  nums[10] = 0.25;
  nums[1] = 0.125;
  for k, v in nums {
    print(k, "->", v);
  }

  // `for k in hmap` walks keys (single-var form), in insertion order.
  let order: hmap[i64, str] = hmap[i64, str];
  order[30] = "thirty";
  order[10] = "ten";
  order[20] = "twenty";
  for k in order {
    print(k);
  }

  // Many insertions exercise rehashing (load factor 0.75, power-of-two growth).
  let big: hmap[i64, i64] = hmap[i64, i64];
  for n in 0..200 {
    big[n * 7 % 1000] = n;
  }
  print(len(big));
  print(big[ (5 * 7) % 1000 ]);
  return 0;
}
