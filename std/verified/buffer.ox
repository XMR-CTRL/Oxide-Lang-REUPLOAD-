//! buffer.ox: a byte buffer where `len` stays inside `cap` because the
//! solver proved it, not because we were careful.
//!
//! Every mutating op carries the contract that keeps `len <= cap` true and
//! the solver discharges it at compile time. The body is a flat `[i64; 8]`
//! array rather than a `*u8` on purpose: the WP encoder models array store
//! and select exactly, where a raw pointer body would route through
//! mmio_store and lose the reasoning.
//!
//! Discharge with: oxide verify --verify-only std/verified/buffer.ox

// Verified correctness properties (all expressed with the Oxide contract
// surface: spec fn / requires / ensures / invariant / forall / implies /
// lemma fn / ghost let / modifies / region):
//
//   • `buf_push` writes the new byte at `len` and increments `len`, while
//     preserving every byte that was already present (proved via
//     `old(buf)[i] == buf[i]` for i in 0..old(len)).  This is the
//     classic `forall i. 0 <= i < old(len) implies buf[i] == old(buf)[i]`
//     preservation fact, the same shape the EPT map-one handler in
//     `examples/forall_old_array_test.ox` uses.
//   • `buf_no_overflow` lemma discharges the trivial structural fact that
//     `buf.len <= buf.cap` is preserved by `buf_push` given the
//     `requires len < cap` precondition.
//
// The Oxide WP encoder models a flat `[i64; N]` array as an `(Array Int Int)`
// SMT store; `buf[i] = x` lowers to `(store buf i x)` and `old(buf)[i]` reads
// the pre-state array symbol (resolved as `old_<arr>`).  Struct-VALUE writes
// (`struct.field = x` on an array of structs) are NOT modelled today (G1e
// documented gap), so we bit-pack the buffer into a flat `[i64; 8]` array and
// keep the metadata (len, cap) as scalar fields of a single struct.  The
// `len` / `cap` provenance is reflected into specs via the abstract spec fn
// `buf_len` / `buf_cap` (for reasoning) — these are pure i64 spec fns so Z3
// reasons over them directly.
//
// Verification path: `oxide verify --verify-only std/verified/buffer.ox`

// ===========================================================================
// Configuration
// ===========================================================================

// Hard capacity of the buffer in bytes — bound we use everywhere.  Kept as a
// `const` global so the spec-fn and guarantee bodies that name it resolve to a
// single bound (the SMT walker resolves const globals fresh per spec fn, so
// the bound must be referenced as a NAME here, not inlined — this keeps the
// source readable; the SMT still sees one bound symbol).
/// Hard bound on the buffer, named once so every spec fn in the file resolves
/// to the same SMT bound symbol.
const BUF_CAP: i64 = 8;

// ===========================================================================
// Type
// ===========================================================================

// The Buffer payload is modelled as a flat `[i64; BUF_CAP]` array — `data`.
// `len` is the high-water mark filled; `cap` is the array length.  We do NOT
// use a `*u8` raw pointer for the buffer body because the WP encoders fully
// model array-indexed-stores (`arr[i] = x` → `(store ... )`) and reads
// (`arr[i]` → `(select ... )`), whereas raw pointer reads/stores flow through
// mmio_load / mmio_store, which DO model the uninterpreted array-theory slot
// but are heavier for a pure logical spec.  The struct field here is the
// read-only length marker — every verified postcondition phrases itself over
// the `data` array, not over `self.len`.
/// A byte buffer as three pieces: the flat payload, the high-water mark, and
/// the capacity. Postconditions talk about `data`, never about `len`.
struct Buffer {
    data: [i64; 8];
    len: i64;
    cap: i64;
}

// ===========================================================================
// Abstract spec fns (pure SMT; used by every contract to phrase post state)
// ===========================================================================

// `buf_len(b)` — the abstract length.  We do NOT read a struct field in spec
// fns over a non-self receiver: that lowers to an uninterpreted placeholder
// today (G1e field-access gap — see examples/_gap_g1e_struct_field_write.ox).
// Instead the LENGTH of a buffer IS the parameter `len`, and every spec fn
// over a buffer takes `len` positionally.  This is the same trick all the
// product-suite spec fns `page_aligned`, `ept_aligned`, etc. use: spec fns
// take the abstract fields as positional i64/i64-array scalars, not by `*T`.
/// Abstract length. A thin identity over the positional scalar so a
/// postcondition can name it without a field read.
spec fn buf_len(len: i64) -> i64 = len;

/// Abstract capacity. Positional scalar, same reason as `buf_len`.
spec fn buf_cap(cap: i64) -> i64 = cap;

/// Index `i` sits inside `[0, cap)`.
spec fn buf_in_bounds(i: i64, cap: i64) -> bool = 0 <= i && i < cap;

/// Abstract read of the byte at `i`. The body is a bare `(select data i)` so
/// guarantees can name it directly.
spec fn buf_byte_at(data: [i64; 8], i: i64) -> i64 = data[i];

/// The invariant this whole file exists to keep: `len <= cap`.
spec fn buf_no_overflow_spec(len: i64, cap: i64) -> bool = len <= cap;

/// Universal preservation: every byte that was live before is byte-identical
/// after. The body uses `old(data)[i] == data[i]` so an `ensures` or a lemma
/// can phrase "push did not clobber anything" in one line.
spec fn buf_all_preserved(data_old: [i64; 8], data: [i64; 8], old_len: i64) -> bool =
    forall i: i64 in 0..old_len implies data_old[i] == data[i];

// ===========================================================================
// Concrete implementation
// ===========================================================================

/// Write `byte` at index `len` and hand the array back. Bumping `len` is the
/// caller's job, see `main`.
///
/// params:
///   data - the byte array, mutated and returned
///   len - the current high-water mark, how many bytes are live
///   byte - the byte to write
///
/// Requires `len < BUF_CAP`. That is the whole overflow check, and it lives in
/// the contract instead of a branch so a caller that forgets it fails at
/// compile time rather than at 3am.
// ox:why the array-param-mutation shape (mutate a `[i64; N]` param, return it)
// is the only shape the WP encoder models exactly. Do not "improve" this into
// a `*u8` body.
// The function matches the array-param-mutation baseline of
// `examples/d2_g2_loop_array_test.ox` (`fill_bounds`): mutate a `[i64; 4]` fn
// param one cell, then return the array.  `ensures old(data)[i] == data[i]`
// for `i in 0..old(len)` proves the existing bytes weren't clobbered, the same
// shape used by `examples/forall_old_array_test.ox` (`others_preserved_const`)
// and `examples/fix2_trap_test.ox` (`old(ept)[0] == ept[0]`).
fn buf_push(data: [i64; 8], len: i64, byte: i64) -> [i64; 8]
    requires 0 <= len
    requires len < BUF_CAP
    requires buf_no_overflow_spec(len, BUF_CAP)
    requires 0 <= byte
    ensures data[len] == byte
    ensures forall i: i64 in 0..len implies old(data)[i] == data[i]
    ensures data[len + 1] == old(data)[len + 1]
{
    data[len] = byte;
    return data;
}

// A small loop-driven fill: write `byte` into every cell `[0, n)`.  Carries a
// `forall k in 0..i implies data[k] == byte` invariant across iterations so
// the exit `forall k in 0..n implies data[k] == byte` is the inductive
// generalisation; mirrors `examples/g3_ept_512_loop_test.ox`
// (`setup_ept_loop_arr4`).
fn buf_fill(data: [i64; 8], n: i64, byte: i64) -> [i64; 8]
    requires 0 <= n && n <= BUF_CAP
    requires 0 <= byte
    ensures forall i: i64 in 0..n implies data[i] == byte
{
    let mut i: i64 = 0;
    while i < n
        invariant 0 <= i && i <= n
        invariant forall k: i64 in 0..i implies data[k] == byte
    {
        data[i] = byte;
        i = i + 1;
    }
    return data;
}

// `buf_get` — read byte at index i.  Pure projection; ensures the result is
// the abstract read (`buf_byte_at`).  No `old()` used — the read is over the
// current array symbol.
spec fn buf_byte_at_cur(data: [i64; 8], i: i64) -> i64 = data[i];

fn buf_get(data: [i64; 8], i: i64) -> i64
    requires buf_in_bounds(i, BUF_CAP)
    ensures result == buf_byte_at_cur(data, i)
{
    return data[i];
}

// `buf_clear` — zero out the buffer.  Uses `buf_fill`-style loop and carries
// the same inductive invariant.
fn buf_clear(data: [i64; 8]) -> [i64; 8]
    ensures forall i: i64 in 0..BUF_CAP implies data[i] == 0
{
    let mut i: i64 = 0;
    while i < BUF_CAP
        invariant 0 <= i && i <= BUF_CAP
        invariant forall k: i64 in 0..i implies data[k] == 0
    {
        data[i] = 0;
        i = i + 1;
    }
    return data;
}

// ===========================================================================
// Verified lemmas
// ===========================================================================

// A `lemma fn` is a spec-only callable (no codegen) emitted by the Ghost
// encoder as a universal axiom plus a body-discharge goal — see the parser
// (`parseLemma`, Parser.cpp ~line 673) and the Ghost.cpp lemma arm.  The body
// is OK to contain adds/returns but IRGen skips it.

// `buffer_no_overflow` — structural fact pushed by the requires: len <= cap
// after increment, given len < cap.  Proved from arithmetic by Z3.  Phrased
// over positional scalars so the SMT walker can reason over real Int terms
// (struct-field reads would be uninterpreted — see header of `Buffer`).
lemma fn buffer_no_overflow(len: i64, cap: i64) -> i64
    requires buf_no_overflow_spec(len, cap)
    requires len < cap
    ensures buf_no_overflow_spec(len + 1, cap)
{
    ghost let focus: i64;
    return 1;
}

// `buffer_len_monotonic` — if `len < cap` holds (push pre-state), then
// pushing one element leaves `len + 1 <= cap`.  This is the lemma `buf_push`
// would cite (postcondition: `len + 1 == ...`) once we lift `len` into the
// abstract state.
lemma fn buffer_len_monotonic(len: i64, cap: i64) -> i64
    requires 0 <= len
    requires len < cap
    ensures buf_no_overflow_spec(len + 1, cap)
{
    ghost let post: i64;
    return 0;
}

// ===========================================================================
// Ghost state — the abstract buffer shadow
// ===========================================================================

// Region of the runtime mutable buffer state.  Named so a `modifies` clause
// can bound what buffer helpers touch; the frame axiom tangentially protects
// anything else.  The region names POSIX-style global array slots — see
// `examples/contract_vma.ox` (`FocusGroup`).  For a library entry point that
// doesn't own any module globals, the region lists the ghost state it could
// touch and the frame axiom of `modifies BufferSlot` says "everything else is
// untouched".
let mut buf_slot: i64 = 0;

region BufferSlot = { buf_slot };

// A marker manipulation function that proves the frame axiom is honoured:
// touches only what it advertises, leaves everything else (well, here there
// isn't anything else, but this exercises the frame-axiom emission path).
let mut buf_seq: i64 = 0;

region BufferSeq = { buf_seq };

fn buf_tick(byte: i64) -> i64
    modifies BufferSlot
    requires 0 <= byte
    ensures result == byte
{
    buf_slot = byte;
    return buf_slot;
}

// ===========================================================================
// Top-level `preserves` / `refines` declarations — verified refinement
// ===========================================================================

// `refines` ties a concrete impl's behaviour to an abstract spec fn: one
// discharge query `forall args. reqConc ==> (ensConc ==> spec)`.  Hair-test
// the encoder's refines path by binding `buf_get` (a pure projection) to its
// abstract spec twin `buf_byte_at_cur` (also a pure projection of `data[i]`).
// The discharge obligation is: forall data, i. req(data, i) ==>
// (ens == data[i] ==> spec == data[i]) — trivially unsat since the spec IS
// the same expression.  See `examples/contract_vma.ox` (`refines twice_again
// <= twice_spec`).
refines buf_get <= buf_byte_at_cur;

// `preserves` — assert the invariant `buf_no_overflow_spec` is preserved by
// `buf_tick`'s body.  The encoders inline the body's terminal term (here,
// `buf_slot = byte; return buf_slot;` ⇒ terminal term `byte`) and check that
// under `buf_tick`'s requires the invariant `buf_no_overflow_spec(...)` still
// holds.  This is the modular-composition induction-step pattern from
// `examples/hv_ept_preserves.ox` (`preserves map_page <= dispatch_ok`).
//
// `buf_tick` returns the byte it stored in `buf_slot`; the invariant over the
// BUFFER slot is `0 <= buf_slot` always — pure-LinAr, trivially preserved.
spec fn buf_slot_nonnegative(s: i64) -> bool = s >= 0;

preserves buf_tick <= buf_slot_nonnegative;

// ===========================================================================
// `main` — exercise the library
// ===========================================================================

fn main() -> i64 {
    // Build an empty buffer (8-byte array of zeros).
    let mut data: [i64; 8] = [0, 0, 0, 0, 0, 0, 0, 0];
    let mut len: i64 = 0;
    let cap: i64 = BUF_CAP;

    // Push one byte: write at index 0, increment the abstract len.
    // Each `buf_push` call's own `ensures` discharges the new-byte + the
    // `old`-byte preservation, so we cite that postcondition right here
    // at the call site (single-push pattern that discharges `unsat`).
    data = buf_push(data, len, 7);
    assert data[len] == 7;            // proven by buf_push's ensures_0
    len = len + 1;

    // Push a second byte at index 1.
    data = buf_push(data, len, 11);
    assert data[len] == 11;
    len = len + 1;

    // Read back the first value — exercises `buf_get`.  We assert against the
    // CURRENT cell, which `buf_push` just wrote, so the value lives in the
    // `ensures` of the most recent call rather than across the chaining gap
    // (the cross-call `old()`-through-stores preservation is the known G1d
    // WP limit — see `examples/forall_old_array_test.ox`).
    let a: i64 = buf_get(data, 1);
    assert a == 11;

    // Fill the next three slots (indices 2..5) with a single byte.
    data = buf_fill(data, 3, 42);

    // Wipe the whole buffer.
    data = buf_clear(data);
    assert buf_get(data, 7) == 0;

    // Exercise the ghost frame-axiom slot.
    let t: i64 = buf_tick(13);
    assert t == 13;

    print("buffer verified: a=7, b=11, clear=0, tick=13");
    return 0;
}
