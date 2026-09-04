#pragma once

#include <string>
#include <vector>
#include <cstdint>

enum class Tok {
  end,

  ident,
  int_lit,
  float_lit,
  str_lit,
  char_lit,

  kw_let,
  kw_mut,
  kw_fn,
  kw_return,
  kw_if,
  kw_else,
  kw_while,
  kw_for,
  kw_break,
  kw_continue,
  kw_in,
  kw_true,
  kw_false,
  kw_as,
  kw_const,
  kw_enum,
  kw_match,
  kw_null,
  kw_extern,
  kw_export,   // `export fn name(...) -> Ret { body }`  -  make an Oxide fn
               // callable from C (emits `define dso_local` instead of `define`
               // in LLVM IR; no `declare` stub). Reverse of `extern fn`.
  kw_typedef,
  kw_impl,
  kw_self,
  kw_private,
  kw_sizeof,
  kw_asm,
  kw_virtual,
  kw_override,
  kw_defer,    // `defer <stmt>`  -  run a statement at scope exit, LIFO with drops
               // (Zig/Go-style). Composes with RAII drops for raw resources.
  // D7  -  `atomic { <stmts> }` block: marks a region as a single non-interleavable
  // step for future concurrency proofs. Semantically identical to a plain block
  // (sequential WP), but emitted with an SMT note so the witness output records
  // the atomicity claim. Pure documentation/hook for now  -  the WP encoding is
  // the same as a `{ ... }` block; the `atomic` keyword is reserved.
  kw_atomic,
  // `unsafe { <stmts> }`  -  block scope where unsafe operations (raw pointer
  // deref, inline asm, extern calls, volatile MMIO, unchecked casts, calls to
  // `unsafe fn`) are permitted. Outside an `unsafe` block these are compile
  // errors. Also `unsafe fn name(...) { body }`  -  a function whose body is an
  // implicit unsafe scope, callable only from another unsafe context.
  kw_unsafe,

  // --- Concurrency primitives (spawn / sync / Channel<T>) ---
  // `spawn { <body> }` or `spawn <expr>` creates a new thread running the
  // body/block expression and returns a join handle. `sync { <stmts> }` is a
  // synchronisation barrier: wraps the body in @ox_sync_begin / @ox_sync_end
  // runtime calls so threads rendezvous at the block boundary. `Channel<T>` is a
  // buffered message-passing channel; `chan <- val` sends and `<- chan`
  // receives (the channel-arrow token `<-` is shared by both forms; position
  // determines whether it is a send (LHS then arrow then RHS) or a recv (arrow
  // then operand)  -  see Parser::parseChannelOps and parseUnary).
  // `kw_chan` is the `Channel` type keyword; `kw_send` is the send-stance
  // marker (lexed as `<-` so both directions share one token), and `kw_recv`
  // is reserved for a keyword-style `chan.recv()` sugar (currently unused  - 
  // the `<-` operator form is the sole surface syntax).
  kw_spawn, kw_sync, kw_chan, kw_send, kw_recv,

  // ox:proof D8  -  `noninterference <vcpuA>, <vcpuB> <= <invariant_spec_fn> ;`
  // Module-level declaration stating that the named VCPUs are interference-
  // free w.r.t. the global invariant: for every atomic step of one VCPU's
  // handler, all other VCPUs' pending assertions remain valid (Owicki-Gries
  // stability). The Ghost encoder (emitNoninterference in Ghost.cpp) emits
  // pairwise stability checks: for each handler pair (hA, hB) drawn from
  // different VCPUs, discharge `forall shared_state. I(pre) ∧ step_hA(pre)
  // ⟹ I(post)`  -  `unsat` ⇒ the step doesn't falsify the invariant.
  kw_noninterference,

  // D9 (gap #6)  -  `cycle_preserves <handler>, <handler>, ... <= <invariant>;`
  // module-level decl. Per-handler VM-exit-cycle refinement discharge: the
  // guest resumes via vmlaunch/vmresume, pre-launch state refines to post-
  // resume state under the cycle invariant. The Ghost encoder
  // (emitCyclePreserves in Ghost.cpp) emits one discharge query per handler:
  //   NOT (forall (cycle_args). req_handler ∧ I_pre ==> I_post)
  // with vmlaunch/vmresume modelled as IDENTITY on the cycle invariant (the
  // trust boundary sits there; the handler's body  -  NOT the VM transitions  - 
  // is what mutates state). `unsat` ⇒ the handler re-establishes the cycle
  // invariant across the trap cycle. The handler list is comma-separated
  // (mirrors `noninterference`); the operator token is `<=` (Tok::lteq).
  // This is the cross-cycle layer that ties together `preserves` (per-handler
  // sequential correctness) and `noninterference` (cross-handler stability)
  // as the Owicki-Gries obligation #1+successor w.r.t. the trap cycle.
  kw_cycle_preserves,

  // --- C++-level templates + concepts ---
  // `concept` declares a named set of required method/associated-fn signatures
  // usable as a constraint on a type parameter (C++20-concepts-style: a
  // compile-time predicate on types, NOT a runtime interface/vtable).
  // `where` introduces a trailing constraint clause on a generic fn/struct:
  //   fn f<T>(...) where T: Ord { ... }
  // `requires` is reserved (unused keyword) for future ad-hoc constraint exprs.
  kw_concept, kw_where, kw_requires,

  // --- Formal-verification contracts ---
  // `requires`/`ensures` attach boolean spec clauses to a function (checked
  // at entry / before every return); `invariant` attaches to a loop (checked at
  // the loop head); `assert` is a body statement checked in place; `old(x)`
  // refers to a variable's value at function entry (valid only in `ensures`);
  // `forall`/`exists` are quantifiers usable inside a contract spec; `implies`
  // is the boolean implication operator (`P implies Q`, right associative).
  kw_ensures, kw_invariant, kw_assert,
  kw_old, kw_implies, kw_forall, kw_exists,

  // D6  -  total-correctness `decreases <expr>` clause on a `fn` head. Alongside
  // requires/ensures (partial correctness), `decreases m` declares a *measure*
  // expression that must strictly decrease on every recursive call, giving a
  // SMT-discharged termination (total-correctness) check at each recursive
  // call site (see smtConcreteCallResult recursion guard in Driver.cpp).
  kw_decreases,

  // ox:proof --- T1/T2/T3: ghost state, named regions, abstraction refinement ---
  // `ghost` introduces a spec-only binding: `ghost let [mut] x: T = e;` (T2  - 
  // visible in contract/spec contexts, NOT codegen) and `ghost fn f(...) { ... }`
  // (T2  -  callable only from spec/contract contexts). `region Name = { a, b };
  // ` (T3) declares a named union of runtime arrays + ghost lets usable in a
  // `modifies` clause; the ghost encoder expands a region in `modifies` to its
  // members when emitting frame axioms. `spec fn name(...) -> T = e` (T1)
  // declares an abstract spec function (single expression return) callable from
  // any contract context. `refines <concrete_fn> <= <spec_fn>` (T1) is a
  // module-level declaration stating the concrete's behaviour implies the spec.
  kw_ghost, kw_region, kw_spec, kw_refines,
  // `implements`  -  the verified-asm link clause keyword. Appears ONLY after an
  // `asm!(...)` block: `asm!(...) implements <spec_fn>(<args>)`. Links the
  // concrete asm block to an `asm spec fn` declaration so the SMT encoder can
  // substitute the args into the spec's `requires`/`ensures` and assert the
  // architectural hypothesis  -  no string naming-convention needed. Reserved
  // everywhere (reserved-keyword rule like `requires`/`ensures`), but a parse
  // error outside the `asm!(...) implements ...` position. See AST AsmExpr::
  // hasImplements + SpecFnDecl::isAsmSpec.
  kw_implements,
  // T3  -  `modifies <name1>, <name2>, ...` clause on a `fn` head, listing the
  // named regions (and/or bare array/global/ghost-let names) the function is
  // allowed to mutate. The ghost encoder emits a frame axiom per NON-modified
  // mutable symbol: it equals its old() value after the call, so callers can
  // reason compositionally. Empty list (omitted) = conservative default (the
  // function may mutate anything; no frame axiom emitted).
  kw_modifies,
  // Effect system  -  `effects { eff1, eff2, ... }` clause on a `fn` head.
  // Lists the named side-effects the function may perform (built-ins: io,
  // alloc, asm, panic, mmio, vmcs_read, vmcs_write, mem_write, trap, sched  - 
  // plus any user-defined name). Empty list or omitted = pure (no effects).
  // Sema checks effect propagation: a function that calls a fn with effect E
  // must itself declare E in its effects. The Ghost encoder emits a stronger
  // (blanket) frame axiom when effects is empty (pure).
  kw_effects,
  // ox:proof Lemma functions  -  `lemma fn name(params) requires R ensures E { body }`.
  // A proof-only top-level declaration (like `ghost fn` but with the `lemma`
  // keyword prefix). A lemma is callable only from `proof { ... }` blocks and
  // other lemma/spec contexts, never from runtime code. The Ghost encoder
  // emits its contract as `(assert (forall (params) (=> R E)))`  -  a universal
  // axiom available to all discharge queries  -  and discharges the lemma's own
  // body (the proof) as a separate goal. A lemma call inside a `proof { ... }`
  // block adds the lemma's `ensures` (with the call's args substituted for its
  // params) as a hypothesis. See AST FuncDecl::isLemma + Program::lemmas,
  // Parser parseLemma, Ghost emitLemmas, Driver smtEncodeStmt's proofBlock arm.
  kw_lemma,
  // Missing-#6  -  `preserves <handler_fn> <= <invariant_spec_fn>` module-level
  // decl. Per-handler discharge query: the handler, under its requires, makes
  // the invariant hold over its inlined result (see `PreservesDecl` + the
  // `emitPreserves` block in src/Ghost.cpp the encoder appends).
  kw_preserves,
  // ox:proof D3  -  `axiom <expr>;` module-level declaration. A top-level SMT axiom
  // emitted as `(assert <lowered-body>)` at the top of the ghost section, so
  // it is available to every discharge query (refines/preserves/contract). The
  // body is a spec expression (inSpec set) so `forall`/`implies` work; an
  // optional `name:` label may precede the body for `; note: axiom name` lines.
  kw_axiom,
  // ox:proof Namespace/audit extension  -  `trusted axiom Namespace::Name: <expr> source
  // "...";`. `trusted` marks an axiom as a trusted (non-machine-verified) fact
  // (e.g. citing the Intel SDM) surfaced by `oxide verify --audit-axioms`;
  // `source "..."` attaches a documentation citation string to an axiom. Both
  // are metadata only and never change the SMT `(assert <body>)` semantics.
  kw_trusted,
  kw_source,
  // ox:unsafe fix2  -  `trap` top-level declaration for hardware trap handlers (VM exits,
  // interrupts). Two forms: `trap name(params) -> Ret;` (a prototype/source
  // declaration, like an extern fn) and `trap handler name(params) -> Ret
  // requires ... ensures ... { body }` (a handler with contracts, like a fn).
  // A trap handler IS a FuncDecl (reuses params/requires/ensures/body/retType);
  // the `isTrapHandler` flag distinguishes it so the SMT encoder ASSUMES its
  // requires (the hardware guarantees them) instead of treating them as proof
  // obligations the caller must discharge. See Parser parseTrapHandler and
  // Driver emitSmt's trapHandlers iteration.
  kw_trap,
  // ox:unsafe fix2  -  `handler` keyword: the optional marker in `trap handler name(...)`
  // that distinguishes the handler-with-body form from the bare `trap name(...);`
  // prototype form. Only meaningful immediately after `trap`; a bare `handler`
  // elsewhere is just an identifier.
  kw_handler,
  // ox:proof Part 2  -  `discharge` keyword: the optional clause marker in
  // `trap handler name(...) discharge requires ... ensures ... { body }`.
  // When present, the SMT encoder DISCHARGES the trap handler's `requires`
  // (proves they follow from the VM-exit context axioms) instead of assuming
  // them as hardware-guaranteed premises. Only meaningful immediately after
  // the trap handler's signature and BEFORE its `requires`; a bare `discharge`
  // elsewhere is just an identifier (no Oxide identifier is reserved
  // everywhere  -  same rule as `on`/`handler`).
  kw_discharge,
  // fixB  -  `instantiate` keyword for the guided-instantiation pragma:
  //   `instantiate forall k. P on k = i;`   (ground form: assert P[i/k])
  //   `instantiate forall k. P on ept, idx;`  (pattern form: :pattern ((select ept idx)))
  // The clause after `on` selects the form: if the first token after `on`
  // parses as an identifier followed by `=`, it's the ground form; otherwise
  // it's the pattern form. See AST InstantiateStmt, Parser parseInstantiate,
  // Driver smtEncodeStmt's instantiateStmt arm.
  kw_instantiate,
  // fixB  -  `on` keyword: the clause separator in `instantiate ... on ...`.
  // Only meaningful immediately after the quantifier; a bare `on` elsewhere
  // is just an identifier (no Oxide identifier is reserved everywhere).
  kw_on,

  // ox:proof fixC  -  `proof` construct for the interactive induction proof mode:
  //   proof that forall k: i64 in 0..N implies P(k)
  //     by induction on k:
  //       base: P(0)
  //       step: assume P(k)
  //             prove P(k + 1)
  // These keywords are reserved (lexed as distinct tokens) so the Parser can
  // recognise the proof skeleton. See AST ProofStmt, Parser parseProof,
  // Driver smtEncodeStmt's proofStmt arm.
  kw_proof,        // 'proof'  -  start a proof block
  kw_that,         // 'that'  -  'proof that forall k. P'
  kw_by,           // 'by'  -  'by induction on k'
  kw_induction,    // 'induction'  -  the proof method
  kw_base,         // 'base'  -  the base case label
  kw_step,         // 'step'  -  the step case label
  kw_assume,       // 'assume'  -  assume the IH in the step case
  kw_prove,        // 'prove'  -  the goal to discharge in the step case
  // (kw_trusted + kw_source are declared above near kw_axiom  -  they mark a
  // trusted fact and attach a citation; both the `trusted assume <expr>;`
  // statement form and the `trusted axiom <expr>;` module-level form share them.)

  // calcD  -  `calc` keyword for the equational-reasoning block:
  //   calc {
  //     <expr>;
  //   == { <hints>; }                       // optional justification block
  //     <expr>;
  //   <= { <hints>; }
  //     <expr>;
  //   }
  // Each step's expression is separated from the next by a relation operator
  // (`==`, `!=`, `<=`, `>=`, `<`, `>`), optionally followed by a `{ <hints> }`
  // block of proof statements (assert / lemma-call / instantiate) that justify
  // the step. The SMT encoder discharges each consecutive pair as a separate
  // `(check-sat)` on the negated relation; the chain proves first REL...last.
  // See AST CalcStmt / CalcStep, Parser parseCalc, Driver smtEncodeStmt's
  // calcStmt arm. Reserved everywhere (not an identifier), like `proof`.
  kw_calc,

  // --- Compile-time macro/metaprogramming ---
  // `macro name(params...) { <body expr> }` declares a compile-time code
  // transformation; `expand name(args...)` invokes it at the use site, where
  // the args are substituted for the corresponding `$param` markers in the
  // body and the resulting expression is type-checked + compiled in place.
  // Macros are purely compile-time  -  never emitted to the runtime IR. See
  // AST MacroDecl/MacroCall, Parser parseMacro + parsePrimary (kw_expand),
  // Sema macroRegistry + checkExpr's MacroCall arm, IRGen genExpr's MacroCall
  // arm. `$param` in a macro body is lexed as `Tok::dollar` + `Tok::ident`.
  kw_macro,
  kw_expand,

  // `bitfield Name: BaseType { field: bit N; field: bits A..B; ... }`  -  a
  // verified bitfield DSL declaration. `bitfield` begins the top-level decl;
  // `bit N` and `bits A..B` are the field specifiers inside the body (single-bit
  // position vs an inclusive bit range, both zero-indexed). Reserved as
  // keywords so the Parser recognises them everywhere (not bare identifiers),
  // matching the rule for `struct`/`enum`/`fn`. See AST BitfieldDecl +
  // BitfieldField, Parser parseBitfield, the lowering in parseBitfield that
  // emits a synthetic StructDecl { raw: baseType } + ImplDecl with accessor/
  // setter methods, and Ghost emitBitfieldAxioms for the SMT layout axioms.
  kw_bitfield,
  kw_bit,    // `bit N`     -  single-bit field specifier (bit position N)
  kw_bits,   // `bits A..B` -  multi-bit field specifier (inclusive range A..B)

  kw_i64,
  kw_f64,
  kw_bool,
  kw_void,
  kw_print,
  kw_struct,
  kw_str,
  kw_vec,
  kw_map,
  kw_set,
  kw_hmap,
  kw_hset,
  kw_char,
  kw_i8, kw_i16, kw_i32,
  kw_u8, kw_u16, kw_u32, kw_u64, kw_usize,
  kw_f32,

  plus, minus, star, slash, percent,
  eq, eqeq, bang, bangeq,
  lt, gt, lteq, gteq,
  ampamp, barbar,
  amp, bar, caret, tilde, shl, shr,
  pluseq, minuseq, stareq, lasheq, percenteq,
  ampeq, bareq, careteq, shleq, shreq,
  inc, dec,
  question,

  lparen, rparen, lbrace, rbrace,
  lbracket, rbracket, dot,
  comma, semicolon, colon, arrow,
  dotdot, dotdoteq,   // range: `a..b` exclusive, `a..=b` inclusive
  coloncolon, fatarrow,
  dollar,             // '$'  -  the substitution prefix in macro bodies ($param)
  chanArrow,          // `<-` channel operator; shared by `chan <- val` (send) and
                      // `<- chan` (recv)  -  stance determined by position

  // --- Advanced math syntax (Unicode + ASCII fallbacks) ---
  // `**` is the ASCII power operator (same as Unicode ^\u00B2 superscript or
  // the ^ caret when used as exponent). `\` is the left-division / linear
  // solve operator (MATLAB-style: A \ b solves Ax=b). These are distinct from
  // the existing `star`/`slash`/`caret` tokens so the Parser can give `**`
  // higher precedence than `*` and route `\` to the linear-solve codepath.
  pow,            // `**`   -  ASCII power operator
  backslash,      // `\`    -  linear solve / left-division (A \ b)
  // ox:why Unicode math constants/symbols are lexed as `math_sym` so the Parser
  // can map each glyph to its meaning via the `text` field (π→pi, ∫→integrate,
  // √→sqrt, ²→pow2, ³→pow3, etc.). The ASCII fallback for each is a regular
  // identifier keyword (e.g. `pi`, `integrate`, `sqrt`) which the existing
  // keyword map already handles  -  we add those there too.
  math_sym,       // Unicode math glyph (π, ∫, √, ², ³, etc.)

  // ASCII fallback keywords for the Unicode math glyphs above. Each maps a
  // plain identifier to the same semantic constant/function the glyph form
  // denotes, so source can be written without Unicode if needed. `pow2`/
  // `pow3` mirror the ²/³ superscript glyphs; `pi` mirrors π; `integrate`
  // mirrors ∫; `sqrt` mirrors √. These are reserved keywords (not bare
  // identifiers) so the Parser recognises them in expression position.
  kw_pi,
  kw_integrate,
  kw_sqrt,
  kw_pow2,
  kw_pow3,

  err,
};

struct Token {
  Tok kind;
  std::string text;
  uint64_t u64 = 0;
  double f64 = 0;
  int line = 1;
  int col = 1;
  bool isF32 = false;   // `2.0f` float literal -> single precision (f32)
};

struct LexError {
  std::string msg;
  int line;
  int col;
};

class Lexer {
public:
  explicit Lexer(std::string src);
  bool lex(std::vector<Token>& out, std::vector<LexError>& errs);

private:
  std::string src_;
  size_t i_ = 0;
  int line_ = 1;
  int col_ = 1;
  std::vector<LexError>* errs_ = nullptr;

  void error(const std::string& msg);
  Tok keyword(const std::string& s);
};
