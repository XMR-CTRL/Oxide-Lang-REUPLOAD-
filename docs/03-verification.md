> **OXIDE** · Contracts and Verification
> The two enforcement paths, what the SMT encoding models, and an honest ledger of what is proven and what is not.

# Contracts and Verification

Oxide has a built in specification language for stating what a program must satisfy. Two enforcement paths exist side by side and they serve different purposes.

**Runtime gates** are the default. The compiler inserts a trap instruction at every contract point so a violated clause aborts the program with a message naming the clause and the line. Zero overhead when nothing violates anything because the check is a single comparison and a branch never taken on the happy path.

**Static discharge** is opt in. The same clauses are emitted as SMT-LIB and an external solver is invited to prove them. This is why `verify` exists as a separate command.

A program with no contracts is unchanged. Contracts are opt in and they do not slow down code that does not use them.

## Contract clauses

Four clause kinds.

| Clause | Attaches to | Checked at |
|--------|-------------|------------|
| `requires` | a function | entry right after parameter binding |
| `ensures` | a function | every exit just before `ret` |
| `invariant` | a `while` or `for` | the loop head on every iteration |
| `assert` | any statement | in place at that point |

`ensures` may name `result` as the returned value and `old(x)` for the entry snapshot of a variable.

```oxide
fn growByOne(x: i64) -> i64
  ensures result > old(x)
{
  let mut v = x;
  v = v + 1;
  return v;
}
```

`old(x)` is snapshotted once at function entry before any statement runs. The snapshot lives in the frame and the `old` reference reaches back to it.

## Runtime enforcement

The runtime path emits a check per clause.

```oxide
fn f(n: i64) -> i64
  requires n >= 0
{
  return n;
}
```

compiles to roughly

```
; pseudo IR
entry:
  %ok = icmp sge i64 %n, 0
  br i1 %ok, label %continue, label %violation
violation:
  call void @ox_contract_fail(i32 1, i32 <line>)
  unreachable
```

`@ox_contract_fail(kind, line)` is a runtime function in the embedded C runtime. It prints the clause kind and line and calls `abort()`. The tag values are stable across versions so external tooling can decode them `(requires=1 ensures=2 invariant=3 assert=0)`.

In freestanding mode the embedded runtime is not linked. A freestanding program that uses contracts must provide `ox_contract_fail` itself. This is a trap handler not a printing function so the typical implementation is just `cli; hlt;` or a serial write of the line number. The point is that the contract is still enforced even on bare metal it is enforced by trapping not by silently passing.

## Static discharge

`--emit-smt PATH` writes every contract in the program as SMT-LIB.

```
(assert (not <clause-term>))
(check-sat)
```

If the solver returns `unsat` the clause is proven. If it returns `sat` the clause is falsifiable. If it returns `unknown` the solver timed out or the encoding fell outside a decidable fragment.

`verify` runs the end to end pipeline.

```
oxide verify --verify-only --solver-timeout 15 examples/contract_fib.ox
```

On the current build that produces

```
// actual output, verified 2026-09-02
4 proven, 0 undischarged, 1 assumed
```

The repository's quality gate is `python verify_quality.py build/oxide-hardened.exe` and it checks the same contract suites against positive and negative controls.

## The SMT encoding

The mapping from Oxide types to SMT sorts is deliberately simple.

| Oxide | SMT |
|-------|-----|
| `bool` | `Bool` |
| all integers | `Int` (unbounded) |
| `f32` `f64` | `Real` |
| arrays | not modelled become fresh uninterpreted constants |
| struct fields | not modelled same |
| calls | not modelled same |
| casts | not modelled same |

This last point matters. If you write `assert arr[i] > 0` the SMT term for `arr[i]` is a fresh unknown constant. The solver will say `sat` because nothing constrains it. And that is honest. The verifier only claims what it can model and it labels everything it cannot with `; note:` comments in the `.smt2`.

Integers are unbounded `Int`. Oxide's `i64` arithmetic is defined as wrapping not modulo. In verifiable code you get the unbounded model and that matches no overflow checking. Bitwise operations cross via a `(_ BitVec 64)` bridge only where needed.

### Why model unbounded Int and not modulo N

Modeling `i64` as `(_ BitVec 64)` makes the solver handle overflow proofs which are hard and most programs do not need. Modeling as `Int` is easier for the solver and matches the reasoning style of programmers who do not think about overflow when they reason about correctness. The tradeoff is that a proof about `x + 1 > x` holds only within the no overflow regime. For kernel code where overflow is a design constraint not an accident that is the right tradeoff. Sat results for bitwise reasoning come back through the bridge only when the program uses them.

## Quantifiers

Spec expressions add `forall` and `exists` over an integer range with `implies` separating the range from the body.

```oxide
forall k: i64 in 0..n implies arr[k] >= 0     // every element non negative
exists k: i64 in 0..=n implies arr[k] == 0    // some element is zero
```

`A implies B` is only legal inside a contract clause. Outside of one it is not an operator. This restriction exists because `implies` requires proof context and the parser keeps the spec grammar separate from the code grammar.

## Ghost machinery

Ghost code is code that is type checked but generates no IR. It exists for the solver.

```oxide
ghost let x = 5;
ghost fn helper(n: i64) -> i64 { return n + 1; }
spec fn double(n: i64) -> i64 = n * 2;
```

`spec fn` is an uninterpreted SMT symbol that is inlined into the encoding when referenced. `ghost let` and `ghost fn` are the same but allow arbitrary bodies.

Lemmas exist as `lemma fn` with a `forall` body. The body is not executed at runtime. The solver emits it as an axiom.

```oxide
lemma fn add_commutes(a: i64, b: i64)
  ensures a + b == b + a
{ }
```

`proof { ... }` blocks group lemma applications.

```oxide
proof {
  add_commutes(5, 10);
}
```

`refines` is a statement of an implementation meeting a spec.

```oxide
refines concrete_sort <= abstract_sort;
```

`decreases` provides a termination measure for recursive functions.

```oxide
fn fact(n: i64) -> i64
  decreases n
{
  if n == 0 { return 1; }
  return n * fact(n - 1);
}
```

`noninterference` asks the prover to verify an Owicki-Gries style non-interference property for concurrent code. This is a theorem prover feature not a runtime feature.

`cycle_preserves` is a specialized non-interference check for loop bodies.

## What is actually proven

Here is the honest short version.

**Proven on this build**
- contract_fib.ox: 4 proven 0 undischarged 1 assumed
- Requires/ensures/invariant/assert in the positive suite all verify
- Negative controls all correctly fail
- The `verify_quality.py` gate passes 5/5

**Not yet proven or partial**
- The full hypervisor from `hv/` verifies slice by slice but not as a whole program yet. The pieces verify. The composition does not.
- EPT verification is limited to small page counts currently 4 not the full 512 entry tree.
- Concurrent program logic is sequential underneath. `noninterference` queries exist but the memory model is a single total order so they verify sequential programs and admit only that.
- Array and struct contents are not yet modelled. Quantifiers over array contents yield `unknown`.

**What is coming**
- Bitwise reasoning over arrays
- Array and struct content reasoning via a memory model
- Real concurrent semantics once `spawn` is wired

## Verify quality gate

```
python verify_quality.py build/oxide-hardened.exe
```

The gate runs the positive contract suites from `verification_tests.json` and confirms the invalid induction proof is still rejected. A passing gate means no proof construct was silently emptied and no invalid proof was accidentally accepted.

## Workflow

For day to day coding write contracts as you write code. Enable `--emit-smt` and keep the resulting `.smt2` files as artifacts. Review them when something fails.

For a release try `oxide verify` on the target. If the proven count is high and the assumed count is zero you have more than most compiled languages will give you. If the undischarged count is non zero look at the specific obligations. They are usually array or call sites that exceeded the encoding.
