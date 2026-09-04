#include "Parser.h"
#include <stdexcept>

Parser::Parser(std::vector<Token> toks, std::vector<ParseError>& errs)
  : toks_(std::move(toks)), errs_(&errs) {}

const Token& Parser::cur() { return toks_[p_]; }
const Token& Parser::peek(size_t off) { return toks_[p_ + off]; }
bool Parser::at(Tok t) { return cur().kind == t; }
bool Parser::accept(Tok t) {
  if (cur().kind == t) { p_++; return true; }
  return false;
}
bool Parser::expect(Tok t, const std::string& what) {
  if (accept(t)) return true;
  error("expected " + what + " but got '" + cur().text + "'");
  return false;
}
void Parser::error(const std::string& msg) {
  errs_->push_back({msg, cur().line, cur().col});
}
bool Parser::atTopLevel() {
  return at(Tok::kw_fn) || at(Tok::kw_struct) || at(Tok::kw_enum) ||
         at(Tok::kw_typedef) || at(Tok::kw_extern) || at(Tok::kw_export) ||
         at(Tok::kw_impl) ||
         at(Tok::kw_bitfield) ||   // verified bitfield DSL: `bitfield Name: T { ... }`
         at(Tok::kw_let) || at(Tok::kw_const) || at(Tok::kw_concept) ||
         at(Tok::kw_ghost) || at(Tok::kw_spec) ||   // T2 ghost fn, T1 spec fn
         at(Tok::kw_region) || at(Tok::kw_refines) ||   // T3 region, T1 refines
         at(Tok::kw_preserves) ||   // Missing-#6: `preserves h <= I`
         at(Tok::kw_noninterference) ||  // D8: `noninterference h1, h2 <= I`
         at(Tok::kw_cycle_preserves) ||  // D9 (gap #6): `cycle_preserves h1, h2 <= I`
         at(Tok::kw_axiom) ||   // D3: `axiom <expr>;`
         at(Tok::kw_trusted) ||  // namespace/audit: `trusted axiom ...;`
         at(Tok::kw_trap) ||   // fix2: `trap [handler] name(...) ...`
         at(Tok::kw_macro) ||   // macro/metaprogramming: `macro name(...) { ... }`
         at(Tok::kw_lemma) ||   // lemma functions: `lemma fn name(...) ...`
         at(Tok::kw_asm) ||     // verified-asm: `asm spec name(...) -> T ...;`
         at(Tok::end);
}

BType Parser::parseType() {


  if (at(Tok::star)) { p_++; BType inner = parseType(); return makePtr(inner); }
  if (at(Tok::amp))  { p_++; BType inner = parseType(); return makePtr(inner); }
  if (at(Tok::kw_i64)) { p_++; return BType::i64; }
  if (at(Tok::kw_f64)) { p_++; return BType::f64; }
  if (at(Tok::kw_f32)) { p_++; return BType::f32; }
  if (at(Tok::kw_bool)) { p_++; return BType::bool_; }
  if (at(Tok::kw_void)) { p_++; return BType::void_; }
  if (at(Tok::kw_fn)) {
    p_++;
    if (!expect(Tok::lparen, "'(' after 'fn' in function type")) return BType::i64;
    std::vector<BType> params;
    if (!at(Tok::rparen)) {
      do {
        params.push_back(parseType());
      } while (accept(Tok::comma) && !at(Tok::rparen));
    }
    expect(Tok::rparen, "')' to close function type params");
    BType ret = BType::void_;
    if (accept(Tok::arrow)) ret = parseType();
    return makeFnType(params, ret);
  }
  if (at(Tok::kw_str)) { p_++; return BType::str; }
  if (at(Tok::kw_char)) { p_++; return BType::char_; }
  if (at(Tok::kw_i8)) { p_++; return BType::i8; }
  if (at(Tok::kw_i16)) { p_++; return BType::i16; }
  if (at(Tok::kw_i32)) { p_++; return BType::i32; }
  if (at(Tok::kw_u8)) { p_++; return BType::u8; }
  if (at(Tok::kw_u16)) { p_++; return BType::u16; }
  if (at(Tok::kw_u32)) { p_++; return BType::u32; }
  if (at(Tok::kw_u64)) { p_++; return BType::u64; }
  if (at(Tok::kw_usize)) { p_++; return BType::usize; }
  if (at(Tok::kw_vec)) {
    p_++;
    if (!expect(Tok::lbracket, "'[' after 'vec'")) { return makeDynArray(BType::i64); }
    BType elem = parseType();
    expect(Tok::rbracket, "']'");
    return makeDynArray(elem);
  }
  if (at(Tok::kw_map)) {
    p_++;
    if (!expect(Tok::lbracket, "'[' after 'map'")) { return makeMapType(BType::i64, BType::i64); }
    BType key = parseType();
    expect(Tok::comma, "',' between key and value in map[K, V]");
    BType val = parseType();
    expect(Tok::rbracket, "']'");
    return makeMapType(key, val);
  }
  if (at(Tok::kw_set)) {
    p_++;
    if (!expect(Tok::lbracket, "'[' after 'set'")) { return makeSetType(BType::i64); }
    BType elem = parseType();
    expect(Tok::rbracket, "']'");
    return makeSetType(elem);
  }
  if (at(Tok::kw_hmap)) {
    p_++;
    if (!expect(Tok::lbracket, "'[' after 'hmap'")) { return makeHMapType(BType::i64, BType::i64); }
    BType key = parseType();
    expect(Tok::comma, "',' between key and value in hmap[K, V]");
    BType val = parseType();
    expect(Tok::rbracket, "']'");
    return makeHMapType(key, val);
  }
  if (at(Tok::kw_hset)) {
    p_++;
    if (!expect(Tok::lbracket, "'[' after 'hset'")) { return makeHSetType(BType::i64); }
    BType elem = parseType();
    expect(Tok::rbracket, "']'");
    return makeHSetType(elem);
  }
  // `Channel[T]`  -  a buffered message channel carrying `T` values. Surfaces
  // as the `Channel` keyword (lexed as `kw_chan`) followed by `[T]`, mirroring
  // `vec[T]`/`set[T]`. The channel type is pointer-width (i8* in LLVM IR) and
  // carries its element type for Sema/IRGen to emit the right `@ox_chan_*_T`.
  if (at(Tok::kw_chan)) {
    p_++;
    if (!expect(Tok::lbracket, "'[' after 'Channel'")) { return makeChannelType(BType::i64); }
    BType elem = parseType();
    expect(Tok::rbracket, "']'");
    return makeChannelType(elem);
  }
  if (at(Tok::lbracket)) {
    p_++;
    BType elem = parseType();
    if (!expect(Tok::semicolon, "';' in array type")) return makeArrayType(elem, 0);
    int count = 0;
    if (at(Tok::int_lit)) { count = (int)cur().u64; p_++; }
    else error("expected array length");
    expect(Tok::rbracket, "']'");
    return makeArrayType(elem, count);
  }
  if (at(Tok::ident)) {

    std::string name = cur().text;
    p_++;


    if (at(Tok::lt) && peek(1).kind != Tok::lt) {
      bool ok = false;
      std::vector<BType> args = tryParseTypeArgs(ok);
      if (ok) return makeGenericInst(name, args,false);
    }
    BType t; t.tag = BType::Tag::struct_; t.structName = name;
    return t;
  }
  error("expected a type but got '" + cur().text + "'");
  p_++;
  return BType::i64;
}

std::unique_ptr<Program> Parser::parseProgram() {
  auto prog = std::make_unique<Program>();


  auto recoverToTopLevel = [&] {
    while (!atTopLevel()) p_++;
  };
  while (!at(Tok::end)) {
    if (at(Tok::kw_struct)) {
      auto s = parseStruct();
      if (s) prog->structs.push_back(std::move(s));
      else recoverToTopLevel();
      continue;
    }
    if (at(Tok::kw_enum)) {
      auto e = parseEnum();
      if (e) prog->enums.push_back(std::move(e));
      else recoverToTopLevel();
      continue;
    }
    if (at(Tok::kw_impl)) {
      auto im = parseImpl();
      if (im) prog->impls.push_back(std::move(im));
      else recoverToTopLevel();
      continue;
    }
    // Verified bitfield DSL  -  `bitfield Name: BaseType { field: bit N; ... }`.
    // parseBitfield parses the surface decl into a BitfieldDecl (pushed to
    // prog->bitfields for the Ghost encoder's SMT layout axioms), then
    // lowerBitfield emits a synthetic StructDecl { raw: baseType } + ImplDecl
    // (accessor/setter methods, verified contracts) into prog->structs /
    // prog->impls so they share the whole existing Sema/IRGen/Ghost pipeline
    // for structs+impls unchanged. See AST BitfieldDecl + Parser lowerBitfield.
    if (at(Tok::kw_bitfield)) {
      auto bf = parseBitfield();
      if (bf) {
        lowerBitfield(*prog, *bf);
        prog->bitfields.push_back(std::move(bf));
      } else {
        recoverToTopLevel();
      }
      continue;
    }
    if (at(Tok::kw_typedef)) {
      auto td = parseTypedef();
      if (td) prog->typedefs.push_back(std::move(td));
      else { while (!at(Tok::semicolon) && !at(Tok::end)) p_++; accept(Tok::semicolon); }
      continue;
    }
    if (at(Tok::kw_extern)) {

      p_++;
      if (at(Tok::kw_fn)) {
        auto fn = parseFunc(true);
        if (fn) prog->funcs.push_back(std::move(fn));
        else recoverToTopLevel();
      } else if (at(Tok::kw_let) || at(Tok::kw_const)) {
        auto vd = parseGlobal(true);
        if (vd) prog->globals.push_back(std::move(vd));
      } else if (at(Tok::kw_struct)) {
        // `extern struct Name;`  -  an opaque C-handle tag type (no body). The
        // addressable form is *Name; it lowers to LLVM `%struct.Name = type {}`
        // and you only ever pass `*Name` handles to extern fns, never by-value.
        p_++;
        auto sd = std::make_unique<StructDecl>();
        sd->line = cur().line;
        sd->isOpaque = true;
        if (!expect(Tok::ident, "struct name after 'extern struct'")) { recoverToTopLevel(); continue; }
        sd->name = toks_[p_ - 1].text;
        expect(Tok::semicolon, "';' after opaque struct name");
        prog->structs.push_back(std::move(sd));
      } else {
        error("expected 'fn', 'let', or 'struct' after 'extern'");
        p_++;
      }
      continue;
    }
    // `export fn name(...) -> Ret { body }`  -  make an Oxide fn callable from C
    // (reverse of `extern fn`). Consume `export`, expect `fn`, reuse parseFunc
    // (with a body, so isExtern=false), then stamp isExport=true on the decl.
    if (at(Tok::kw_export)) {
      auto fn = parseExportFn();
      if (fn) prog->funcs.push_back(std::move(fn));
      else recoverToTopLevel();
      continue;
    }
    // `unsafe fn name(...) -> Ret { body }`  -  a function whose body is an
    // implicit unsafe scope (like Rust's `unsafe fn`). Consume `unsafe`; if the
    // next token is `fn`, parse it as a normal fn (parseFunc(false)  -  with a
    // body, so isExtern=false) and stamp isUnsafe=true on the returned FuncDecl
    // (mirrors parseExportFn/parseLemma). A stray `unsafe` NOT followed by `fn`
    // falls through to parseStmt-style handling below  -  but at module scope only
    // `unsafe fn` is a legal top-level form (an `unsafe { ... }` block lives
    // inside fn bodies), so anything else emits a parse error and recovers.
    if (at(Tok::kw_unsafe) && peek(1).kind == Tok::kw_fn) {
      p_++;   // eat `unsafe`
      auto fn = parseFunc(false);
      if (fn) fn->isUnsafe = true;
      if (fn) prog->funcs.push_back(std::move(fn));
      else recoverToTopLevel();
      continue;
    }
    if (at(Tok::kw_let) || at(Tok::kw_const)) {
      auto vd = parseGlobal(false);
      if (vd) prog->globals.push_back(std::move(vd));
      else { while (!at(Tok::semicolon) && !at(Tok::end)) p_++; accept(Tok::semicolon); }
      continue;
    }
    if (at(Tok::kw_concept)) {
      auto cd = parseConceptDecl();
      if (cd) prog->concepts.push_back(std::move(cd));
      else recoverToTopLevel();
      continue;
    }
    // --- T1/T2/T3 module-level productions ---
    // `ghost fn ...` reuses the ordinary function parser; only the leading
    // `ghost` keyword is consumed here and `isGhost=true` is forwarded. A
    // stray `ghost` not followed by `fn` (e.g. `ghost let` at top level) falls
    // through to parseStmt-style dispatch  -  but at module scope only `ghost
    // fn` is a legal top-level form, so anything else emits a parse error and
    // recovers. `ghost let` is a STATE and lives inside fn bodies (see
    // parseStmt). `spec fn`/`region`/`refines` are their own decl kinds.
    if (at(Tok::kw_ghost)) {
      p_++;   // eat `ghost`
      if (at(Tok::kw_fn)) {
        auto fn = parseFunc(false, /*isGhost=*/true);
        if (fn) prog->funcs.push_back(std::move(fn));
        else recoverToTopLevel();
      } else {
        error("expected 'fn' after 'ghost' at top level (ghost let is a body statement)");
        recoverToTopLevel();
      }
      continue;
    }
    if (at(Tok::kw_asm)) {
      // Top-level `asm spec name(params) -> T requires ... ensures ...;`  -  a
      // hardware-instruction spec declaration. Reuses SpecFnDecl with
      // isAsmSpec=true (kept in prog->specFns so the existing collectSpecFns +
      // c.specFns map + Ghost emitSpecFns path all see it unchanged). `asm` at
      // top level is ONLY this form: a bare `asm! ...` is a *body* expression
      // (parsePrimary), never a decl, so `asm` here MUST be followed by `spec`.
      // On any other shape (e.g. `asm <ident>`) we fall through and let the
      // FreeFn/parseFunc error fire  -  but we eat `asm` first so the message is
      // accurate rather than "expected fn".
      bool isA = (p_ + 1 < toks_.size() && toks_[p_ + 1].kind == Tok::kw_spec);
      if (isA) {
        auto sf = parseAsmSpecFn();
        if (sf) prog->specFns.push_back(std::move(sf));
        else recoverToTopLevel();
        continue;
      }
      error("expected 'spec' after 'asm' at top level (only `asm spec fn ...` is a top-level decl; `asm!(...)` is a body expression)");
      p_++;   // eat `asm` so the recovery cursor advances
      recoverToTopLevel();
      continue;
    }
    if (at(Tok::kw_spec)) {
      auto sf = parseSpecFn();
      if (sf) prog->specFns.push_back(std::move(sf));
      else recoverToTopLevel();
      continue;
    }
    if (at(Tok::kw_region)) {
      auto rg = parseRegion();
      if (rg) prog->regions.push_back(std::move(rg));
      else { while (!at(Tok::semicolon) && !at(Tok::end)) p_++; accept(Tok::semicolon); }
      continue;
    }
    if (at(Tok::kw_refines)) {
      auto rf = parseRefines();
      if (rf) prog->refines_.push_back(std::move(rf));
      else { while (!at(Tok::semicolon) && !at(Tok::end)) p_++; accept(Tok::semicolon); }
      continue;
    }
    if (at(Tok::kw_preserves)) {
      auto pv = parsePreserves();
      if (pv) prog->preserves_.push_back(std::move(pv));
      else { while (!at(Tok::semicolon) && !at(Tok::end)) p_++; accept(Tok::semicolon); }
      continue;
    }
    // ox:proof D8  -  `noninterference <h1>, <h2>, ... <= <invariant>;`  -  Owicki-Gries
    // non-interference (stability) discharge over handler pairs.
    if (at(Tok::kw_noninterference)) {
      auto ni = parseNoninterference();
      if (ni) prog->noninterference_.push_back(std::move(ni));
      else { while (!at(Tok::semicolon) && !at(Tok::end)) p_++; accept(Tok::semicolon); }
      continue;
    }
    // D9 (gap #6)  -  `cycle_preserves <h1>, <h2>, ... <= <invariant>;`  -  VM-exit-
    // cycle refinement discharge (per-handler; vmlaunch/vmresume modelled as
    // identity on the cycle invariant). Mirrors noninterference in shape.
    if (at(Tok::kw_cycle_preserves)) {
      auto cp = parseCyclePreserves();
      if (cp) prog->cyclePreserves_.push_back(std::move(cp));
      else { while (!at(Tok::semicolon) && !at(Tok::end)) p_++; accept(Tok::semicolon); }
      continue;
    }
    if (at(Tok::kw_axiom) || at(Tok::kw_trusted)) {
      auto ax = parseAxiom();
      if (ax) prog->axioms.push_back(std::move(ax));
      else { while (!at(Tok::semicolon) && !at(Tok::end)) p_++; accept(Tok::semicolon); }
      continue;
    }
    // ox:unsafe fix2  -  `trap [handler] name(...) ...` top-level trap-handler declaration.
    // Two forms: `trap handler name(...) requires ... ensures ... { body }`
    // (Form 2  -  a handler WITH a body and contracts) and `trap name(...);`
    // (Form 1  -  a body-less prototype). Both produce a FuncDecl with
    // isTrapHandler=true, added to prog->trapHandlers. The optional `handler`
    // keyword is pure documentation  -  the actual disambiguator between the two
    // forms is the terminator AFTER the signature+contracts: `{` => handler
    // with body, `;` => prototype. (The task's test file uses the body form
    // WITHOUT `handler`; the spec's example uses it WITH. Both must parse.)
    if (at(Tok::kw_trap)) {
      p_++;   // eat `trap`
      accept(Tok::kw_handler);   // optional `handler` marker (documentation-only)
      auto fn = parseTrapHandler(/*withBody=*/true);   // body-or-`;` decided inside
      if (fn) prog->trapHandlers.push_back(std::move(fn));
      else recoverToTopLevel();
      continue;
    }
    // Compile-time macro declaration: `macro name(params) { <body expr> }`.
    // The body is a single expression (no trailing `;`); `$param` markers inside
    // are parsed as VarRef nodes named `"$param"` and substituted at expand time.
    // Stored in `prog->macros`; Sema builds the name->MacroDecl* registry.
    if (at(Tok::kw_macro)) {
      auto mc = parseMacro();
      if (mc) prog->macros.push_back(std::move(mc));
      else recoverToTopLevel();
      continue;
    }
    // ox:proof Lemma function: `lemma fn name(params) requires R ensures E { body }`.
    // A proof-only top-level decl (like `ghost fn` but with the `lemma` prefix).
    // parseLemma consumes `lemma`, expects `fn`, reuses parseFunc, and stamps
    // isLemma=true on the FuncDecl. Stored on `prog->lemmas` (SEPARATE from
    // `funcs`, like `trapHandlers`) so Sema/IRGen/SMT apply lemma semantics.
    if (at(Tok::kw_lemma)) {
      auto fn = parseLemma();
      if (fn) prog->lemmas.push_back(std::move(fn));
      else recoverToTopLevel();
      continue;
    }
    auto f = parseFunc();
    if (f) prog->funcs.push_back(std::move(f));
    else {

      recoverToTopLevel();
    }
  }
  return prog;
}

std::unique_ptr<EnumDecl> Parser::parseEnum() {
  p_++;
  auto ed = std::make_unique<EnumDecl>();
  ed->line = cur().line;
  if (!expect(Tok::ident, "enum name")) return nullptr;
  ed->name = toks_[p_ - 1].text;
  if (!expect(Tok::lbrace, "'{' to begin enum body")) return nullptr;
  while (!at(Tok::rbrace) && !at(Tok::end)) {
    if (!expect(Tok::ident, "variant name")) break;
    ed->variants.push_back(toks_[p_ - 1].text);
    if (!accept(Tok::comma)) break;
  }
  expect(Tok::rbrace, "'}'");
  return ed;
}


std::unique_ptr<ImplDecl> Parser::parseImpl() {
  p_++;
  auto im = std::make_unique<ImplDecl>();
  im->line = cur().line;
  if (!expect(Tok::ident, "struct name after 'impl'")) return nullptr;
  im->structName = toks_[p_ - 1].text;
  if (!expect(Tok::lbrace, "'{' to begin impl body")) return nullptr;
  while (!at(Tok::rbrace) && !at(Tok::end)) {
    // A method may be prefixed by `virtual`/`override` before `fn`.
    bool isMethodStart = at(Tok::kw_fn) || at(Tok::kw_virtual) || at(Tok::kw_override);
    if (!isMethodStart) {
      error("expected 'fn' for a method in impl block");

      while (!isMethodStart && !at(Tok::rbrace) && !at(Tok::end)) {
        p_++;
        isMethodStart = at(Tok::kw_fn) || at(Tok::kw_virtual) || at(Tok::kw_override);
      }
      if (isMethodStart) { continue; }
      break;
    }
    auto fn = parseFunc(false);
    if (fn) im->methods.push_back(std::move(fn));
    else {
      while (!at(Tok::kw_fn) && !at(Tok::kw_virtual) && !at(Tok::kw_override) &&
             !at(Tok::rbrace) && !at(Tok::end)) p_++;
    }
  }
  expect(Tok::rbrace, "'}' to close impl body");
  return im;
}

std::unique_ptr<StructDecl> Parser::parseStruct() {
  p_++;
  auto sd = std::make_unique<StructDecl>();
  sd->line = cur().line;
  if (!expect(Tok::ident, "struct name")) return nullptr;
  sd->name = toks_[p_ - 1].text;
  parseTypeParamsRich(sd->tparams);
  sd->isGeneric = !sd->tparams.empty();
  sd->typeParams.reserve(sd->tparams.size());
  for (auto& tp : sd->tparams) sd->typeParams.push_back(tp.name);

  // Single inheritance: `struct Derived: Base { ... }`. The optional `: Base`
  // is parsed before the body. The base must name a (non-generic) struct; that's
  // checked in Sema. Inheritance is intentionally NOT allowed on generic structs.
  if (accept(Tok::colon)) {
    if (!expect(Tok::ident, "base struct name after ':'")) return nullptr;
    sd->baseName = toks_[p_ - 1].text;
  }

  if (!expect(Tok::lbrace, "'{' to begin struct body")) return nullptr;
  while (!at(Tok::rbrace) && !at(Tok::end)) {
    bool priv = accept(Tok::kw_private);
    if (!expect(Tok::ident, "field name")) break;
    std::string fname = toks_[p_ - 1].text;
    if (!expect(Tok::colon, "':' after field name")) break;
    BType ft = parseType();
    sd->fields.push_back({fname, ft, priv});
    accept(Tok::semicolon);
    accept(Tok::comma);
  }
  expect(Tok::rbrace, "'}'");
  return sd;
}

// `concept Name<T> { fn req1(&self, args...) -> Ret;  fn assoc(args) -> Ret;  ... }`
//  -  a named, compile-time set of required method/associated-fn signatures usable
// as a constraint on a generic type param. The concept takes AT MOST ONE type
// param (the `Self` being constrained), declared as `<T>` after the name; if
// absent the concept is a degenerate always-trivial one (`selfParam` = ""). Each
// required signature is `fn` name `(` optional-receiver + params `)` optional
// `-> Ret` `;`  -  NO body, NO default args on a required-fn. Receivers mirror
// parseFunc: `self` (by-value), `&self` / `&mut self` (by-ref). Each req is a
// ConceptReqDecl in `ConceptDecl::reqs`.
std::unique_ptr<ConceptDecl> Parser::parseConceptDecl() {
  p_++;   // consume `concept`
  auto cd = std::make_unique<ConceptDecl>();
  cd->line = cur().line;
  if (!expect(Tok::ident, "concept name")) return nullptr;
  cd->name = toks_[p_ - 1].text;
  cd->selfParam = "";
  // Optional `<T>`  -  the lone type param (the Self being constrained). Keep it
  // simple: exactly one ident between `<` `>`. Anything richer: Sema can reject.
  if (accept(Tok::lt)) {
    if (!expect(Tok::ident, "type-parameter name in concept")) return nullptr;
    cd->selfParam = toks_[p_ - 1].text;
    if (!accept(Tok::gt)) {
      // Not a clean `<T>`; recover by rolling back the self-param and continuing
      // to parse the body anyway (Sema will diagnose the bad syntax).
      error("expected '>' after concept type-parameter");
      cd->selfParam = "";
    }
  }
  if (!expect(Tok::lbrace, "'{' to begin concept body")) return nullptr;
  while (!at(Tok::rbrace) && !at(Tok::end)) {
    if (!expect(Tok::kw_fn, "'fn' for a required signature in concept body")) break;
    ConceptReqDecl req;
    if (!expect(Tok::ident, "required fn name in concept body")) break;
    req.name = toks_[p_ - 1].text;
    if (!expect(Tok::lparen, "'(' in required fn signature")) break;

    // Optional self-receiver, mirroring parseFunc.
    if (at(Tok::kw_self)) {
      p_++;
      req.hasSelf = true;
      req.selfByRef = false;
      accept(Tok::comma);   // a trailing comma if there are more params
    } else if (at(Tok::amp) && peek(1).kind == Tok::kw_self) {
      // `&self`
      p_ += 2;
      req.hasSelf = true;
      req.selfByRef = true;
      accept(Tok::comma);
    } else if (at(Tok::amp) && peek(1).kind == Tok::kw_mut && peek(2).kind == Tok::kw_self) {
      // `&mut self`  -  same as `&self` (mut is a documented marker only).
      p_ += 3;
      req.hasSelf = true;
      req.selfByRef = true;
      accept(Tok::comma);
    }

    // Params (NOT counting the receiver). Each: ident `:` Type, comma-separated.
    while (!at(Tok::rparen) && !at(Tok::end)) {
      if (!expect(Tok::ident, "parameter name in required fn")) break;
      std::string pname = toks_[p_ - 1].text;
      if (!expect(Tok::colon, "':' after parameter name in required fn")) break;
      BType pt = parseType();
      req.params.emplace_back(pname, pt);
      if (!accept(Tok::comma)) break;
    }
    expect(Tok::rparen, "')' to close required fn signature");
    if (accept(Tok::arrow)) req.retType = parseType();
    else req.retType = BType::void_;
    expect(Tok::semicolon, "';' after required fn signature");
    cd->reqs.push_back(std::move(req));
  }
  expect(Tok::rbrace, "'}' to close concept body");
  return cd;
}

// Parses a generic parameter list `<T, U: ConceptName, V = DefaultType, ...>`
// (and the optional trailing `where T: C, ...` clause) into `out`. Returns with
// `out` empty if there is no `<` here, OR if the `<...>` doesn't cleanly close
// with `>`  -  the latter case is the disambiguation that lets `a < b` comparisons
// survive (we roll back to `save` and treat `<` as the less-than operator).
//
// Per-param grammar (mirror C++20 concepts, single constraint only):
//   T                       bare name (unconstrained, no default)
//   T : ConceptName         constrained
//   T : ConceptName = Type  constrained with a default type arg
//   T = Type                default only (no constraint)
// Trailing `where T: ConceptName, U: ConceptName, ...` clause overrides any
// inline constraint set on the SAME name  -  where-clause wins. We mutate parsed
// tparams in place to apply where-clause constraints before attaching to the
// decl, so no separate AST field is needed.
void Parser::parseTypeParamsRich(std::vector<TypeParam>& out) {
  out.clear();

  if (!at(Tok::lt)) return;
  // ox:unsafe The first token after `<` must be an ident (a type-param NAME); otherwise
  // this is `a < b ...` and `<` is a less-than, NOT a type-arg list. Bail.
  if (peek(1).kind != Tok::ident) return;
  size_t save = p_;
  size_t errCount = errs_->size();
  p_++;   // consume `<`
  while (!at(Tok::gt) && !at(Tok::end)) {
    if (!at(Tok::ident)) { error("expected type-parameter name"); break; }
    TypeParam tp;
    tp.name = cur().text;
    tp.hasDefault = false;
    tp.defaultType = BType::i64;
    tp.constraint = "";
    p_++;
    // `: ConceptName`  -  constraint (a single concept name only).
    if (accept(Tok::colon)) {
      if (!expect(Tok::ident, "concept name after ':' in type-parameter")) break;
      tp.constraint = toks_[p_ - 1].text;
    }
    // `= DefaultType`  -  default type argument.
    if (accept(Tok::eq)) {
      tp.hasDefault = true;
      tp.defaultType = parseType();
    }
    out.push_back(std::move(tp));
    if (!accept(Tok::comma)) break;
  }
  if (!accept(Tok::gt)) {
    // Not a clean type-param list  -  roll back, treat `<` as the less-than op.
    p_ = save;
    if (errs_->size() > errCount) errs_->resize(errCount);
    out.clear();
    return;
  }

  // Optional trailing `where T: ConceptName, U: ConceptName, ...` clause  - 
  // the inline-after-`>` spelling of the same clause that parseFunc also
  // accepts after the return type. (See applyWhereClause for the override
  // semantics; a where item overrides any inline `T: C` constraint for the
  // matching name.) Factored so the post-return-type form reuses the same code.
  if (accept(Tok::kw_where)) {
    applyWhereClause(out);   // parses the rest of the clause (we already ate `where`)
  }
}

// Consumes the body of a `where` clause that starts at the CURRENT token (the
// `where` keyword itself has already been eaten by the caller). Each item is
// `T: ConceptName` and may be followed by `,` for another item. A where item's
// constraint OVERRIDES any inline `T: C` already on the matching param
// (where-clause wins over inline, mirroring the documented semantics). Naming
// an unknown type param is a deferred Sema error  -  we just record the
// constraint and let Sema validate. Used by:
//   - parseTypeParamsRich (inline-after-`>` spelling: `fn f<T> where T: C`)
//   - parseFunc / parseTrapHandler (post-return-type spelling:
//     `fn f<T>(...) -> R where T: C { ... }`)
void Parser::applyWhereClause(std::vector<TypeParam>& tparams) {
  while (!at(Tok::end)) {
    if (!expect(Tok::ident, "type-parameter name in 'where' clause")) break;
    std::string pname = toks_[p_ - 1].text;
    if (!expect(Tok::colon, "':' in 'where' clause item")) break;
    if (!expect(Tok::ident, "concept name after ':' in 'where' clause")) break;
    std::string cname = toks_[p_ - 1].text;
    bool matched = false;
    for (auto& tp : tparams) {
      if (tp.name == pname) { tp.constraint = cname; matched = true; break; }
    }
    if (!matched) {
      // Unknown type-param name: synthesise a fresh TypeParam entry carrying
      // only the constraint, so the parse doesn't drop the user's intent. Sema
      // will diagnose the dangling constraint at instantiation time.
      TypeParam phantom;
      phantom.name = pname;
      phantom.constraint = cname;
      tparams.push_back(std::move(phantom));
    }
    if (!accept(Tok::comma)) break;
  }
}


std::vector<BType> Parser::tryParseTypeArgs(bool& ok) {
  ok = false;
  std::vector<BType> out;
  if (!at(Tok::lt)) return out;


  size_t save = p_;
  p_++;


  size_t errCount = errs_->size();
  while (!at(Tok::gt) && !at(Tok::end)) {
    out.push_back(parseType());
    if (!accept(Tok::comma)) break;
  }
  if (at(Tok::gt)) {


    errs_->resize(errCount);
    p_++;
    ok = true;
    return out;
  }


  p_ = save;
  if (errs_->size() > errCount) errs_->resize(errCount);
  out.clear();
  return out;
}

// `export fn name(...) -> Ret { body }`  -  make an Oxide fn callable from C
// (reverse of `extern fn`). Consume `export`, expect `fn`, reuse parseFunc
// (with a body, so isExtern=false), then stamp isExport=true on the decl.
std::unique_ptr<FuncDecl> Parser::parseExportFn() {
  if (!at(Tok::kw_export)) return nullptr;
  p_++;   // eat `export`
  if (!at(Tok::kw_fn)) {
    error("expected 'fn' after 'export'");
    p_++;
    return nullptr;
  }
  auto fn = parseFunc(false);
  if (fn) fn->isExport = true;
  return fn;
}

// ox:proof `lemma fn name(params) requires R ensures E { body }`  -  a proof-only
// top-level decl. Mirrors parseExportFn: consume `lemma`, expect `fn`, reuse
// parseFunc(false) (with a body), then stamp isLemma=true on the FuncDecl.
// The caller (parseProgram) pushes the result into `prog->lemmas`, SEPARATE
// from `funcs`. A lemma follows the SAME call-site restriction as `ghost fn`
// (Sema rejects runtime calls), does NOT codegen, and is emitted by the Ghost
// encoder as a universal axiom plus a body-discharge goal.
std::unique_ptr<FuncDecl> Parser::parseLemma() {
  if (!at(Tok::kw_lemma)) return nullptr;
  p_++;   // eat `lemma`
  if (!at(Tok::kw_fn)) {
    error("expected 'fn' after 'lemma'");
    p_++;
    return nullptr;
  }
  auto fn = parseFunc(false);
  if (fn) fn->isLemma = true;
  return fn;
}

std::unique_ptr<FuncDecl> Parser::parseFunc(bool isExtern, bool isGhost) {
  // `virtual`/`override` qualifiers are only meaningful on an impl method
  // (a free fn or associated fn carrying one is a Sema error). Both precede
  // `fn`. We parse them here and stamp them onto the FuncDecl below.
  bool isVirtual = false, isOverride = false;
  if (at(Tok::kw_virtual)) { isVirtual = true; p_++; }
  if (at(Tok::kw_override)) { isOverride = true; p_++; }
  if (!at(Tok::kw_fn)) {
    error("expected 'fn'");
    return nullptr;
  }
  p_++;
  auto fn = std::make_unique<FuncDecl>();
  fn->line = cur().line;
  fn->isExtern = isExtern;
  fn->isGhost = isGhost;        // T2  -  `ghost fn ...` stamps isGhost
  fn->isVirtual = isVirtual;
  fn->isOverride = isOverride;
  if (!expect(Tok::ident, "function name")) return nullptr;
  fn->name = toks_[p_ - 1].text;
  parseTypeParamsRich(fn->tparams);
  fn->isGeneric = !fn->tparams.empty();
  fn->typeParams.reserve(fn->tparams.size());
  for (auto& tp : fn->tparams) fn->typeParams.push_back(tp.name);
  if (!expect(Tok::lparen, "'('")) return nullptr;


  // Self-receiver (only meaningful on an impl method; Sema rejects a self on a
  // free fn). The three forms `self`, `&self`, `&mut self` set hasSelf/selfByRef
  // and consume the receiver, then fall through to the normal param-list parse
  // (the receiver is NOT a regular param). When self is the only param, the
  // param list ends at `)` immediately and we reach the shared tail that
  // parses the optional `-> Ret`, contract clauses, `modifies`, and body.
  // Previously each self form had its own early-return here that bypassed
  // contract/modifies parsing; folding them back into the unified tail was the
  // single change that lets a `fn foo(&mut self) modifies F { ... }` work.
  bool rpConsumed = false;   // set when the self-no-comma form closes the list
  if (at(Tok::kw_self)) {
    p_++;
    fn->hasSelf = true;
    fn->selfByRef = false;
  } else if (at(Tok::amp) && peek(1).kind == Tok::kw_self) {
    // `&self`  -  receiver passed by reference (the address of the struct).
    p_ += 2;
    fn->hasSelf = true;
    fn->selfByRef = true;
  } else if (at(Tok::amp) && peek(1).kind == Tok::kw_mut && peek(2).kind == Tok::kw_self) {
    // `&mut self`  -  like `&self` but conveyed to Sema as a mutably-borrowed
    // receiver (the drop method conventionally takes this). selfByRef is true;
    // the `mut` is a documented marker, not otherwise encoded  -  Oxide's
    // mutability is per-binding, and a &self is already mutable through the
    // pointer at the IR level.
    p_ += 3;
    fn->hasSelf = true;
    fn->selfByRef = true;
  }
  if (fn->hasSelf) {
    // If a comma follows the receiver, the param list continues with ordinary
    // params; otherwise the list ends at `)` and self was the only param.
    if (!accept(Tok::comma)) {
      expect(Tok::rparen, "')'");
      rpConsumed = true;
    }
  }
  // Parse remaining params (none if self was only param and rpConsumed). When
  // a self receiver with trailing comma supplied some, we parse the rest.
  while (!rpConsumed && !at(Tok::rparen) && !at(Tok::end)) {
    if (!expect(Tok::ident, "parameter name")) break;
    std::string pname = toks_[p_ - 1].text;
    if (!expect(Tok::colon, "':' after parameter name")) break;
    BType pt = parseType();
    Param pp; pp.name = pname; pp.type = pt;
    // C++-style default argument: `name: Type = expr`. Only parsed for defined
    // (non-extern) functions  -  extern decls name a C ABI symbol whose signature
    // we must mirror exactly; defaults on the Oxide side wouldn't be honoured by
    // the C call and would be a silent footgun, so reject them. The trailing-ness
    // rule is enforced in Sema.
    if (!fn->isExtern && accept(Tok::eq)) {
      pp.hasDefault = true;
      pp.defaultExpr = parseAssign();   // allow nested ternary/assign (rare) ; match C precedence
    }
    fn->params.push_back(std::move(pp));
    if (!accept(Tok::comma)) break;
  }
  if (!rpConsumed) {
    expect(Tok::rparen, "')'");
  }
  if (accept(Tok::arrow)) {
    fn->retType = parseType();
  } else {
    fn->retType = BType::void_;
  }
  // ox:note Contract clauses are a property of a DEFINED function (a C ABI extern
  // decl names a foreign symbol whose contract the Oxide side cannot enforce
  // and whose body is unknown  -  `ensures` would be unprovable and `requires`
  // uncheckable, so they are rejected on extern decls).
  // Contract clauses (requires / ensures / modifies) are a property of a
  // DEFINED function (a C ABI extern decl names a foreign symbol whose contract
  // the Oxide side cannot enforce and whose body is unknown  -  `ensures` would be
  // unprovable and `requires` uncheckable, so they are rejected on extern
  // decls). The three clauses may appear in any order and interleave; we loop
  // until none of them appear, then expect the body `{` (or `;` for extern).
  if (!fn->isExtern) {
    while (at(Tok::kw_requires) || at(Tok::kw_ensures) ||
           at(Tok::kw_modifies) || at(Tok::kw_decreases) ||
           at(Tok::kw_effects)) {
      if (at(Tok::kw_requires) || at(Tok::kw_ensures)) {
        parseFuncContracts(fn->requires_, fn->ensures_);
      } else if (at(Tok::kw_decreases)) {
        // D6  -  `decreases <spec-expr>` (semicolon-less, like requires/ensures;
        // the clause ends at the next clause keyword or `{`). A measure
        // expression that must strictly decrease on every recursive call.
        // Currently at most one `decreases` clause per fn (last one wins if
        // redundantly repeated), mirroring the single `decreases` field on
        // FuncDecl. Stored in fn->decreases; discharged at each recursive call
        // site by the recursion guard in smtConcreteCallResult (Driver.cpp).
        p_++;   // eat `decreases`
        fn->decreases = parseSpecExpr();
      } else if (at(Tok::kw_effects)) {
        // Effect system  -  `effects { eff1, eff2, ... }` (or `effects { }`).
        // Brace-delimited, comma-separated effect names. May appear in any
        // order relative to the other clauses; multiple occurrences
        // accumulate (parseEffects handles its own loop), and latches
        // `fn->effectsExplicit` true so Sema/Ghost distinguish "explicitly
        // pure" from "omitted (untracked)".
        parseEffects(fn->effects, fn->effectsExplicit);
      } else {
        parseModifies(fn->modifies);
      }
    }
  }
  // Post-signature `where T: ConceptName, ...` clause  -  the deferred spelling
  // of the inline `<T: C>` constraint, e.g. `fn f<T, U>(a: T, b: U) -> R
  // where T: Sumable { ... }`. Parsed AFTER the return type and (for non-extern
  // fns) AFTER the requires/ensures/modifies/decreases/effects clauses, since
  // the `where` keyword would otherwise terminate the contract-clause loop and
  // be mis-tokenised by it. A where item overrides any inline `T: C` already set
  // on `tparams` by `parseTypeParamsRich`. (For extern fns the clause is parsed
  // but inert  -  externs never monomorphise  -  kept for syntactic consistency.)
  if (accept(Tok::kw_where)) {
    applyWhereClause(fn->tparams);
    // typeParams is the cheap names-only projection of tparams; a where item
    // that named an unknown param may have appended a phantom entry, so refresh
    // the projection to keep it in sync (Sema validates the dangling name).
    fn->typeParams.clear();
    fn->typeParams.reserve(fn->tparams.size());
    for (auto& tp : fn->tparams) fn->typeParams.push_back(tp.name);
    fn->isGeneric = !fn->tparams.empty();
  }
  if (fn->isExtern) {


    expect(Tok::semicolon, "';' after extern fn declaration");
    return fn;
  }
  if (!at(Tok::lbrace)) {
    error("expected '{' to begin function body");
    return nullptr;
  }
  fn->body = parseBlock();
  return fn;
}


std::unique_ptr<TypedefDecl> Parser::parseTypedef() {
  p_++;
  auto td = std::make_unique<TypedefDecl>();
  td->line = cur().line;
  if (!expect(Tok::ident, "typedef name")) return nullptr;
  td->name = toks_[p_ - 1].text;
  if (!expect(Tok::eq, "'=' in typedef")) return nullptr;
  td->target = parseType();
  // Feature 4  -  optional `where <bool-expr>;` clause. The expression is a
  // spec expression (inSpec set) so `<=`, `<`, `>=`, `>`, `&&` / `||`, and
  // (if the user writes one) `forall`/`implies` parse cleanly. The typedef
  // name (e.g. `Idx` in `type Idx = i64 where 0 <= Idx < N;`) is treated as
  // a self-reference to the param's value; Sema rebinds it at SMT-emit time.
  if (accept(Tok::kw_where)) {
    td->rangeExpr = parseSpecExpr();
  }
  expect(Tok::semicolon, "';' after typedef");
  return td;
}


std::unique_ptr<VarDecl> Parser::parseGlobal(bool isExtern) {
  auto vd = std::make_unique<VarDecl>();
  vd->isExtern = isExtern;
  vd->line = cur().line;
  if (accept(Tok::kw_const)) {
    vd->isConst = true;
  } else {
    p_++;
    if (accept(Tok::kw_mut)) vd->isMut = true;
  }
  if (!expect(Tok::ident, "global name")) return nullptr;
  vd->name = toks_[p_ - 1].text;
  if (accept(Tok::colon)) {
    vd->type = parseType();
    vd->typeAnnotated = true;
  }
  if (accept(Tok::eq)) {
    if (!at(Tok::semicolon)) vd->init = parseExpr();
  }
  expect(Tok::semicolon, "';' after global declaration");
  return vd;
}

// ox:proof T1  -  `spec fn name(params) -> T = <expr> ;`
// Parse like a function head (name, optional generics, params, arrow+ret),
// but instead of a body block, require `= <expr>` then `;`. The body is a
// single spec expression (inSpec set so `forall`/`implies`/`old` work). The
// produced SpecFnDecl carries the params, the ret type, and the body expr.
std::unique_ptr<SpecFnDecl> Parser::parseSpecFn() {
  p_++;   // eat `spec`
  if (!at(Tok::kw_fn)) { error("expected 'fn' after 'spec'"); return nullptr; }
  p_++;   // eat `fn`
  auto sf = std::make_unique<SpecFnDecl>();
  sf->line = cur().line;
  if (!expect(Tok::ident, "spec function name")) return nullptr;
  sf->name = toks_[p_ - 1].text;
  // ox:proof Generics on a spec fn are accepted syntactically (parsed and dropped)  - 
  // a spec fn over a bounded type domain is rare and the SMT define-fun only
  // takes concrete sorts, so we keep the shape for forward-compat and ignore
  // typeParams in the encoder for v1.
  std::vector<TypeParam> tparams;
  parseTypeParamsRich(tparams);
  if (!expect(Tok::lparen, "'('")) return nullptr;
  while (!at(Tok::rparen) && !at(Tok::end)) {
    if (!expect(Tok::ident, "parameter name")) break;
    std::string pname = toks_[p_ - 1].text;
    if (!expect(Tok::colon, "':' after parameter name")) break;
    BType pt = parseType();
    Param pp; pp.name = pname; pp.type = pt;
    sf->params.push_back(std::move(pp));
    if (!accept(Tok::comma)) break;
  }
  expect(Tok::rparen, "')'");
  if (accept(Tok::arrow)) sf->retType = parseType();
  else sf->retType = BType::bool_;
  if (!expect(Tok::eq, "'=' in spec fn definition")) return nullptr;
  sf->body = parseSpecExpr();
  expect(Tok::semicolon, "';' after spec fn definition");
  return sf;
}

// `asm spec name(params) -> T requires ... ensures ...;`  -  a hardware-
// instruction specification. The shape mirrors a regular `spec fn` (same
// SpecFnDecl carrier, same prog->specFns storage so the existing
// collectSpecFns + c.specFns map + Ghost emitSpecFns path all see it) but with
// two differences:
//   1. The leading keyword sequence is `asm spec` (NOT `spec fn`)  -  `spec`
//      follows `asm` rather than `fn` following `spec`, and there is NO `fn`
//      token and NO `= body` expression. The instruction's behaviour lives in
//      the `ensures` clauses, not in a body expression.
//   2. The decl carries `requires` / `ensures` contract clauses (like a
//      FuncDecl). `requires` is the precondition the hardware instruction
//      promises (e.g. `valid_msr(msr)`); `ensures` is the architectural effect
//      the implementing `asm!(...) implements <name>` block trusts the hardware
//      to satisfy for its args (may reference `result` + the params).
// We stamp `isAsmSpec=true` so Sema/Ghost/Driver apply the asm-semantics path
// (caller-discharged requires, hypothesis-asserted ensures, universal-axiom
// emit). `parseFuncContracts` is reused verbatim for the requires/ensures loop
// (it sets inSpec_ via parseSpecExpr so `forall`/`implies`/`old` work). The
// decl is closed by `;` (no body block  -  an asm spec is spec-only, never
// codegen'd).
std::unique_ptr<SpecFnDecl> Parser::parseAsmSpecFn() {
  p_++;   // eat `asm`
  if (!at(Tok::kw_spec)) { error("expected 'spec' after 'asm' in `asm spec` declaration"); return nullptr; }
  p_++;   // eat `spec`
  // ox:proof Optional `fn` for readability (`asm spec fn name(...) ...`); the canonical
  // surface is `asm spec name(...)` but accepting a redundant `fn` keeps the
  // language consistent with `spec fn` for users who prefer it. Either way
  // the decl is spec-only  -  `fn` here is documentation, not a body marker.
  accept(Tok::kw_fn);
  auto sf = std::make_unique<SpecFnDecl>();
  sf->isAsmSpec = true;
  sf->line = cur().line;
  if (!expect(Tok::ident, "asm spec function name")) return nullptr;
  sf->name = toks_[p_ - 1].text;
  // Generics are accepted syntactically (parsed and dropped) just like a plain
  // spec fn  -  the SMT declare-fun only takes concrete sorts.
  std::vector<TypeParam> tparams;
  parseTypeParamsRich(tparams);
  if (!expect(Tok::lparen, "'('")) return nullptr;
  while (!at(Tok::rparen) && !at(Tok::end)) {
    if (!expect(Tok::ident, "parameter name")) break;
    std::string pname = toks_[p_ - 1].text;
    if (!expect(Tok::colon, "':' after parameter name")) break;
    BType pt = parseType();
    Param pp; pp.name = pname; pp.type = pt;
    sf->params.push_back(std::move(pp));
    if (!accept(Tok::comma)) break;
  }
  expect(Tok::rparen, "')'");
  if (accept(Tok::arrow)) sf->retType = parseType();
  else sf->retType = BType::bool_;
  // Contract clauses (requires/ensures). Reuses parseFuncContracts verbatim  - 
  // it lowers each clause via parseSpecExpr (inSpec set), so `result`, `forall`,
  // `implies`, `old(x)` all work as on a FuncDecl. An asm spec with no clauses
  // is degenerate (just the uninterpreted symbol) but still legal + sound.
  parseFuncContracts(sf->requires_, sf->ensures_);
  expect(Tok::semicolon, "';' after asm spec declaration");
  return sf;
}


// T3  -  `region Name = { var1, var2, ghost_let1, ... };`
// A named union of bare source-level names (runtime arrays/globals, ghost
// lets). The members list lives in `members` for the Ghost encoder to expand
// in `modifies` frame axioms. We do NOT type-check members here  -  Sema/encoder
// resolve them lazily.
std::unique_ptr<RegionDecl> Parser::parseRegion() {
  p_++;   // eat `region`
  auto rg = std::make_unique<RegionDecl>();
  rg->line = cur().line;
  if (!expect(Tok::ident, "region name")) return nullptr;
  rg->name = toks_[p_ - 1].text;
  if (!expect(Tok::eq, "'=' in region declaration")) return nullptr;
  if (!expect(Tok::lbrace, "'{' in region declaration")) return nullptr;
  while (!at(Tok::rbrace) && !at(Tok::end)) {
    if (!expect(Tok::ident, "region member name")) break;
    rg->members.push_back(toks_[p_ - 1].text);
    if (!accept(Tok::comma)) break;
  }
  expect(Tok::rbrace, "'}' to close region members");
  expect(Tok::semicolon, "';' after region declaration");
  return rg;
}

// T1  -  `refines <concrete_fn> <= <spec_fn> ;`
// A module-level declaration stating the concrete fn's behaviour implies the
// abstract spec's. The encoder emits a discharge query per refines. We just
// record the two names and the line; signature matching + discharge happens
// in the Ghost encoder (which has both FuncDecl and SpecFnDecl visible).
std::unique_ptr<RefinesDecl> Parser::parseRefines() {
  p_++;   // eat `refines`
  auto rf = std::make_unique<RefinesDecl>();
  rf->line = cur().line;
  if (!expect(Tok::ident, "concrete function name after 'refines'")) return nullptr;
  rf->concreteName = toks_[p_ - 1].text;
  if (!accept(Tok::lteq)) {
    error("expected '<=' between concrete and spec fn names in 'refines'");
    return nullptr;
  }
  if (!expect(Tok::ident, "spec function name after '<=' in 'refines'")) return nullptr;
  rf->specName = toks_[p_ - 1].text;
  expect(Tok::semicolon, "';' after refines declaration");
  return rf;
}

// Missing-#6  -  `preserves <handler_fn> <= <invariant_spec_fn> ;`
// A module-level declaration stating the handler preserves the named Bool
// invariant across its execution. The encoder emits a per-handler discharge
// query: forall args. requires_handler(args) ==> I(poststate), where the
// poststate binds `result` to the #2 WP-inlined body's terminal term.
// We just record the two names and the line; signature matching + discharge
// happens in the Ghost encoder (which has both FuncDecl and SpecFnDecl
// visible). The grammar mirrors `refines` exactly.
std::unique_ptr<PreservesDecl> Parser::parsePreserves() {
  p_++;   // eat `preserves`
  auto pv = std::make_unique<PreservesDecl>();
  pv->line = cur().line;
  if (!expect(Tok::ident, "handler function name after 'preserves'")) return nullptr;
  pv->concreteName = toks_[p_ - 1].text;
  if (!accept(Tok::lteq)) {
    error("expected '<=' between handler and invariant spec fn names in 'preserves'");
    return nullptr;
  }
  if (!expect(Tok::ident, "invariant spec fn name after '<=' in 'preserves'")) return nullptr;
  pv->specName = toks_[p_ - 1].text;
  expect(Tok::semicolon, "';' after preserves declaration");
  return pv;
}

// ox:proof D8  -  `noninterference <h1>, <h2>, ... <= <invariant_spec_fn> ;`
// A module-level declaration stating the named handlers are pairwise
// interference-free w.r.t. the shared invariant (Owicki-Gries stability).
// The encoder (emitNoninterference in Ghost.cpp) cross-checks every ordered
// pair (hA, hB) with hA ≠ hB: for each, discharge
//   forall shared. req_hA(args_hA) ∧ I(pre, args_hB) ∧ post == step_hA(pre)
//       ==> I(post, args_hB)
// `unsat` ⇒ hA's atomic step doesn't falsify the invariant for hB. We just
// collect the handler name list + the invariant name + the line; resolution
// and discharge happens in the Ghost encoder (which has FuncDecl + SpecFnDecl
// visible). The grammar mirrors `preserves` but takes a comma-separated list
// of handler names before the `<=`.
std::unique_ptr<NoninterferenceDecl> Parser::parseNoninterference() {
  p_++;   // eat `noninterference`
  auto ni = std::make_unique<NoninterferenceDecl>();
  ni->line = cur().line;
  // Parse comma-separated handler names: h1, h2, h3, ...
  if (!expect(Tok::ident, "handler function name after 'noninterference'")) return nullptr;
  ni->handlers.push_back(toks_[p_ - 1].text);
  while (accept(Tok::comma)) {
    if (!expect(Tok::ident, "handler function name after ','")) return nullptr;
    ni->handlers.push_back(toks_[p_ - 1].text);
  }
  if (!accept(Tok::lteq)) {
    error("expected '<=' between handler list and invariant spec fn in 'noninterference'");
    return nullptr;
  }
  if (!expect(Tok::ident, "invariant spec fn name after '<=' in 'noninterference'")) return nullptr;
  ni->specName = toks_[p_ - 1].text;
  expect(Tok::semicolon, "';' after noninterference declaration");
  return ni;
}

// D9 (gap #6)  -  `cycle_preserves <h1>, <h2>, ... <= <invariant_spec_fn> ;`
// Module-level declaration stating each named handler re-establishes the cycle
// invariant across the VM-exit cycle (vmlaunch → guest → exit → handler →
// vmresume). The encoder (emitCyclePreserves in Ghost.cpp) emits ONE discharge
// query PER handler (NOT cross-handler  -  that's `noninterference`'s job):
//   NOT (forall (cycle_args). req_handler(cycle_args) ∧ I_pre(cycle_args)
//                              ==> I_post(cycle_args_post_handler))
// with vmlaunch/vmresume modelled as IDENTITY on the cycle invariant (the VM
// transitions don't touch the invariant's state; only the handler does). `unsat`
// ⇒ the handler re-establishes the invariant at the next VM exit. We just
// collect the handler name list + the invariant name + the line; resolution
// and discharge happens in the Ghost encoder (which has FuncDecl + SpecFnDecl
// visible). The grammar mirrors `noninterference` exactly: a comma-separated
// list of handler names, then `<=`, then the invariant spec fn name, then `;`.
std::unique_ptr<CyclePreservesDecl> Parser::parseCyclePreserves() {
  p_++;   // eat `cycle_preserves`
  auto cp = std::make_unique<CyclePreservesDecl>();
  cp->line = cur().line;
  // Parse comma-separated handler names: h1, h2, h3, ...
  if (!expect(Tok::ident, "handler function name after 'cycle_preserves'")) return nullptr;
  cp->handlers.push_back(toks_[p_ - 1].text);
  while (accept(Tok::comma)) {
    if (!expect(Tok::ident, "handler function name after ','")) return nullptr;
    cp->handlers.push_back(toks_[p_ - 1].text);
  }
  if (!accept(Tok::lteq)) {
    error("expected '<=' between handler list and invariant spec fn in 'cycle_preserves'");
    return nullptr;
  }
  if (!expect(Tok::ident, "invariant spec fn name after '<=' in 'cycle_preserves'")) return nullptr;
  cp->specName = toks_[p_ - 1].text;
  expect(Tok::semicolon, "';' after cycle_preserves declaration");
  return cp;
}

// ox:proof D3  -  `axiom <expr>;` (optionally `axiom name: <expr>;`)
// A module-level top-level SMT axiom. The body is a full spec expression
// (inSpec set via parseSpecExpr) so `forall`/`exists`/`implies` are legal  - 
// e.g. `axiom forall gpa. is_ram(gpa) implies gpa >= 0;`. We record the
// optional label name and the parsed body; the Ghost encoder lowers the body
// via smtExpr (same path as a spec fn body) and emits `(assert <body>)` at the
// top of the ghost section. There is no signature, no params, and no define-fun
// wrapper  -  an axiom is a bare top-level assertion over whatever spec fn /
// const-global names the body mentions (resolved via the encoder's
// collectSpecFns/collectConstGlobals maps, identical to spec fn bodies).
//
// Namespace/audit extension  -  also accepts the fully-namespaced trusted form:
//   trusted axiom Namespace::NAME: <expr> source "Intel SDM Vol 3C ...";
// `trusted` (optional prefix) sets isTrusted=true; `Namespace::Name` (via
// `ident :: ident`) sets namespace_/name; `source "..."` (optional trailing
// clause) sets sourceCitation. The label name, namespace, trusted marker, and
// source citation are all metadata  -  the `(assert <body>)` SMT semantics are
// identical to the plain form. The existing `axiom NAME: <expr>;` and unlabelled
// `axiom <expr>;` forms MUST still parse unchanged.
std::unique_ptr<AxiomDecl> Parser::parseAxiom() {
  auto ax = std::make_unique<AxiomDecl>();
  ax->line = cur().line;
  // Optional `trusted` prefix  -  marks a trusted (non-machine-verified) fact.
  if (at(Tok::kw_trusted)) {
    ax->isTrusted = true;
    p_++;   // eat `trusted`
  }
  expect(Tok::kw_axiom, "'axiom' (in 'axiom <expr>;' or 'trusted axiom ...')");
  // Optional `name:` label or `Namespace::Name:`  -  a bare identifier
  // immediately followed by `:` (label) or by `::` (namespaced name) is the
  // label; otherwise the identifier is the start of the body expr (and we
  // must NOT consume it). We peek ahead to distinguish the shapes:
  //   ident `:`      => label `NAME`
  //   ident `::` ... => namespaced `Namespace::Name` (label is the NAME part)
  //   ident <other>  => body starts with this ident (consume nothing here)
  if (at(Tok::ident)) {
    if (peek(1).kind == Tok::coloncolon) {
      // `Namespace::Name`  -  first ident is the namespace, second is the label.
      ax->namespace_ = cur().text;
      p_++;            // eat namespace ident
      p_++;            // eat `::`
      if (!expect(Tok::ident, "axiom name after 'Namespace::'")) return nullptr;
      ax->name = toks_[p_ - 1].text;
      // A `:` must follow the namespaced name (`Namespace::Name: <expr>`).
      if (!expect(Tok::colon, "':' after axiom name")) return nullptr;
    } else if (peek(1).kind == Tok::colon) {
      // Plain `name:` label.
      ax->name = cur().text;
      p_++;            // eat name ident
      p_++;            // eat `:`
    }
  }
  ax->body = parseSpecExpr();
  if (!ax->body) {
    error("expected an expression after 'axiom'");
    return nullptr;
  }
  // Optional `source "..."` clause  -  documentation citation for the audit
  // report (e.g. `source "Intel SDM Vol 3C §24.6"`). Metadata only.
  if (at(Tok::kw_source)) {
    p_++;   // eat `source`
    if (!expect(Tok::str_lit, "string literal after 'source'")) return nullptr;
    ax->sourceCitation = toks_[p_ - 1].text;
  }
  expect(Tok::semicolon, "';' after axiom declaration");
  return ax;
}

// ox:unsafe fix2  -  parse the tail of a `trap [handler] name(...) ...` declaration.
// PRECONDITION: the caller has already consumed the leading `trap` (and the
// optional `handler` keyword). The grammar mirrors `parseFunc`'s post-`fn`
// tail (name, generic params, `( params )`, optional `-> Ret`, contract
// clauses, body-or-semicolon)  -  deliberately NOT factored into a shared
// helper because parseFunc's tail is entangled with virtual/override/self
// receiver handling that does not apply to a free top-level trap handler.
// A trap handler has NO self receiver and NO virtual/override (it's a free
// fn-like decl triggered by hardware, not an impl method). Stamps
// `isTrapHandler=true` so the SMT encoder assumes its requires.
//
// `withBody` is a hint but the ACTUAL form is decided by the terminator after
// the signature+contracts: `{` => Form 2 (handler with body, isExtern=false),
// `;` => Form 1 (prototype stub, isExtern=true). This lets the task's test
// file (`trap name(...) { body }`  -  no `handler` keyword) and the spec's
// example (`trap handler name(...) { body }`) both parse to a body form.
std::unique_ptr<FuncDecl> Parser::parseTrapHandler(bool withBody) {
  (void)withBody;   // terminator decides; the hint is kept for API clarity
  auto fn = std::make_unique<FuncDecl>();
  fn->line = cur().line;
  fn->isTrapHandler = true;   // fix2  -  the one semantic flag that matters
  if (!expect(Tok::ident, "trap handler name")) return nullptr;
  fn->name = toks_[p_ - 1].text;
  // Generic type params are parsed (and dropped) for forward-compat  -  the
  // SMT encoder doesn't monomorphize a trap handler for v1. Mirrors spec fn.
  parseTypeParamsRich(fn->tparams);
  fn->isGeneric = !fn->tparams.empty();
  fn->typeParams.reserve(fn->tparams.size());
  for (auto& tp : fn->tparams) fn->typeParams.push_back(tp.name);
  if (!expect(Tok::lparen, "'(' after trap handler name")) return nullptr;
  // ox:unsafe No self-receiver on a trap handler (it's a free hardware-triggered fn,
  // not an impl method). Parse an ordinary comma-separated param list.
  while (!at(Tok::rparen) && !at(Tok::end)) {
    if (!expect(Tok::ident, "parameter name")) break;
    std::string pname = toks_[p_ - 1].text;
    if (!expect(Tok::colon, "':' after parameter name")) break;
    BType pt = parseType();
    Param pp; pp.name = pname; pp.type = pt;
    // Default args: only meaningful for the body form. A prototype (`;`)
    // mirrors a C ABI stub whose signature the Oxide side can't add defaults
    // to (same rule as extern fn in parseFunc). We tentatively parse a default
    // whenever `=` follows; `isExtern` is set below once the terminator is
    // known, and Sema enforces trailing-ness either way.
    if (accept(Tok::eq)) {
      pp.hasDefault = true;
      pp.defaultExpr = parseAssign();
    }
    fn->params.push_back(std::move(pp));
    if (!accept(Tok::comma)) break;
  }
  expect(Tok::rparen, "')'");
  if (accept(Tok::arrow)) {
    fn->retType = parseType();
  } else {
    fn->retType = BType::void_;
  }
  // ox:proof Part 2  -  optional `discharge` clause marker:
  //   `trap handler name(...) discharge requires ... ensures ... { body }`
  // When present, sets `dischargeRequires=true` so the SMT encoder DISCHARGES
  // this trap handler's `requires` clauses (proves they follow from the
  // VM-exit context axioms + any user `axiom` declarations about the VMCS /
  // exit state) instead of ASSUMING them as hardware-guaranteed premises.
  // The keyword is legal ONLY in this position  -  after the optional `-> Ret`
  // and BEFORE any `requires`/`ensures`/`modifies`/`decreases` clause. A
  // `discharge` token outside this position leaves `dischargeRequires=false`
  // (the lexer still emits `Tok::kw_discharge`; the parse loop below never
  // consults it elsewhere).
  if (at(Tok::kw_discharge)) {
    p_++;   // eat `discharge`
    fn->dischargeRequires = true;
  }
  // Contract clauses (requires / ensures / modifies / decreases)  -  parsed
  // whenever present (they only appear before a `{` body in well-formed
  // input; a prototype is `trap name(...);` with no contracts). A prototype
  // (terminated by `;`) with contracts would be a parse oddity but we still
  // accept the clauses; `isExtern=true` skips their SMT discharge anyway.
  while (at(Tok::kw_requires) || at(Tok::kw_ensures) ||
         at(Tok::kw_modifies) || at(Tok::kw_decreases) ||
         at(Tok::kw_effects)) {
    if (at(Tok::kw_requires) || at(Tok::kw_ensures)) {
      parseFuncContracts(fn->requires_, fn->ensures_);
    } else if (at(Tok::kw_decreases)) {
      // D6  -  `decreases <spec-expr>` (at most one per fn; last wins). A
      // trap handler's termination measure is discharged at each recursive
      // call site by the recursion guard  -  same as an ordinary fn.
      p_++;   // eat `decreases`
      fn->decreases = parseSpecExpr();
    } else if (at(Tok::kw_effects)) {
      // ox:unsafe Effect system  -  `effects { ... }` (or `effects { }`). A trap handler
      // may carry an effects clause (e.g. `effects { vmcs_read, vmcs_write }`)
      // mirroring the contract-clause tail of an ordinary fn; the Sema effect
      // propagation + purity checks apply to trap handlers identically.
      // `fn->effectsExplicit` is latched by parseEffects on consume.
      parseEffects(fn->effects, fn->effectsExplicit);
    } else {
      parseModifies(fn->modifies);
    }
  }
  // Post-signature `where T: ConceptName, ...` clause  -  the deferred spelling
  // of the inline `<T: C>` constraint, mirroring parseFunc. Parsed AFTER the
  // contract clauses so the `where` keyword isn't mis-tokenised by the
  // requires/ensures loop. A where item overrides any inline `T: C` set by
  // parseTypeParamsRich. Inert for prototype stubs; validated at instantiation
  // for body-form trap handlers (where the SMT encoder monomorphises generically).
  if (accept(Tok::kw_where)) {
    applyWhereClause(fn->tparams);
    fn->typeParams.clear();
    fn->typeParams.reserve(fn->tparams.size());
    for (auto& tp : fn->tparams) fn->typeParams.push_back(tp.name);
    fn->isGeneric = !fn->tparams.empty();
  }
  // Disambiguate by terminator: `{` => Form 2 (handler with body),
  // `;` => Form 1 (prototype stub). `isExtern` picks up the Form 1 stub
  // semantics (no body verification, no codegen) for free.
  if (at(Tok::lbrace)) {
    fn->isExtern = false;
    fn->body = parseBlock();
    return fn;
  }
  fn->isExtern = true;   // prototype  -  signature stub only
  expect(Tok::semicolon, "';' after trap prototype declaration");
  return fn;
}

// `macro name(p1, p2, ...) { <let-stmts> <result expr> }`  -  compile-time code
// transformation. The parameter list is a comma-separated set of bare
// identifiers (no types; macros are untyped  -  the substituted body is type-
// checked in full at each use site). The body is an OPTIONAL sequence of
// leading `let` statements (each `;`-terminated) followed by a single trailing
// expression that is the macro's value; `macro square(x) { ($x) * ($x) }` has
// no lets and `($x) * ($x)` as the result, while `macro max3(a,b,c) { let ab =
// imax($a,$b); imax(ab,$c) }` has one let and `imax(ab,$c)` as the result.
// Struct-literal `{` recognition is disabled while parsing the body so a `{}`
// following the result isn't mis-read as a struct literal. `$param` markers
// inside the body are parsed by parsePrimary as VarRef nodes named `"$" + param`;
// Sema's macro expander clones the body and substitutes each `$param` VarRef
// with the corresponding caller arg.
std::unique_ptr<MacroDecl> Parser::parseMacro() {
  p_++;   // eat `macro`
  auto m = std::make_unique<MacroDecl>();
  m->line = cur().line;
  if (!expect(Tok::ident, "macro name")) return nullptr;
  m->name = toks_[p_ - 1].text;
  if (!expect(Tok::lparen, "'(' after macro name")) return nullptr;
  while (!at(Tok::rparen) && !at(Tok::end)) {
    if (!expect(Tok::ident, "macro parameter name")) break;
    m->paramNames.push_back(toks_[p_ - 1].text);
    if (!accept(Tok::comma)) break;
  }
  expect(Tok::rparen, "')' to close macro parameter list");
  if (!expect(Tok::lbrace, "'{' to begin macro body")) return nullptr;
  // ox:why Disable struct-literal `{` recognition so a struct-literal-looking `{` in
  // the body opens/closes a struct literal only at struct-literal positions;
  // the macro's CLOSING `}` is consumed by the explicit expect below. Restored
  // after the body parses.
  bool saved = allowStructLit_; allowStructLit_ = false;
  // Leading `let`/`mut` statements  -  the macro's local bindings. Each is a
  // full LetStmt (reuses parseLetMut); trailing `;` is consumed by parseLetMut's
  // statement machinery (parseStmt dispatches let to parseLetMut which expects
  // an init + `;`). Only `let`/`mut` leading statements are supported in v1
  // (control-flow statements as macro body members would complicate the
  // expression-valued expansion contract).
  while (at(Tok::kw_let) || at(Tok::kw_mut)) {
    bool isMut = at(Tok::kw_mut);
    p_++;   // eat `let`/`mut`
    auto s = parseLetMut(isMut);
    if (s) m->bodyStmts.push_back(std::move(s));
  }
  // The trailing result expression  -  the macro's value.
  m->bodyExpr = parseExpr();
  allowStructLit_ = saved;
  expect(Tok::rbrace, "'}' to close macro body");
  // An optional trailing ';' is accepted (and ignored) for ergonomics: a macro
  // declaration is terminated by `}` but a stray `;` (common in list style)
  // should not be a hard error.
  accept(Tok::semicolon);
  return m;
}

std::vector<StmtPtr> Parser::parseBlock() {
  std::vector<StmtPtr> out;
  expect(Tok::lbrace, "'{'");
  while (!at(Tok::rbrace) && !at(Tok::end)) {
    auto s = parseStmt();
    if (s) out.push_back(std::move(s));
    else {

      while (!at(Tok::semicolon) && !at(Tok::rbrace) && !at(Tok::end)) p_++;
      accept(Tok::semicolon);
    }
  }
  expect(Tok::rbrace, "'}'");
  return out;
}

StmtPtr Parser::parseStmt() {
  Tok k = cur().kind;
  // ox:proof T2  -  `ghost let [mut] x: T = e;` is a spec-only binding. We parse it like a
  // regular let (reusing parseLetMut) but repackage into a GhostLetStmt so IRGen
  // skips the alloca and the Ghost encoder can name it. `ghost` is a reserved
  // keyword so it can never be confused with a runtime expr-statement start.
  if (k == Tok::kw_ghost) {
    int gline = cur().line, gcol = cur().col;
    p_++;   // eat `ghost`
    // ox:proof Grammar: `ghost let [mut] x: T = e;`  -  `let` is required; optional `mut`
    // is handled by parseLetMut (it accepts `mut` itself). A bare `ghost mut x`
    // (without `let`) is NOT a valid form; `mut x` standalone is itself a let.
    if (!at(Tok::kw_let)) {
      error("expected 'let' after 'ghost' (use `ghost let [mut] x: T = e;`)");
      return nullptr;
    }
    p_++;   // eat `let`
    auto s = parseLetMut(false);   // parseLetMut consumes optional `mut`
    if (!s) return nullptr;
    // ox:proof Repackage into a GhostLetStmt so IRGen skips the alloca + Ghost encoder
    // can name it. We can't downcast a LetStmt to GhostLetStmt (it isn't one),
    // so we move the fields into a freshly-constructed GhostLetStmt.
    auto gls = std::make_unique<GhostLetStmt>();
    auto* lsRaw = static_cast<LetStmt*>(s.get());
    gls->isMut         = lsRaw->isMut;
    gls->name          = lsRaw->name;
    gls->type          = lsRaw->type;
    gls->typeAnnotated = lsRaw->typeAnnotated;
    gls->init          = std::move(lsRaw->init);
    gls->line          = gline;
    gls->col           = gcol;
    return gls;
  }
  if (k == Tok::kw_let) { p_++; return parseLetMut(false); }
  if (k == Tok::kw_mut) { p_++; return parseLetMut(true); }
  if (k == Tok::kw_return) return parseReturn();
  if (k == Tok::kw_if) return parseIf();
  if (k == Tok::kw_while) return parseWhile();
  if (k == Tok::kw_for) return parseFor();
  if (k == Tok::kw_match) return parseMatch();
  if (k == Tok::kw_assert) {
    int line = cur().line, col = cur().col;
    p_++;
    auto a = std::make_unique<AssertStmt>();
    a->line = line; a->col = col;
    // Optional parens: `assert (expr)` or `assert expr`. The body is a spec
    // expression so `forall`/`implies` work inside it.
    bool hasParen = accept(Tok::lparen);
    if (hasParen) {
      a->cond = parseSpecExpr();
      expect(Tok::rparen, "')' to close assert(...)");
    } else {
      a->cond = parseSpecExpr();
    }
    // `assert <expr> by { <hints> };`  -  optional proof hints block emitted as
    // SMT premises before the assertion condition is discharged. parseBlock
    // consumes `{`..`}`; the trailing `;` closes the statement.
    if (accept(Tok::kw_by)) {
      expect(Tok::lbrace, "'{' after 'by' in assert<...> by { ... }");
      while (!at(Tok::rbrace) && !at(Tok::end)) {
        auto s = parseStmt();
        if (s) a->byBody.push_back(std::move(s));
        else {
          while (!at(Tok::semicolon) && !at(Tok::rbrace) && !at(Tok::end)) p_++;
          accept(Tok::semicolon);
        }
      }
      expect(Tok::rbrace, "'}' to close assert<...> by { ... }");
    }
    expect(Tok::semicolon, "';' after assert");
    return a;
  }
  // ox:proof `assume <expr>;` (non-trusted) or `trusted assume <expr>;`  -  hypothesis
  // assumption. The condition is a spec expression (forall/implies legal).
  // The trusted form may carry a trailing `source "<citation>";` clause. See
  // parseAssume. NOTE: the proof block's internal `assume <IH>` is parsed
  // directly by parseProof (above the `kw_proof` dispatch), so this dispatch
  // only fires for the standalone statement form  -  there is no conflict.
  if (k == Tok::kw_assume || k == Tok::kw_trusted) return parseAssume();
  // ox:proof fixB  -  `instantiate forall k. P on ...;` guided instantiation pragma.
  if (k == Tok::kw_instantiate) return parseInstantiate();
  // ox:proof fixC  -  `proof (that)? forall k. P by induction on k: base: ...; step:
  // assume ...; prove ...;` interactive proof block.
  if (k == Tok::kw_proof) return parseProof();
  // calcD  -  `calc { <expr>; <REL> {hints;} <expr>; ... }` equational-reasoning
  // block. Sema type-checks each step expression + hint; the SMT encoder
  // discharges each consecutive pair under the hints. Spec-only (no codegen).
  if (k == Tok::kw_calc) return parseCalc();
  if (k == Tok::kw_defer) {
    int line = cur().line;
    p_++;
    auto ds = std::make_unique<DeferStmt>();
    ds->line = line;
    if (at(Tok::lbrace)) {
      auto blk = std::make_unique<Block>();
      blk->stmts = parseBlock();
      ds->body = std::move(blk);
    } else {
      auto s = parseStmt();
      if (s) ds->body = std::move(s);
    }
    return ds;
  }
  if (k == Tok::kw_break) {
    int line = cur().line;
    p_++;
    accept(Tok::semicolon);
    auto b = std::make_unique<BreakStmt>();
    b->line = line;
    return b;
  }
  if (k == Tok::kw_continue) {
    int line = cur().line;
    p_++;
    accept(Tok::semicolon);
    auto c = std::make_unique<ContinueStmt>();
    c->line = line;
    return c;
  }
  // D7  -  `atomic { <stmts> }` block: a non-interleavable step mark.
  // Semantically identical to a plain `{ <stmts> }` block (sequential WP, same
  // codegen); the Parser sets `Block::isAtomic=true` so the SMT witness output
  // records the atomicity claim via a `; note: atomic block at line N` comment.
  // The block must be brace-delimited (an `atomic <stmt>;` form is rejected  - 
  // an atomic region is inherently multi-statement).
  if (k == Tok::kw_atomic) {
    int line = cur().line, col = cur().col;
    p_++;   // eat `atomic`
    if (!at(Tok::lbrace)) {
      error("expected '{' to begin atomic block (use `atomic { <stmts> }`)");
      return nullptr;
    }
    auto blk = std::make_unique<Block>();
    blk->isAtomic = true;
    blk->line = line;
    blk->col = col;
    blk->stmts = parseBlock();   // parseBlock consumes `{`..`}`
    return blk;
  }
  // `sync { <stmts> }`  -  a block whose body runs between @ox_sync_begin (enter
  // a barrier) and @ox_sync_end (leave it) so concurrent `spawn`ed threads may
  // rendezvous before/from the block. Semantically like a plain Block (fresh
  // Sema scope for the inner names); IRGen wraps the lowered body with the two
  // runtime calls. The block must be brace-delimited (a sync region is a
  // multi-statement scope, like `atomic` and a bare `{ ... }`).
  if (k == Tok::kw_sync) {
    int line = cur().line, col = cur().col;
    p_++;   // eat `sync`
    if (!at(Tok::lbrace)) {
      error("expected '{' to begin sync block (use `sync { <stmts> }`)");
      return nullptr;
    }
    auto sb = std::make_unique<SyncBlock>();
    sb->line = line;
    sb->col = col;
    sb->body = parseBlock();   // parseBlock consumes `{`..`}`
    return sb;
  }
  // `unsafe { <stmts> }`  -  block scope where unsafe operations (raw pointer
  // deref, inline asm, extern calls, unchecked casts, calls to `unsafe fn`) are
  // permitted. Brace-delimited like `atomic`/`sync` (an unsafe region is
  // inherently multi-statement). Top-level `unsafe fn <name>...` is handled in
  // parseProgram, NOT here. `unsafe` is reserved and never a valid expression,
  // so a stray `unsafe` not immediately followed by `{` is a parse error.
  if (k == Tok::kw_unsafe) {
    return parseUnsafeBlock();
  }
  if (at(Tok::lbrace)) {
    auto blk = std::make_unique<Block>();
    blk->stmts = parseBlock();
    return blk;
  }
  auto es = std::make_unique<ExprStmt>();
  es->line = cur().line;
  es->expr = parseExpr();
  accept(Tok::semicolon);
  return es;
}

StmtPtr Parser::parseLetMut(bool isMut) {
  auto ls = std::make_unique<LetStmt>();
  if (accept(Tok::kw_mut)) isMut = true;
  ls->isMut = isMut;
  ls->line = cur().line;
  if (!expect(Tok::ident, "variable name")) { return nullptr; }
  ls->name = toks_[p_ - 1].text;
  if (accept(Tok::colon)) {
    ls->type = parseType();
    ls->typeAnnotated = true;
  }
  if (accept(Tok::eq)) {
    if (!at(Tok::semicolon)) ls->init = parseExpr();
  }
  expect(Tok::semicolon, "';' after variable");
  return ls;
}

StmtPtr Parser::parseReturn() {
  int line = cur().line;
  p_++;
  auto rs = std::make_unique<ReturnStmt>();
  rs->line = line;
  if (!at(Tok::semicolon)) {
    rs->value = parseExpr();
  }
  expect(Tok::semicolon, "';' after return");
  return rs;
}

// `unsafe { <stmts> }`  -  a block scope where unsafe operations (raw pointer
// deref, inline asm, extern calls, volatile MMIO, unchecked casts, calls to
// `unsafe fn`) are permitted by Sema. Outside an `unsafe` block such operations
// are compile errors. The block introduces a new lexical scope (like `{ }`);
// statements inside are checked normally with the Sema `inUnsafe_` flag set.
// Mirrors parseSyncBlock, but emits an UnsafeBlock AST node.
StmtPtr Parser::parseUnsafeBlock() {
  int line = cur().line, col = cur().col;
  p_++;   // eat `unsafe`
  if (!at(Tok::lbrace)) {
    error("expected '{' to begin unsafe block (use `unsafe { <stmts> }`)");
    return nullptr;
  }
  auto ub = std::make_unique<UnsafeBlock>();
  ub->line = line;
  ub->col = col;
  ub->body = parseBlock();   // parseBlock consumes `{`..`}`
  return ub;
}

static void copyStmtPos(Stmt* s, const Expr& e) { s->line = e.line; s->col = e.col; }

StmtPtr Parser::parseIf() {
  p_++;
  auto is = std::make_unique<IfStmt>();
  is->line = cur().line;
  bool hasParen = accept(Tok::lparen);
  bool saved = allowStructLit_; allowStructLit_ = false;
  is->cond = parseExpr();
  allowStructLit_ = saved;
  if (hasParen) expect(Tok::rparen, "')'");
  if (at(Tok::lbrace)) {
    is->then = parseBlock();
  } else {
    auto s = parseStmt();
    if (s) is->then.push_back(std::move(s));
  }
  if (accept(Tok::kw_else)) {
    if (at(Tok::kw_if)) {
      auto nested = parseIf();
      if (nested) is->else_.push_back(std::move(nested));
    } else if (at(Tok::lbrace)) {
      is->else_ = parseBlock();
    } else {
      auto s = parseStmt();
      if (s) is->else_.push_back(std::move(s));
    }
  }
  return is;
}

StmtPtr Parser::parseWhile() {
  p_++;
  auto ws = std::make_unique<WhileStmt>();
  ws->line = cur().line;
  bool hasParen = accept(Tok::lparen);
  bool saved = allowStructLit_; allowStructLit_ = false;
  ws->cond = parseExpr();
  allowStructLit_ = saved;
  if (hasParen) expect(Tok::rparen, "')'");
  parseLoopInvariants(ws->invariants);
  if (at(Tok::lbrace)) ws->body = parseBlock();
  else { auto s = parseStmt(); if (s) ws->body.push_back(std::move(s)); }
  return ws;
}


StmtPtr Parser::parseMatch() {
  p_++;
  int matchLine = cur().line;


  bool saved = allowStructLit_; allowStructLit_ = false;
  ExprPtr scrutinee = parseExpr();
  allowStructLit_ = saved;
  if (!expect(Tok::lbrace, "'{' to begin match body")) return nullptr;


  std::vector<std::pair<ExprPtr, std::vector<StmtPtr>>> arms;
  while (!at(Tok::rbrace) && !at(Tok::end)) {
    bool isDefault = false;
    if (at(Tok::ident) && cur().text == "_") { isDefault = true; p_++; }
    if (!isDefault) {
      saved = allowStructLit_; allowStructLit_ = false;
      ExprPtr pat = parseExpr();
      allowStructLit_ = saved;
      if (!expect(Tok::fatarrow, "'=>' in match arm")) break;
      std::vector<StmtPtr> body;
      if (at(Tok::lbrace)) body = parseBlock();
      else { auto s = parseStmt(); if (s) body.push_back(std::move(s)); }
      arms.emplace_back(std::move(pat), std::move(body));
      accept(Tok::comma);
      continue;
    }
    if (!expect(Tok::fatarrow, "'=>' in match arm")) break;
    std::vector<StmtPtr> body;
    if (at(Tok::lbrace)) body = parseBlock();
    else { auto s = parseStmt(); if (s) body.push_back(std::move(s)); }
    arms.emplace_back(nullptr, std::move(body));
    accept(Tok::comma);
  }
  expect(Tok::rbrace, "'}'");


  const std::string synth = "__match_scrut";
  auto let = std::make_unique<LetStmt>();
  let->isMut = false; let->name = synth;
  let->type = BType::void_;
  let->typeAnnotated = false;
  let->init = std::move(scrutinee);
  let->line = matchLine;


  StmtPtr chain;
  IfStmt* live = nullptr;
  std::vector<StmtPtr>* fill = nullptr;
  for (size_t i = 0; i < arms.size(); i++) {
    auto& arm = arms[i];
    auto isStmt = std::make_unique<IfStmt>();
    isStmt->line = matchLine;
    bool isDefault = (arm.first == nullptr);
    if (arm.first) {
      auto lhsVar = std::make_unique<VarRef>();
      lhsVar->name = synth; lhsVar->line = matchLine;
      auto cmp = std::make_unique<BinaryExpr>();
      cmp->op = BinaryExpr::Op::eq;
      cmp->line = matchLine;
      cmp->lhs = std::move(lhsVar);
      cmp->rhs = std::move(arm.first);
      isStmt->cond = std::move(cmp);
    } else {

      auto t = std::make_unique<BoolLit>(); t->v = true; t->line = matchLine;
      isStmt->cond = std::move(t);
    }
    isStmt->then = std::move(arm.second);

    if (!chain) {
      live = isStmt.get();
      chain = std::move(isStmt);
    } else {
      fill->push_back(std::move(isStmt));
      live = dynamic_cast<IfStmt*>(fill->back().get());
    }

    fill = isDefault ? nullptr : &live->else_;
  }

  auto blk = std::make_unique<Block>();
  blk->line = matchLine;
  blk->stmts.push_back(std::move(let));
  if (chain) blk->stmts.push_back(std::move(chain));
  return blk;
}

StmtPtr Parser::parseFor() {
  p_++;
  auto fs = std::make_unique<ForStmt>();
  fs->line = cur().line;
  bool hasParen = accept(Tok::lparen);
  accept(Tok::kw_let);
  accept(Tok::kw_mut);
  if (!expect(Tok::ident, "loop variable")) return nullptr;
  fs->varName = toks_[p_ - 1].text;

  // `for k, v in <iterable>`  -  an optional second loop variable, parsed BEFORE
  // the `in` keyword. Bound to the iterated value; only meaningful for maps
  // (arrays/vecs/strings expose one element; Sema rejects a 2nd var there).
  if (at(Tok::comma)) {
    p_++;
    if (!expect(Tok::ident, "second loop variable after ','")) return nullptr;
    fs->varName2 = toks_[p_ - 1].text;
  }

  if (at(Tok::kw_in)) {
    p_++;
    fs->isForeach = true;
    bool saved = allowStructLit_; allowStructLit_ = false;
    fs->iter = parseExpr();
    allowStructLit_ = saved;
    if (hasParen) expect(Tok::rparen, "')'");
    parseLoopInvariants(fs->invariants);
    if (at(Tok::lbrace)) fs->body = parseBlock();
    else { auto s = parseStmt(); if (s) fs->body.push_back(std::move(s)); }
    return fs;
  }
  expect(Tok::eq, "'=' in for header");
  fs->start = parseExpr();
  expect(Tok::semicolon, "';'");
  bool saved = allowStructLit_; allowStructLit_ = false;
  fs->end = parseExpr();
  allowStructLit_ = saved;
  expect(Tok::semicolon, "';'");
  fs->step = parseExpr();
  if (hasParen) expect(Tok::rparen, "')'");
  parseLoopInvariants(fs->invariants);
  if (at(Tok::lbrace)) fs->body = parseBlock();
  else { auto s = parseStmt(); if (s) fs->body.push_back(std::move(s)); }
  return fs;
}

ExprPtr Parser::parseExpr() { return parseRange(); }

// ox:proof Parse a contract spec expression. Spec-only forms (`old`, `forall`, `exists`,
// `implies`) are enabled only while inSpec_ is true, which this sets for the
// duration and restores  -  so a stray `implies`/`forall` outside a contract is
// never silently accepted (it falls back to an ident/expr parse and Sema flags
// the leftover token). A spec expression is a full expression (range/assign/
// ternary/implies)  -  contracts use `parseAssign` so assignment-as-expression is
// NOT part of a contract (a contract is a boolean predicate, not code).
ExprPtr Parser::parseSpecExpr() {
  bool saved = inSpec_;
  inSpec_ = true;
  // A contract clause is followed (eventually) by either the next clause
  // keyword or a `{` block (function/loop body). Disable struct-literal
  // parsing for the duration: otherwise a trailing identifier in the clause
  // (e.g. the `i` in `invariant 0 <= i`) followed by the loop's `{` body is
  // mis-read as `i { ... }` struct literal, eating the whole body. Mirrors the
  // `allowStructLit_=false` already applied around the while/for condition.
  bool savedSL = allowStructLit_;
  allowStructLit_ = false;
  auto e = parseTernary();
  allowStructLit_ = savedSL;
  inSpec_ = saved;
  return e;
}

// `fn ... requires E0 requires E1 ensures E2 { ... }`  -  one or more clauses,
// each `KEYWORD <spec-expr>`, with NO semicolons (a clause ends at the next
// `requires`/`ensures`/`{`). Called only for defined (non-extern) functions,
// after the signature and before the `{`.
void Parser::parseFuncContracts(std::vector<ExprPtr>& out_req,
                                std::vector<ExprPtr>& out_ens) {
  while (at(Tok::kw_requires) || at(Tok::kw_ensures)) {
    Tok k = cur().kind;
    p_++;
    auto e = parseSpecExpr();
    if (k == Tok::kw_requires) out_req.push_back(std::move(e));
    else out_ens.push_back(std::move(e));
  }
}

// T3  -  `modifies Name1, Name2, ...` on a fn head. Parsed AFTER the
// requires/ensures clauses (the grammar is `requires ... ensures ... modifies
// ... {`). `modifies` may also appear BEFORE the contracts and may occur more
// than once; we accept zero or more occurrences (the caller -- parseFunc --
// currently calls us once, after contracts; a `modifies` placed before
// `requires` would not be consumed here and would cause "expected '{'"  -  a
// documented v1 limit: the recommended style is contracts-then-modifies). Each
// occurrence is one or more comma-separated identifiers (region or global/
// ghost-let names), terminated by the next clause keyword or the body `{` /
// extern `;`. We append each bare name to `out`; the Ghost encoder expands
// region names lazily at SMT-emit time.
void Parser::parseModifies(std::vector<std::string>& out) {
  while (at(Tok::kw_modifies)) {
    p_++;   // eat `modifies`
    // One `modifies` clause takes a non-empty comma-separated list of names.
    // We parse at least one ident to keep the grammar unambiguous; further
    // idents are accepted via comma. We stop at the next keyword or `{`.
    do {
      if (!at(Tok::ident)) {
        error("expected identifier in 'modifies' clause");
        return;
      }
      out.push_back(cur().text);
      p_++;
    } while (accept(Tok::comma));
  }
}

// Effect system  -  `effects { eff1, eff2, ... }` (or `effects { }`) on a fn head.
// Mirrors parseModifies but brace-delimited: consume `effects`, expect `{`,
// parse a (possibly empty) comma-separated list of identifiers naming effects
// (built-in like io/alloc/asm/mmio/vmcs_read/vmcs_write/mem_write/panic/trap/
// sched, or user-defined), until `}`. Empty `{ }` is accepted (pure). May occur
// more than once; names accumulate into `out` (Sema dedups). Effect names are
// bare identifiers  -  no type, no value  -  so a stray `}` after `effects {`
// cleanly yields a pure fn. Rejected ill-formed forms (`effects` without `{`,
// or a non-identifier where an effect name is expected) emit a parse error and
// return, so the caller's tail loop stops consuming.
// `outExplicit` is set true on the FIRST `effects` clause consumed (latched;
// never reset to false here)  -  the caller seeds it false before the tail loop,
// and even an empty `effects { }` latches it true, distinguishing "explicitly
// pure" from "omitted (untracked)" for the Sema/Ghost gates downstream.
void Parser::parseEffects(std::vector<std::string>& out, bool& outExplicit) {
  while (at(Tok::kw_effects)) {
    p_++;   // eat `effects`
    outExplicit = true;                   // an `effects` clause was written
    if (!expect(Tok::lbrace, "'{' to begin 'effects' clause")) return;
    // Empty `effects { }` is a valid pure declaration: allow zero names and
    // bail at `rbrace`. We parse comma-separated idents until braces close.
    while (!at(Tok::rbrace) && !at(Tok::end)) {
      if (!at(Tok::ident)) {
        error("expected effect name (identifier) in 'effects' clause");
        return;
      }
      out.push_back(cur().text);
      p_++;
      // Continue only if a comma separates more names; `}` ends the list.
      if (!accept(Tok::comma)) break;
    }
    if (!expect(Tok::rbrace, "'}' to close 'effects' clause")) return;
  }
}


// ox:proof fixB  -  `instantiate forall k. P on ...;` guided-instantiation pragma.
//
// Two syntactic shapes share the same prefix:
//   instantiate forall k in 0..n implies P on k = <witness>;
//   instantiate forall k in 0..n implies P on <term>, <term>, ...;
// The first (GROUND) fixes the binder to a concrete witness value and lets the
// SMT solver discharge P at that single point; the second (PATTERN) hands the
// solver a list of trigger terms to guide quantifier instantiation search.
//
// The quantifier is parsed via parseSpecExpr (so forall/implies behave as spec
// forms); we then downcast the resulting ExprPtr to a QuantExpr. The `on`
// clause disambiguation peeks two tokens ahead: an identifier equal to the
// quantifier's binder followed by `=` is the ground form, otherwise we parse a
// comma-separated pattern list. Each term in the `on` clause is itself a spec
// expression (forall/implies/old are legal inside patterns).
StmtPtr Parser::parseInstantiate() {
  int line = cur().line, col = cur().col;
  p_++;   // eat `instantiate`

  // ox:proof Parse `forall k in lo..hi implies P` as a spec expression so the quantifier
  // forms are recognised. Save/restore inSpec_ for good measure even though
  // parseSpecExpr does so internally  -  the caller's state must be untouched.
  bool savedSpec = inSpec_;
  inSpec_ = true;
  auto qexpr = parseSpecExpr();
  inSpec_ = savedSpec;

  // Downcast ExprPtr -> unique_ptr<QuantExpr>. parseSpecExpr yields a QuantExpr
  // when it sees forall/exists; anything else is a usage error.
  auto* raw = qexpr.release();
  std::unique_ptr<QuantExpr> q(dynamic_cast<QuantExpr*>(raw));
  if (!q) {
    error("instantiate requires a forall/exists quantifier");
    // raw is leaked only on the error path by design (罕见); return a null
    // statement so the caller's parseStmt sees a recovery absence.
    return nullptr;
  }

  auto stmt = std::make_unique<InstantiateStmt>();
  stmt->line = line;
  stmt->col = col;
  stmt->q = std::move(q);

  // `on` separator.
  expect(Tok::kw_on, "'on' in instantiate pragma");

  // Disambiguate the two forms. GROUND: `on <binder> = <expr>`. PATTERN:
  // `on <term>, <term>, ...`. We peek two tokens: current must be an ident
  // whose text matches the quantifier binder, and the following token must be
  // `=`. Guard array bounds: the parser keeps a Tok::end sentinel, so
  // toks_[p_+1] is always safe (end maps to itself).
  bool ground = false;
  if (at(Tok::ident) &&
      cur().text == stmt->q->binder &&
      peek(1).kind == Tok::eq) {
    ground = true;
  }

  if (ground) {
    p_++;                       // eat binder ident
    p_++;                       // eat `=`
    stmt->isGround = true;
    stmt->witness = parseSpecExpr();
  } else {
    stmt->isGround = false;
    // Comma-separated pattern terms. Always parse at least one; the loop then
    // eats trailing `, <term>` pairs. An empty pattern list (stray `;`) is a
    // user error left for Sema to diagnose  -  here we just parse what's there.
    stmt->patternTerms.push_back(parseSpecExpr());
    while (accept(Tok::comma)) {
      stmt->patternTerms.push_back(parseSpecExpr());
    }
  }

  expect(Tok::semicolon, "';' after instantiate pragma");
  return stmt;
}

// ox:proof fixC  -  `proof [that] forall k: i64 in 0..N implies P(k)
//         by induction on k:
//           base: <expr>
//           step: assume <expr>
//                 prove <expr>;`
//
// Interactive proof-mode block. The theorem MUST be a `forall` quantifier
// (parsed via parseSpecExpr with inSpec_ set so forall/implies are legal).
// `that` is optional  -  `proof that forall ...` and `proof forall ...` are
// both accepted. The induction variable is read after `by induction on`
// (it should match the theorem's binder, but the parser does not enforce
// that  -  Sema can). The base case is a ground boolean spec expression; the
// step is `assume <IH>; prove <goal>`. The whole block is a statement, so
// it ends with `;`.
//
// Lemma functions add a SECOND surface form: `proof { <stmts> }` (a brace
// block immediately after `proof`). This is the block-form proof block that
// carries lemma calls (e.g. `add_comm(x, y);`) and `assert`s. The parser
// disambiguates by the terminator that follows `proof` (and optional
// `that`):
//   - `{`            => ProofBlockStmt (lemma-call block form), parsed by
//                       parseBlock; IRGen + the SMT encoder handle it via the
//                       proofBlockStmt arm.
//   - anything else  => the induction-form ProofStmt (the original fixC
//                       path), parsed as a forall theorem + base/step below.
StmtPtr Parser::parseProof() {
  int line = cur().line, col = cur().col;
  p_++;   // eat `proof`

  // ox:proof Optional `that`  -  `proof that forall ...` (induction) or, for the brace
  // block form, `proof that { ... }` (the `that` is accepted but redundant  - 
  // we keep it for ergonomics; the brace is the actual disambiguator).
  accept(Tok::kw_that);

  // ox:proof Lemma-functions brace form: `proof { <stmts> }`. parseBlock consumes
  // `{`..`}`. The body is a sequence of statements; lemma calls parse as
  // ExprStmt holding a `Call` (the SMT encoder detects lemma callees and
  // assumes their ensures with args substituted). Stored on a ProofBlockStmt.
  if (at(Tok::lbrace)) {
    auto blk = std::make_unique<ProofBlockStmt>();
    blk->line = line;
    blk->col = col;
    blk->body = parseBlock();
    // An optional trailing `;` is accepted (the brace form is terminated by
    // `}` but a stray `;` should not be a hard error; matches the lenient
    // style used elsewhere).
    accept(Tok::semicolon);
    return blk;
  }

  // ox:proof Parse the theorem as a spec expression so `forall`/`implies` are
  // recognised. Save/restore inSpec_ so the caller's state is untouched
  // (parseSpecExpr itself does this internally too, but the mirror keeps the
  // contract explicit  -  same idiom as parseInstantiate).
  bool savedSpec = inSpec_;
  inSpec_ = true;
  auto theoExpr = parseSpecExpr();
  inSpec_ = savedSpec;

  // Downcast ExprPtr -> unique_ptr<QuantExpr>. parseSpecExpr yields a
  // QuantExpr when it sees forall/exists; anything else is a usage error.
  auto* raw = theoExpr.release();
  std::unique_ptr<QuantExpr> theo(dynamic_cast<QuantExpr*>(raw));
  if (!theo || !theo->isForall) {
    error("proof requires a forall theorem");
    // raw is leaked only on the error path by design (罕见); return a null
    // statement so the caller's parseStmt sees a recovery absence.
    return nullptr;
  }

  auto stmt = std::make_unique<ProofStmt>();
  stmt->line = line;
  stmt->col = col;
  stmt->theorem = std::move(theo);

  // ox:proof `by induction on <ident>`  -  the induction variable. It should match
  // theorem->binder but the parser does not enforce that (Sema can).
  expect(Tok::kw_by, "'by' in proof block");
  expect(Tok::kw_induction, "'induction' after 'by' in proof block");
  expect(Tok::kw_on, "'on' after 'induction' in proof block");
  if (!expect(Tok::ident, "induction variable name")) { return nullptr; }
  stmt->inductionVar = toks_[p_ - 1].text;
  expect(Tok::colon, "':' after induction variable");

  // `base: <expr>`  -  a ground boolean spec expression.
  expect(Tok::kw_base, "'base' clause in proof block");
  expect(Tok::colon, "':' after 'base'");
  {
    bool sv = inSpec_;
    inSpec_ = true;
    stmt->baseCase = parseSpecExpr();
    inSpec_ = sv;
  }

  // `step: assume <IH> prove <goal>`.
  expect(Tok::kw_step, "'step' clause in proof block");
  expect(Tok::colon, "':' after 'step'");
  expect(Tok::kw_assume, "'assume' in step");
  {
    bool sv = inSpec_;
    inSpec_ = true;
    stmt->ih = parseSpecExpr();
    inSpec_ = sv;
  }
  expect(Tok::kw_prove, "'prove' in step");
  {
    bool sv = inSpec_;
    inSpec_ = true;
    stmt->goal = parseSpecExpr();
    inSpec_ = sv;
  }

  expect(Tok::semicolon, "';' after proof block");
  return stmt;
}

// `assume <expr>;` (non-trusted) and `trusted assume <expr>;` (trusted)  - 
// a hypothesis assumption statement. The condition is a full spec expression
// (parseSpecExpr with inSpec set so `forall`/`implies` work, mirroring
// `assert`). The trusted form may carry a trailing `source "<citation>"`
// clause that attaches a documentation citation (printed in the trust audit).
//
// Grammar:
//   'trusted'? 'assume' <spec-expr> ('source' <str-lit>)? ';'
//
// The leading `trusted` sets `isTrusted=true`; otherwise the assume is a silent
// hypothesis. The standalone statement form is dispatched from parseStmt for
// `kw_assume` and `kw_trusted`; the proof block's internal `assume <IH>` is
// parsed directly by parseProof, so the two productions never collide. See
// AST AssumeStmt.
StmtPtr Parser::parseAssume() {
  int line = cur().line, col = cur().col;

  auto stmt = std::make_unique<AssumeStmt>();
  stmt->line = line;
  stmt->col = col;

  // `trusted assume ...`  -  the trusted form. Consume `trusted` then require
  // `assume` immediately after. A bare `trusted` with no `assume` is a parse
  // error (we don't fall back to another statement kind here  -  parseStmt has
  // already dispatched to us on seeing `kw_trusted`).
  if (at(Tok::kw_trusted)) {
    p_++;   // eat `trusted`
    stmt->isTrusted = true;
    if (!expect(Tok::kw_assume, "'assume' after 'trusted'")) return nullptr;
  } else {
    // ox:proof Bare `assume <expr>;`  -  the non-trusted silent-hypothesis form.
    expect(Tok::kw_assume, "'assume'");
  }

  // ox:proof The condition is a spec expression so `forall`/`implies` are usable inside
  // it, exactly like `assert`. parseSpecExpr saves/restores inSpec_ internally;
  // mirror it explicitly for parity with parseInstantiate/parseProof.
  {
    bool sv = inSpec_;
    inSpec_ = true;
    stmt->cond = parseSpecExpr();
    inSpec_ = sv;
  }
  if (!stmt->cond) {
    error("expected an expression after 'assume'");
    return nullptr;
  }

  // Optional `source "<citation>"` clause (only meaningful for the trusted
  // form; on a non-trusted assume we still accept + record it for parity, but
  // the audit only surfaces trusted assumes  -  a non-trusted assume's source is
  // effectively unreachable metadata). Mirrors parseAxiom's source handling.
  if (at(Tok::kw_source)) {
    p_++;   // eat `source`
    if (!expect(Tok::str_lit, "string literal after 'source'")) return nullptr;
    stmt->sourceCitation = cur().text;
    p_++;   // eat the string literal
  }

  expect(Tok::semicolon, "';' after assume");
  return stmt;
}

// calcD  -  `calc { <expr>; <REL> {hints;} <expr>; ... <expr>; }`
// Equational-reasoning block. The grammar (note the position of `;` vs REL):
//   calc '{'
//     <step-0 expr> ';'
//     ( <REL> '{' <hint-stmts> '}' )?     // REL block between steps (optional)
//     <step-1 expr> ';'
//     ( <REL> '{' <hint-stmts> '}' )?
//     ...
//     <step-N expr> ';'                    // last step: no trailing REL
//   '}'
//
// Each `<step-i expr>` is parsed via parseSpecExpr (inSpec set so forall/implies
// are legal inside a step, mirroring `assert`/`proof`). After each expression a
// `;` is required. BETWEEN a `;` and the next expression, a relation operator
// (`==`/`!=`/`<=`/`>=`/`<`/`>`) MAY appear, optionally followed by a `{ <hint
// stmts> }` block (parsed via parseBlock). The relation connects step i to step
// i+1 and is stored on step i (`steps[i].relation`); the hint block is stored on
// step i (`steps[i].hints`) too, since it justifies the i→i+1 transition.
//
// The last expression has NO trailing REL  -  after its `;` the next token is the
// closing `}`. We detect end-of-chain by checking for `}` right after a `;`.
//
// inSpec_ is saved/restored around each step-expression parse (same idiom as
// parseInstantiate/parseProof), so the caller's state is untouched and the
// spec-grammar productions are precisely scoped to each step.
//
// Saved/restored error recovery: any parseSpecExpr failure is non-fatal here  - 
// the parser's error list already has the diagnostic; we keep building a best-
// effort CalcStmt with whatever step we have so the caller's block walker still
// progresses past the malformed construct. parseBlock itself recovers on its
// own (see its per-stmt fallback loop).
StmtPtr Parser::parseCalc() {
  int line = cur().line, col = cur().col;
  p_++;   // eat `calc`

  auto stmt = std::make_unique<CalcStmt>();
  stmt->line = line;
  stmt->col = col;

  expect(Tok::lbrace, "'{' to begin calc block");

  // The body must contain at least one expression step. Parse step 0.
  {
    bool sv = inSpec_;
    inSpec_ = true;
    CalcStep step0;
    step0.expr = parseSpecExpr();
    inSpec_ = sv;
    stmt->steps.push_back(std::move(step0));
  }
  expect(Tok::semicolon, "';' after first calc expression");

  // Main loop: after each `;` we see one of:
  //   - `}`           => end of the calc block (last step).
  //   - <REL> [`{` <hints> `}`]  => the relation to the NEXT step, plus an
  //                                 optional justification block. After parsing
  //                                 these we parse the next step's expression
  //                                 and its terminating `;`.
  while (!at(Tok::rbrace) && !at(Tok::end)) {
    // Relation operator. One of `==`/`!=`/`<=`/`>=`/`<`/`>`. Stored on the
    // CURRENT (most recently pushed) step, since it connects step i to step i+1.
    std::string rel;
    if      (accept(Tok::eqeq))   rel = "==";
    else if (accept(Tok::bangeq)) rel = "!=";
    else if (accept(Tok::lteq))   rel = "<=";
    else if (accept(Tok::gteq))   rel = ">=";
    else if (accept(Tok::lt))     rel = "<";
    else if (accept(Tok::gt))     rel = ">";
    else {
      error("expected a relation operator ('==','!=','<=','>=','<','>') or '}' "
            "to close the calc block");
      // Recovery: skip to next `}` or `;` so parseBlock-style fallback can finish.
      while (!at(Tok::semicolon) && !at(Tok::rbrace) && !at(Tok::end)) p_++;
      accept(Tok::semicolon);
      break;
    }
    // The relation attaches to the PREVIOUS step (step i, the one already on
    // the steps vector  -  it connects step i to the step we're about to push).
    stmt->steps.back().relation = rel;

    // Optional `{ <hint stmts> }` justification block. Each hint is a full
    // statement (assert / lemma-call ExprStmt / instantiate) parsed via
    // parseBlock, so the sequences are exactly Oxide statement lists. The
    // hints are stored on step i (they justify the i→i+1 transition).
    if (at(Tok::lbrace)) {
      stmt->steps.back().hints = parseBlock();   // parseBlock consumes `{`..`}`
    }

    // The next expression (step i+1).
    {
      bool sv = inSpec_;
      inSpec_ = true;
      CalcStep nextStep;
      nextStep.expr = parseSpecExpr();
      inSpec_ = sv;
      stmt->steps.push_back(std::move(nextStep));
    }
    expect(Tok::semicolon, "';' after calc expression");
  }

  expect(Tok::rbrace, "'}' to close calc block");
  // A trailing `;` after `calc { ... }` is optional (proof statements often end
  // with one); a stray one should not be a hard error  -  matches the lenient style
  // used elsewhere (defer/sync accept a trailing `;`).
  accept(Tok::semicolon);
  return stmt;
}

// `while cond invariant E0 invariant E1 { ... }` and the same for `for`.
void Parser::parseLoopInvariants(std::vector<ExprPtr>& out) {
  while (at(Tok::kw_invariant)) {
    p_++;
    out.push_back(parseSpecExpr());
  }
}

// Integer ranges `a..b` (exclusive, like Rust) and `a..=b` (inclusive). Bind
// looser than assignment so `let r = 0..n` and `for x in 0..=n` read naturally;
// the operands are themselves full `parseAssign` exprs, so `0..n-1` and
// `0..len(arr)` work. A range is currently meaningful ONLY as the iterable of a
// `for x in <range>` (Sema rejects it elsewhere); we still parse it everywhere
// `parseExpr` is used so it composes, but a stray range that isn't consumed by
// `for` is a compile error, not a silent mis-parse.
ExprPtr Parser::parseRange() {
  auto lhs = parseAssign();
  if (at(Tok::dotdot) || at(Tok::dotdoteq)) {
    bool inclusive = at(Tok::dotdoteq);
    int line = cur().line;
    int col = cur().col;
    p_++;
    auto rhs = parseAssign();
    auto r = std::make_unique<RangeLit>();
    r->lo = std::move(lhs);
    r->hi = std::move(rhs);
    r->inclusive = inclusive;
    r->line = line;
    r->col = col;
    return r;
  }
  return lhs;
}


ExprPtr Parser::parseTernary() {
  auto cond = parseImplies();
  if (at(Tok::question)) {
    p_++;
    auto thenE = parseAssign();
    expect(Tok::colon, "expected ':' in ternary expression");
    auto elseE = parseAssign();
    auto t = std::make_unique<TernaryExpr>();
    t->cond = std::move(cond);
    t->thenE = std::move(thenE);
    t->elseE = std::move(elseE);
    return t;
  }
  return cond;
}

ExprPtr Parser::parseAssign() {
  auto lhs = parseTernary();
  // `chan <- val`  -  channel SEND (infix stance of the `<-` operator). After a
  // primary parsed as the LHS, a `<-` in infix position is a send into that
  // channel; the RHS is a full assignment-precedence expression so `ch <- a+b`
  // sends `a+b`. The result type is void (a send produces no value). This sits
  // at the lowest expression precedence (parseAssign) so `chan <- val` is a
  // natural statement-expression; it cannot nest inside comparison/arithmetic.
  if (at(Tok::chanArrow)) {
    int line = cur().line, col = cur().col;
    p_++;   // eat `<-`
    auto rhs = parseAssign();
    auto s = std::make_unique<ChannelSend>();
    s->chan = std::move(lhs);
    s->val = std::move(rhs);
    s->line = line; s->col = col;
    return s;
  }
  if (at(Tok::eq) || at(Tok::pluseq) || at(Tok::minuseq) || at(Tok::stareq) ||
      at(Tok::lasheq) || at(Tok::percenteq) || at(Tok::ampeq) || at(Tok::bareq) ||
      at(Tok::careteq) || at(Tok::shleq) || at(Tok::shreq)) {
    Tok op = cur().kind;
    p_++;
    auto rhs = parseAssign();
    auto a = std::make_unique<AssignTarget>();
    if (auto v = dynamic_cast<VarRef*>(lhs.get())) {
      a->kind = AssignTarget::Kind::var;
      a->name = v->name;
      a->line = v->line;
    } else if (auto ix = dynamic_cast<Index*>(lhs.get())) {
      a->kind = AssignTarget::Kind::index;
      a->base = std::move(ix->base);
      a->index = std::move(ix->index);
      a->line = lhs->line; a->col = lhs->col;
    } else if (auto fl = dynamic_cast<Field*>(lhs.get())) {
      a->kind = AssignTarget::Kind::field;
      a->base = std::move(fl->base);
      a->field = fl->field;
      a->line = lhs->line; a->col = lhs->col;
    } else if (auto u = dynamic_cast<UnaryExpr*>(lhs.get())) {
      if (u->op != UnaryExpr::Op::deref) { error("invalid assignment target"); return lhs; }
      a->kind = AssignTarget::Kind::deref;
      a->base = std::move(u->base);
      a->line = lhs->line; a->col = lhs->col;
    } else {
      error("invalid assignment target");
      return lhs;
    }
    a->value = std::move(rhs);
    if (op != Tok::eq) {
      a->isCompound = true;
      switch (op) {
        case Tok::pluseq: a->compound = BinaryExpr::Op::add; break;
        case Tok::minuseq: a->compound = BinaryExpr::Op::sub; break;
        case Tok::stareq: a->compound = BinaryExpr::Op::mul; break;
        case Tok::lasheq: a->compound = BinaryExpr::Op::div; break;
        case Tok::percenteq: a->compound = BinaryExpr::Op::mod; break;
        case Tok::ampeq: a->compound = BinaryExpr::Op::band; break;
        case Tok::bareq: a->compound = BinaryExpr::Op::bor; break;
        case Tok::careteq: a->compound = BinaryExpr::Op::bxor; break;
        case Tok::shleq: a->compound = BinaryExpr::Op::shl; break;
        case Tok::shreq: a->compound = BinaryExpr::Op::shr; break;
        default: break;
      }
    }
    return a;
  }
  return lhs;
}

ExprPtr Parser::parseLogicOr() {
  auto lhs = parseLogicAnd();
  while (at(Tok::barbar)) {
    p_++;
    auto rhs = parseLogicAnd();
    auto b = std::make_unique<BinaryExpr>();
    b->op = BinaryExpr::Op::lor; b->lhs = std::move(lhs); b->rhs = std::move(rhs);
    lhs = std::move(b);
  }
  return lhs;
}

// ox:proof `A implies B` (binary implication, right-associative)  -  legal ONLY inside a
// contract spec (inSpec_). Outside a spec this returns parseLogicOr() directly
// so a stray `implies` keyword is never consumed as an operator. We DESUGAR to
// `!A || B` (the standard logical equivalence) at parse time: this reuses the
// existing unary-`not` + `||` lowering in Sema, IRGen, and SMT emission, so no
// new runtime/proof machinery is needed and the semantics are identical to a
// dedicated `=>` node. A `BinaryExpr::Op::eq` on the SMT side prints `(or (not A) B)`,
// which Z3 treats the same as `(=> A B)`.
ExprPtr Parser::parseImplies() {
  auto lhs = parseLogicOr();
  if (!inSpec_) return lhs;          // `implies` never consumed outside a spec
  while (at(Tok::kw_implies)) {
    p_++;
    auto rhs = parseImplies();      // right-associative
    // ox:proof Desugar `lhs implies rhs` -> `!lhs || rhs`.
    auto neg = std::make_unique<UnaryExpr>();
    neg->op = UnaryExpr::Op::not_;
    neg->base = std::move(lhs);
    auto b = std::make_unique<BinaryExpr>();
    b->op = BinaryExpr::Op::lor;
    b->lhs = std::move(neg);
    b->rhs = std::move(rhs);
    lhs = std::move(b);
  }
  return lhs;
}
ExprPtr Parser::parseLogicAnd() {
  // We descend through Equality → Rel → BitOr so comparisons wrap bitwise ops
  // (C/Rust standard)  -  see parseBitOr for why this matters.
  auto lhs = parseEquality();
  while (at(Tok::ampamp)) {
    p_++;
    auto rhs = parseEquality();
    auto b = std::make_unique<BinaryExpr>();
    b->op = BinaryExpr::Op::land; b->lhs = std::move(lhs); b->rhs = std::move(rhs);
    lhs = std::move(b);
  }
  return lhs;
}
ExprPtr Parser::parseBitOr() {
  // Bitwise `|` binds LOOSER than `^` and `&` but TIGHTER than relational/equality  - 
  // the C/Rust/Java standard. Previously Oxide chained `BitOr → BitXor → BitAnd →
  // Equality → Rel → Shift`, which put `==` tighter than `&`, so `mask & 0xff == 0`
  // misparsed as `mask & (0xff == 0)` (a bool-vs-int type clash every masked-equality
  // contract hit unless fully parenthesized). The corrected chain below is
  // Equality(<)Rel(<)BitOr(<)BitXor(<)BitAnd(<)Shift(<)Add  -  bitwise now nest INSIDE
  // comparisons the way every other C-family language does.
  auto lhs = parseBitXor();
  while (at(Tok::bar)) {
    p_++;
    auto rhs = parseBitXor();
    auto b = std::make_unique<BinaryExpr>();
    b->op = BinaryExpr::Op::bor; b->lhs = std::move(lhs); b->rhs = std::move(rhs);
    lhs = std::move(b);
  }
  return lhs;
}
ExprPtr Parser::parseBitXor() {
  auto lhs = parseBitAnd();
  while (at(Tok::caret)) {
    p_++;
    auto rhs = parseBitAnd();
    auto b = std::make_unique<BinaryExpr>();
    b->op = BinaryExpr::Op::bxor; b->lhs = std::move(lhs); b->rhs = std::move(rhs);
    lhs = std::move(b);
  }
  return lhs;
}
ExprPtr Parser::parseBitAnd() {
  auto lhs = parseShift();
  while (at(Tok::amp)) {
    p_++;
    auto rhs = parseShift();
    auto b = std::make_unique<BinaryExpr>();
    b->op = BinaryExpr::Op::band; b->lhs = std::move(lhs); b->rhs = std::move(rhs);
    lhs = std::move(b);
  }
  return lhs;
}
ExprPtr Parser::parseEquality() {
  // Equality is LOOSER than relational and bitwise  -  drop parens in
  // `(entry & MASK) == WB` style contracts and they still parse right.
  auto lhs = parseRel();
  while (at(Tok::eqeq) || at(Tok::bangeq)) {
    Tok t = cur().kind; p_++;
    auto rhs = parseRel();
    auto b = std::make_unique<BinaryExpr>();
    b->op = (t == Tok::eqeq) ? BinaryExpr::Op::eq : BinaryExpr::Op::ne;
    b->line = lhs->line; b->col = lhs->col; b->lhs = std::move(lhs); b->rhs = std::move(rhs);
    lhs = std::move(b);
  }
  return lhs;
}
ExprPtr Parser::parseRel() {
  // Relational is tighter than equality but looser than bitwise  -  again the
  // C/Rust standard so `(a & MASK) < THRESH` parses as `((a & MASK) < THRESH)`.
  auto lhs = parseBitOr();
  while (at(Tok::lt) || at(Tok::gt) || at(Tok::lteq) || at(Tok::gteq)) {
    Tok t = cur().kind; p_++;
    auto rhs = parseBitOr();
    auto b = std::make_unique<BinaryExpr>();
    switch (t) {
      case Tok::lt: b->op = BinaryExpr::Op::lt; break;
      case Tok::gt: b->op = BinaryExpr::Op::gt; break;
      case Tok::lteq: b->op = BinaryExpr::Op::le; break;
      default: b->op = BinaryExpr::Op::ge; break;
    }
    b->line = lhs->line; b->col = lhs->col; b->lhs = std::move(lhs); b->rhs = std::move(rhs);
    lhs = std::move(b);
  }
  return lhs;
}
ExprPtr Parser::parseShift() {
  auto lhs = parseAdd();
  while (at(Tok::shl) || at(Tok::shr)) {
    Tok t = cur().kind; p_++;
    auto rhs = parseAdd();
    auto b = std::make_unique<BinaryExpr>();
    b->op = (t == Tok::shl) ? BinaryExpr::Op::shl : BinaryExpr::Op::shr;
    b->line = lhs->line; b->col = lhs->col; b->lhs = std::move(lhs); b->rhs = std::move(rhs);
    lhs = std::move(b);
  }
  return lhs;
}
ExprPtr Parser::parseAdd() {
  auto lhs = parseMul();
  while (at(Tok::plus) || at(Tok::minus)) {
    Tok t = cur().kind; p_++;
    auto rhs = parseMul();
    auto b = std::make_unique<BinaryExpr>();
    b->op = (t == Tok::plus) ? BinaryExpr::Op::add : BinaryExpr::Op::sub;
    b->line = lhs->line; b->col = lhs->col; b->lhs = std::move(lhs); b->rhs = std::move(rhs);
    lhs = std::move(b);
  }
  return lhs;
}
ExprPtr Parser::parseMul() {
  auto lhs = parseCast();
  // `Tok::backslash` (A \ b) is the linear-solve / left-division operator  - 
  // same precedence as `*` / `/` / `%` and left-associative, exactly like
  // MATLAB's `\`. It builds a SolveExpr (`lhs` is matrix A, `rhs` vector b)
  // rather than a BinaryExpr, since it desugars to a runtime `ox_linsolve(A,
  // b)` (Gauss-Jordan), not an arithmetic op.
  while (at(Tok::star) || at(Tok::slash) || at(Tok::percent) ||
         at(Tok::backslash)) {
    if (at(Tok::backslash)) {
      int line = cur().line, col = cur().col;
      p_++;   // eat `\`
      auto rhs = parseCast();
      auto s = std::make_unique<SolveExpr>();
      s->lhs = std::move(lhs);
      s->rhs = std::move(rhs);
      s->line = line; s->col = col;
      lhs = std::move(s);
      continue;
    }
    Tok t = cur().kind; p_++;
    auto rhs = parseCast();
    auto b = std::make_unique<BinaryExpr>();
    switch (t) {
      case Tok::star: b->op = BinaryExpr::Op::mul; break;
      case Tok::slash: b->op = BinaryExpr::Op::div; break;
      default: b->op = BinaryExpr::Op::mod; break;
    }
    b->line = lhs->line; b->col = lhs->col; b->lhs = std::move(lhs); b->rhs = std::move(rhs);
    lhs = std::move(b);
  }
  return lhs;
}
ExprPtr Parser::parseCast() {
  auto lhs = parseUnary();
  while (at(Tok::kw_as)) {
    int line = cur().line;
    p_++;
    BType target = parseType();
    auto c = std::make_unique<CastExpr>();
    c->line = line; c->col = lhs->col;
    c->e = std::move(lhs);
    c->target = target;
    lhs = std::move(c);
  }
  return lhs;
}

ExprPtr Parser::parseUnary() {
  if (at(Tok::bang)) {
    p_++;
    auto u = std::make_unique<UnaryExpr>();
    u->op = UnaryExpr::Op::not_;
    u->base = parseUnary();
    return u;
  }
  if (at(Tok::tilde)) {
    p_++;
    auto u = std::make_unique<UnaryExpr>();
    u->op = UnaryExpr::Op::bnot;
    u->base = parseUnary();
    return u;
  }
  if (at(Tok::amp)) {
    p_++;
    auto u = std::make_unique<UnaryExpr>();
    u->op = UnaryExpr::Op::addr;
    u->line = cur().line; u->col = cur().col;
    u->base = parseUnary();
    return u;
  }
  if (at(Tok::star)) {
    p_++;
    auto u = std::make_unique<UnaryExpr>();
    u->op = UnaryExpr::Op::deref;
    u->line = cur().line; u->col = cur().col;
    u->base = parseUnary();
    return u;
  }
  if (at(Tok::minus)) {
    p_++;
    auto u = std::make_unique<UnaryExpr>();
    u->op = UnaryExpr::Op::neg;
    u->base = parseUnary();
    return u;
  }
  // `<- chan`  -  channel RECEIVE (prefix stance of the `<-` operator). The
  // prefix position (the arrow starts the expression) is always a receive:
  // `<- ch` reads one value of the channel's element type. The operand is a
  // channel-typed expression (a VarRef or any expr producing a Channel<T>).
  // Sema caches the channel's element type on the node so IRGen emits the
  // right `@ox_chan_recv_*` call.
  if (at(Tok::chanArrow)) {
    int line = cur().line, col = cur().col;
    p_++;   // eat `<-`
    auto r = std::make_unique<ChannelRecv>();
    r->chan = parseUnary();
    r->line = line; r->col = col;
    return r;
  }


  if (at(Tok::inc) || at(Tok::dec)) {
    bool isInc = at(Tok::inc);
    p_++;
    auto base = parseUnary();
    return makeIncDec(isInc,false, std::move(base));
  }
  // ox:why Power binds tighter than unary minus (`-x^2` == `-(x^2)`), so the leaf of
  // parseUnary dispatches to parsePower (which then descends to parsePostfix
  // for the base). Prefix unary operators above recursively call parseUnary,
  // so e.g. `!!x**2` is `!( !(x**2) )`.
  return parsePower();
}


// parsePower  -  right-associative `**` power operator. Sits between
// parseUnary (looser) and parsePostfix (tighter) so power binds tighter than
// unary minus but looser than postfix (call, index, field, postfix `²`).
// `2 ** 3 ** 2` == `2 ** (3 ** 2)` == 512.
//
// `**` is the only ASCII power operator here: `^` deliberately stays
// `Tok::caret` (bitwise XOR) for backward compat, per the feature spec. The
// postfix Unicode superscripts (`²`, `³`) and the `kw_pow2`/`kw_pow3`
// fast-paths are handled in parsePostfix; the standalone `π`/`√`/`∫` glyphs
// are handled in parsePrimary.
ExprPtr Parser::parsePower() {
  auto base = parsePostfix();
  if (at(Tok::pow)) {
    int line = cur().line, col = cur().col;
    p_++;   // eat `**`
    // Right-associative: the RHS goes through parseUnary so unary minus and
    // other prefix operators work on exponents (`2 ** -3` == `1/8`). parseUnary
    // dispatches back to parsePower for its base, so `2 ** 3 ** 2` still nests
    // right. Calling parsePower directly here would skip parseUnary and break
    // `2 ** -3` (and other prefix ops on the exponent).
    auto exponent = parseUnary();
    auto p = std::make_unique<PowerExpr>();
    p->base = std::move(base);
    p->exponent = std::move(exponent);
    p->line = line; p->col = col;
    return p;
  }
  return base;
}


ExprPtr Parser::makeIncDec(bool isInc, bool isPost, ExprPtr base) {
  auto d = std::make_unique<IncDecExpr>();
  d->isInc = isInc;
  d->isPost = isPost;
  d->line = base->line; d->col = base->col;
  if (auto v = dynamic_cast<VarRef*>(base.get())) {
    d->kind = AssignTarget::Kind::var;
    d->name = v->name;
  } else if (auto ix = dynamic_cast<Index*>(base.get())) {
    d->kind = AssignTarget::Kind::index;
    d->base = std::move(ix->base);
    d->index = std::move(ix->index);
  } else if (auto fl = dynamic_cast<Field*>(base.get())) {
    d->kind = AssignTarget::Kind::field;
    d->base = std::move(fl->base);
    d->field = fl->field;
  } else if (auto u = dynamic_cast<UnaryExpr*>(base.get())) {
    if (u->op != UnaryExpr::Op::deref) { error("invalid increment/decrement target"); return base; }
    d->kind = AssignTarget::Kind::deref;
    d->base = std::move(u->base);
  } else {
    error("invalid increment/decrement target");
    return base;
  }
  return d;
}

ExprPtr Parser::parsePostfix() {
  auto e = parsePrimary();

  while (true) {
    if (at(Tok::lbracket)) {
      p_++;
      auto ix = std::make_unique<Index>();
      ix->line = cur().line; ix->col = cur().col;
      ix->base = std::move(e);
      ix->index = parseExpr();
      expect(Tok::rbracket, "']'");
      e = std::move(ix);
      continue;
    }
    if (at(Tok::dot)) {
      p_++;
      int line = cur().line, col = cur().col;
      if (!expect(Tok::ident, "field or method name")) break;
      std::string name = toks_[p_ - 1].text;

      if (at(Tok::lparen)) {
        p_++;
        auto mc = std::make_unique<MethodCall>();
        mc->callee = name;
        mc->receiver = std::move(e);
        mc->line = line; mc->col = col;
        while (!at(Tok::rparen) && !at(Tok::end)) {
          mc->args.push_back(parseExpr());
          if (!accept(Tok::comma)) break;
        }
        expect(Tok::rparen, "')'");
        e = std::move(mc);
        continue;
      }
      auto fl = std::make_unique<Field>();
      fl->line = line; fl->col = col;
      fl->base = std::move(e);
      fl->field = name;
      e = std::move(fl);
      continue;
    }


    // Call applied to ANY expression: `(fn(){...})()`, `f()()`, `arr[i]()`,
    // `obj.methodptr()`, etc.  -  the callee is an arbitrary value computing a
    // function (a lambda value or a fn pointer). This is the C++-parity call
    // form: the identifier `name(...)` path handles bare-name calls; everything
    // else goes through here with `call->calleeExpr` set.
    if (at(Tok::lparen)) {
      p_++;
      auto call = std::make_unique<Call>();
      call->line = cur().line; call->col = cur().col;
      call->fnPtr = true;
      call->calleeExpr = std::move(e);
      while (!at(Tok::rparen) && !at(Tok::end)) {
        call->args.push_back(parseExpr());
        if (!accept(Tok::comma)) break;
      }
      expect(Tok::rparen, "')'");
      e = std::move(call);
      continue;
    }
    // Postfix Unicode-superscript power (`x²`, `x³`) and its ASCII keyword
    // fast-paths (`x pow2`, `x pow3`). The lexer emits the superscript glyphs
    // ²/³ as `Tok::math_sym` with `text` = the raw UTF-8 bytes; the ASCII
    // forms lex to `Tok::kw_pow2`/`Tok::kw_pow3`. Both build a PowerExpr so
    // Sema/IRGen lower them through the uniform power pipeline (`@ox_pow_f64`
    // / `@ipow` / `@ox_square`). For a bare single `²`/`³` the exponent is
    // an IntLit literal; SuperscriptExpr is reserved for the GENERAL run
    // (not currently produced by the lexer, e.g. `x⁻¹` once lexed)  -  that
    // keeps the AST node round-tripping for source-form tooling.
    bool isSup2 = false, isSup3 = false;
    if (at(Tok::math_sym)) {
      const std::string& tx = cur().text;
      // The lexer's known superscript glyphs are the single code points ²/³.
      if (tx == "\xC2\xB2") isSup2 = true;       // U+00B2 ²
      else if (tx == "\xC2\xB3") isSup3 = true;  // U+00B3 ³
    } else if (at(Tok::kw_pow2)) {
      isSup2 = true;
    } else if (at(Tok::kw_pow3)) {
      isSup3 = true;
    }
    if (isSup2 || isSup3) {
      int line = cur().line, col = cur().col;
      p_++;   // eat the superscript glyph / `pow2` / `pow3`
      auto exp = std::make_unique<IntLit>();
      exp->v = isSup2 ? 2u : 3u;
      exp->line = line; exp->col = col;
      auto p = std::make_unique<PowerExpr>();
      p->base = std::move(e);
      p->exponent = std::move(exp);
      p->line = line; p->col = col;
      e = std::move(p);
      continue;
    }
    // General multi-code-point superscript run (e.g. a future `x⁻¹` once the
    // lexer emits a longer math_sym). Decodes the Unicode superscript digits
    // to an ASCII string and parses it as a decimal integer; builds a
    // SuperscriptExpr carrying both the raw text and the decoded exponent so
    // the AST round-trips. Currently the lexer only emits the single-²/³
    // fast-path above, so this arm is the extension hook for richer glyphs.
    if (at(Tok::math_sym)) {
      const std::string& tx = cur().text;
      // Decode superscript code points U+2070..U+2079 (⁰¹²³⁴⁵⁶⁷⁸⁹), U+207B
      // (⁻), U+00B2 (²), U+00B3 (³), U+00B9 (¹) into an ASCII string. If the
      // text contains no superscript code point at all it is NOT a postfix
      // power (it's a standalone glyph that belongs to parsePrimary); skip.
      std::string ascii;
      bool anySup = false;
      for (size_t k = 0; k + 1 < tx.size();) {
        unsigned char b0 = (unsigned char)tx[k];
        unsigned char b1 = (unsigned char)tx[k + 1];
        if (k + 2 < tx.size()) {
          unsigned char b2 = (unsigned char)tx[k + 2];
          // U+2070..U+2079 ⁻ (3-byte UTF-8: E2 81 B0..B9 / BB)  -  digits + minus
          if (b0 == 0xE2 && b1 == 0x81 && b2 >= 0xB0 && b2 <= 0xB9) {
            ascii.push_back(char('0' + (b2 - 0xB0))); anySup = true; k += 3; continue;
          }
          if (b0 == 0xE2 && b1 == 0x81 && b2 == 0xBB) {  // U+207B ⁻
            ascii.push_back('-'); anySup = true; k += 3; continue;
          }
          if (b0 == 0xE2 && b1 == 0x81 && b2 == 0xBC) {  // U+207C ⁼ (rare)
            k += 3; continue;
          }
          if (b0 == 0xE2 && b1 == 0x81 && b2 == 0xB9) {  // U+2079 ⁹ already above
            ascii.push_back('9'); anySup = true; k += 3; continue;
          }
        }
        if (b0 == 0xC2 && b1 == 0xB9) { ascii.push_back('1'); anySup = true; k += 2; continue; } // ¹
        if (b0 == 0xC2 && b1 == 0xB2) { ascii.push_back('2'); anySup = true; k += 2; continue; } // ²
        if (b0 == 0xC2 && b1 == 0xB3) { ascii.push_back('3'); anySup = true; k += 2; continue; } // ³
        // Not a recognised superscript code point  -  leave it (sticky glyphs).
        k += 1;
      }
      if (anySup) {
        int line = cur().line, col = cur().col;
        p_++;   // eat the math_sym token
        // Build the decoded exponent as an IntLit (the lexer only emits known
        // code points, so for negative exponents we wrap a negation).
        ExprPtr exp;
        bool neg = !ascii.empty() && ascii[0] == '-';
        std::string digits = neg ? ascii.substr(1) : ascii;
        if (!digits.empty()) {
          uint64_t val = 0;
          for (char c : digits) val = val * 10 + uint64_t(c - '0');
          auto il = std::make_unique<IntLit>();
          il->v = val; il->line = line; il->col = col;
          if (neg) {
            auto u = std::make_unique<UnaryExpr>();
            u->op = UnaryExpr::Op::neg; u->base = std::move(il);
            u->line = line; u->col = col;
            exp = std::move(u);
          } else {
            exp = std::move(il);
          }
        }
        auto s = std::make_unique<SuperscriptExpr>();
        s->base = std::move(e);
        s->text = tx;
        s->exponent = std::move(exp);
        s->line = line; s->col = col;
        e = std::move(s);
        continue;
      }
      // else: not a superscript  -  fall through, leave the math_sym token for
      // parsePrimary (but parsePostfix should not consume it here). It will be
      // handled when a fresh parsePrimary peers at it, so just break out.
    }
    if (at(Tok::inc) || at(Tok::dec)) {
      bool isInc = at(Tok::inc);
      p_++;
      e = makeIncDec(isInc,true, std::move(e));
      continue;
    }
    break;
  }
  return e;
}
ExprPtr Parser::parsePrimary() {
  if (at(Tok::int_lit)) {
    auto l = std::make_unique<IntLit>();
    l->v = cur().u64; l->line = cur().line; l->col = cur().col;
    p_++;
    return l;
  }
  // `expand name(arg1, arg2, ...)`  -  compile-time macro invocation. Builds a
  // MacroCall node (not a Call  -  the name resolves to a MacroDecl, not a fn).
  // Sema clones the macro body, substitutes the `$param` markers with the args,
  // type-checks + compiles the result in place. Nested `expand` inside a macro
  // body recurses naturally: the substituted inner MacroCall reaches checkExpr.
  if (at(Tok::kw_expand)) {
    int line = cur().line, col = cur().col;
    p_++;   // eat `expand`
    if (!expect(Tok::ident, "macro name after 'expand'")) {
      auto z = std::make_unique<IntLit>(); z->v = 0; z->line = line; return z;
    }
    std::string name = toks_[p_ - 1].text;
    if (!expect(Tok::lparen, "'(' after macro name in expand")) {
      auto z = std::make_unique<IntLit>(); z->v = 0; z->line = line; return z;
    }
    auto mc = std::make_unique<MacroCall>();
    mc->macroName = name;
    mc->line = line; mc->col = col;
    while (!at(Tok::rparen) && !at(Tok::end)) {
      mc->args.push_back(parseExpr());
      if (!accept(Tok::comma)) break;
    }
    expect(Tok::rparen, "')' to close expand(...)");
    return mc;
  }
  // `$ident`  -  a substitution marker inside a macro body. Lexed as `dollar` +
  // `ident`; parsed here as a VarRef whose `name` is `"$" + ident`, so the
  // normal Expr machinery (cloneExpr, Sema lookup, IRGen genExpr) treats it as
  // an ordinary variable reference. Sema's macro expander clones the body and
  // replaces every `$param` VarRef with a clone of the corresponding arg.
  // Outside a macro body, a `$name` reaches Sema's `lookup` which fails to find
  // a `"$x"` binding → a clean "unknown variable" error (no special-casing).
  if (at(Tok::dollar)) {
    int line = cur().line, col = cur().col;
    p_++;   // eat `$`
    if (!expect(Tok::ident, "identifier after '$' in macro parameter marker")) {
      auto z = std::make_unique<IntLit>(); z->v = 0; z->line = line; return z;
    }
    std::string pname = "$" + toks_[p_ - 1].text;
    auto v = std::make_unique<VarRef>();
    v->name = pname; v->line = line; v->col = col;
    return v;
  }
  if (at(Tok::float_lit)) {
    auto l = std::make_unique<FloatLit>();
    l->v = cur().f64; l->isF32 = cur().isF32; l->line = cur().line; l->col = cur().col;
    p_++;
    return l;
  }
  if (at(Tok::str_lit)) {
    auto l = std::make_unique<StrLit>();
    l->v = cur().text; l->line = cur().line; l->col = cur().col;
    p_++;
    return l;
  }
  if (at(Tok::char_lit)) {
    auto l = std::make_unique<CharLit>();
    l->v = (uint8_t)cur().u64; l->line = cur().line; l->col = cur().col;
    p_++;
    return l;
  }
  if (at(Tok::kw_null)) {
    auto n = std::make_unique<NullLit>();
    n->line = cur().line; n->col = cur().col;
    p_++;
    return n;
  }
  if (at(Tok::kw_fn)) {


    int line = cur().line, col = cur().col;
    p_++;
    auto lam = std::make_unique<LambdaLit>();
    lam->line = line; lam->col = col;

    // Optional capture list: `fn[a, &b, c](...)`  -  `[...]` after `fn`, before `(`.
    //   a    captures `a` by value (copied at the lex site)
    //   &a   captures `a` by reference (the local's address is passed in)
    //   =    capture all in-scope locals by value  (a pull of the whole scope)
    //   &    capture all in-scope locals by reference
    if (at(Tok::lbracket)) {
      p_++;
      // Support `[=]` and `[&]` capture-all (single-token forms).
      if (at(Tok::eq) && peek(1).kind == Tok::rbracket) {
        p_++; lam->captures.push_back({"=", false}); accept(Tok::rbracket);
      } else if (at(Tok::amp) && peek(1).kind == Tok::rbracket) {
        p_++; lam->captures.push_back({"&", true}); accept(Tok::rbracket);
      } else {
        while (!at(Tok::rbracket) && !at(Tok::end)) {
          bool byRef = accept(Tok::amp);
          if (!expect(Tok::ident, "capture name")) break;
          std::string cname = toks_[p_ - 1].text;
          lam->captures.push_back({cname, byRef});
          if (!accept(Tok::comma)) break;
        }
        accept(Tok::rbracket);
      }
    }

    if (!expect(Tok::lparen, "'(' to begin lambda parameter list")) {
      auto f = std::make_unique<IntLit>(); f->v = 0; f->line = line; return f;
    }
    while (!at(Tok::rparen) && !at(Tok::end)) {
      if (!expect(Tok::ident, "lambda parameter name")) break;
      std::string pname = toks_[p_ - 1].text;
      if (!expect(Tok::colon, "':' after lambda parameter name")) break;
      BType pt = parseType();
      Param pp; pp.name = pname; pp.type = pt;   // lambdas take no default args
      pp.hasDefault = false;
      lam->params.push_back(std::move(pp));
      if (!accept(Tok::comma)) break;
    }
    expect(Tok::rparen, "')'");
    if (accept(Tok::arrow)) lam->retType = parseType();
    else lam->retType = BType::void_;

    if (!at(Tok::lbrace)) {
      error("expected '{' to begin lambda body");
      auto f = std::make_unique<IntLit>(); f->v = 0; f->line = line; return f;
    }
    bool saved = allowStructLit_; allowStructLit_ = true;
    lam->body = parseBlock();
    allowStructLit_ = saved;
    return lam;
  }
  if (at(Tok::kw_sizeof)) {

    int line = cur().line, col = cur().col;
    p_++;
    expect(Tok::lparen, "'(' after 'sizeof'");
    BType ty = parseType();
    expect(Tok::rparen, "')' to close sizeof(...)");
    auto s = std::make_unique<SizeofExpr>();
    s->target = ty;
    s->line = line; s->col = col;
    return s;
  }
  // `spawn { ... }` / `spawn <expr>`  -  run the body on a freshly created
  // thread. Two surface forms: a brace-delimited statement block
  // (`spawn { ... }`) or a single expression (`spawn doThing()`,
  // `spawn (a + b)`) whose type is the body's type. Sema infers `resultTy`
  // from the body (void_ for a statement block); IRGen synthesises a
  // `void(void*)` wrapper that captures the closure's env, calls
  // @ox_thread_create(fnptr, arg) -> i8*, and returns the handle.
  if (at(Tok::kw_spawn)) {
    int line = cur().line, col = cur().col;
    p_++;   // eat `spawn`
    auto sp = std::make_unique<SpawnExpr>();
    sp->line = line; sp->col = col;
    if (at(Tok::lbrace)) {
      // Brace form: SpawnExpr::body is a single ExprPtr, so for a one-statement
      // block of a bare expression we lift that expression as the body (its type
      // is resultTy); any other block shape is a void body (IRGen runs nothing
      // meaningful in the thread). Oxide blocks aren't value-yielding, so a
      // multi-statement `spawn { ... }` has resultTy=void_.
      auto stmts = parseBlock();   // consumes `{`..`}`
      if (stmts.size() == 1 &&
          dynamic_cast<ExprStmt*>(stmts[0].get())) {
        auto* es = static_cast<ExprStmt*>(stmts[0].get());
        sp->body = std::move(es->expr);
      } else {
        sp->body = std::make_unique<IntLit>();
        static_cast<IntLit*>(sp->body.get())->v = 0;
        static_cast<IntLit*>(sp->body.get())->line = line;
        sp->resultTy = BType::void_;
      }
    } else {
      // Bare/parenthesised expression form: `spawn <expr>` / `spawn (a + b)`.
      // A following `(` is a parenthesised expression, not a call  -  parse the
      // whole assignment-precedence expression so resultTy is the body's type.
      sp->body = parseExpr();
    }
    return sp;
  }
  // `Channel<T>::new()`  -  construct a new buffered channel. Surface syntax is
  // the `Channel` keyword (lexed `kw_chan`), a `[T]` element-type argument,
  // then `::new()`. We parse `[T]` via parseType and accept the `::new` (any
  // method name is tolerated; Sema only cares about the element type). Builds
  // a ChannelNew carrying the element type; IRGen calls @ox_chan_new_<suffix>.
  // (The `Channel` token is a keyword, so this arm must precede the ident path
  //  -  `Channel<T>::new()` would never reach the ident handler otherwise.)
  if (at(Tok::kw_chan)) {
    int line = cur().line, col = cur().col;
    p_++;   // eat `Channel`
    BType elem = BType::i64;
    if (accept(Tok::lt)) {
      // Generic-angle form `Channel<T>` (also valid surface)  -  parse one type.
      elem = parseType();
      expect(Tok::gt, "'>' to close Channel<T>");
    } else if (accept(Tok::lbracket)) {
      elem = parseType();
      expect(Tok::rbracket, "']' to close Channel[T]");
    } else {
      error("expected '<T>' or '[T]' after 'Channel'");
    }
    if (at(Tok::coloncolon)) {
      p_++;   // eat `::`
      // Expect `new` (any ident) then `()`.
      if (!expect(Tok::ident, "method name after '::' (expected 'new')")) {
        auto z = std::make_unique<IntLit>(); z->v = 0; z->line = line; return z;
      }
      if (!expect(Tok::lparen, "'(' after Channel<T>::new")) {
        auto z = std::make_unique<IntLit>(); z->v = 0; z->line = line; return z;
      }
      // No constructor args yet (the buffer size is runtime-default).
      expect(Tok::rparen, "')' to close Channel<T>::new()");
    }
    auto cn = std::make_unique<ChannelNew>();
    cn->elemType = elem;
    cn->line = line; cn->col = col;
    return cn;
  }
  // --- Contract spec forms (legal only inside a contract clause) ---
  // `old(x)`  -  pre-state reference. The `(...)` is a single POSIX-ish primary
  // (a name or field/index chain); we parse it as a full spec expression so
  // `old(p.x)`, `old(arr[i])` work. Sema rejects `old` outside an `ensures`.
  if (inSpec_ && at(Tok::kw_old)) {
    int line = cur().line, col = cur().col;
    p_++;
    expect(Tok::lparen, "'(' after 'old'");
    auto o = std::make_unique<OldExpr>();
    o->sub = parseSpecExpr();
    o->line = line; o->col = col;
    expect(Tok::rparen, "')' to close old(...)");
    return o;
  }
  // ox:proof `forall i: T in lo..(=)hi implies P` / `exists i: T in lo..(=)hi implies P`.
  // The binder `i` is a fresh name in scope for the `implies` body only (Sema
  // binds it in an ephemeral env at check time). Range is an integer pair; the
  // bound form mirrors `for i in a..b` (exclusive) / `a..=b` (inclusive). We
  // keep the range literal here rather than reusing `RangeLit` because a
  // quantifier range is part of the quantifier node, not a standalone value.
  if (inSpec_ && (at(Tok::kw_forall) || at(Tok::kw_exists))) {
    int line = cur().line, col = cur().col;
    bool isForall = at(Tok::kw_forall);
    p_++;
    if (!expect(Tok::ident, "quantifier binder name")) {
      auto z = std::make_unique<BoolLit>(); z->line = line; return z;
    }
    std::string binder = toks_[p_ - 1].text;
    if (!at(Tok::colon)) {
      // ox:proof Allow an inferred-int binder: `forall i in 0..n implies ...` (no `: T`)
      error("expected ':' after quantifier binder");
    } else {
      p_++;
    }
    BType btype = BType::i64;
    // Try to parse a type after the colon. If the next token is `in`, there was
    // no type annotation (inferred i64); otherwise parse an explicit type.
    if (!at(Tok::kw_in)) {
      btype = parseType();
    }
    expect(Tok::kw_in, "'in' after quantifier binder");
    {
      // Range bounds are integer expressions; temporarily disable inSpec_ while
      // parsing them so the `implies` that SEPARATES range from body is NOT
      // swallowed as a binary operator by parseImplies (it belongs to the body).
      // Restore inSpec_ for the body parse.
      bool savedSpec = inSpec_; inSpec_ = false;
      bool saved = allowStructLit_; allowStructLit_ = false;
      auto lo = parseAssign();    // range low (parseAssign so `0..n-1` binds right)
      bool inclusive = accept(Tok::dotdoteq);
      if (!inclusive) expect(Tok::dotdot, "'..' or '..=' in quantifier range");
      auto hi = parseAssign();
      allowStructLit_ = saved;
      inSpec_ = savedSpec;
      auto q = std::make_unique<QuantExpr>();
      q->isForall = isForall;
      q->binder = binder;
      q->binderType = btype;
      q->lo = std::move(lo);
      q->hi = std::move(hi);
      q->inclusive = inclusive;
      q->line = line; q->col = col;
      // ox:proof The `implies` keyword is the syntactic separator between the range and
      // the quantified body (`forall i in a..b implies P`). Consume it here,
      // then parse the body as a spec expression. (A bare `implies` operator
      // mid-expression is still allowed elsewhere via parseImplies.)
      expect(Tok::kw_implies, "'implies' after quantifier range");
      q->body = parseSpecExpr();
      return q;
    }
  }
  if (at(Tok::kw_asm)) {


    int line = cur().line, col = cur().col;
    p_++;
    if (!accept(Tok::bang)) {
      error("expected '!' after 'asm'");
      auto z = std::make_unique<IntLit>(); z->v = 0; z->line = line; return z;
    }
    expect(Tok::lparen, "'(' after 'asm!'");
    auto a = std::make_unique<AsmExpr>();
    a->line = line; a->col = col;
    if (!at(Tok::str_lit)) {
      error("expected string literal as asm body");
      auto z = std::make_unique<IntLit>(); z->v = 0; z->line = line; return z;
    }
    a->asmText = cur().text;
    p_++;

    while (accept(Tok::comma)) {
      bool isInOutClause = (cur().kind == Tok::kw_in || cur().kind == Tok::ident) &&
                           (cur().text == "in" || cur().text == "out" || cur().text == "inout");
      if (isInOutClause) {
        bool isInOut = (cur().text == "inout");
        bool isOut = isInOut || (cur().text == "out");
        p_++;
        if (!expect(Tok::lparen, "'(' after in/out/inout clause")) break;

        std::string cstr;
        if (at(Tok::str_lit)) { cstr = cur().text; p_++; }
        else { error("expected constraint string in in/out/inout clause"); break; }
        expect(Tok::rparen, "')' to close in/out/inout clause");
        AsmIO io;
        io.isOutput = isOut;
        io.isInOut = isInOut;
        io.constraint = cstr;
        io.val = parseUnary();
        a->ios.push_back(std::move(io));
      } else if ((cur().kind == Tok::ident) && cur().text == "clobbers") {
        p_++;
        if (!expect(Tok::eq, "'=' after clobbers")) break;
        if (!at(Tok::str_lit)) { error("expected string after clobbers="); break; }
        a->clobbers = cur().text; p_++;
      } else if (at(Tok::ident) && cur().text == "sideeffect") {
        p_++; if (!expect(Tok::eq, "'=' after sideeffect")) break;
        if (at(Tok::kw_true)) { a->sideEffect = true; p_++; }
        else if (at(Tok::kw_false)) { a->sideEffect = false; p_++; }
        else error("expected true/false after sideeffect=");
      } else if (at(Tok::ident) && cur().text == "memory") {
        p_++; if (!expect(Tok::eq, "'=' after memory")) break;
        if (at(Tok::kw_true)) { a->hasMemory = true; p_++; }
        else if (at(Tok::kw_false)) { a->hasMemory = false; p_++; }
        else error("expected true/false after memory=");
      } else {
        error("expected in()/out()/inout()/clobbers=/sideeffect=/memory= in asm! block");
        p_++;
        if (!at(Tok::rparen) && !at(Tok::end)) continue; else break;
      }
    }
    expect(Tok::rparen, "')' to close asm!(...)");
    // Verified-asm link clause: `asm!(...) implements <spec_fn>(<args>)`.
    // After the asm block's close parenthesis, an optional `implements` clause
    // binds this block to an `asm spec fn` declaration. Parse the spec fn name
    // (identifier) + a parenthesized comma-separated argument list (positional,
    // one per spec param). Stored on the AsmExpr; Sema resolves + type-checks
    // the link, and the SMT emitter substitutes these terms for the spec's
    // params to assert the architectural `ensures` hypothesis. With NO
    // `implements`, the asm block falls back to the legacy naming-convention
    // path (`spec fn asm_<fn>` matched by string), so existing source stays
    // verbatim. The clause is spec-only  -  IRGen never looks at these fields.
    if (accept(Tok::kw_implements)) {
      a->hasImplements = true;
      if (!expect(Tok::ident, "asm spec function name after 'implements'"))
        return a;
      a->implementsSpec = toks_[p_ - 1].text;
      if (!expect(Tok::lparen, "'(' after implements spec name")) return a;
      while (!at(Tok::rparen) && !at(Tok::end)) {
        // ox:why parseExpr (not parseUnary) so the full expression grammar is allowed
        //  -  a spec arg can be `msr + 1`, `arr[i]`, a call, etc. Each is lowered
        // later by smtExpr for the hypothesis substitution.
        a->implementsArgs.push_back(parseExpr());
        if (!accept(Tok::comma)) break;
      }
      expect(Tok::rparen, "')' to close implements argument list");
    }
    return a;
  }
  if (at(Tok::kw_self)) {


    auto v = std::make_unique<VarRef>();
    v->name = "self";
    v->line = cur().line; v->col = cur().col;
    p_++;
    return v;
  }
  if (at(Tok::kw_true) || at(Tok::kw_false)) {
    auto l = std::make_unique<BoolLit>();
    l->v = (cur().kind == Tok::kw_true); l->line = cur().line;
    p_++;
    return l;
  }
  if (at(Tok::lparen)) {
    p_++;
    auto e = parseExpr();
    expect(Tok::rparen, "')'");
    return e;
  }
  if (at(Tok::kw_vec)) {

    p_++;
    expect(Tok::lbracket, "'[' after 'vec'");
    BType elem = parseType();
    expect(Tok::rbracket, "']'");
    auto dn = std::make_unique<DynNew>();
    dn->elemType = elem;
    dn->line = cur().line; dn->col = cur().col;
    return dn;
  }
  if (at(Tok::kw_map)) {

    p_++;
    expect(Tok::lbracket, "'[' after 'map'");
    BType key = parseType();
    expect(Tok::comma, "',' between key and value in map[K, V]");
    BType val = parseType();
    expect(Tok::rbracket, "']'");
    auto mn = std::make_unique<MapNew>();
    mn->keyType = key; mn->valType = val;
    mn->line = cur().line; mn->col = cur().col;
    return mn;
  }
  if (at(Tok::kw_set)) {
    p_++;
    expect(Tok::lbracket, "'[' after 'set'");
    BType elem = parseType();
    expect(Tok::rbracket, "']'");
    auto sn = std::make_unique<SetNew>();
    sn->elemType = elem;
    sn->line = cur().line; sn->col = cur().col;
    return sn;
  }
  if (at(Tok::kw_hmap)) {
    p_++;
    expect(Tok::lbracket, "'[' after 'hmap'");
    BType key = parseType();
    expect(Tok::comma, "',' between key and value in hmap[K, V]");
    BType val = parseType();
    expect(Tok::rbracket, "']'");
    auto hn = std::make_unique<HMapNew>();
    hn->keyType = key; hn->valType = val;
    hn->line = cur().line; hn->col = cur().col;
    return hn;
  }
  if (at(Tok::kw_hset)) {
    p_++;
    expect(Tok::lbracket, "'[' after 'hset'");
    BType elem = parseType();
    expect(Tok::rbracket, "']'");
    auto hn = std::make_unique<HSetNew>();
    hn->elemType = elem;
    hn->line = cur().line; hn->col = cur().col;
    return hn;
  }
  if (at(Tok::lbracket)) {
    // Three spellings share the `[` open:
    //   1. `[ a, b, c ]`                 -  ArrayLit (existing behaviour).
    //   2. `[ a, b ; c, d ]`             -  flat MatrixLit, `;`-separated rows,
    //                                    entries comma-separated within a row.
    //   3. `[ [a, b], [c, d] ]`          -  nested MatrixLit, one ArrayLit per
    //                                    row; rows comma- or `;`-separated.
    // To pick (1) vs (2) we scan ahead for a top-level `;` (depth 0) before
    // the matching `]`. (3) is detected by a leading `[` right after the open.
    // MATLAB-style space-separated entries (`[1 0 0]`) are NOT supported here
    //  -  they require lexer-injected commas and stay an ArrayLit of one entry
    // (the parser sees `1`, then `0` is not an operator → comma expected).
    int line = cur().line, col = cur().col;

    // Lookahead: is there a top-level `;` (depth-0, excluding nested []/())
    // before the matching `]`? Cheap and side-effect-free.
    auto looksLikeMatrixFlat = [&]() -> bool {
      size_t k = p_ + 1;  // skip the opening `[`
      int depth = 0;
      while (k < toks_.size()) {
        Tok kk = toks_[k].kind;
        if (kk == Tok::end) return false;
        if (kk == Tok::lbracket || kk == Tok::lparen) { depth++; k++; continue; }
        if (kk == Tok::rbracket) {
          if (depth == 0) return false;  // closing `]`  -  no top-level `;` seen
          depth--; k++; continue;
        }
        if (kk == Tok::rparen) { if (depth > 0) depth--; k++; continue; }
        if (kk == Tok::semicolon && depth == 0) return true;  // matrix!
        k++;
      }
      return false;
    };
    // Detect the MIXED matrix syntax `[[a, b]; [c, d]]`  -  a flat matrix whose
    // rows are themselves bracketed `[...]` groups separated by `;`.  This is
    // a user-friendly hybrid of the nested (`[[..],[..]]`) and flat
    // (`[a b; c d]`) spellings. Without this arm the parser would route the
    // construct to the plain flat-matrix path, parse each `[...]` group as a
    // single ArrayLit expression, and produce a 1-expr-wide "matrix" whose
    // every row holds one ArrayLit  -  which then fails in Sema with the
    // baffling "matrix literal elements must be numeric, got [f64; N]". We
    // instead recognise the shape here and unwrap each bracketed row group's
    // ArrayLit into the row's element vector, producing a real MatrixLit.
    auto looksLikeMixedMatrix = [&]() -> bool {
      // First real token after the opening `[` must itself be `[`.
      size_t k = p_ + 1;  // skip the opening `[`
      while (k < toks_.size() && toks_[k].kind != Tok::end) {
        Tok kk = toks_[k].kind;
        if (kk == Tok::lbracket) break;       // found a nested `[`  -  candidate
        // Skip whitespace-equivalent trivia. The lexer doesn't emit ws tokens,
        // but be defensive: anything that isn't `[` (e.g. a comment) means the
        // first element is NOT a bracketed group → not the mixed form.
        return false;
      }
      return looksLikeMatrixFlat();  // ...and there's a top-level `;`.
    };
    // Only `;`-separated flat matrices route to MatrixLit.  `[[a,b],[c,d]]`
    // is a plain nested ArrayLit (an array of arrays), NOT a matrix literal.
    // Sema/IRGen will lower a 2-D array-of-arrays with numeric elements to the
    // matrix runtime if needed; the parser must not steal it here.
    bool flatMat = looksLikeMatrixFlat();
    // Detect the mixed form `[[a, b]; [c, d]]`  -  a flat matrix whose rows are
    // themselves bracketed `[...]` groups separated by `;`. Must be computed
    // BEFORE we eat the opening `[` (both helpers index from p_+1).
    bool mixed = flatMat && looksLikeMixedMatrix();

    if (flatMat) {
      p_++;   // eat `[`
      auto m = std::make_unique<MatrixLit>();
      m->line = line; m->col = col;

      // Mixed form: `[[a, b]; [c, d]]`  -  each row is a bracketed `[...]`
      // group. Parse the group as a primary expression and, if it is an
      // ArrayLit, splice its `elems` into the row (so downstream Sema/IRGen
      // see a real 2-D matrix of scalars, not a 1-wide matrix of arrays).
      // If a group is not an ArrayLit (scalar, call, etc.) emit the clear
      // error telling the user to use one syntax consistently.
      auto parseRowMixed = [&]() -> std::vector<ExprPtr> {
        // Each row in the mixed form must start with `[`.
        if (!at(Tok::lbracket)) {
          error("cannot mix nested array syntax [[...]] with flat matrix "
                "syntax [a b; c d]. Use one or the other consistently.");
          // Best-effort recovery: parse whatever expression is here as a
          // single-element row so the rest of the file can still compile.
          std::vector<ExprPtr> row;
          row.push_back(parseExpr());
          return row;
        }
        auto grp = parsePrimary();  // parses `[...]` as an ArrayLit
        auto al = dynamic_cast<ArrayLit*>(grp.get());
        if (!al) {
          error("mixed matrix row must be a bracketed array '[...]'");
          std::vector<ExprPtr> row;
          row.push_back(std::move(grp));
          return row;
        }
        // Transfer ownership of the ArrayLit's elements into the row.
        std::vector<ExprPtr> row;
        for (auto& e : al->elems) row.push_back(std::move(e));
        return row;
      };

      auto parseRowFlat = [&]() {
        std::vector<ExprPtr> row;
        while (!at(Tok::rbracket) && !at(Tok::rparen) && !at(Tok::end) &&
               !at(Tok::semicolon) && !at(Tok::comma)) {
          row.push_back(parseExpr());
          if (!accept(Tok::comma)) break;
        }
        return row;
      };

      // `[ a, b ; c, d ]`  -  rows split on `;`, entries on `,`. In the mixed
      // form each row is a single `[...]` group with NO `,` between rows.
      while (!at(Tok::rbracket) && !at(Tok::end)) {
        if (mixed) {
          m->rows.push_back(parseRowMixed());
          // Rows in the mixed form are separated by `;`, NOT `,`. If a `,`
          // appears we've drifted into the nested form  -  flag it clearly.
          if (at(Tok::comma)) {
            error("mixed matrix rows must be separated by ';', not ','. "
                  "Use [[a, b]; [c, d]] or [a, b, c; d, e, f]  -  not "
                  "[[a, b], [c, d]] with ';'.");
            accept(Tok::comma);  // recover
          }
        } else {
          m->rows.push_back(parseRowFlat());
        }
        if (!accept(Tok::semicolon)) break;
      }
      expect(Tok::rbracket, "']' to close matrix literal");
      return m;
    }

    p_++;
    auto al = std::make_unique<ArrayLit>();
    al->line = cur().line; al->col = cur().col;
    while (!at(Tok::rbracket) && !at(Tok::end)) {
      al->elems.push_back(parseExpr());
      if (!accept(Tok::comma)) break;
    }
    expect(Tok::rbracket, "']'");
    return al;
  }
  // --- Advanced math: standalone Unicode glyphs and ASCII keyword forms ---
  //
  // The lexer emits each Unicode math glyph (π, ∫, √) as `Tok::math_sym` with
  // `text` = the raw UTF-8 bytes; the ASCII fallback names lex to dedicated
  // keywords (`kw_pi`, `kw_integrate`, `kw_sqrt`). Postfix superscript glyphs
  // (`²`, `³`) and `kw_pow2`/`kw_pow3` are handled in parsePostfix  -  they
  // only make sense applied to a base. Here we handle the START-of-expression
  // forms:
  //   `π` / `pi`        → MathSymExpr (Sema folds to the constant).
  //   `√ <expr>` / `sqrt <expr>` → unary sqrt: lowered to `ox_sqrt(x)`.
  //   `∫ <body> from a to b` / `integrate ...` → IntegrateExpr.
  if (at(Tok::math_sym)) {
    const std::string& tx = cur().text;
    int line = cur().line, col = cur().col;
    // `π` (U+03C0, UTF-8 0xCF 0x80) → MathSymExpr("π").
    if (tx == "\xCF\x80") {
      p_++;
      auto m = std::make_unique<MathSymExpr>();
      m->text = "pi"; m->line = line; m->col = col;
      return m;
    }
    // `√` (U+221A, UTF-8 E2 88 9A) → prefix unary sqrt: `√ <expr>` becomes a
    // runtime sqrt call. UnaryExpr has no sqrt opcode, so we lower directly
    // to a Call node naming the `sqrt` runtime fn (mirroring the ASCII
    // `sqrt` keyword form below). Sema resolves `sqrt` to the math builtin.
    if (tx == "\xE2\x88\x9A") {
      p_++;
      auto operand = parseUnary();
      auto c = std::make_unique<Call>();
      c->callee = "sqrt";
      c->args.push_back(std::move(operand));
      c->line = line; c->col = col;
      return c;
    }
    // `∫` (U+222B, UTF-8 E2 88 AB) → definite integral. Surface syntax:
    //   ∫ <body> from <lo> to <hi>
    // where <body> is a unary-precedence expression (typically a Call like
    // `f(x)` or a bare fn name) that Sema treats as the integrand. Builds the
    // IntegrateExpr { body, lo, hi }  -  `samples` keeps its default 1000.
    // `from`/`to` are CONTEXTUAL words here (bare `ident` tokens with that
    // literal text), NOT reserved keywords  -  the lexer has no `kw_from`/`kw_to`,
    // and this keeps the words usable as identifiers elsewhere.
    if (tx == "\xE2\x88\xAB") {
      p_++;
      auto body = parseUnary();
      if (!(at(Tok::ident) && cur().text == "from")) {
        error("expected 'from' after integral body");
        return body;  // graceful degradation: treat as the body expr alone
      }
      p_++;   // eat `from`
      auto lo = parseAdd();   // bounds tighter than `,`/`;` to avoid surprises
      if (!(at(Tok::ident) && cur().text == "to")) {
        error("expected 'to' after integral lower bound");
        auto ig = std::make_unique<IntegrateExpr>();
        ig->body = std::move(body); ig->lo = std::move(lo);
        ig->line = line; ig->col = col;
        return ig;
      }
      p_++;   // eat `to`
      auto hi = parseAdd();
      auto ig = std::make_unique<IntegrateExpr>();
      ig->body = std::move(body); ig->lo = std::move(lo); ig->hi = std::move(hi);
      ig->line = line; ig->col = col;
      return ig;
    }
    // Any other math_sym glyph (none currently emitted by the lexer) → a
    // generic MathSymExpr carrying the raw text; Sema resolves it (or rejects).
    p_++;
    auto m = std::make_unique<MathSymExpr>();
    m->text = tx; m->line = line; m->col = col;
    return m;
  }
  // ASCII `pi` → MathSymExpr("pi").
  if (at(Tok::kw_pi)) {
    int line = cur().line, col = cur().col;
    p_++;
    auto m = std::make_unique<MathSymExpr>();
    m->text = "pi"; m->line = line; m->col = col;
    return m;
  }
  // ASCII `sqrt <expr>` → runtime sqrt call, mirroring the `√` glyph.
  if (at(Tok::kw_sqrt)) {
    int line = cur().line, col = cur().col;
    p_++;
    auto operand = parseUnary();
    auto c = std::make_unique<Call>();
    c->callee = "sqrt";
    c->args.push_back(std::move(operand));
    c->line = line; c->col = col;
    return c;
  }
  // ASCII `integrate <body> from <lo> to <hi>` → IntegrateExpr, mirroring `∫`.
  if (at(Tok::kw_integrate)) {
    int line = cur().line, col = cur().col;
    p_++;
    auto body = parseUnary();
    // `from` / `to` are contextual  -  accept either Kw_from (if the lexer ever
    // adds it) or a bare ident "from"/"to".
    bool sawFrom = false;
    if (at(Tok::ident) && cur().text == "from") { p_++; sawFrom = true; }
    if (!sawFrom) error("expected 'from' after integrate body");
    auto lo = parseAdd();
    if (!(at(Tok::ident) && cur().text == "to"))
      error("expected 'to' after integrate lower bound");
    else p_++;
    auto hi = parseAdd();
    auto ig = std::make_unique<IntegrateExpr>();
    ig->body = std::move(body); ig->lo = std::move(lo); ig->hi = std::move(hi);
    ig->line = line; ig->col = col;
    return ig;
  }
  if (at(Tok::ident) || at(Tok::kw_print)) {
    auto name = cur().text;
    int line = cur().line, col = cur().col;
    p_++;


    if (name != "print" && at(Tok::lt)) {
      bool ok = false;
      std::vector<BType> gargs = tryParseTypeArgs(ok);
      if (ok) {

        if (at(Tok::lparen)) {
          p_++;
          auto call = std::make_unique<Call>();
          call->callee = name;
          call->line = line; call->col = col;
          call->typeArgs = std::move(gargs);
          call->hasTypeArgs = true;
          while (!at(Tok::rparen) && !at(Tok::end)) {
            call->args.push_back(parseExpr());
            if (!accept(Tok::comma)) break;
          }
          expect(Tok::rparen, "')'");
          return call;
        }

        if (allowStructLit_ && at(Tok::lbrace)) {
          p_++;
          auto sl = std::make_unique<StructLit>();
          sl->name = name;
          sl->line = line; sl->col = col;
          sl->typeArgs = std::move(gargs);
          sl->hasTypeArgs = true;
          while (!at(Tok::rbrace) && !at(Tok::end)) {
            if (!expect(Tok::ident, "field name")) break;
            sl->fieldNames.push_back(toks_[p_ - 1].text);
            if (!expect(Tok::colon, "':' in struct literal")) break;
            sl->values.push_back(parseExpr());
            if (!accept(Tok::comma)) break;
          }
          expect(Tok::rbrace, "'}'");
          return sl;
        }


        auto v = std::make_unique<VarRef>();
        v->name = name; v->line = line; v->col = col;
        return v;
      }
    }


    if (at(Tok::coloncolon) && name != "print") {
      p_++;

      if (!expect(Tok::ident, "method name after '::'")) {
        auto f = std::make_unique<IntLit>(); f->v = 0; f->line = line; return f;
      }
      std::string mname = toks_[p_ - 1].text;
      if (!expect(Tok::lparen, "'(' for associated function call")) {
        auto f = std::make_unique<IntLit>(); f->v = 0; f->line = line; return f;
      }
      auto ac = std::make_unique<AssocCall>();
      ac->typeName = name;
      ac->callee = mname;
      ac->line = line; ac->col = col;
      while (!at(Tok::rparen) && !at(Tok::end)) {
        ac->args.push_back(parseExpr());
        if (!accept(Tok::comma)) break;
      }
      expect(Tok::rparen, "')'");
      return ac;
    }

    if (allowStructLit_ && at(Tok::lbrace) && name != "print") {
      p_++;
      auto sl = std::make_unique<StructLit>();
      sl->name = name;
      sl->line = line; sl->col = col;
      while (!at(Tok::rbrace) && !at(Tok::end)) {
        if (!expect(Tok::ident, "field name")) break;
        sl->fieldNames.push_back(toks_[p_ - 1].text);
        if (!expect(Tok::colon, "':' in struct literal")) break;
        sl->values.push_back(parseExpr());
        if (!accept(Tok::comma)) break;
      }
      expect(Tok::rbrace, "'}'");
      return sl;
    }
    if (at(Tok::lparen)) {
      p_++;
      auto call = std::make_unique<Call>();
      call->callee = name;
      call->isPrint = (name == "print");
      call->line = line; call->col = col;
      while (!at(Tok::rparen) && !at(Tok::end)) {
        call->args.push_back(parseExpr());
        if (!accept(Tok::comma)) break;
      }
      expect(Tok::rparen, "')'");
      return call;
    }
    auto v = std::make_unique<VarRef>();
    v->name = name; v->line = line; v->col = col;
    return v;
  }
  error("expected an expression but got '" + cur().text + "'");
  auto f = std::make_unique<IntLit>();
  f->v = 0; f->line = cur().line;
  p_++;
  return f;
}

// Verified bitfield DSL  -  `bitfield Name: BaseType { field: bit N; field:
// bits A..B; ... }`. See AST BitfieldDecl/BitfieldField (AST.h), Lexer
// kw_bitfield/kw_bit/kw_bits (Lexer.h/.cpp), and the dispatch in
// parseProgram/atTopLevel above.
//
// DESIGN: parseBitfield parses the surface decl into a BitfieldDecl. Then
// lowerBitfield (a free helper called by parseProgram right after parsing)
// lowers it into a synthetic `struct Name { raw: baseType }` + `impl Name`
// carrying the generated accessor methods (field() -> u64) and setter
// methods (with_field(v) -> Self), each with verified `requires`/`ensures`
// contracts. The synthetic struct+impl flow through the WHOLE existing
// Sema/IRGen/Ghost pipeline for structs+impls UNCHANGED  -  method dispatch
// resolves via resolveMethod(structName, ...), bodies type-check via the
// `self.raw` Field access, and contracts discharge via emitFnContracts,
// exactly like hand-written structs+impls. This keeps the feature a thin
// frontend lowering with zero special-casing in Sema/IRGen.
//
// The BitfieldDecl itself is pushed into Program::bitfields (by parseProgram)
// for the Ghost encoder's SMT layout axioms (emitBitfieldAxioms in Ghost.cpp)
// so the bitfield semantics are available to ALL discharge queries, not just a
// method's own contract.

// --- Local AST builder helpers (programmatic Expr construction) ---
// These build the exact Expr trees the Parser would produce for the equivalent
// source expressions, so Sema/IRGen/the SMT encoder treat them identically to
// hand-written code. Line/col default to 0 (no source location) which is the
// same convention used for other synthetically-constructed nodes.
static ExprPtr mkIntLit(uint64_t v) {
  auto e = std::make_unique<IntLit>();
  e->v = v;
  return e;
}
static ExprPtr mkVar(const std::string& name) {
  auto e = std::make_unique<VarRef>();
  e->name = name;
  return e;
}
// `self.raw`  -  Field access on the implicit self receiver.
static ExprPtr mkSelfRaw() {
  auto f = std::make_unique<Field>();
  f->base = mkVar("self");
  f->field = "raw";
  return f;
}
static ExprPtr mkBinOp(BinaryExpr::Op op, ExprPtr lhs, ExprPtr rhs) {
  auto b = std::make_unique<BinaryExpr>();
  b->op = op;
  b->lhs = std::move(lhs);
  b->rhs = std::move(rhs);
  return b;
}
static ExprPtr mkUnaryOp(UnaryExpr::Op op, ExprPtr base) {
  auto u = std::make_unique<UnaryExpr>();
  u->op = op;
  u->base = std::move(base);
  return u;
}
// `result`  -  the special ensures name for the function's return value.
static ExprPtr mkResult() { return mkVar("result"); }
// `old(self.raw)`  -  pre-state snapshot of self.raw in ensures.
static ExprPtr mkOldSelfRaw() {
  auto o = std::make_unique<OldExpr>();
  o->sub = mkSelfRaw();
  return o;
}
// `result.field()`  -  a MethodCall on the result receiver. recvType is fixed
// up by Sema during contract checking (checkExpr's MethodCall arm sets it).
static ExprPtr mkResultMethodCall(const std::string& callee) {
  auto mc = std::make_unique<MethodCall>();
  mc->callee = callee;
  mc->receiver = mkResult();
  return mc;
}
// `old(self.field())`  -  pre-state snapshot of a self accessor in ensures.
static ExprPtr mkOldSelfMethodCall(const std::string& callee) {
  auto mc = std::make_unique<MethodCall>();
  mc->callee = callee;
  mc->receiver = mkVar("self");
  auto o = std::make_unique<OldExpr>();
  o->sub = std::move(mc);
  return o;
}
// `return <expr>;`  -  single-return body statement.
static StmtPtr mkReturn(ExprPtr value) {
  auto r = std::make_unique<ReturnStmt>();
  r->value = std::move(value);
  return r;
}

std::unique_ptr<BitfieldDecl> Parser::parseBitfield() {
  // Consume `bitfield`.
  int line = cur().line;
  p_++;   // eat kw_bitfield

  auto bf = std::make_unique<BitfieldDecl>();
  bf->line = line;

  // Parse the bitfield name.
  if (!expect(Tok::ident, "bitfield name after 'bitfield'")) return nullptr;
  bf->name = toks_[p_ - 1].text;

  // Expect `:` then the base integer type.
  if (!expect(Tok::colon, "':' after bitfield name")) return nullptr;
  BType baseType = parseType();
  if (!isInt(baseType)) {
    error("bitfield base type must be an integer type (u64, u32, i64, ...)");
    // Recover: skip to the matching rbrace or the next top-level decl.
    while (!at(Tok::rbrace) && !at(Tok::end)) p_++;
    accept(Tok::rbrace);
    return nullptr;
  }
  bf->baseType = baseType;

  // Expect `{` ... `}` body of field specifiers.
  if (!expect(Tok::lbrace, "'{' to begin bitfield body")) return nullptr;
  while (!at(Tok::rbrace) && !at(Tok::end)) {
    BitfieldField field;
    if (!expect(Tok::ident, "field name in bitfield body")) break;
    field.name = toks_[p_ - 1].text;
    if (!expect(Tok::colon, "':' after bitfield field name")) break;

    if (at(Tok::kw_bit)) {
      // `<name>: bit N;`  -  single-bit field at position N.
      p_++;   // eat kw_bit
      if (!expect(Tok::int_lit, "bit position after 'bit'")) break;
      int pos = (int)toks_[p_ - 1].u64;
      field.isSingleBit = true;
      field.lo = pos;
      field.hi = pos;
    } else if (at(Tok::kw_bits)) {
      // `<name>: bits A .. B;`  -  multi-bit inclusive range A..B.
      p_++;   // eat kw_bits
      if (!expect(Tok::int_lit, "low bit position after 'bits'")) break;
      int lo = (int)toks_[p_ - 1].u64;
      if (!expect(Tok::dotdot, "'..' between bit range bounds")) break;
      if (!expect(Tok::int_lit, "high bit position after '..'")) break;
      int hi = (int)toks_[p_ - 1].u64;
      field.isSingleBit = false;
      field.lo = lo;
      field.hi = hi;
    } else {
      error("expected 'bit N' or 'bits A..B' for a bitfield field specifier");
      // Skip to the next `;` or `}` and continue parsing fields.
      while (!at(Tok::semicolon) && !at(Tok::rbrace) && !at(Tok::end)) p_++;
      accept(Tok::semicolon);
      continue;
    }

    if (!expect(Tok::semicolon, "';' after bitfield field specifier")) break;
    bf->fields.push_back(std::move(field));
  }
  expect(Tok::rbrace, "'}' to close bitfield body");

  // Validate the bit layout at parse time so invalid decls surface early:
  //   lo >= 0, hi >= lo, hi < bitWidth(baseType).
  // (single-bit fields are parsed with lo == hi already, so no extra check.)
  int width = bitWidth(bf->baseType);
  // Report at the bitfield declaration's source line (cur() has moved past the
  // rbrace by validation time). Sema's errAt helper isn't visible to the
  // Parser, so we push directly into the parser's error vector.
  auto report = [&](const std::string& msg) {
    errs_->push_back({msg, bf->line, 0});
  };
  for (auto& f : bf->fields) {
    if (f.lo < 0) {
      report("bitfield '" + bf->name + "' field '" + f.name +
             "' has a negative bit position");
    }
    if (f.hi < f.lo) {
      report("bitfield '" + bf->name + "' field '" + f.name +
             "' has hi < lo in its bit range");
    }
    if (f.hi >= width) {
      report("bitfield '" + bf->name + "' field '" + f.name +
             "' bit " + std::to_string(f.hi) +
             " is out of range for base type " + typeSpelling(bf->baseType) +
             " (width " + std::to_string(width) + ")");
    }
  }

  // ox:why Field-name uniqueness  -  a duplicate name would otherwise collide when the
  // synthetic methods are registered (Sema registerMethod reports a
  // redefinition, but surfacing it here gives a clearer message).
  for (size_t i = 0; i < bf->fields.size(); ++i) {
    for (size_t j = i + 1; j < bf->fields.size(); ++j) {
      if (bf->fields[i].name == bf->fields[j].name) {
        report("bitfield '" + bf->name + "' redeclares field '" +
               bf->fields[i].name + "'");
      }
    }
  }

  return bf;
}

// lowerBitfield  -  emit a synthetic `struct Name { raw: baseType }` + `impl
// Name` with the accessor and setter methods into prog. The synthetic decls
// share the existing Sema/IRGen/Ghost pipeline for structs+impls unchanged.
//
// For each field (bit range [lo..hi], width w = hi-lo+1, mask = (1<<w)-1):
//  - Accessor `fn Name.field(self) -> u64`
//      ensures result == ((self.raw >> lo) & mask)
//      { return (self.raw >> lo) & mask; }
//  - Setter  `fn Name.with_field(self, v: u64) -> Name`
//      requires v <= mask
//      ensures result.field() == v
//      ensures result.other() == old(self.other())  [for every other field]
//      { return Name { raw: (self.raw & ~(mask << lo)) | ((v & mask) << lo) }; }
//
// `self` is taken by value (an integer-wrapper struct): cheap to copy, no
// aliasing, and the accessors are pure projections the SMT solver inlines
// trivially (single-return fast path). Setters return the bitfield struct
// (Self) so callers chain them: `entry.with_read(1).with_write(1)`.
void Parser::lowerBitfield(Program& prog, const BitfieldDecl& bf) {
  // The synthetic struct may already exist if a prior `bitfield` or hand-
  // written `struct` used the same name  -  Sema will report the redefinition
  // when it registers structs, so here we always push; the duplicate surfaces
  // cleanly at check time.
  // For each field generate accessor + setter; both are FuncDecls in one ImplDecl.
  auto sd = std::make_unique<StructDecl>();
  sd->name = bf.name;
  sd->line = bf.line;
  sd->isGeneric = false;
  sd->isOpaque = false;
  sd->fields.push_back({ "raw", bf.baseType, /*isPrivate=*/false });
  prog.structs.push_back(std::move(sd));

  auto im = std::make_unique<ImplDecl>();
  im->structName = bf.name;
  im->line = bf.line;

  // The bitfield struct type (Self), used as the setter return type.
  BType selfType;
  selfType.tag = BType::Tag::struct_;
  selfType.structName = bf.name;

  for (const auto& f : bf.fields) {
    int lo = f.lo;
    int hi = f.hi;
    int w = hi - lo + 1;
    // mask for the field's bits. A full-width field (w==64, only possible when
    // the base type IS 64-bit and the field occupies every bit) would overflow
    // `(1ULL << 64)-1`, so spell that case as the all-ones literal. Valid
    // sub-word fields (w <= 63) use the natural `(1<<w)-1`.
    bool fullWord = (w >= 64);
    uint64_t mask = fullWord ? ~0ULL : ((1ULL << w) - 1);

    auto mkMaskLit = [&] { return mkIntLit(mask); };

    // --- Accessor `fn Name.field(self) -> u64` ---
    //   ensures result == ((self.raw >> lo) & mask)
    //   { return (self.raw >> lo) & mask; }
    {
      auto fn = std::make_unique<FuncDecl>();
      fn->name = f.name;
      fn->line = bf.line;
      fn->retType = BType::u64;
      fn->hasSelf = true;
      fn->selfByRef = false;   // by-value: raw wrapper, cheap, pure
      fn->implStruct = bf.name;
      // ensures: result == ((self.raw >> lo) & mask)
      ExprPtr rhs = mkBinOp(BinaryExpr::Op::band,
                            mkBinOp(BinaryExpr::Op::shr,
                                    mkSelfRaw(),
                                    mkIntLit((uint64_t)lo)),
                            mkMaskLit());
      auto ens = std::make_unique<BinaryExpr>();
      ens->op = BinaryExpr::Op::eq;
      ens->lhs = mkResult();
      ens->rhs = std::move(rhs);
      fn->ensures_.push_back(std::move(ens));
      // body: return (self.raw >> lo) & mask;
      ExprPtr bodyExpr = mkBinOp(BinaryExpr::Op::band,
                                 mkBinOp(BinaryExpr::Op::shr,
                                         mkSelfRaw(),
                                         mkIntLit((uint64_t)lo)),
                                 mkMaskLit());
      fn->body.push_back(mkReturn(std::move(bodyExpr)));
      im->methods.push_back(std::move(fn));
    }

    // --- Setter `fn Name.with_field(self, v: u64) -> Name` ---
    //   requires v <= mask
    //   ensures result.field() == v
    //   ensures result.other() == old(self.other())  [for every OTHER field]
    //   { return Name { raw: (self.raw & ~(mask << lo)) | ((v & mask) << lo) }; }
    {
      auto fn = std::make_unique<FuncDecl>();
      fn->name = "with_" + f.name;
      fn->line = bf.line;
      fn->retType = selfType;
      fn->hasSelf = true;
      fn->selfByRef = false;
      fn->implStruct = bf.name;
      Param vp;
      vp.name = "v";
      vp.type = BType::u64;
      // Param owns a unique_ptr (defaultExpr); std::move it into the vector so
      // the deleted copy ctor is not invoked.
      fn->params.push_back(std::move(vp));
      // requires: v <= mask
      auto req = std::make_unique<BinaryExpr>();
      req->op = BinaryExpr::Op::le;
      req->lhs = mkVar("v");
      req->rhs = mkMaskLit();
      fn->requires_.push_back(std::move(req));
      // ensures: result.field() == v
      {
        auto ens = std::make_unique<BinaryExpr>();
        ens->op = BinaryExpr::Op::eq;
        ens->lhs = mkResultMethodCall(f.name);
        ens->rhs = mkVar("v");
        fn->ensures_.push_back(std::move(ens));
      }
      // ensures (preservation): for every OTHER field,
      //   result.other() == old(self.other())
      for (const auto& other : bf.fields) {
        if (other.name == f.name) continue;
        auto ens = std::make_unique<BinaryExpr>();
        ens->op = BinaryExpr::Op::eq;
        ens->lhs = mkResultMethodCall(other.name);
        ens->rhs = mkOldSelfMethodCall(other.name);
        fn->ensures_.push_back(std::move(ens));
      }
      // body: return Name { raw: (self.raw & ~(mask << lo)) | ((v & mask) << lo) };
      ExprPtr clearedMask = mkBinOp(BinaryExpr::Op::shl,
                                    mkMaskLit(), mkIntLit((uint64_t)lo));
      ExprPtr cleared = mkUnaryOp(UnaryExpr::Op::bnot, std::move(clearedMask));
      ExprPtr preserved = mkBinOp(BinaryExpr::Op::band,
                                   mkSelfRaw(), std::move(cleared));
      ExprPtr vMasked = mkBinOp(BinaryExpr::Op::band,
                                mkVar("v"), mkMaskLit());
      ExprPtr shifted = mkBinOp(BinaryExpr::Op::shl,
                                std::move(vMasked), mkIntLit((uint64_t)lo));
      ExprPtr newRaw = mkBinOp(BinaryExpr::Op::bor,
                               std::move(preserved), std::move(shifted));
      auto sl = std::make_unique<StructLit>();
      sl->name = bf.name;
      sl->fieldNames.push_back("raw");
      sl->values.push_back(std::move(newRaw));
      fn->body.push_back(mkReturn(std::move(sl)));
      im->methods.push_back(std::move(fn));
    }
  }

  prog.impls.push_back(std::move(im));
}
