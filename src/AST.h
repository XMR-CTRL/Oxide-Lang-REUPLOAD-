#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <tuple>


struct BType {
  enum class Tag {
    i64, f64, bool_, void_, str, array, struct_, dynarray,

    char_, ptr,
    i8, i16, i32, u8, u16, u32, u64, usize,


    enum_,


    f32,


    map_,


    set_,


    hmap_,


    hset_,

    fn_,


    generic_,

    // Concurrency: `Channel<T>` is a buffered message-passing channel between
    // threads. The element type lives in the shared type table (see
    // Types.cpp::makeChannelType), referenced via `BType::elem` exactly as the
    // other interned composite types (array/dynarray/ptr/...) index into it.
    channel_,
  } tag = Tag::void_;

  int32_t count = 0;
  int32_t elem = 0;
  std::string structName;

  // Feature 4  -  Range types. When a typedef carries a `where lo <= X < hi`
  // clause, the resolved BType marks hasRange=true and records the (possibly
  // symbolic) integer bounds. rangeLo/rangeHi are 0 when the bound is symbolic
  // (it references a param name like `n`); in that case the range constraint is
  // reconstructed at SMT-emit time from the registry (see registerRangeType /
  // findRangeType). A concrete-literal bound (e.g. `where 0 <= X < 10`) is
  // stored directly so simple numeric ranges discharge without the registry.
  // rangeTypeName is the typedef NAME whose `where` clause produced this range
  // (used to look up the symbolic constraint in the registry). It is "" for
  // ordinary (non-range) types, so existing code that copies BType by value
  // keeps working  -  the fields default to inert values.
  bool hasRange = false;
  int64_t rangeLo = 0;
  int64_t rangeHi = 0;
  std::string rangeTypeName;   // "" unless this is a range type alias

  bool operator==(const BType& o) const {
    return tag == o.tag && count == o.count && elem == o.elem && structName == o.structName;
  }
  bool operator!=(const BType& o) const { return !(*this == o); }


  static const BType i64;
  static const BType f64;
  static const BType bool_;
  static const BType void_;
  static const BType str;

  static const BType char_;
  static const BType i8, i16, i32, u8, u16, u32, u64, usize;
  static const BType f32;
};

inline const BType BType::i64   {Tag::i64};
inline const BType BType::f64   {Tag::f64};
inline const BType BType::bool_ {Tag::bool_};
inline const BType BType::void_ {Tag::void_};
inline const BType BType::str    {Tag::str};
inline const BType BType::char_  {Tag::char_};
inline const BType BType::i8     {Tag::i8};
inline const BType BType::i16    {Tag::i16};
inline const BType BType::i32    {Tag::i32};
inline const BType BType::u8     {Tag::u8};
inline const BType BType::u16    {Tag::u16};
inline const BType BType::u32    {Tag::u32};
inline const BType BType::u64    {Tag::u64};
inline const BType BType::usize  {Tag::usize};
inline const BType BType::f32    {Tag::f32};

bool isInt(const BType& t);
bool isFloat(const BType& t);
bool isNumeric(const BType& t);
bool isScalar(const BType& t);
bool isSignedInt(const BType& t);
int bitWidth(const BType& t);
const char* typeName(const BType& t);
std::string typeSpelling(const BType& t);


BType makeArrayType(const BType& elem, int32_t count);
const BType& arrayElem(const BType& arr);


BType makeDynArray(const BType& elem);
const BType& dynArrayElem(const BType& arr);

BType makePtr(const BType& pointee);
const BType& pointee(const BType& p);

BType makeMapType(const BType& key, const BType& val);
const BType& mapKeyType(const BType& m);
const BType& mapValType(const BType& m);


BType makeSetType(const BType& elem);
const BType& setElemType(const BType& s);
BType makeHMapType(const BType& key, const BType& val);
BType makeHSetType(const BType& elem);


// Concurrency  -  `Channel<T>`: a buffered message-passing channel between
// threads. The element type is interned in the shared type table (exactly like
// set/dynarray/ptr); `channelElemType` unwraps it. Used by Sema to type-check
// `chan <- val` (T must equal T_chan) and `<- chan` (returns T_chan), and by
// IRGen to choose the right `@ox_chan_*` runtime call sequence.
BType makeChannelType(const BType& elem);
const BType& channelElemType(const BType& ch);


BType makeFnType(const std::vector<BType>& params, const BType& ret);
const std::vector<BType>& fnParams(const BType& t);
BType fnRet(const BType& t);


BType makeGenericInst(const std::string& base, const std::vector<BType>& args, bool isFn);
bool isGenericInst(const BType& t);
const std::string& genericInstBase(const BType& t);
const std::vector<BType>& genericInstArgs(const BType& t);
bool genericInstIsFn(const BType& t);


struct StructDecl;
struct FuncDecl;
void registerGenericStruct(const StructDecl* tmpl);
const StructDecl* findGenericStruct(const std::string& name);
void registerGenericFn(const FuncDecl* tmpl);
const FuncDecl* findGenericFn(const std::string& name);


BType instantiateGenericStruct(const std::string& base, const std::vector<BType>& args);


// A generic type parameter, as declared in `<T: ConceptName = DefaultType>`.
//   name:       the parameter name (T, U, ...).
//   constraint: a concept the arg must satisfy, or "" for an unconstrained
//               param (C++20-concepts / Rust-bound style).
//   hasDefault: true iff a default type was supplied (`= Type`); the default
//               is used when the callee names the generic with fewer explicit
//               type args than there are params (trailing-default fill, like
//               C++ `template <typename T = i64>`).
//   defaultType: the default type (only meaningful when hasDefault). Parsed as
//               a BType; resolveAlias/fixType applied at instantiation time.
//
// The existing `typeParams: vector<string>` on StructDecl/FuncDecl is kept as a
// cheap "just the names" projection of `tparams` so the large body of code that
// only cares about parameter COUNT/NAMES keeps working unchanged; the richer
// `tparams` vector carries constraint/default info for the new constraint and
// default-type-argument machinery. Defined early here so FuncDecl/StructDecl
// (below) can embed it.
struct TypeParam {
  std::string name;
  std::string constraint;   // concept name, or ""
  bool hasDefault = false;
  BType defaultType = BType::i64;
};


// --- C++-level templates + concepts ---
// A `concept` is a NAMED, COMPILE-TIME SET OF REQUIREMENTS on a type (think
// C++20 concepts, NOT a Rust trait / runtime interface). It lists required
// method and associated-function signatures that a candidate type must supply
// (via an `impl`) to "satisfy" the concept. Concepts are used purely as
// *constraints* on generic type params: `fn f<T: ConceptName>(...)` rejects
// instantiations whose T lacks the required methods. They introduce NO runtime
// polymorphism, NO vtable, NO inheritance  -  purely a static predicate. This
// fills the "any type with a write method" niche the single-inheritance / no-
// traits model would otherwise miss, at compile time only.
//
// ConceptDef: the runtime table entry (registered by Sema during the decl-walk
// pass). ConceptReq: one required signature, parsed from the concept body as
// `fn method(&self) -> Ret` (self-receiver method) or `fn assoc(arg) -> Ret`
// (an associated function  -  no self). `hasSelf` mirrors FuncDecl::hasSelf.
struct ConceptDef;
void registerConceptDef(std::unique_ptr<struct ConceptDecl> c);
ConceptDef* findConceptDef(const std::string& name);
std::vector<ConceptDef*> allConceptDefs();

struct ConceptReq {
  // One required callable. `name` is the method/associated-fn name. `retType`
  // and `params` are the required signature; if `hasSelf`, the FIRST param is
  // a `&self`/`self` receiver and `selfByRef` says whether it is `&self`. For
  // matching, the candidate's impl method must have a compatible signature
  // (param/return types must agree up to the candidate's own `Self` type  -  for
  // simplicity we require EXACT shape match against the candidate's impl, with
  // the receiver elided; see satisfiesConcept in Sema). `typeParamsUsed` lists
  // type-parameter names mentioned in the signature that should be substituted
  // by the concrete type being checked when the req is evaluated.
  std::string name;
  bool hasSelf = false;
  bool selfByRef = false;
  std::vector<BType> params;   // NOT counting the receiver
  BType retType = BType::void_;
};

struct ConceptDef {
  std::string name;
  std::vector<ConceptReq> reqs;
  // The single type-parameter the concept is parameterised on, as declared by
  // `concept C<T> { ... }`. Oxide concepts take exactly ONE type param (the
  // `Self` being constrained) for now  -  multi-param concepts (`C<T, U>`) are a
  // possible extension but not needed for `T: C` constraint syntax. "" = the
  // concept takes no param (a degenerate always-trivial concept).
  std::string selfParam;
};

// A parsed `concept` declaration. Registered by Sema into the ConceptDef table
// (ConceptReq-by-ConceptReq) during the decl-walk pass; after that the parser
// copy is not consulted again. `selfParam` is the lone type param declared in
// `concept C<T>`. Each `req` entry mirrors a parsed `fn` signature in the body.


void registerAlias(const std::string& name, const BType& target);
bool isAliasName(const std::string& name);
std::pair<bool, BType> resolveAlias(const std::string& name);

// Forward decl  -  Expr is defined later in this file (line ~312); the range
// type registry stores a non-owning pointer to the parsed `where` expression.
struct Expr;

// Forward decl + alias for Stmt. `struct Stmt` is defined later (line ~654),
// but `MacroCall` (line ~510) holds `std::vector<StmtPtr>` for expanded
// leading-lets before Stmt's full definition is in scope. `std::unique_ptr`
// only needs a forward-declared pointee (the deleter is incomplete at use and
// instantiated where the deleter runs), so declaring the alias here lets the
// earlier users parse. The `using StmtPtr` that previously lived at the `Stmt`
// definition (line ~661) is removed to avoid a duplicate-alias-declaration
// error in MSVC's strict mode (which this build exercises).
struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

// Feature 4  -  Range type registry. `registerRangeType(name, target, expr)`
// associates a typedef NAME with its resolved base BType AND the boolean
// constraint expression parsed from the `where` clause. The SMT encoder
// (Driver.cpp emitFnContracts) consults `findRangeType` for each function
// param whose BType carries a non-empty `rangeTypeName` (set by Sema when a
// param's type resolves to a range alias), so it can emit the constraint
// against the param's SMT symbol as an automatic premise.
//
// `RangeTypeEntry::target` is the resolved (fixType'd) base type  -  duplicated
// from the alias table so callers don't need a second lookup. `expr` is the
// owned Expr tree whose VarRef to the typedef NAME will be rebound to the
// param's SMT symbol at emit time (the SmtCtx nameMap carries that binding).
struct RangeTypeEntry {
  BType target = BType::i64;
  Expr* expr = nullptr;   // NOT owned here; owned by the TypedefDecl in Program
};
void registerRangeType(const std::string& name, const BType& target, Expr* expr);
const RangeTypeEntry* findRangeType(const std::string& name);

struct StructField { std::string name; BType type; int32_t offset = 0; bool isPrivate = false; };
struct StructDef {
  std::string name;
  std::vector<StructField> fields;
  int32_t size = 0;
  int32_t align = 0;
  std::string genericOf;
  std::vector<BType> genericArgs;
  bool isOpaque = false;   // `extern struct Name;`  -  no fields, only addressable via *T

  // --- RAII (rule-of-five, opt-in / Rust-style) ---
  // hasDrop: the type has an `impl T { fn drop(&mut self) {...} }`. Such types are
  // move-only by default (no implicit copy)  -  the destructor is run at scope exit.
  // hasClone: the type has an `impl T { fn clone(&self) -> T {...} }`, an explicit
  // opt-in copy (by-REF receiver  -  borrows, does not consume; like Rust's
  // `Clone::clone`). Together with hasDrop these model the C++ rule-of-five
  // without ANY implicit compiler-generated special members: a hasDrop type is
  // NEVER implicitly copied (let/assign/pass-by-value is a MOVE); clone() is the
  // ONLY way to copy, and it does not change the move-on-implicit-copy rule.
  bool hasDrop = false;
  bool hasClone = false;

  // --- Single inheritance + vtables ---
  // baseName: "" = no base. A derived struct lays its base's fields out FIRST,
  // then its own. `base` is the resolved StructDef* (set in Sema after all structs
  // are registered). hasVirtuals: this type or any base declares a `virtual fn`,
  // so instances carry a vtable pointer at offset 0. vtableSlots is the ordered
  // list of virtual method slots (base slots first, then this type's own new
  // virtuals); a derived override replaces the base slot in place.
  std::string baseName;
  StructDef* base = nullptr;
  bool hasVirtuals = false;
  std::vector<std::string> vtableSlots;   // method names, one per vtable slot
};
StructDef* registerStruct(const std::string& name);
StructDef* findStruct(const std::string& name);


std::vector<StructDef*> allStructDefs();
int32_t structFieldIndex(const StructDef* d, const std::string& field);


int32_t fieldByteWidth(const BType& t);


int32_t fieldAlign(const BType& t);


struct EnumDef {
  std::string name;
  std::vector<std::string> variants;
};
EnumDef* registerEnum(const std::string& name);
EnumDef* findEnum(const std::string& name);


std::pair<EnumDef*, int64_t> resolveEnumVariant(const std::string& name);

BType makeEnumType(const std::string& name);


struct Expr {
  int line = 0;
  int col = 0;
  virtual ~Expr() = default;
};
using ExprPtr = std::unique_ptr<Expr>;


ExprPtr cloneExpr(const Expr* e);

struct IntLit : Expr { uint64_t v = 0; };
struct FloatLit : Expr { double v = 0; bool isF32 = false; };   // 2.0f marks f32
struct BoolLit : Expr { bool v = false; };
struct StrLit : Expr { std::string v; };
struct CharLit : Expr { uint8_t v = 0; };

struct VarRef : Expr { std::string name; };

struct UnaryExpr : Expr {
  enum class Op { neg, not_, bnot, addr, deref } op;
  ExprPtr base;


  bool methodOverload = false;
  std::string overloadStruct;
  std::string overloadMethod;
  BType overloadRecvType = BType::void_;
  bool recvByRef = false;
};


struct CastExpr : Expr {
  ExprPtr e;
  BType target = BType::i64;
};


struct TernaryExpr : Expr {
  ExprPtr cond;
  ExprPtr thenE;
  ExprPtr elseE;
  BType resultTy = BType::void_;
};


struct SizeofExpr : Expr {
  BType target = BType::i64;
  int32_t size = 0;
};


struct AsmIO {
  bool isOutput = false;
  bool isInOut = false;


  std::string constraint;
  ExprPtr val;
  BType ty = BType::void_;
};
struct AsmExpr : Expr {
  std::string asmText;
  std::vector<AsmIO> ios;
  std::string clobbers;
  bool sideEffect = true;
  bool hasMemory = false;
  BType resultTy = BType::void_;


  std::vector<BType> outputTypes;

  // `asm!(...) implements spec_fn(args)`  -  EXPLICIT verified-asm link clause.
  // When `hasImplements` is true, this asm! block is bound to an `asm spec fn`
  // declaration (`SpecFnDecl` with `isAsmSpec=true`) named by `implementsSpec`,
  // passing the concrete expressions in `implementsArgs` (one per spec param,
  // positional). The SMT emitter then:
  //   a) substitutes the implementsArgs terms for the spec's params and the
  //      asm's result term for `result`, and ASSERTS each of the spec's
  //      `ensures_` clauses as a hypothesis at the asm's point in the WP trace
  //      (the hardware is TRUSTED to satisfy its spec for these arguments), so
  //      downstream contract checks can reason about the asm's observable
  //      behaviour; and
  //   b) discharges each of the spec's `requires_` clauses (with the same
  //      substitution) as a CALLER proof obligation the caller must prove holds
  //      before the asm executes  -  exactly like a normal `requires`.
  // This REQUIRES the old naming-convention trick (`spec fn asm_<fn>` matched by
  // string): the link is now first-class and the SMT encoder needs no name
  // heuristic. Sema verifies `implementsSpec` resolves to an `isAsmSpec` decl,
  // and that the argument and return types line up. IRGen ignores these fields
  //  -  the `implements` clause is spec-only and does not affect codegen.
  bool hasImplements = false;
  std::string implementsSpec;            // the asm spec fn name being linked
  std::vector<ExprPtr> implementsArgs;    // arguments to the spec fn (positional)
};


struct NullLit : Expr {};


struct GenericTypeRef : Expr {
  std::string base;
  std::vector<ExprPtr> typeArgs;
};


struct BinaryExpr : Expr {
  enum class Op {
    add, sub, mul, div, mod,
    eq, ne, lt, gt, le, ge,
    land, lor, band, bor, bxor, shl, shr,
  } op;
  ExprPtr lhs, rhs;


  bool methodOverload = false;
  std::string overloadStruct;
  std::string overloadMethod;
  BType overloadRecvType = BType::void_;
  bool recvByRef = false;


  bool isPtrArith = false;
  BType ptrArithPointee = BType::void_;

  // Set by Sema when both operands are 2-D arrays and op == mul, so IRGen
  // lowers this to @ox_mat_mul instead of a scalar mul instruction.
  bool isMatMul = false;
};

struct AssignTarget : Expr {
  enum class Kind { var, index, field, deref } kind = Kind::var;
  std::string name;
  ExprPtr base;
  ExprPtr index;
  std::string field;
  ExprPtr value;
  BinaryExpr::Op compound = BinaryExpr::Op::add;
  bool isCompound = false;


  bool methodOverload = false;
  std::string overloadStruct;
  std::string overloadMethod;
  BType overloadRecvType = BType::void_;
  bool recvByRef = true;
};


struct IncDecExpr : Expr {
  bool isInc = true;
  bool isPost = false;
  AssignTarget::Kind kind = AssignTarget::Kind::var;
  std::string name;
  ExprPtr base;
  ExprPtr index;
  std::string field;
  BType valueTy = BType::void_;
};

struct Call : Expr {
  std::string callee;
  std::vector<ExprPtr> args;
  bool isPrint = false;


  std::vector<BType> typeArgs;
  bool hasTypeArgs = false;


  bool fnPtr = false;
  ExprPtr calleeExpr;
  // For postfix/indirect calls (fnPtr && calleeExpr), the resolved function
  // value type of the callee. For a bare-fn-pointer callee this is a `fn_`
  // type; for a capturing-closure callee this is a `struct_` __oxclosure_*
  // type. Set by Sema so IRGen need not re-derive it from a VarRef binding.
  BType calleeFnType = BType::void_;
};


struct MethodCall : Expr {
  std::string callee;
  ExprPtr receiver;
  std::vector<ExprPtr> args;
  bool receiverByRef = false;
  BType recvType = BType::void_;
};


struct AssocCall : Expr {
  std::string typeName;
  std::string callee;
  std::vector<ExprPtr> args;
};

// Compile-time macro invocation: `expand name(arg1, arg2, ...)`. Like a Call
// but the `name` resolves to a MacroDecl (not a runtime function). Sema clones
// the macro body (the leading `let` statements + the trailing result
// expression), substitutes the `$param` markers with the caller's args,
// declares the let bindings in a fresh scope, type-checks the result
// expression in that scope, and stashes the EXPANDED tree on this node
// (`expandedStmts` + `expanded`) so IRGen codegens the same expanded tree (no
// re-substitution needed at codegen, but IRGen keeps a fallback look-up +
// substitute path for robustness per the spec). Nested `expand` inside a macro
// body recurses: after substitution the inner MacroCall nodes reach checkExpr
// again. `resultTy` is set by Sema so the MacroCall node carries the expanded
// type for its parent-expression context even though codegen uses `expanded`.
struct MacroCall : Expr {
  std::string macroName;
  std::vector<ExprPtr> args;
  std::vector<StmtPtr> expandedStmts; // set by Sema: substituted leading lets
  ExprPtr expanded;                   // set by Sema: substituted result expr
  BType resultTy = BType::void_;
};


struct Index : Expr {
  ExprPtr base;
  ExprPtr index;


  bool methodOverload = false;
  std::string overloadStruct;
  std::string overloadMethod;
  BType overloadRecvType = BType::void_;
  bool recvByRef = false;
};
struct Field : Expr {
  ExprPtr base;
  std::string field;
};

struct ArrayLit : Expr {
  std::vector<ExprPtr> elems;
};
struct StructLit : Expr {
  std::string name;
  std::vector<std::string> fieldNames;
  std::vector<ExprPtr> values;


  std::vector<BType> typeArgs;
  bool hasTypeArgs = false;
};

struct DynNew : Expr {
  BType elemType = BType::i64;
};
struct MapNew : Expr {
  BType keyType = BType::i64;
  BType valType = BType::i64;
};
struct SetNew : Expr {
  BType elemType = BType::i64;
};
struct HMapNew : Expr {
  BType keyType = BType::i64;
  BType valType = BType::i64;
};
struct HSetNew : Expr {
  BType elemType = BType::i64;
};


// --- Concurrency primitive expressions ---
//
// `spawn { <body> }` or `spawn <expr>` runs the body on a freshly created
// thread. `body` is either the expression directly (an expression-statement
// closure: `spawn doThing()`) or a `{ ... }` block-statement closure. Sema
// infers `resultTy` from the body (void_ for a pure statement block); IRGen
// synthesizes a lowered `void(void*)` wrapper that captures the closure's
// env, calls `@ox_thread_create(fnptr, arg) -> i8*`, and returns the handle.
struct SpawnExpr : Expr {
  ExprPtr body;        // expression (or Block-as-expr) to run on a new thread
  BType   resultTy = BType::void_;
};

// `Channel<T>`  -  construct a new buffered channel carrying `T` values.
// Like DynNew/MapNew/SetNew the element type lives as a BType; the channel
// itself is pointer-width (i8* in LLVM IR).
struct ChannelNew : Expr {
  BType elemType = BType::i64;
};

// `chan <- val`  -  send `val` into the channel `chan`. Sema type is void.
struct ChannelSend : Expr {
  ExprPtr chan;
  ExprPtr val;
};

// `<- chan`  -  receive one value from `chan`. Sema caches `elemType` from
// the channel's BType so IRGen knows what to load after `@ox_chan_recv`.
struct ChannelRecv : Expr {
  ExprPtr chan;
  BType   elemType = BType::i64;
};


// `a..b` (exclusive) or `a..=b` (inclusive) integer range, usable as the
// iterable of `for x in <range>` (and stored in a generic slot when a range
// degenerates to {i64,i64,i1}). Outside a `for`, a range is currently a "carry-
// type only" value: Sema reports its element type (i64) for parsing `for`'s
// elemType, and IRGen iterates it directly. The range itself is not (yet) a
// first-class value you can pass around or index.
struct RangeLit : Expr {
  ExprPtr lo, hi;
  bool inclusive = false;   // true: `a..=b`  (hi is the last value); false: `a..b`.
};


// --- Advanced math operator expressions ---
// These are added by the "advanced math operators" feature: power (**/^/²),
// matrix literals, matrix multiplication, linear solve (A \ b), and numeric
// integration. IRGen lowers each to a call into a runtime C helper; Sema
// gives them a numeric result type (f64 unless every operand is an integer
// literal and the operation is integer-valued, in which case i64); the runtime
// is emitted into the preamble if and only if the program actually uses one of
// the operators (gated by the IRGen.usedPow_/usedMat_/usedMatMul_/usedSolve_/
// usedIntegrate_ flags).
//
// PowerExpr: `base ** exponent`, `base ^ exponent`, or `x²` (postfix square).
// `^` is only the power operator, NOT XOR (XOR keeps the existing bor/bxor
// BinaryExpr path). `²` is a postfix unary square (exponent is the literal 2
// and is not stored). `resultType` is the Sema-assigned result (f64 for a
// floating base or any non-integer exponent, i64 for an integer base with an
// integer exponent ≥ 0). IRGen lowers to @ox_pow_f64 / @ipow / @ox_square.
struct PowerExpr : Expr {
  ExprPtr base;
  ExprPtr exponent;        // nullptr for the postfix `²` (square) form
  BType resultType = BType::f64;
};

// MatrixLit: a literal matrix `[ [a, b], [c, d] ]` laid out as rows of
// element expressions. `elemType` is the inferred element type (f64 unless
// every element is an integer literal). IRGen lays the values out
// contiguously in a heap buffer (via @ox_mat_new) and returns an opaque i8*
// handle carrying { rows, cols } dimensions  -  lowered to a call to the
// matrix-multiply / linear-solve runtimes and to the generic print helper.
struct MatrixLit : Expr {
  std::vector<std::vector<ExprPtr>> rows;
  BType elemType = BType::f64;
};

// MatMulExpr: `A * B` matrix multiplication (the Sema layer only routes a
// `*` to MatMulExpr when BOTH operands are matrix-typed; a scalar `*` keeps
// the ordinary BinaryExpr path). Result is a fresh matrix handle.
struct MatMulExpr : Expr {
  ExprPtr lhs;
  ExprPtr rhs;
  BType elemType = BType::f64;
};

// SolveExpr: `A \ b`  -  solve the linear system A·x = b for x. `lhs` is the
// matrix A, `rhs` is the vector b (a one-column matrix or a vec handle).
// `resultType` is f64 (the solution vector element type). IRGen lowers to
// @ox_mat_solve, which performs a Gauss-Jordan elimination with partial
// pivoting and returns a freshly-allocated solution vector handle.
struct SolveExpr : Expr {
  ExprPtr lhs;
  ExprPtr rhs;
  BType resultType = BType::f64;
};

// IntegrateExpr: numeric definite integration of `body` over [lo, hi] with N
// samples. `body` is the integrand as a function-valued or lambda expression,
// or a bare Oxide function name; if it is nullptr (the parser allows a bare
// `<fn>(x)` form that Sema rewrites) the lowering falls back to a trapezoidal
// rule call with the function pointer. `samples` is the number of evaluation
// points (defaults to 1000). IRGen lowers to @ox_integrate_trapz(f, lo, hi, N).
struct IntegrateExpr : Expr {
  ExprPtr lo, hi;          // integration bounds
  ExprPtr body;            // integrand: a Call to a 1-arg f64->f64 function
  int64_t samples = 1000;  // number of evaluation points
  BType resultType = BType::f64;
};

// --- Formal-verification contract spec expressions ---
// These extend the boolean-expression grammar usable INSIDE a contract clause
// (`requires`/`ensures`/`invariant`/`assert`). They carry no runtime type of
// their own beyond what Sema assigns; IRGen lowers them like ordinary boolean
// expressions (with `old(x)` reading a pre-state snapshot), and the SMT emitter
// walks them to print SMT-LIB.
//
// OldExpr: `old(x)` refers to the value of `x` AT FUNCTION ENTRY. Valid only
// inside an `ensures` clause (Sema rejects it elsewhere). On the IR side, IRGen
// snapshots every `old(x)` mention into a stack slot at function entry and
// loads from it at ensures-check time.
//
// QuantExpr: `forall i: T in lo..hi implies P` / `exists i: T in lo..hi implies P`
// (or `a..=b` inclusive). The binder `i` is in scope only inside `implies`. The
// range operands are integer expressions; the bound `lo..hi` is exclusive,
// `lo..=hi` inclusive  -  same semantics as `for` ranges. Sema scopes the binder
// into an ephemeral env for the `implies` body check; IRGen/SMT traverse the
// quantifier structurally (IR path is best-effort expansion-free: a quantifier
// in a runtime gate is lowered by an explicit bounded loop that checks each
// element; the SMT path emits a real `forall`/`exists`).
struct OldExpr : Expr {
  ExprPtr sub;
};
struct QuantExpr : Expr {
  bool isForall = true;
  std::string binder;        // the quantified variable name
  BType binderType = BType::i64;
  ExprPtr lo, hi;            // range bounds (exclusive unless inclusive)
  bool inclusive = false;    // true: `lo..=hi`; false: `lo..hi`
  ExprPtr body;              // the `implies`/condition expression
};


// --- Advanced math syntax: SUPERSCRIPT and UNICODE MATH SYMBOL nodes ---
//
// PowerExpr (and the shared MatrixLit / MatMulExpr / SolveExpr / the
// trapezoidal IntegrateExpr) are defined earlier in this file (see the
// "Advanced math operator expressions" block just above QuantExpr)  -  they
// were added by a parallel task. Do NOT redeclare them here; only the nodes
// that the parallel task did not cover follow.
//
// The parallel task's PowerExpr handles the `²` postfix square by leaving
// `exponent` null. That covers only the single square code point. The parser
// in THIS task additionally supports the GENERAL superscript run (², ³, ⁻¹,
// mixed digits like ¹²) via SuperscriptExpr, which keeps the raw code points
// and a decoded numeric exponent, so `x³` and `x⁻¹` are first-class.
//
// PowerExpr: right-associative; binds TIGHTER than unary minus, so `-x^2`
// parses as `-(x^2)` (unary minus wraps the PowerExpr, not its base). The
// parallel task's struct fields are reused unchanged:
//   ExprPtr base; ExprPtr exponent; BType resultType = BType::f64;
// (exponent is null only for the `²` postfix-square form.)

// SuperscriptExpr: a postfix Unicode superscript applied to a primary, e.g.
// `x²`, `x³`, `x⁻¹`. The lexer emits the run of superscript code points as a
// single `Tok::math_sym` token whose `text` is the literal superscript string
// (e.g. "²", "³", "⁻¹"); the parser converts that string into the equivalent
// ASCII digit string, evaluates it as a numeric exponent, and stores that
// ExprPtr in `exponent`. For a single `²` the parser may instead build a
// PowerExpr with a null exponent (the parallel task's square fast-path), but
// for `³`, `⁻¹`, or any multi-code-point run this SuperscriptExpr is the
// canonical node so the source form round-trips for tooling (formatter, AST
// dumper). Sema may fold it into a PowerExpr if it prefers one shape.
struct SuperscriptExpr : Expr {
  ExprPtr base;
  std::string text;          // the raw superscript code points ("²", "³", "⁻¹")
  ExprPtr exponent;          // decoded numeric exponent (set by the parser)
  BType resultType = BType::f64;
};

// MathSymExpr: a standalone Unicode math symbol used as a value or operator
// shorthand  -  π (pi), τ (tau), ℯ/e (Euler's number), √ (sqrt prefix), ∞
// (infinity), etc. The lexer emits each such symbol as a `Tok::math_sym`
// token whose `text` is the raw code point(s). The parser builds a
// MathSymExpr for the standalone-symbol case (the postfix-superscript case
// `x²`/`x³` is handled separately above as SuperscriptExpr, or as a
// PowerExpr-with-null-exponent for the `²` fast-path). Sema resolves
// well-known symbols to constant values (π→3.14159…, ℯ→2.71828…, ∞→+inf)
// and routes unary-operator-form symbols (√, ∑, ∫) to the appropriate
// runtime calls. Unknown symbols become a VarRef-of-the-symbol-text so a
// user-defined binding named `π` etc. still works.
struct MathSymExpr : Expr {
  std::string text;          // the raw code point(s), e.g. "π", "√", "∞"
  BType resultType = BType::f64;   // ∞/π/ℯ are f64; Sema may narrow
};


enum class StmtKind {
  exprStmt, letStmt, returnStmt, ifStmt, whileStmt, forStmt,
  breakStmt, continueStmt, block,
  deferStmt,
  assertStmt,   // `assert <expr>`  -  runtime contract checkpoint
  // ox:proof `assume <expr>;` (non-trusted) or `trusted assume <expr>;`  -  a hypothesis
  // assumption. Adds <expr> as an SMT premise (assumed true, NOT discharged as
  // a goal). `trusted assume` is logged in the proof report's trust audit; a
  // bare `assume` is a silent hypothesis. See AssumeStmt below.
  assumeStmt,
  instantiateStmt,  // fixB  -  `instantiate forall k. P on ...;` guided instantiation
  proofStmt,        // fixC  -  `proof that forall k. P by induction on k ...`
  calcStmt,         // calcD  -  `calc { <expr>; <REL> {hints;} <expr>; ... }` block
  syncBlock,   // `sync { ... }`  -  body wrapped by @ox_sync_begin / @ox_sync_end
  // ox:proof Lemma functions  -  `proof { <stmts> }` block form: a proof block whose body
  // is a sequence of statements (typically lemma calls and `assert`s). Unlike
  // the induction-form `proofStmt` (which carries a theorem + base/step), this
  // is a plain block of statements evaluated in the SMT encoder as a sequence
  // of hypotheses: each lemma-call statement adds the callee's `ensures` (args
  // substituted for its params) as a premise; each `assert` adds its condition.
  // The premises accumulate so later statements/discharge queries see them.
  // IRGen skips it (no codegen  -  proof-only). See AST ProofBlockStmt, Parser
  // parseProof (brace-form branch), Driver smtEncodeStmt's proofBlockStmt arm.
  proofBlockStmt,
  // `unsafe { <stmts> }`  -  a block scoped region where unsafe operations
  // (raw-pointer deref, inline asm, extern calls, volatile MMIO, unchecked
  // casts, calls to `unsafe fn`) are permitted without further ceremony.
  // Outside an `unsafe` block / `unsafe fn` body these operations are compile
  // errors (Sema::inUnsafe_ guards them). The block introduces a fresh lexical
  // scope (like `{ }`)  -  the only difference is the Sema `inUnsafe_` flag is set
  // during checking. IRGen treats it as a plain block (genBlock the body).
  unsafeBlock,
};

struct Stmt {
  int line = 0;
  int col = 0;
  StmtKind kind;
  Stmt(StmtKind k) : kind(k) {}
  virtual ~Stmt() = default;
};
// (StmtPtr now declared near the top with the Stmt forward decl  -  see line ~259.)

struct ExprStmt : Stmt { ExprPtr expr; ExprStmt() : Stmt(StmtKind::exprStmt) {} };
struct LetStmt : Stmt {
  bool isMut = false;
  std::string name;
  BType type = BType::i64;
  bool typeAnnotated = false;
  ExprPtr init;
  LetStmt() : Stmt(StmtKind::letStmt) {}
};
// ox:proof T2  -  a `ghost let [mut] x: T = e;` binding. Subclasses LetStmt so existing
// `dynamic_cast<LetStmt*>` sites (IRGen allocas, Sema type-checking, AST clone)
// keep working unchanged; the `isGhost` flag is consulted at one point each in
// Sema (allow body references to other ghost lets) and IRGen (skip the alloca).
// Ghost lets DO NOT codegen  -  their value is only meaningful in the SMT contract
// domain, where the ghost encoder names them so `ensures`/`assert`/`refines`
// clauses can reference them.
struct GhostLetStmt : LetStmt {
  bool isGhost = true;
  GhostLetStmt() {}
};
struct ReturnStmt : Stmt { ExprPtr value; ReturnStmt() : Stmt(StmtKind::returnStmt) {} };
struct IfStmt : Stmt {
  ExprPtr cond;
  std::vector<StmtPtr> then;
  std::vector<StmtPtr> else_;
  IfStmt() : Stmt(StmtKind::ifStmt) {}
};
struct WhileStmt : Stmt {
  ExprPtr cond;
  std::vector<StmtPtr> body;
  // Formal-verification loop invariants: each is a boolean spec expression
  // checked at the loop head (and must hold on exit). Optional; empty in normal
  // code so behaviour is unchanged.
  std::vector<ExprPtr> invariants;
  WhileStmt() : Stmt(StmtKind::whileStmt) {}
};
struct ForStmt : Stmt {
  std::string varName;
  ExprPtr start, end, step;
  bool inclusiveEnd = true;


  bool isForeach = false;
  ExprPtr iter;
  BType elemType = BType::i64;
  // `for k, v in <map>`  -  a second loop variable bound to the iterated VALUE.
  // `varName2` is "" for the single-variable form. `elemType` is the key type;
  // `elemType2` is the value type (only set when iterating a map with a v).
  std::string varName2;
  BType elemType2 = BType::void_;
  bool isMapIter = false;   // Sema sets this when the iterable is a map; IRGen
                           // uses the per-key runtime API to bind k and v.
  // The full iterable type (map/hmap/set/etc.), carried so IRGen's foreach can
  // dispatch a hash map (@ox_hmap_*) vs the ordered map (@ox_map_*). elemType
  // alone is the key type and cannot distinguish map_ from hmap_.
  BType iterType = BType::i64;
  std::vector<StmtPtr> body;
  // Formal-verification loop invariants, checked at the loop head.
  std::vector<ExprPtr> invariants;
  ForStmt() : Stmt(StmtKind::forStmt) {}
};
struct BreakStmt : Stmt { BreakStmt() : Stmt(StmtKind::breakStmt) {} };
struct ContinueStmt : Stmt { ContinueStmt() : Stmt(StmtKind::continueStmt) {} };
struct Block : Stmt {
  std::vector<StmtPtr> stmts;
  // D7  -  `isAtomic` is set by the Parser when this Block was introduced by the
  // `atomic { ... }` form (vs. a plain `{ ... }`). Semantically identical to a
  // plain block (sequential WP, same codegen)  -  the flag only triggers a
  // `; note: atomic block at line N` comment in the SMT witness output so the
  // atomicity claim is recorded for future interleaving proofs. The WP
  // encoding treats the block as a straightline sequence with no intermediate
  // observable state, which is honest for Oxide's single-threaded model.
  bool isAtomic = false;
  Block() : Stmt(StmtKind::block) {}
};

// `sync { <stmts> }`  -  a block whose body executes between @ox_sync_begin
// (enter a barrier) and @ox_sync_end (leave it) so concurrent `spawn`ed
// threads may rendezvous before/from the block. Semantically the inner
// statements are checked in a fresh scope exactly like a plain Block; the
// only difference is IRGen wrapping the generated body with the two runtime
// calls. The body carries no return value.
struct SyncBlock : Stmt {
  std::vector<StmtPtr> body;
  SyncBlock() : Stmt(StmtKind::syncBlock) {}
};
// `unsafe { <stmts> }`  -  a block scope where unsafe operations (raw pointer
// deref, inline asm, extern calls, volatile MMIO, unchecked casts, calls to
// `unsafe fn`) are permitted. Outside an `unsafe` block, these operations
// are compile errors. The block introduces a new lexical scope (like `{ }`)
// and the statements are checked normally  -  the only difference is that the
// Sema `inUnsafe_` flag is set during checking, allowing unsafe operations.
// IRGen treats it as a plain block (genBlock the body); the safety gate is a
// compile-time check, not a runtime check.
struct UnsafeBlock : Stmt {
  std::vector<StmtPtr> body;
  UnsafeBlock() : Stmt(StmtKind::unsafeBlock) {}
};
// `defer <stmt>`  -  run `body` at the end of the enclosing scope, in LIFO order
// with the RAII drops, on every exit path (fall-through, return, break,
// continue). The deferred statement is checked/compiled in the enclosing scope's
// context, so it references locals still live there. Optionally takes the name
// `label` (unused for now; reserved for `defer<label>` selection later).
struct DeferStmt : Stmt {
  StmtPtr body;          // the single deferred statement (often a Block)
  DeferStmt() : Stmt(StmtKind::deferStmt) {}
};

// ox:unsafe `assert <expr>`  -  a runtime contract checkpoint. `<expr>` must be boolean;
// a false value calls `@ox_contract_fail` and traps. Treated by the verifier
// as both a hypothesis (after it, the expression holds) and a goal (the
// expression must be provable from preceding facts/code). Skipped in
// freestanding mode (no runtime trap symbol), where it is SMT-only.
//
// `assert <expr> by { <hints> }`  -  the extended form. `byBody` carries the
// proof hints (assert, instantiate, lemma calls, calc blocks, expr-stmts)
// that are emitted as SMT premises BEFORE the assertion condition is
// discharged. Empty for a plain `assert <expr>;` (backward compatible).
struct AssertStmt : Stmt {
  ExprPtr cond;
  // `assert <expr> by { ... }`  -  optional proof hints block emitted as
  // premises before the assertion condition is discharged. Each hint statement
  // is walked through the standard SMT statement encoder inside a fresh
  // `(push)…(pop)` scope so the premises do not leak beyond this assert.
  // Empty for plain `assert <expr>;`.
  std::vector<StmtPtr> byBody;
  AssertStmt() : Stmt(StmtKind::assertStmt) {}
};

// ox:proof `assume <expr>;`  -  add <expr> as an SMT hypothesis (assumed true, NOT
// discharged as a goal). The condition is a spec expression so `forall`/
// `implies` work inside it. `trusted assume <expr>;` is the same SMT
// semantics but is EXPLICITLY marked as a trusted assumption: it is logged
// in the proof report's trust audit (use `oxide verify --audit-trust` to
// print the full list) and emitted to SMT with a `; note: trusted assume at
// line N` comment so the witness output records the trust boundary. A bare
// `assume <expr>;` (isTrusted=false) is a silent hypothesis  -  same SMT
// premise, no audit entry, no note. The optional `source "<citation>"` clause
// (only meaningful on the trusted form) attaches a human-readable citation
// (e.g. "Intel SDM Vol 3C §24.6") that is printed in the trust audit. The
// `isTrusted` flag and `sourceCitation` mirror AxiomDecl's fields (the same
// `kw_trusted`/`kw_source` tokens introduce both) so the audit can present
// trusted assumes and trusted axioms uniformly. See Parser parseAssume,
// Driver smtEncodeStmt's assumeStmt arm.
struct AssumeStmt : Stmt {
  ExprPtr cond;            // the assumed-true boolean spec expression
  bool isTrusted = false;  // `trusted assume`  -  logged in the trust audit
  // Optional source citation for trusted assumptions (e.g. "Intel SDM Vol 3C
  // §24.6"). Non-empty only when the user writes `trusted assume P source "...";`.
  // Metadata only  -  does NOT change the SMT `(assert <cond>)` semantics.
  std::string sourceCitation;
  AssumeStmt() : Stmt(StmtKind::assumeStmt) {}
};

// ox:proof fixB  -  `instantiate` pragma: guided quantifier instantiation for Z3.
// Two forms, selected by the clause after `on`:
//   (1) Ground: `instantiate forall k. P on k = i;`
//       Emit (assert (=> (in-range i) P[i/k]))  -  a ground instance of the
//       quantified formula at k = i, which Z3 uses as a premise for
//       downstream discharge. Useful for loop invariants: tell Z3 "the
//       invariant holds at the loop counter's current value" so the
//       invariant's quantifier doesn't need MBQI to discover it.
//   (2) Pattern: `instantiate forall k. P on ept, idx;`
//       Emit the quantifier with a :pattern ((select ept idx))  -  overrides
//       the auto-collected patterns (Fix 4) with the user-specified trigger.
//       Z3 instantiates the quantifier whenever it sees (select ept idx) in
//       a goal, even if `idx` is not the bound variable.
// The quantifier (q) is a QuantExpr (forall/exists, binder, lo, hi, body).
// For ground form, witness is the substitution expression (e.g. `i`).
// For pattern form, patternTerms is the list of array/index terms whose
// (select arr idx) becomes the :pattern.
struct InstantiateStmt : Stmt {
  std::unique_ptr<QuantExpr> q;      // the quantifier to instantiate
  // Form 1: ground  -  `on k = <expr>`; witness is the expr to substitute.
  bool isGround = false;
  ExprPtr witness;                   // ground form: substitute k = witness
  // Form 2: pattern  -  `on <term1>, <term2>, ...`; override :pattern.
  std::vector<ExprPtr> patternTerms; // pattern form: arrays/indices for :pattern
  InstantiateStmt() : Stmt(StmtKind::instantiateStmt) {}
};

// ox:proof fixC  -  `proof that forall k. P by induction on k: base: ... step: assume ... prove ...`
// Interactive proof mode: the user writes the induction skeleton; the compiler
// emits base case and step case as GROUND (quantifier-free) discharge queries.
// Both are decidable for Z3 (no quantifiers over arrays)  -  Z3 never returns
// 'unknown' on ground goals. The proof block proves P holds for all k in range.
struct ProofStmt : Stmt {
  // ox:unsafe The theorem to prove: a forall quantifier (must be forall, not exists).
  std::unique_ptr<QuantExpr> theorem;
  // ox:proof The induction variable (should match theorem->binder). Used for clarity;
  // the encoder uses theorem->binder for substitution.
  std::string inductionVar;
  // Base case: the ground instance at k = lo (the range's lower bound).
  // This is a boolean expression (already ground  -  references concrete values
  // or params, not the bound variable). E.g. for P(k) = ept[k]==gpa, base is
  // ept[0]==gpa (if lo=0).
  ExprPtr baseCase;
  // ox:proof Step case: the induction hypothesis (assume P(k)) and the goal (prove P(k+1)).
  // Both are boolean expressions. The IH references the bound variable k as a
  // FREE variable (SMT will assume it). The goal references k+1 (or any
  // successor expression the user writes). The encoder substitutes k with a
  // fresh SMT const for the IH, and k with k+1 for the goal.
  ExprPtr ih;        // 'assume P(k)'  -  the induction hypothesis
  ExprPtr goal;      // 'prove P(k+1)'  -  the step goal
  ProofStmt() : Stmt(StmtKind::proofStmt) {}
};

// ox:proof `proof { <stmts> }`  -  the block-form proof block for lemma functions. Unlike
// the induction-form `ProofStmt` (theorem + base/step), this carries a plain
// statement list (parsed by parseBlock), typically a sequence of lemma calls
// `add_comm(x, y);` and `assert <expr>;`. The Parser produces this when `proof`
// is immediately followed by `{`; the induction form (no `{`) still produces
// `ProofStmt`. The SMT encoder (Driver.cpp smtEncodeStmt's proofBlockStmt arm)
// walks `body` and, for each Call expression-statement whose callee resolves to
// a registered lemma (FuncDecl::isLemma), assumes the lemma's `ensures` with the
// call's actual args substituted for the lemma's formal params  -  adding the
// lemma's conclusion as a premise for subsequent facts/discharge. `assert`
// statements inside the block both discharge (the assert must hold from prior
// premises) AND add their condition as a hypothesis for later facts. IRGen skips
// it (no codegen  -  proof-only, exactly like the induction-form ProofStmt).
struct ProofBlockStmt : Stmt {
  std::vector<StmtPtr> body;
  ProofBlockStmt() : Stmt(StmtKind::proofBlockStmt) {}
};

// calcD  -  `calc { ... }` equational-reasoning block. A sequence of expression
// steps connected by relation operators (`==`, `!=`, `<=`, `>=`, `<`, `>`),
// where each step may carry an optional `{ <proof hints> }` justification
// block. The SMT encoder discharges each consecutive pair (i, i+1) as a
// separate `(check-sat)` on the negated relation, using the hints of step i
// (plus all previously-proven steps) as premises; the chain proves that the
// first expression relates to the last through the composition of relations.
//
// Syntax:
//   calc {
//     x + y;                       // step 0  -  the first expression
//   == { add_comm(x, y); }         // relation == to step 1, with hints
//     y + x;                       // step 1
//   == { add_zero(y); }            // relation == to step 2, with hints
//     y + x + 0;                   // step 2  -  the last expression (no trailing REL)
//   }
//
// Each step is an expression parsed in spec context (so `forall`/`implies` are
// legal inside, mirroring `assert`/`proof`). The hints are ordinary statements
// (lemma-call ExprStmt, `assert`, `instantiate`) parsed via `parseStmt`, so a
// hint block is exactly a brace-delimited statement list  -  keeping parity with
// the block-form `proof { <stmts> }` (ProofBlockStmt) and `assert` body.
//
// `relation` holds the operator that connects THIS step to the NEXT step:
// "==", "!=", "<=", ">=", "<", ">", or "" on the last step (no trailing REL).
// The Encoder reads `steps[i].relation` to discharge the pair (i, i+1):
//   t_i = lower(steps[i].expr);  t_{i+1} = lower(steps[i+1].expr);
//   discharge NOT( t_i  REL_i  t_{i+1} )  under steps[i].hints  and prior steps.
// IRGen skips it (no codegen  -  proof-only, exactly like ProofStmt/ProofBlockStmt).
// See Parser parseCalc, Sema checkStmt's calcStmt arm, Driver smtEncodeStmt's
// calcStmt arm.
struct CalcStep {
  ExprPtr expr;              // the expression at this step (spec-context parse)
  // The relation to the NEXT step: "==", "!=", "<=", ">=", "<", ">". "" for the
  // last step (no relation  -  the final expression terminates the chain).
  std::string relation;
  // Proof hints in the optional `{ ... }` block after the relation. Empty when
  // the user omits the justification block (the step is discharged from prior
  // premises alone). Each hint is a full statement (assert / lemma-call
  // ExprStmt / instantiate) checked by Sema and emitted as a premise by Driver.
  std::vector<StmtPtr> hints;
};
struct CalcStmt : Stmt {
  std::vector<CalcStep> steps;   // one entry per expression line; ≥ 1
  CalcStmt() : Stmt(StmtKind::calcStmt) {}
};

// A parameter may carry a default-initialiser expression (C++-style default
// argument): `fn f(x: i32, y: i32 = 0)`. `hasDefault` says the slot was given a
// default; `defaultExpr` is the parsed initialiser. Sema requires defaults to be
// TRAILING (a defaulted param may not be followed by a non-defaulted one).
struct Param {
  std::string name;
  BType type;
  bool hasDefault = false;
  ExprPtr defaultExpr;   // nullptr unless hasDefault
};

struct LambdaCapture { std::string name; bool byRef = false; };

struct LambdaLit : Expr {
  std::vector<Param> params;
  std::vector<LambdaCapture> captures;   // optional `[a, &b]` capture list
  BType retType = BType::void_;
  std::vector<StmtPtr> body;
  std::string loweredName;
  BType fnType = BType::void_;
  // Per-capture data filled in by Sema: the type of the captured local as it
  // will appear AS A LOWERED-FN PARAMETER (by-ref captures become *T).
  // `captureOuterType[i]` is the type as seen in the enclosing scope (T).
  std::vector<BType> captureTypes;
  std::vector<BType> captureOuterTypes;
  std::string closureStructName;   // "" for a non-capturing lambda (plain fn value)
  bool isOuterMut = false;          // for by-ref capture of a mutable local
};

struct FuncDecl {
  std::string name;
  std::vector<Param> params;
  BType retType = BType::void_;
  std::vector<StmtPtr> body;
  int line = 0;
  bool isExtern = false;
  // `export fn name(...) -> Ret { body }`  -  this Oxide fn is callable from C
  // (reverse of isExtern). IRGen emits `define dso_local` for export fns so C
  // can link against them; no `declare` stub is emitted (they have a body).
  bool isExport = false;
  // `unsafe fn name(...) { body }`  -  a function whose body is implicitly an
  // unsafe scope (like Rust's `unsafe fn`). All unsafe operations inside are
  // allowed without a nested `unsafe { }` block. Callers must wrap the call
  // in an `unsafe` block (or be themselves `unsafe fn`)  -  Sema enforces this
  // at the Call site via the FuncSig's `isUnsafe` flag (set from this field
  // during registration). The body is codegen'd normally; the unsafe check is
  // a compile-time safety gate, not a runtime check.
  bool isUnsafe = false;


  std::vector<std::string> typeParams;
  bool isGeneric = false;

  // Richer type-param list carrying concept constraints + default type args.
  // `typeParams` (above) stays the cheap "names only" projection so existing
  // count/name code is untouched; `tparams` is the source of truth when Sema
  // needs to validate constraints or fill defaults.
  std::vector<TypeParam> tparams;

  std::string implStruct;
  bool hasSelf = false;
  bool selfByRef = false;

  // --- Single inheritance + vtables ---
  // isVirtual: declared `virtual fn foo(...)`  -  registers a new vtable slot in
  //   the owning struct (and marks the struct `hasVirtuals`). isOverride:
  //   declared `override fn foo(...)`  -  must match a base virtual's signature,
  //   replaces that slot in place (no new slot). Both imply hasSelf.
  bool isVirtual = false;
  bool isOverride = false;

  // --- Formal-verification contracts ---
  // `requires_`: boolean spec expressions that must hold on entry (checked at
  //   the function's entry block after param allocas). `ensures_`: boolean spec
  //   expressions that must hold on every exit (checked in a per-function exit
  //   block before the final ret; may reference `old(x)` for the entry value).
  //   Both optional; empty in normal code so existing behaviour is unchanged.
  //   Extern/lambda declarations have no body and may not carry contracts.
  std::vector<ExprPtr> requires_;
  std::vector<ExprPtr> ensures_;

  // D6  -  total-correctness `decreases <expr>` clause. A single measure
  // expression that must strictly decrease on every recursive call, giving a
  // SMT-discharged termination check at each recursive call site (see the
  // recursion guard arm of smtConcreteCallResult in Driver.cpp). Optional;
  // `nullptr`/empty for normal code (no termination check, partial
  // correctness only) so existing behaviour is unchanged. Currently a function
  // may carry at most one `decreases` clause (last one wins on redundant use).
  ExprPtr decreases;

  // ox:proof --- T2  -  ghost state / ghost code ---
  // `isGhost`: declared `ghost fn ...`  -  a spec-only helper callable only from
  //   a contract/spec context, never from runtime code. Doesn't codegen. Sema
  //   flags calling it from a non-spec frame; the ghost encoder treats its
  //   ensures/requires like any other for discharge reference.
  bool isGhost = false;

  // ox:proof Lemma functions  -  `lemma fn name(params) requires R ensures E { body }`.
  // A proof-only top-level declaration (like a `ghost fn` but with the `lemma`
  // keyword prefix and the additional SMT semantics below). A lemma is callable
  // ONLY from `proof { ... }` blocks and from other lemmas/spec contexts  -  the
  // SAME call-site restriction as `isGhost` (Sema rejects a runtime call). It
  // does NOT codegen (IRGen skips it, exactly like a ghost fn). The Ghost
  // encoder emits its contract as a universally quantified axiom
  //   `(assert (forall (params) (=> R E)))`
  // so the lemma's conclusion is available to every discharge query, AND
  // discharges the lemma's own body (the proof) as a separate goal
  //   `NOT (forall params. R ==> body ==> E)` negated and (check-sat)'d
  //    -  `unsat` => the lemma proves its own ensures from its requires + body.
  // A lemma CALL inside a `proof { ... }` block adds the callee's `ensures`
  // (with the call's actual args substituted for the lemma's formal params) as
  // a hypothesis (Driver.cpp's smtEncodeStmt proofBlockStmt arm). Set by
  // Parser::parseLemma; consumed by Sema (registration + body type-check), the
  // Ghost encoder (emitLemmas), and Driver (body discharge). Stored on
  // `Program::lemmas` (kept SEPARATE from `funcs`, like `trapHandlers`, so the
  // Sema/IRGen/SMT pipelines can apply the lemma-specific semantics cleanly).
  bool isLemma = false;

  // --- T3  -  `modifies` frame clauses ---
  // `modifies`: a list of array/ghost names this function may mutate, used by
  //   the ghost encoder to emit frame axioms: every array NOT in the (region-
  //   expanded) set equals its old() value. A name may be a bare array or a
  //   `region` name (expanded to members at emit time). Empty => no frame claim
  //   (the function is treated as a potential mutator of everything, the
  //   conservative default). The Tier-A encoder consumes the same field.
  std::vector<std::string> modifies;
  // Effect system  -  `effects { eff1, eff2, ... }` on a fn head. Lists the
  // named side-effects the function may perform (io, alloc, asm, mmio,
  // vmcs_read, vmcs_write, mem_write, panic, trap, sched, or user-defined).
  // Empty list or omitted = pure (no effects). A function that calls another
  // function with effect E must itself declare E in its effects (effect
  // propagation, checked by Sema's call-site analysis in checkExpr). The
  // verifier (Ghost.cpp emitRegionsAndModifies) uses effects to issue a
  // stronger frame condition: a pure function (empty effects) gets a blanket
  // "all memory unchanged" frame axiom over every mutable global, beyond what
  // the named-only `modifies` list provides. Effects that touch hardware
  // state (`mmio`, `vmcs_read`, `vmcs_write`) suppress the blanket frame  - 
  // those effects legitimately mutate external/hardware state, so claiming
  // memory unchanged would be unsound. Spec fns (SpecFnDecl) are pure by
  // definition and carry no effects field. `effectsExplicit`: set true ONLY
  // when the source literally wrote an `effects` clause (even `effects { }`).
  // It distinguishes "omitted" (effectsExplicit false  -  UNTRACKED: the
  // propagation/purity checks are NOT enforced, preserving the behaviour of
  // pre-effect-system programs) from "explicitly pure" (`effects { }`  - 
  // purity IS enforced). Sema uses this flag as the gate for effect checks;
  // the Ghost encoder uses it together with `effects.empty()` to decide the
  // blanket frame axiom (only emitted for an EXPLICITLY pure fn, never for an
  // untracked one).
  bool effectsExplicit = false;
  std::vector<std::string> effects;
  // ox:unsafe fix2  -  `trap handler` top-level declarations reuse FuncDecl verbatim
  // (params/requires/ensures/body/retType all flow through the SAME Sema +
  // SMT-encoder + (someday) IRGen paths as an ordinary fn). `isTrapHandler`
  // is stamped true by Parser::parseTrapHandler for a `trap [handler] name(...)`
  // decl so the SMT encoder's `emitFnContracts` treats the `requires` clauses
  // as ASSUMED premises (the hardware guarantees them on a VM exit / interrupt)
  // rather than as proof obligations the caller must discharge  -  the one
  // semantic difference between a trap handler and a regular fn. Ensures /
  // invariant / assert discharge identically. A `trap name(...);` prototype
  // (no body, no `handler`) is also a FuncDecl with isTrapHandler=true and an
  // empty body  -  Sema registers its signature so callers can cite it, but
  // emitFnContracts skips it (no body => nothing to verify).
  bool isTrapHandler = false;
  // ox:unsafe Part 2  -  `trap handler name(...) discharge requires ... ensures ... { }`.
  // When `discharge` is present in the source (Parser::parseTrapHandler sets
  // this true), the SMT encoder's trap-handler iteration in `emitFnContracts`
  // SWITCHES the `requires` clauses from assumed (the default trap-handler
  // mode) to DISCHARGED: each `requires` is negated and (check-sat) as a
  // proof obligation that follows from the VM-exit context axioms
  // (`SmtCtx::memModelAxioms`  -  TSO/cache coherence/TLB  -  plus any user
  // `axiom NAME: ...` declarations about the VMCS/exit state). `ensures` /
  // `invariant` / `assert` discharge identically either way. `discharge` is
  // legal ONLY on a `trap [handler] ...` decl; on an ordinary `fn` it is a
  // parse error (the parser checks `dischargeRequires` is set ONLY when
  // `isTrapHandler` is also true  -  see `parseTrapHandler`).
  bool dischargeRequires = false;
};


struct VarDecl {
  std::string name;
  BType type = BType::i64;
  bool typeAnnotated = false;
  bool isConst = false;
  bool isExtern = false;
  bool isMut = false;
  ExprPtr init;
  int line = 0;
};


// Feature 4  -  Range types. A `type X = BaseTy where <bool-expr>;` declaration
// carries an OPTIONAL boolean constraint expression over a self-reference (the
// type name `X` appears in `where` and refers to the param's value at runtime).
// `rangeExpr` is nullptr for an ordinary `type X = BaseTy;` alias (the existing
// поведение); when present, Sema registers the type as a range type and the
// SMT encoder auto-emits the constraint as a per-param premise.
struct TypedefDecl {
  std::string name;
  BType target = BType::i64;
  ExprPtr rangeExpr;   // nullptr unless `where <expr>` was present
  int line = 0;
};

struct StructDecl {
  std::string name;


  std::vector<std::tuple<std::string, BType, bool>> fields;
  int line = 0;


  std::vector<std::string> typeParams;
  bool isGeneric = false;
  bool isOpaque = false;   // `extern struct Name;`  -  opaque C-handle tag type

  // Richer param list (constraint + default), parallel to FuncDecl::tparams.
  std::vector<TypeParam> tparams;

  // Single inheritance: `struct Derived: Base { ... }`  -  baseName is "" when
  // there is no base. Resolved into StructDef::base (a StructDef*) in Sema after
  // all structs are registered (so forward/base references work).
  std::string baseName;
};

struct EnumDecl {
  std::string name;
  std::vector<std::string> variants;
  int line = 0;
};

// A parsed `concept` declaration. See ConceptDef (the runtime table entry) for
// the semantic model. `selfParam` is the lone type param of `concept C<T>`.
// Each `req` mirrors a `fn ...` signature line in the concept body.
struct ConceptReqDecl {
  std::string name;
  bool hasSelf = false;
  bool selfByRef = false;
  std::vector<std::pair<std::string, BType>> params;   // (name, type), NOT counting receiver
  BType retType = BType::void_;
};
struct ConceptDecl {
  std::string name;
  std::string selfParam;
  std::vector<ConceptReqDecl> reqs;
  int line = 0;
};

struct ImplDecl {
  std::string structName;
  std::vector<std::unique_ptr<FuncDecl>> methods;
  int line = 0;
};

// Verified bitfield DSL  -  `bitfield Name: BaseType { field: bit N; field:
// bits A..B; ... }` top-level declaration.
//
// A bitfield is a thin wrapper over a single scalar base type (e.g. u64)
// that automatically generates accessor methods (self.field() -> u64) and
// setting methods (self.with_field(v) -> Self) with verified contracts, plus
// SMT axioms modeling the bitfield layout. Single-bit fields use `bit N`
// (bit position N, zero-indexed); multi-bit fields use `bits A..B` (an
// inclusive range, zero-indexed). The generated accessors carry `ensures`
// contracts stating `result == ((self.raw >> lo) & mask)`, and setters carry
// `ensures` for field preservation (`result.field() == v` and
// `result.other_field() == old(self.other_field())`), enabling verified
// bitfield manipulation discharged by the SMT solver.
//
// PARSE-TIME LOWERING: parseBitfield does NOT hand a BitfieldDecl downstream
// for Sema/IRGen to interpret. Instead it (a) records the decl in
// Program::bitfields (consumed only by the Ghost encoder for SMT layout
// axioms  -  see emitBitfieldAxioms in Ghost.cpp), and (b) EMITS a synthetic
// StructDecl { raw: baseType } + ImplDecl with the accessor/setter methods
// into Program::structs / Program::impls. Those synthetic decls then share
// the WHOLE existing Sema/IRGen/Ghost pipeline for structs+impls unchanged:
// method dispatch resolves via resolveMethod(structName, ...), bodies type-
// check via `self.raw` Field access, and contracts discharge via
// emitFnContracts  -  exactly like hand-written structs+impls. This keeps the
// bitfield feature a thin frontend lowering with zero special-casing in the
// type checker or code generator.
struct BitfieldField {
  std::string name;          // the field's source name (e.g. "memtype")
  bool isSingleBit = true;   // true: `bit N`; false: `bits A..B`
  int lo = 0;                // bit position (single) or low bit (multi)
  int hi = 0;                // high bit (multi); equals lo for single-bit
};
struct BitfieldDecl {
  std::string name;          // e.g. "EPTEntry"
  BType baseType = BType::u64;  // the underlying integer type (u64, u32, etc.)
  std::vector<BitfieldField> fields;
  int line = 0;
};

// --- T1/T3 new top-level declarations ---
// All three live at module scope alongside `fn` / `struct` / `let` and are
// consumed by the Ghost encoder (src/Ghost.cpp). They introduce NO runtime
// state and NO codegen; they exist solely to let the user STATE an abstraction
// relation, a ghost invariant, and a named-region frame so the SMT contract
// encoder can discharge over the small abstract state.

// T3  -  `region Name = { var1, var2, ghost_let1 };`
// A named union of runtime array/global names plus ghost-let names. Used as a
// short-hand in a `modifies` clause: the Ghost encoder expands a region to its
// members when emitting frame axioms. `members` are bare source-level names
// (no types here  -  a region is just a named grouping for frame reasoning, the
// type info comes from the referenced runtime/ghost decls themselves).
struct RegionDecl {
  std::string name;
  std::vector<std::string> members;
  int line = 0;
};

// ox:proof T1  -  `spec fn name(params) -> T = <expr>`
// An abstract spec function: a single expression return, no body block,
// callable from any contract context (requires/ensures/invariant/assert/
// refines). Lowered to SMT as `(define-fun name (params) Sort <expr-body>)` if
// the body is pure arithmetic/boolean over params, OR `(declare-fun name ...)`
// as an uninterpreted function otherwise (honest  -  a body with calls/array
// accesses is best left uninterpreted with a `; note:` line). `name`, `params`
// and `retType` mirror FuncDecl; `body` is the single return expression. There
// is no `requires`/`ensures` on a spec fn  -  the body IS the spec.
//
// `asm spec name(params) -> T requires ... ensures ...;`  -  a hardware-
// instruction ASM SPECIFICATION. Like a regular spec fn (still in
// `Program::specFns`, still no runtime body, still callable from contract
// contexts) but marked `isAsmSpec=true` so it models a hardware instruction
// rather than an abstract Oxide function. Unlike a plain spec fn, an asm spec
// HAS `requires`/`ensures` clauses (its `body` is optional/unused in the asm
// path)  -  the spec's `ensures` describes the architectural effect of the
// instruction in SMT, and the spec's `requires` describes the precondition the
// hardware promises (e.g. `valid_msr(msr)`). An `asm!(...) implements <name>`
// clause on a concrete asm block links it to this decl: the SMT encoder
// substitutes the assembler args for the spec's params and (a) asserts each
// `ensures_` as a hypothesis (the hardware is TRUSTED to satisfy its spec),
// and (b) discharges each `requires_` as a caller proof obligation. The
// Ghost encoder emits the spec as `(declare-fun name (params) Sort)` plus a
// universal axiom `(assert (forall params. requires ==> ensures))` so the
// instruction's behaviour is available to EVERY discharge query, not just the
// implementing block's WP. Stored alongside plain spec fns in
// `Program::specFns` (separate collection would only complicate `collectSpecFns`
// and the existing `c.specFns` map; `isAsmSpec` is the disambiguator).
struct SpecFnDecl {
  std::string name;
  std::vector<Param> params;
  BType retType = BType::bool_;
  ExprPtr body;          // single expression return (NOT a body block); empty/unused for an asm spec
  int line = 0;
  // ox:proof `asm spec fn ...`  -  marks this decl as a hardware-instruction specification
  // (see top-of-struct comment). False on a plain `spec fn`; true only on
  // `asm spec`. Set by Parser::parseAsmSpecFn (the `asm` prefix dispatch arm),
  // consumed by Sema (AsmExpr `implements` link check) and the Ghost/Driver
  // emitters. When true, `requires_`/`ensures_` carry the contract and `body`
  // is ignored (an asm spec's behaviour is captured by the clauses, not a body
  // expression).
  bool isAsmSpec = false;
  // ox:proof Asm-spec-only formal-verification contracts (empty on a plain spec fn).
  // `requires_`: the precondition the hardware instruction promises (e.g.
  //   `valid_msr(msr)`). Discharged as a CALLER proof obligation at each
  //   `asm!(...) implements <name>` link (the caller must prove the asm's
  //   actual args satisfy the precondition before the asm executes).
  // `ensures_`: the architectural effect  -  asserted as a HYPOTHESIS at the
  //   asm's point in the WP trace (the hardware is TRUSTED to satisfy its spec
  //   for the implementing block's args). May reference `result` (bound to the
  //   asm's result SMT term) and the spec's params (bound to the implementing
  //   block's implementsArgs terms). Both empty => the asm spec adds only the
  //   uninterpreted-function symbol (no contract linkage)  -  degenerate but
  //   sound.
  std::vector<ExprPtr> requires_;
  std::vector<ExprPtr> ensures_;
};

// T1  -  `refines <concrete_fn> <= <spec_fn>`
// Module-level declaration stating that the concrete function's behaviour
// implies the abstract spec's (i.e. for all argument tuples matching the
// signatures: ensures_concrete(args) ==> ensures_spec(args), under
// requires_spec(args) ==> requires_concrete(args)). The Ghost encoder emits a
// discharge query per refines. If either name is unresolved or the signatures
// don't match, it emits nothing but a `; note: refines <a> <= <b> skipped`
// honesty line  -  NEVER a false `unsat`. The operator token is `<=` (Tok::lteq).
struct RefinesDecl {
  std::string concreteName;
  std::string specName;
  int line = 0;
};

// Missing-#6  -  `preserves <handler_fn> <= <invariant_spec_fn>`
// Module-level declaration stating that the handler preserves the named
// invariant across its execution. The Ghost encoder emits ONE discharge query
// per `preserves`:
//   forall args.  requires_handler(args)  ==>  I(args_post)
// where `args_post` is the handler's params with `result` rebound to the #2
// WP mini-walker's inlined body terminal term  -  i.e. the value the body
// actually computes, NOT the handler's assumed ensures. `unsat` ⇒ for every
// input satisfying the handler's preconditions, the handler's computed result
// makes the invariant hold; per-handler preservation composes into "the
// machine preserves I" via induction over call sequences (each `preserves`
// decl IS the induction step for its handler, so no separate induction
// emission is needed  -  discharging every handler independently is the proof).
//
// The invariant `I` is a Bool-typed `spec fn` whose params align positionally
// with the handler's params (same convention as `refines`). The result term
// binds into the invariant under the handler's nameMap, so `I` can reference
// either the handler's param names (for unchanged state) or, when a `spec fn`
// takes a parameter of the handler's return type, the spec's name for that
// slot  -  which is bound to the inlined result term by `emitPreserves`. If no
// spec param type-matches the handler's return slot, the result term is
// additionally bound to the name `result` in the spec's nameMap, so an
// invariant written as `(... && result == ...) -> bool` resolves cleanly.
//
// Skip behaviour mirrors `refines`: unresolved handler or invariant, arity
// mismatch, or a non-Bool invariant all emit a `; note: preserves ... skipped`
// honesty line and skip  -  NEVER a false `unsat`. The operator token is `<=`
// (Tok::lteq), the same as `refines` (the directional metaphor is identical:
// the concrete handler's behaviour <= the abstract invariant's preservation).
struct PreservesDecl {
  std::string concreteName;   // the handler fn (e.g. `handle_dispatch`)
  std::string specName;        // the Bool spec fn (the invariant, e.g. `ept_ok`)
  int line = 0;
};

// ox:proof D8  -  `noninterference <h1>, <h2>, ... <= <invariant_spec_fn> ;`
// Module-level declaration stating that the named handlers are pairwise
// interference-free w.r.t. the shared invariant: for every atomic step of
// handler hA, the invariant still holds in the post-state for handler hB
// (Owicki-Gries non-interference / stability). The Ghost encoder emits one
// discharge query per ORDERED pair (hA, hB) with hA ≠ hB:
//   forall shared_state.  req_hA(args_hA) ∧ I(shared_pre, args_hB)
//       ∧ shared_post == step_hA(shared_pre, args_hA)
//       ==> I(shared_post, args_hB)
// negated and `(check-sat)`'d  -  `unsat` ⇒ hA's step doesn't falsify the
// invariant that hB relies on.
//
// This is the cross-VCPU stability obligation that `preserves` (Missing-#6)
// does NOT cover: `preserves` proves each handler INDIVIDUALLY preserves I
// (sequential correctness), while `noninterference` proves that one handler's
// step doesn't break the invariant for another handler (concurrent stability).
// Together they form the Owicki-Gries proof: sequential correctness (preserves)
// + non-interference (noninterference) ⇒ the full system preserves I under
// every interleaving of atomic steps.
//
// The invariant must be a Bool spec fn. `handlers` is the comma-separated list
// of handler function names; the encoder cross-checks every pair. Skip honesty
// mirrors `preserves`/`refines`: unresolved handler or invariant, arity
// mismatch, or a non-Bool invariant all emit a `; note:` and skip.
struct NoninterferenceDecl {
  std::vector<std::string> handlers;   // the handler fn names (cross-product)
  std::string specName;                 // the Bool spec fn (the invariant)
  int line = 0;
};

// D9 (gap #6)  -  `cycle_preserves <handler>, <handler>, ... <= <invariant_spec_fn> ;`
// Module-level declaration discharging the VM-exit-cycle refinement
// obligation: the guest resumes via vmlaunch/vmresume, and the pre-launch
// state refines to the post-resume state under the cycle invariant. This is
// the cross-cycle layer that is NOT just `preserves` (per-handler sequential
// correctness) or `noninterference` (cross-handler stability): it covers the
// FULL trap cycle (vmlaunch → guest runs → VM exit → handler runs → vmresume
// → guest runs → …) as a single preservation step per handler.
//
// For EACH handler in the decl's handler list, the Ghost encoder emits one
// discharge query:
//   NOT (forall (cycle_args).  req_handler(cycle_args) ∧ I_pre(cycle_args)
//                             ==> I_post(cycle_args_post_handler))
// negated and `(check-sat)`'d  -  `unsat` ⇒ for every pre-launch state matching
// the handler's preconditions, the handler's inlined body yields a post-state
// in which the cycle invariant still holds at the NEXT VM exit.
//
// The KEY DIFFERENCE from `preserves`: `cycle_preserves` EXPLICITLY models the
// vmlaunch/vmresume transition as IDENTITY on the cycle invariant  -  VM
// entry/exit/resume do NOT touch the INVARIANT's state (only the handler
// does). So `shared_pre` (the pre-vmlaunch guest state) and `shared_post`
// (the post-vmresume state) are the SAME SMT symbol for the invariant's
// UNCHANGED slots, and only the handler-mutated slots flow through the
// mini-walker's post-state. The trust boundary sits at vmresume/vmlaunch:
//   - handler-body preservation is discharged by `preserves` (per-handler);
//   - cross-handler pair overlap is discharged by `noninterference`;
//   - `cycle_preserves` ties them together across the trap cycle as the
//     Owicki-Gries obligation #1+successor w.r.t. the cycle (each handler's
//     step is the induction step; the cycle invariant is the induction
//     hypothesis that must be re-established after each handler run before
//     the next vmresume).
//
// `handlers` is the comma-separated list of handler fn names (mirrors
// `noninterference`); `specName` is the Bool spec fn (the cycle invariant).
// Skip honesty mirrors `preserves`/`noninterference`/`refines`: unresolved
// handler or invariant, arity mismatch, non-Bool invariant, or an empty
// handler list all emit a `; note:` and skip  -  NEVER a false `unsat`.
struct CyclePreservesDecl {
  std::vector<std::string> handlers;   // the handler fn names (per-handler)
  std::string specName;                 // the Bool spec fn (the cycle invariant)
  int line = 0;
};

// ox:proof D3  -  `axiom <expr>;` (optionally `axiom name: <expr>;`)
// A module-level top-level SMT axiom. The body is a spec expression (inSpec
// set during its parse) so `forall`/`exists`/`implies` are legal  -  e.g.
//   axiom forall gpa. is_ram(gpa) implies gpa >= 0;
// lowers to `(assert (forall ((gpa Int)) (=> (is_ram gpa) (>= gpa 0))))` at the
// TOP of the ghost section, available to every discharge query (refines/
// preserves/contract walker). The optional `name` is purely a documentation
// label surfaced as a `; note: axiom name` line; SMT semantics come entirely
// from `body`. The body resolves spec fn names and const globals the same way
// a spec fn body does (via the same collectSpecFns/collectConstGlobals maps).
//
// Namespace/audit extension: `trusted axiom Namespace::Name: <expr> source
// "...";`. `namespace_` is the prefix (e.g. "IntelSDM"), `name` is the label,
// and `isTrusted` / `sourceCitation` mark the axiom as a trusted fact and its
// documentation source (e.g. "Intel SDM Vol 3C §24.6"). All three are metadata
//  -  the SMT `(assert <body>)` is unchanged; `qualifiedName()` surfaces a fully-
// qualified "Namespace::Name" for the audit report from `--audit-axioms`.
struct AxiomDecl {
  std::string name;        // optional label ("" when omitted)
  // Namespace prefix (e.g. "IntelSDM") for `Namespace::Name` axioms; "" for
  // unnamespaced axioms. See `qualifiedName()`.
  std::string namespace_;
  ExprPtr body;            // the asserted spec expression
  // ox:proof `trusted axiom`  -  marks the axiom as a trusted (non-machine-verified) fact
  // (a documentation-only assertion cited from e.g. the Intel SDM). Surfaced by
  // `oxide verify --audit-axioms`; does NOT change SMT semantics.
  bool isTrusted = false;
  // ox:proof `source "..."`  -  documentation citation string attached to an axiom
  // (e.g. "Intel SDM Vol 3C §24.6"). Printed in the audit report. Metadata only.
  std::string sourceCitation;
  int line = 0;

  // Returns the fully-qualified name: "Namespace::Name" or "Name" if no namespace.
  std::string qualifiedName() const {
    return namespace_.empty() ? name : (namespace_ + "::" + name);
  }
};

// Compile-time macro declaration: `macro name(p1, p2, ...) { <stmts> <expr> }`.
// `paramNames` are the formal substitution names. The body is a (possibly
// empty) sequence of leading `let` statements (`bodyStmts`) followed by a
// trailing result expression (`bodyExpr`)  -  the macro's value. `$param`
// markers are VarRef nodes whose `name` is `"$" + param` (so `macro square(x)
// { ($x) * ($x) }` parses the body as `({...}) * ({...})` where each `{...}`
// is a VarRef named `$x`). A body like `macro max3(a,b,c) { let ab = imax($a,
// $b); imax(ab, $c) }` has one let in `bodyStmts` and `imax(ab, $c)` as the
// result. At an `expand name(args...)` call site, Sema clones the body, swaps
// each `$param` VarRef for the corresponding arg clone, declares the let
// bindings in a fresh scope, type-checks the result in that scope, and stashes
// the expanded tree on the MacroCall so IRGen codegens it  -  purely a
// compile-time transform, never emitted as a runtime decl. See AST MacroCall,
// Parser parseMacro, Sema macroRegistry + expandMacro.
struct MacroDecl {
  std::string name;
  std::vector<std::string> paramNames;
  std::vector<StmtPtr> bodyStmts;   // leading `let` bindings (substituted at expand)
  ExprPtr bodyExpr;                 // trailing result expression (substituted at expand)
  int line = 0;
};

struct Program {
  std::vector<std::unique_ptr<FuncDecl>> funcs;
  std::vector<std::unique_ptr<StructDecl>> structs;
  std::vector<std::unique_ptr<EnumDecl>> enums;
  std::vector<std::unique_ptr<VarDecl>> globals;
  std::vector<std::unique_ptr<TypedefDecl>> typedefs;
  std::vector<std::unique_ptr<ImplDecl>> impls;
  std::vector<std::unique_ptr<ConceptDecl>> concepts;   // C++-level: concept decls
  // ox:proof T1/T3 module-level decls consumed by the Ghost encoder (src/Ghost.cpp).
  // `specFns` mirrors FuncDecl-shaped spec functions (T1); `regions` are the
  // named unions for `modifies` frame expansion (T3); `refines_` are the
  // concrete-implies-spec relations discharged as separate SMT queries (T1);
  // `preserves_` (Missing-#6) are the handler-preserves-invariant relations,
  // also discharged as a separate SMT query per decl.
  std::vector<std::unique_ptr<RegionDecl>> regions;
  std::vector<std::unique_ptr<RefinesDecl>> refines_;
  std::vector<std::unique_ptr<PreservesDecl>> preserves_;
  // ox:proof D8  -  `noninterference <handler_list> <= <invariant>;` per-handler-pair
  // Owicki-Gries non-interference (stability) discharge queries. Separate
  // from `preserves_` (which is per-handler sequential correctness).
  std::vector<std::unique_ptr<NoninterferenceDecl>> noninterference_;
  // D9 (gap #6)  -  `cycle_preserves <handler_list> <= <invariant>;` per-handler
  // VM-exit-cycle refinement discharge queries (vmlaunch/vmresume modelled as
  // identity on the cycle invariant; the handler step re-establishes it).
  // Separate from `preserves_` (per-handler sequential correctness) and from
  // `noninterference_` (cross-handler pair stability)  -  this is the cross-cycle
  // layer that ties them together over the full trap cycle.
  std::vector<std::unique_ptr<CyclePreservesDecl>> cyclePreserves_;
  std::vector<std::unique_ptr<SpecFnDecl>> specFns;
  // ox:proof D3  -  module-level top-level SMT axioms (`axiom <expr>;`). Emitted at the
  // TOP of the ghost section (before spec fns) as `(assert <lowered-body>)` so
  // every discharge query sees them. See `AxiomDecl` above.
  std::vector<std::unique_ptr<AxiomDecl>> axioms;
  // ox:unsafe fix2  -  `trap [handler] name(...) { ... }` top-level declarations. Each
  // entry is a FuncDecl with `isTrapHandler=true` (Form 2, with body + contracts)
  // or a body-less prototype (Form 1, `trap name(...);`  -  an extern-style
  // signature stub). Kept SEPARATE from `funcs` so the SMT encoder can apply the
  // trap-handler semantics (requires ASSUMED, not discharged) on exactly these
  // decls; Sema ALSO registers their signatures into its `funcs` map so a
  // caller (e.g. `main` unit-testing a handler) resolves, and type-checks their
  // bodies alongside `funcs`. IRGen is intentionally NOT touched (trap-handler
  // codegen is a later pass); for now `verify` (SMT-only) is the supported path.
  std::vector<std::unique_ptr<FuncDecl>> trapHandlers;
  // Compile-time macro declarations (`macro name(params) { body }`). Purely
  // compile-time: never emitted to runtime IR. Sema builds a `macroRegistry`
  // (name -> MacroDecl*) at the top of `check` and expands each `expand`
  // MacroCall in-place; IRGen codegens the Sema-stashed expanded tree. See
  // `MacroDecl` / `MacroCall` above.
  std::vector<std::unique_ptr<MacroDecl>> macros;
  // ox:proof Lemma functions  -  `lemma fn name(params) requires R ensures E { body }`.
  // Each entry is a FuncDecl with `isLemma=true` (set by Parser::parseLemma).
  // Proof-only: callable only from `proof { ... }` blocks and other lemma/spec
  // contexts (Sema enforces the call-site restriction, same rule as `ghost
  // fn`). NOT codegen'd (IRGen skips isLemma decls). The Ghost encoder
  // (emitLemmas) emits each lemma's contract as a universal axiom
  //   `(assert (forall (params) (=> R E)))`
  // available to every discharge query, AND discharges the lemma's own body
  // (its proof) as a separate goal. Kept SEPARATE from `funcs` (like
  // `trapHandlers`) so the Sema/IRGen/SMT pipelines apply the lemma semantics
  // cleanly; Sema ALSO registers each lemma's signature into its `funcs` map so
  // a `proof { name(args) }` call resolves. See FuncDecl::isLemma above.
  std::vector<std::unique_ptr<FuncDecl>> lemmas;
  // Verified bitfield DSL  -  `bitfield Name: BaseType { ... }` top-level
  // declarations. Each entry is the parsed BitfieldDecl (name + baseType +
  // fields). Parser::parseBitfield ALSO lowers each decl into a synthetic
  // StructDecl { raw: baseType } + ImplDecl (accessor/setter methods) pushed
  // into `structs`/`impls` so they flow through the existing Sema/IRGen/Ghost
  // pipeline unchanged. This list is consumed ONLY by the Ghost encoder
  // (emitBitfieldAxioms in Ghost.cpp) to emit the SMT bitfield-layout axioms
  // `(assert (forall ((raw Int)) (= (Name_field raw) ...)))` so the bitfield
  // semantics are available to ALL discharge queries, not just a method's own
  // contract. See AST BitfieldDecl/BitfieldField above.
  std::vector<std::unique_ptr<BitfieldDecl>> bitfields;
};
