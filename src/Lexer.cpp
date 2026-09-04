#include "Lexer.h"
#include <cctype>
#include <map>
#include <cstdio>

Lexer::Lexer(std::string src) : src_(std::move(src)) {}

void Lexer::error(const std::string& msg) {
  if (errs_) errs_->push_back({msg, line_, col_});
}

Tok Lexer::keyword(const std::string& s) {
  static const std::map<std::string, Tok> kws = {
    {"let", Tok::kw_let}, {"mut", Tok::kw_mut}, {"fn", Tok::kw_fn},
    {"return", Tok::kw_return}, {"if", Tok::kw_if}, {"else", Tok::kw_else},
    {"while", Tok::kw_while}, {"for", Tok::kw_for}, {"break", Tok::kw_break},
    {"continue", Tok::kw_continue}, {"in", Tok::kw_in},
    {"true", Tok::kw_true}, {"false", Tok::kw_false},
    {"as", Tok::kw_as}, {"const", Tok::kw_const},
    {"enum", Tok::kw_enum},
    {"match", Tok::kw_match}, {"null", Tok::kw_null},
    {"extern", Tok::kw_extern}, {"export", Tok::kw_export},
    {"typedef", Tok::kw_typedef},
    {"impl", Tok::kw_impl}, {"self", Tok::kw_self}, {"private", Tok::kw_private},
    {"sizeof", Tok::kw_sizeof},
    {"asm", Tok::kw_asm},
    {"virtual", Tok::kw_virtual},
    {"override", Tok::kw_override},
    {"defer", Tok::kw_defer},
    {"atomic", Tok::kw_atomic},   // D7  -  `atomic { <stmts> }` block (spec mark)
    {"unsafe", Tok::kw_unsafe},   // `unsafe { <stmts> }` block / `unsafe fn`
    // Concurrency primitives: `spawn`/`sync`/`Channel<T>`. `Channel` lexes as
    // kw_chan so it is reserved everywhere (matches the surface `Channel<T>`
    // type syntax in parseType and the `Channel<T>` primary form in
    // parsePrimary). `<-` is a separate operator token (Tok::chanArrow), NOT a
    // keyword, so `kw_send`/`kw_recv` are reserved but currently lack a bare
    // keyword spelling  -  they exist for future keyword-style channel methods.
    {"spawn",   Tok::kw_spawn},
    {"sync",    Tok::kw_sync},
    {"Channel", Tok::kw_chan},
    {"noninterference", Tok::kw_noninterference},  // D8  -  Owicki-Gries stability
    {"cycle_preserves", Tok::kw_cycle_preserves},  // D9 (gap #6)  -  VM-exit-cycle refinement
    {"concept", Tok::kw_concept}, {"where", Tok::kw_where},
    {"requires", Tok::kw_requires},
    // Formal-verification contracts (reserved; not identifiers).
    {"ensures", Tok::kw_ensures}, {"invariant", Tok::kw_invariant},
    {"assert", Tok::kw_assert}, {"old", Tok::kw_old},
    {"implies", Tok::kw_implies}, {"forall", Tok::kw_forall},
    {"exists", Tok::kw_exists},
    // D6  -  `decreases <expr>` termination-measure clause (contracts keyword).
    {"decreases", Tok::kw_decreases},
    // ox:proof T1/T2/T3: ghost state, named regions, abstraction refinement.
    {"ghost", Tok::kw_ghost}, {"region", Tok::kw_region},
    {"spec", Tok::kw_spec}, {"refines", Tok::kw_refines},
    {"modifies", Tok::kw_modifies},
    // Verified-asm link clause  -  `asm!(...) implements <spec_fn>(<args>)`.
    // Reserved as a keyword so the Parser sees `kw_implements` after an asm!
    // block; outside that position it is a parse error.
    {"implements", Tok::kw_implements},
    // Effect system  -  `effects { eff1, eff2, ... }` clause on a `fn` head.
    // Reserved keyword (lexed as kw_effects, not a bare identifier) so the
    // Parser recognises it as a contract-clause head in the post-signature
    // tail loop (alongside requires/ensures/modifies/decreases).
    {"effects", Tok::kw_effects},
    // Missing-#6: module-level `preserves <handler_fn> <= <invariant_spec_fn>`.
    {"preserves", Tok::kw_preserves},
    // ox:proof Lemma functions  -  `lemma fn name(params) requires ... ensures ... { }`.
    // Proof-only top-level decl (like `ghost fn` with a `lemma` prefix).
    {"lemma", Tok::kw_lemma},
    // ox:proof D3: module-level `axiom <expr>;`  -  top-level SMT axiom over spec fns.
    {"axiom", Tok::kw_axiom},
    // ox:proof Namespace/audit extension: `trusted axiom` + `source "..."` clause. Both
    // mark trusted (non-machine-verified) facts and attach citations surfaced
    // by `oxide verify --audit-axioms`; see AxiomDecl.
    {"trusted", Tok::kw_trusted},
    {"source", Tok::kw_source},
    // ox:unsafe fix2: `trap` / `handler`  -  top-level trap-handler declaration keywords.
    {"trap", Tok::kw_trap},
    {"handler", Tok::kw_handler},
    // ox:unsafe Part 2: `discharge`  -  optional clause marker for `trap handler ...
    // discharge requires ...`. Switches the trap handler's `requires` from
    // ASSUMED (the default trap-handler mode) to DISCHARGED (proven from the
    // VM-exit context axioms). A bare `discharge` elsewhere stays an
    // identifier (same reserved-keyword rule as `on`/`handler`).
    {"discharge", Tok::kw_discharge},
    // fixB: `instantiate` keyword for the guided-instantiation pragma.
    {"instantiate", Tok::kw_instantiate},
    // fixB: `on` keyword  -  clause separator inside `instantiate`.
    {"on", Tok::kw_on},
    // ox:proof fixC: `proof` construct  -  interactive induction proof mode keywords.
    {"proof", Tok::kw_proof},
    {"that", Tok::kw_that},
    {"by", Tok::kw_by},
    {"induction", Tok::kw_induction},
    {"base", Tok::kw_base},
    {"step", Tok::kw_step},
    {"assume", Tok::kw_assume},
    {"prove", Tok::kw_prove},
    // calcD: `calc` keyword for the equational-reasoning block. Reserved
    // everywhere (not an identifier), like `proof`.
    {"calc", Tok::kw_calc},
    // Compile-time macro/metaprogramming: `macro name(params) { body }` and
    // `expand name(args)` at the use site. Reserved everywhere (not identifiers).
    {"macro", Tok::kw_macro},
    {"expand", Tok::kw_expand},
    {"i64", Tok::kw_i64}, {"f64", Tok::kw_f64}, {"bool", Tok::kw_bool},
    {"void", Tok::kw_void}, {"print", Tok::kw_print}, {"struct", Tok::kw_struct},
    // Verified bitfield DSL  -  `bitfield Name: BaseType { field: bit N; field:
    // bits A..B; ... }`. `bitfield` starts the top-level decl; `bit` and
    // `bits` are the field specifiers inside the body (single-bit position vs
    // an inclusive bit range). See Lexer.h kw_bitfield/kw_bit/kw_bits.
    {"bitfield", Tok::kw_bitfield},
    {"bit", Tok::kw_bit},
    {"bits", Tok::kw_bits},
    {"str", Tok::kw_str}, {"vec", Tok::kw_vec},
    {"map", Tok::kw_map}, {"set", Tok::kw_set},
    {"hmap", Tok::kw_hmap}, {"hset", Tok::kw_hset},
    {"char", Tok::kw_char},
    {"i8", Tok::kw_i8}, {"i16", Tok::kw_i16}, {"i32", Tok::kw_i32},
    {"u8", Tok::kw_u8}, {"u16", Tok::kw_u16}, {"u32", Tok::kw_u32},
    {"u64", Tok::kw_u64}, {"usize", Tok::kw_usize},
    {"f32", Tok::kw_f32},
    // ASCII fallback keywords for Unicode math glyphs. `pi`↔π, `integrate`↔∫,
    // `sqrt`↔√, `pow2`↔², `pow3`↔³. Reserved everywhere (not identifiers) so the
    // Parser recognises them in expression position alongside the `math_sym`
    // Unicode glyphs. See Lexer.h kw_pi/kw_integrate/kw_sqrt/kw_pow2/kw_pow3.
    {"pi", Tok::kw_pi},
    {"integrate", Tok::kw_integrate},
    {"sqrt", Tok::kw_sqrt},
    {"pow2", Tok::kw_pow2},
    {"pow3", Tok::kw_pow3},
  };
  auto it = kws.find(s);
  return it == kws.end() ? Tok::ident : it->second;
}

static bool isIdentStart(char c) {
  return std::isalpha((unsigned char)c) || c == '_';
}
static bool isIdentPart(char c) {
  return std::isalnum((unsigned char)c) || c == '_';
}

bool Lexer::lex(std::vector<Token>& out, std::vector<LexError>& errs) {
  errs_ = &errs;
  auto peekc = [&]() -> char { return (i_ + 1 < src_.size()) ? src_[i_ + 1] : 0; };
  auto peekc2 = [&]() -> char { return (i_ + 2 < src_.size()) ? src_[i_ + 2] : 0; };
  while (i_ < src_.size()) {
    char c = src_[i_];


    if (c == ' ' || c == '\t' || c == '\r') { i_++; col_++; continue; }
    if (c == '\n') { i_++; line_++; col_ = 1; continue; }


    if (c == '/' && i_ + 1 < src_.size() && src_[i_ + 1] == '/') {
      while (i_ < src_.size() && src_[i_] != '\n') { i_++; col_++; }
      continue;
    }

    if (c == '/' && i_ + 1 < src_.size() && src_[i_ + 1] == '*') {
      i_ += 2; col_ += 2;
      while (i_ + 1 < src_.size() &&
             !(src_[i_] == '*' && src_[i_ + 1] == '/')) {
        if (src_[i_] == '\n') { line_++; col_ = 1; }
        else col_++;
        i_++;
      }
      if (i_ + 1 >= src_.size()) {
        error("unterminated block comment");
        i_ = src_.size();
        continue;
      }
      i_ += 2; col_ += 2;
      continue;
    }

    Token t;
    t.line = line_;
    t.col = col_;


    if (isIdentStart(c)) {
      size_t start = i_;
      while (i_ < src_.size() && isIdentPart(src_[i_])) { i_++; col_++; }
      t.text = src_.substr(start, i_ - start);
      t.kind = keyword(t.text);
      out.push_back(t);
      continue;
    }


    if (std::isdigit((unsigned char)c)) {
      size_t start = i_;
      bool isFloat = false;


      int base = 10;
      if (c == '0' && i_ + 1 < src_.size() &&
          (src_[i_ + 1] == 'x' || src_[i_ + 1] == 'X')) {
        i_ += 2; col_ += 2;
        base = 16;
        while (i_ < src_.size() && std::isxdigit((unsigned char)src_[i_])) { i_++; col_++; }
        t.text = src_.substr(start, i_ - start);
        t.kind = Tok::int_lit;
        try { t.u64 = std::stoull(t.text, nullptr, 16); }
        catch (...) { error("invalid hex literal"); t.u64 = 0; }
        out.push_back(t);
        continue;
      }
      if (c == '0' && i_ + 1 < src_.size() &&
          (src_[i_ + 1] == 'b' || src_[i_ + 1] == 'B')) {
        i_ += 2; col_ += 2;
        base = 2;
        while (i_ < src_.size() && (src_[i_] == '0' || src_[i_] == '1')) { i_++; col_++; }
        t.text = src_.substr(start, i_ - start);
        t.kind = Tok::int_lit;
        try { t.u64 = std::stoull(t.text, nullptr, 2); }
        catch (...) { error("invalid binary literal"); t.u64 = 0; }
        out.push_back(t);
        continue;
      }
      while (i_ < src_.size() && std::isdigit((unsigned char)src_[i_])) { i_++; col_++; }
      if (i_ < src_.size() && src_[i_] == '.' &&
          i_ + 1 < src_.size() && std::isdigit((unsigned char)src_[i_ + 1])) {
        isFloat = true;
        i_++; col_++;
        while (i_ < src_.size() && std::isdigit((unsigned char)src_[i_])) { i_++; col_++; }
      }
      t.text = src_.substr(start, i_ - start);
      if (isFloat) {
        t.kind = Tok::float_lit;
        // Optional single-precision suffix `f` or `F` (2.0f / 1.5F). Forms
        // like 0.5f are f32; 0.5 is f64. The suffix is NOT an identifier char,
        // so `2.0foo` still reads as 2.0 then ident `oo`  -  but `2.0f` with an
        // immediately-following identifier char (e.g. method call `0.5f.sin`)
        // would wrongly swallow the `s`... no: `f`/`F` is consumed only as a
        // suffix when not an identifier-start, so guard against letter runs.
        bool hasFSuffix = false;
        if (i_ < src_.size() && (src_[i_] == 'f' || src_[i_] == 'F')) {
          size_t nx = i_ + 1;
          if (nx >= src_.size() || !isIdentPart(src_[nx])) {
            hasFSuffix = true;
            i_++; col_++;
            t.text = src_.substr(start, i_ - start);
          }
        }
        t.isF32 = hasFSuffix;
        if (hasFSuffix) {
          // std::stod ignores a trailing 'f'; parse the numeric core alone.
          t.f64 = std::stod(src_.substr(start, i_ - start - 1));
        } else {
          t.f64 = std::stod(t.text);
        }
      } else {
        t.kind = Tok::int_lit;
        // Integer literals also accept `f`? No  -  `5f` is ambiguous with hex `f`.
        // Only allow f-suffix on a *floating* literal (must have a `.` or be a
        // digit run we decided was float). Keep ints unaffected.
        t.u64 = std::stoull(t.text);
      }
      out.push_back(t);
      continue;
    }


    if (c == '\'') {
      i_++; col_++;
      unsigned char ch;
      bool ok = false, closing = false;
      while (i_ < src_.size()) {
        char cur = src_[i_];
        if (cur == '\'') { i_++; col_++; closing = true; break; }
        if (cur == '\\') {
          i_++; col_++;
          if (i_ >= src_.size()) { error("unterminated char"); break; }
          char esc = src_[i_++];
          col_++;
          switch (esc) {
            case 'n': ch = '\n'; break;
            case 't': ch = '\t'; break;
            case 'r': ch = '\r'; break;
            case '0': ch = '\0'; break;
            case 'a': ch = '\a'; break;
            case 'b': ch = '\b'; break;
            case 'f': ch = '\f'; break;
            case 'v': ch = '\v'; break;
            case '\'': ch = '\''; break;
            case '\\': ch = '\\'; break;
            case '"': ch = '"'; break;
            case 'x': {

              auto hexval = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
              };
              int hi = (i_ < src_.size()) ? hexval(src_[i_]) : -1;
              if (hi < 0) { ch = (unsigned char)'x'; }
              else {
                i_++; col_++;
                int lo = (i_ < src_.size()) ? hexval(src_[i_]) : -1;
                int val = hi;
                if (lo >= 0) { val = (hi << 4) | lo; i_++; col_++; }
                ch = (unsigned char)(val & 0xFF);
              }
              break;
            }
            default: ch = (unsigned char)esc; break;
          }
          ok = true;
          if (i_ < src_.size() && src_[i_] == '\'') { i_++; col_++; closing = true; break; }
          else { error("char literal must be a single character"); break; }
        } else {
          if (cur == '\n') { line_++; col_ = 1; }
          else col_++;
          ch = (unsigned char)cur;
          ok = true;
          i_++;

          if (i_ < src_.size() && src_[i_] == '\'') { i_++; col_++; closing = true; break; }
          else { error("char literal must be a single character"); break; }
        }
      }
      if (!closing) error("unterminated char");
      t.kind = Tok::char_lit;
      t.u64 = ok ? (uint64_t)ch : 0;
      out.push_back(t);
      continue;
    }


    if (c == '"') {
      i_++; col_++;
      std::string s;
      bool closing = false;
      while (i_ < src_.size()) {
        char ch = src_[i_];
        if (ch == '"') { i_++; col_++; closing = true; break; }
        if (ch == '\\') {
          i_++; col_++;
          if (i_ >= src_.size()) {
            error("unterminated string");
            break;
          }
          char esc = src_[i_++];
          col_++;
          switch (esc) {
            case 'n': s += '\n'; break;
            case 't': s += '\t'; break;
            case 'r': s += '\r'; break;
            case '0': s += '\0'; break;
            case 'a': s += '\a'; break;
            case 'b': s += '\b'; break;
            case 'f': s += '\f'; break;
            case 'v': s += '\v'; break;
            case '"': s += '"'; break;
            case '\\': s += '\\'; break;
            case 'x': {


              auto hexval = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
              };
              int hi = (i_ < src_.size()) ? hexval(src_[i_]) : -1;
              if (hi >= 0) {
                i_++; col_++;
                int lo = (i_ < src_.size()) ? hexval(src_[i_]) : -1;
                int val = hi;
                if (lo >= 0) { val = (hi << 4) | lo; i_++; col_++; }
                s += static_cast<char>(val & 0xFF);
              } else {
                s += 'x';
              }
              break;
            }
            default: s += esc; break;
          }
        } else {
          if (ch == '\n') { line_++; col_ = 1; }
          else col_++;
          s += ch;
          i_++;
        }
      }
      if (!closing) { error("unterminated string"); }
      t.kind = Tok::str_lit;
      t.text = std::move(s);
      out.push_back(t);
      continue;
    }


    auto two = [&](char first, char second) -> bool {
      if (i_ + 1 < src_.size() && src_[i_] == first && src_[i_ + 1] == second) {
        i_ += 2; col_ += 2; return true;
      }
      return false;
    };
    auto one = [&]() { i_++; col_++; };

    switch (c) {
      case '+':
        if (two('+', '+')) t.kind = Tok::inc;
        else if (two('+', '=')) t.kind = Tok::pluseq;
        else { t.kind = Tok::plus; one(); }
        break;
      case '-':
        if (two('-', '-')) t.kind = Tok::dec;
        else if (two('-', '>')) t.kind = Tok::arrow;
        else if (two('-', '=')) t.kind = Tok::minuseq;
        else { t.kind = Tok::minus; one(); }
        break;
      case '*': {
        // `**` power operator (Tok::pow, higher precedence than `*`),
        // `*=` compound assign, else plain `*`.
        char n = peekc();
        if (n == '*') { t.kind = Tok::pow; i_ += 2; col_ += 2; }
        else if (two('*', '=')) t.kind = Tok::stareq;
        else { t.kind = Tok::star; one(); }
        break;
      }
      case '\\': t.kind = Tok::backslash; one(); break;
      case '/': t.kind = two('/', '=') ? Tok::lasheq : Tok::slash; if (t.kind == Tok::slash) one(); break;
      case '%': t.kind = two('%', '=') ? Tok::percenteq : Tok::percent; if (t.kind == Tok::percent) one(); break;
      case '=': t.kind = two('=', '=') ? Tok::eqeq : (two('=', '>') ? Tok::fatarrow : Tok::eq); if (t.kind == Tok::eq) one(); break;
      case '!': t.kind = two('!', '=') ? Tok::bangeq : Tok::bang; if (t.kind == Tok::bang) one(); break;
      case '<': {
        char n = peekc();
        if (n == '<') { char m = peekc2(); t.kind = (m == '=') ? Tok::shleq : Tok::shl; i_ += (m=='=')?3:2; col_ += (m=='=')?3:2; }
        else if (n == '=') { t.kind = Tok::lteq; i_+=2; col_+=2; }
        else if (n == '-') { t.kind = Tok::chanArrow; i_+=2; col_+=2; }
        else { t.kind = Tok::lt; one(); }
        break;
      }
      case '>': {
        char n = peekc();
        if (n == '>') { char m = peekc2(); t.kind = (m == '=') ? Tok::shreq : Tok::shr; i_ += (m=='=')?3:2; col_ += (m=='=')?3:2; }
        else if (n == '=') { t.kind = Tok::gteq; i_+=2; col_+=2; }
        else { t.kind = Tok::gt; one(); }
        break;
      }
      case '&': t.kind = two('&', '&') ? Tok::ampamp : (two('&', '=') ? Tok::ampeq : Tok::amp); if (t.kind == Tok::amp) one(); break;
      case '|': t.kind = two('|', '|') ? Tok::barbar : (two('|', '=') ? Tok::bareq : Tok::bar); if (t.kind == Tok::bar) one(); break;
      case '^': t.kind = two('^', '=') ? Tok::careteq : Tok::caret; if (t.kind == Tok::caret) one(); break;
      case '~': t.kind = Tok::tilde; one(); break;
      case '?': t.kind = Tok::question; one(); break;
      case '(': t.kind = Tok::lparen; one(); break;
      case ')': t.kind = Tok::rparen; one(); break;
      case '{': t.kind = Tok::lbrace; one(); break;
      case '}': t.kind = Tok::rbrace; one(); break;
      case '[': t.kind = Tok::lbracket; one(); break;
      case ']': t.kind = Tok::rbracket; one(); break;
      case ',': t.kind = Tok::comma; one(); break;
      case ';': t.kind = Tok::semicolon; one(); break;
      case ':':
        if (two(':', ':')) t.kind = Tok::coloncolon;
        else { t.kind = Tok::colon; one(); }
        break;
      case '.': {
        // `..=` inclusive range, `..` exclusive range, `.` field/select.
        if (i_ + 2 < src_.size() && src_[i_] == '.' && src_[i_+1] == '.' && src_[i_+2] == '=') {
          t.kind = Tok::dotdoteq; i_ += 3; col_ += 3;
        } else {
          t.kind = two('.', '.') ? Tok::dotdot : Tok::dot; if (t.kind == Tok::dot) one();
        }
        break;
      }
      case '$': t.kind = Tok::dollar; one(); break;
      default: {
        // Unicode math glyph → Tok::math_sym. We recognise a fixed set of
        // multi-byte UTF-8 sequences (π, ∫, √, ², ³) and store the raw glyph
        // bytes in `t.text`; the Parser maps text→meaning (π→pi, ∫→integrate,
        // √→sqrt, ²→pow2, ³→pow3). Any other non-ASCII byte sequence falls
        // through to the unexpected-character error below. We count one
        // column per glyph (not per byte).
        auto emitMathSym = [&](size_t nbytes, const char* name) {
          t.kind = Tok::math_sym;
          t.text.assign(src_, i_, nbytes);
          i_ += nbytes; col_ += 1;
          (void)name;  // name kept for readability of the table below
        };
        if (i_ + 1 < src_.size()) {
          unsigned char b0 = (unsigned char)src_[i_];
          unsigned char b1 = (unsigned char)src_[i_ + 1];
          // 2-byte sequences (U+0080..U+07FF): 0xC2/0xC3 lead byte.
          if (b0 == 0xC2 && b1 == 0xB2) { emitMathSym(2, "superscript two (pow2)"); break; }
          if (b0 == 0xC2 && b1 == 0xB3) { emitMathSym(2, "superscript three (pow3)"); break; }
          if (b0 == 0xCF && b1 == 0x80) { emitMathSym(2, "pi"); break; }
          // 3-byte sequences (U+0800..U+FFFF): 0xE2 lead byte, math block.
          if (i_ + 2 < src_.size()) {
            unsigned char b2 = (unsigned char)src_[i_ + 2];
            if (b0 == 0xE2 && b1 == 0x88 && b2 == 0xAB) { emitMathSym(3, "integral"); break; }
            if (b0 == 0xE2 && b1 == 0x88 && b2 == 0x9A) { emitMathSym(3, "square root"); break; }
          }
        }
        error(std::string("unexpected character '") + c + "'");
        i_++; col_++;
        t.kind = Tok::err;
        t.text = std::string(1, c);
        break;
      }
    }
    out.push_back(t);
  }

  Token eof;
  eof.kind = Tok::end;
  eof.line = line_;
  eof.col = col_;
  out.push_back(eof);
  return errs.empty();
}
