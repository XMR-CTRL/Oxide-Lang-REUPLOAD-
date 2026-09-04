#pragma once

#include "AST.h"
#include <map>
#include <vector>
#include <string>
#include <set>

struct SemanticError {
  std::string msg;
  int line;
  int col = 0;
  std::string hint;
};

struct VarInfo {
  BType type = BType::void_;
  bool isMut = false;
  bool found = false;
};

struct FuncSig {
  BType retType;
  std::vector<BType> paramTypes;
  bool isExtern = false;
  // `export fn`  -  this Oxide fn is callable from C. Set from FuncDecl::isExport
  // during Sema registration; IRGen reads it to emit `define dso_local`.
  bool isExport = false;
  // Effect system  -  copied from FuncDecl::effects at every Sema registration
  // site (free fns, impl methods, trap handlers, generic instantiations, and
  // spec/ghost fns which default to empty == pure). `isPure` is the derived
  // flag `effects.empty()` (empty list OR omitted clause => pure). Sema's
  // checkExpr reads `curFunc_->isPure`/`curFunc_->effects` (the CALLER's
  // effects) and `it->second.effects` (a CALLEE's effects, looked up via the
  // funcs table) to enforce propagation: any effect E on the callee must
  // appear in the caller's effects, or emit an error. Pure callers may not
  // call any function with effects (a strict special case), use `asm!`
  // (effect `asm`), call extern fns (effect `io`), or call `print` (effect
  // `io`). The Ghost encoder (emitRegionsAndModifies) consumes `isPure` to
  // emit a blanket "all memory unchanged" frame axiom beyond the named-only
  // `modifies` list. Dedup is applied at registration so duplicate effect
  // names collapse to one entry (set membership is what the propagation
  // check queries).
  bool isPure = true;                      // empty effects / omitted = pure
  // True ONLY when the source literally wrote an `effects` clause (even the
  // pure `effects { }`). Sema uses this as the gate for the propagation +
  // purity checks (omitted => untracked => no enforcement, so pre-effect-
  // system programs keep working), and the Ghost encoder uses it to decide
  // whether to emit the blanket "all memory unchanged" frame axiom (only for
  // an EXPLICITLY pure fn).
  bool effectsExplicit = false;
  std::vector<std::string> effects;        // declared effect names
  // ox:proof Lemma functions / ghost fns  -  proof-only helpers callable ONLY from
  // `proof { ... }` blocks and other lemma/spec contexts, NOT from runtime
  // code. `isLemma` is set from FuncDecl::isLemma during Sema registration;
  // `isGhost` is set from FuncDecl::isGhost. Sema rejects a runtime call to a
  // lemma/ghost fn by checking the `inSpecContext_` counter (a proof/spec
  // context marker) at the Call arm of checkExpr. A lemma call inside a
  // `proof { ... }` block (ProofBlockStmt) sets inSpecContext_ so the call is
  // admitted; a lemma's own body type-check also runs under inSpecContext_
  // (a lemma may call other lemmas).
  bool isLemma = false;
  bool isGhost = false;
  // ox:proof `asm spec fn`  -  this slot models a hardware-instruction specification
  // (set from SpecFnDecl::isAsmSpec during the specFns registration loop).
  // Consumed by checkExpr's AsmExpr arm to validate an `asm!(...) implements
  // <name>` link: the named spec MUST resolve to an isAsmSpec slot (a plain
  // `spec fn` or a normal `fn` cannot serve as a hardware-instruction spec  - 
  // only an `asm spec fn` can). The actual requires/ensures discharge is done
  // by the SMT encoder via the `c.specFns` map (Program::specFns carries the
  // SpecFnDecl with its clauses), but this flag lets Sema catch a mis-link
  // (e.g. `implements fib(0)` pointing at a regular fn) at compile time.
  bool isAsmSpec = false;
  // `unsafe fn`  -  set from FuncDecl::isUnsafe during Sema registration (mirrors
  // how `isExtern`/`isExport`/`isLemma`/`isGhost` are stamped). A function
  // whose body is an implicit unsafe scope (its body is checked with Sema's
  // `inUnsafe_` flag raised, so raw pointer deref, inline asm, extern calls,
  // volatile MMIO, unchecked casts, and calls to OTHER `unsafe fn`s are all
  // permitted inside without a nested `unsafe { }` wrapper). Callers in SAFE
  // code must wrap the call in an `unsafe { }` block (or be themselves
  // `unsafe fn`); the Call arm of checkExpr enforces this by rejecting a call
  // to a `isUnsafe` callee when `inUnsafe_` is currently false.
  bool isUnsafe = false;
};


struct MethodInfo {
  BType retType = BType::void_;
  std::vector<BType> paramTypes;
  bool hasSelf = false;
  bool selfByRef = false;
  std::string implStruct;
  std::string mangled;
  // Single inheritance + vtables. isVirtual: this method participates in
  // virtual dispatch (declared `virtual fn` or `override fn`). isOverride: this
  // method reuses a base virtual's vtable slot (declared `override fn`). The
  // actual vtable-slot layout lives on StructDef::vtableSlots. IRGen reads
  // isVirtual to decide direct vs indirect call and vtableSlots for the slot.
  bool isVirtual = false;
  bool isOverride = false;
  // The index of this virtual method's slot in its OWNING struct's vtableSlots
  // (and thus in the per-type vtable global IRGen emits). -1 = not a virtual /
  // no slot. For an `override fn`, this is the SAME index as the base slot it
  // reuses (overrides do NOT add a new slot  -  they replace in place). For a
  // `virtual fn` declaring a NEW slot, this is the appended index.
  int vtableSlot = -1;
};


std::string mangleMethod(const std::string& structName, const std::string& methodName);


struct GlobalInfo {
  BType type = BType::void_;
  bool isConst = false;
  bool isExtern = false;
  bool isMut = false;

  bool hasConstVal = false;
  int64_t iVal = 0;
  double fVal = 0;
  bool bVal = false;
  uint8_t cVal = 0;
  std::string sVal;
};

class Sema {
public:
  bool check(Program& prog);


  bool requireMain = true;


  bool freestanding = false;

  std::vector<SemanticError> errs;
  std::map<std::string, FuncSig> funcs;

  // Default arguments, keyed by the function's call symbol (the plain fn name,
  // or the mangled `structName__methodName` for impl methods / associated fns).
  // `defaultArgs_[sym].types[k]` is the k-th param type; `.exprs[k]` is non-null
  // iff param k has a default initialiser (a clone, type-checked lazily).
  struct DefaultSet { std::vector<BType> types; std::vector<ExprPtr> exprs; };
  std::map<std::string, DefaultSet> defaultArgs_;


  std::map<std::string, GlobalInfo> globals;


  std::map<std::string, std::map<std::string, MethodInfo>> methods;


  // ox:proof T1  -  names of every `spec fn` declared in the program (bare user-written
  // name, NOT mangled). Populated when Sema registers the spec fns in the
  // `funcs` table at the top of the body-check pass. Used by IRGen's
  // genContractGate to OMIT a runtime contract gate whose expression calls a
  // spec fn  -  a spec fn has no runtime body and is purely an SMT abstraction
  // (the real contract discharge is in the SMT path; the runtime gate is
  // best-effort, so a gate that would emit an undefined-symbol reference to a
  // spec fn is skipped). Sound: omitting a runtime gate is a sound UNDER-
  // approximation (the gate re-runs the check that SMT already did).
  std::set<std::string> specFnNames_;
  bool isSpecFnName(const std::string& name) const {
    return specFnNames_.count(name) > 0;
  }

  // Compile-time macro registry: bare macro name -> MacroDecl*. Populated at
  // the top of `check` from `prog.macros`. Public so IRGen (which holds a const
  // Sema&) can consult it as a fallback for a MacroCall whose Sema-stashed
  // `expanded` tree is missing (shouldn't happen, but the IRGen spec mandates a
  // lookup+substitute path). `findMacro` is the lookup helper.
  std::map<std::string, const struct MacroDecl*> macroRegistry;
  const struct MacroDecl* findMacro(const std::string& name) const {
    auto it = macroRegistry.find(name);
    return it == macroRegistry.end() ? nullptr : it->second;
  }


  std::vector<std::unique_ptr<FuncDecl>> monomorphFns;

  // Generic-struct method clones  -  one FuncDecl per (instantiation, method),
  // paired with the MANGLED instantiation struct name it belongs to. Mirrors
  // monomorphFns: the method bodies are substituted with the concrete type
  // args and their bodies are type-checked in the same per-instantiation loop
  // that checks monomorphFns. IRGen emits each via genMethod(structName, *fn).
  // Populated lazily by instantiateGenericStruct as each instantiation is
  // registered (the struct's existing impls are cloned with env substitution).
  struct ClonedMethod {
    std::string structName;   // mangled instantiation struct name, e.g. __oxgs__Box__i64
    std::unique_ptr<FuncDecl> fn;
  };
  std::vector<ClonedMethod> monomorphMethods;

  // Resolve a method/associated-function name walking the base chain. Public so
  // IRGen (which holds a const Sema&) can dispatch inherited methods. The private
  // twin below is the implementation used inside Sema's own checkExpr paths.
  const struct MethodInfo* resolveMethod(const std::string& structName,
                                          const std::string& methodName) const;

  // Public wrapper around the private registerMethod, for the free function
  // instantiateGenericStruct (which has no `this` and reaches Sema via a file-
  // scope pointer). Used by the generic-struct method-cloning path to register
  // a cloned method's signature under its mangled instantiation struct. The
  // caller separately appends the clone to Sema::monomorphMethods.
  void registerMethodPublic(const std::string& structName, FuncDecl& fn);

private:
  struct Entry { BType type; bool isMut; bool isGlobal = false; bool hasDrop = false; };

  // A scope is a name->Entry map PLUS an ordered list of the locally-declared
  // names whose type has a destructor (`hasDrop`). The order is *declaration*
  // order; destructors run in *reverse* declaration order at scope exit
  // (mirrors C++). The same list drives drop emission on every early-exit path
  // (return/break/continue) that leaves this scope  -  see emitScopeUnwind in
  // IRGen. Globals are addressable as @name and never dropped.
  struct Scope {
    std::map<std::string, Entry> vars;
    std::vector<std::string> dropVars;   // declaration order, drop-having locals
  };


  std::string curImpl_;
  std::vector<Scope> scopes_;
  FuncSig* curFunc_ = nullptr;
  int loopDepth_ = 0;
  int inDefer_ = 0;   // >0 while type-checking the body of a `defer`. A defer
                      // runs at scope exit, so it may NOT itself `return`/`break`
                      // /`continue` (those jump out of the scope the defer is
                      // cleaning up  -  undefined, and unsafe to emit).
  bool inEnsures_ = false;   // true while checking an `ensures` clause, so
                              // `old(x)` is permitted (and only there).

  // ox:proof Lemma/ghost proof-context marker. >0 while Sema is type-checking inside a
  // proof/spec-only context  -  specifically: a lemma's own body (a lemma may
  // call other lemmas/spec fns), and the body of a `proof { ... }` block
  // (ProofBlockStmt, where lemma calls live). When >0, the Call arm of
  // checkExpr ADMITS calls to isLemma/isGhost fns; when ==0 (a runtime frame),
  // such calls are rejected as "proof-only helper called from runtime code".
  // This is the call-site restriction the task requires (lemmas follow the
  // SAME rule as ghost fns). Toggled by the lemma body-check loop and the
  // ProofBlockStmt arm of checkStmt (save/restore so nested frames compose).
  int inSpecContext_ = 0;

  // `inUnsafe_`  -  current checking context is inside an `unsafe { ... }` block
  // or an `unsafe fn` body. While true, the unsafe operation checks in
  // checkExpr (raw pointer deref `*p`, inline asm `asm!`, extern fn calls,
  // calls to `unsafe fn`, unchecked pointer/lossy casts) are PERMITTED ;
  // outside (=false) those operations are compile errors. Toggled by the
  // UnsafeBlock arm of checkStmt (save+raise+restore so nested blocks
  // compose) and by the function-body check loop when `FuncDecl::isUnsafe`
  // is true (mirrors how `inSpecContext_` is toggled for lemma bodies).
  bool inUnsafe_ = false;


  // Move-checking: per function, the set of local names that have been moved
  // out of (via a copy/move into another local, an assign, or a value-return).
  // Reading a moved-from var in checkExpr is a compile error (use-after-move),
  // like Rust. Cleared per function (not per scope) so a move in an inner scope
  // still invalidates the outer binding. Re-assignment re-validates the name.
  std::set<std::string> movedVars_;


  int lambdaSeq_ = 0;
  int monoSeq_ = 0;

  VarInfo lookup(const std::string& name);
  void pushScope();
  void popScope();
  // Declare a binding into the CURRENT (back) scope. By default this rejects a
  // same-scope redeclaration of an existing name (soundness: silent overwrite
  // would break drop order, ownership/borrow tracking, ghost-variable meaning,
  // and SMT variable mapping  -  `let x = 1; let x = 2;` in one scope is now an
  // error; nested-scope shadowing remains fine because pushScope gives a fresh
  // vars map). `allowRedecl` opts out of the check: it is set ONLY for the
  // ghost-let pre-declare pass (`predeclareGhostLetsStmt`) and the matching body
  // walker arm, where the pre-declare and the body walk deliberately write the
  // SAME name with the SAME type into the SAME scope-slot (see the long comment
  // at Sema.cpp:3342). `line` is the source line for the redeclaration error
  // (0 when the caller has no line, e.g. synthetic binds).
  void declare(const std::string& name, BType t, bool isMut, bool isGlobal = false,
               bool allowRedecl = false, int line = 0);
  // Check a list of boolean contract expressions (already in the right scope).
  // Used for both function `requires`/`ensures` (with inEnsures_ toggled for the
  // ensures pass so `old(x)` is admitted) and loop `invariant` clauses. Reports
  // a Sema error for any clause whose type isn't bool.
  void checkContractExprs(const std::vector<ExprPtr>& exprs, bool ensures);
  // Check a function's requires/ensures in its parameter scope. Called once per
  // (non-extern) function right after its params are declared, before its body.
  void checkFuncContracts(const FuncDecl& fn);

  // --- Effect system ---
  // Effect propagation: a Call to `calleeName` whose FuncSig declares effects
  // E1, E2, ... requires the CALLER (curFunc_) to declare every one of them in
  // its own effects list. Reports a Sema error for the first effect the caller
  // is missing, with a hint naming the callee + the effect. Skipped (no-op)
  // when curFunc_ is null (e.g. during contract-clause or global-init checks
  // that don't run under a function frame), and when the callee's sig has no
  // effects (pure callee  -  nothing to propagate).
  void checkEffectPropagation(int line, int col,
                              const std::string& calleeName,
                              const FuncSig& callee);
  // Purity check for a single named effect performed in the CALLER's body that
  // is NOT an ordinary-user-function call: `asm!` (effect `asm`), `print`
  // (effect `io`), and a call to an `extern fn` (effect `io`  -  an extern has
  // no Oxide-side effects field, so the untyped io effect is implied). If
  // curFunc_ is pure (curFunc_->isPure), emit a Sema error pinpointing the
  // violation with a hint suggesting the offending effect be declared. No-op
  // when curFunc_ is null or not pure. `eff` is the effect name (`asm`,`io`).
  void checkPurityViolation(int line, int col, const char* eff,
                            const std::string& detail);

  // ox:proof T3-1  -  pre-declare every `ghost let` name in a function body BEFORE
  // contract-clause type-checking runs, so an `ensures result == g` clause
  // can name a ghost `g` declared in the body. The normal body walk
  // (`checkBlock`) declares ghost lets as it reaches them, which is too
  // late for contract resolution  -  `checkFuncContracts` runs first. This
  // pre-scanner walks the same nesting shapes (Block/If/While/For/Defer)
  // and `declare`s each ghost let's name + type into the function's param
  // scope, matching what the body walk will do later. Harmless overlap:
  // a later declare of the same name in a nested scope shadows correctly.
  void predeclareGhostLets(const std::vector<StmtPtr>& stmts);
  // ox:why Per-statement dispatcher used by `predeclareGhostLets`; private because
  // it's a Sema-internal walker. (Both member functions so they can call
  // private `declare`.)
  void predeclareGhostLetsStmt(Stmt* s);

  // Record a local of a drop-having type into the current scope's dropVars (so
  // IRGen knows to call drop at scope exit + on early-exit paths). Called from
  // checkStmt's LetStmt handling whenever the let's type's struct has a drop.
  void recordDropLocal(const std::string& name);

  // If `init` is a value (possibly reached through Field/deref) rooted at a
  // local VarRef of a move-only struct type (hasDrop && !hasClone), mark that
  // root local as moved  -  it is now unusable until re-assignment. This is the
  // compile-time move discipline (Rust-style): moving a non-Clone owning type
  // into a new binding consumes it, preventing use-after-move at compile time.
  void noteMovedFrom(Expr* init, const BType& initType);

  // ox:proof Returns true if struct `sn` (or any base, once inheritance exists) has a
  // `drop` method  -  i.e. its instances own resources that must be dropped.
  bool structHasDrop(const std::string& sn) const;
  bool structHasClone(const std::string& sn) const;

  // Single inheritance upcast questions, used by checkExpr's coercion points:
  //   isBaseOf(parentName, childName): childName's base chain passes through
  //     parentName (i.e. a `child` value is usable where a `parent` is expected).
  //     parentName==childName is false (same type is "exact", handled by BType==).
  //   implicitAssignable(from, to): true if a value of type `from` may be used
  //     where type `to` is expected WITHOUT a user-visible cast  -  exact match,
  //     OR a pointer-to-derived upcast to pointer-to-base (single inheritance,
  //     base at offset 0 so it's an IR bitcast, already what genCoerce emits).
  //     By-value struct upcast (Derived -> Base truncation) is deliberately NOT
  //     implicit (use an explicit field slice or a load-through-&Base helper);
  //     only `&Derived -> &Base` / `*Derived -> *Base` are implicit.
  bool isBaseOf(const std::string& parentName, const std::string& childName) const;
  bool implicitAssignable(const BType& from, const BType& to) const;

  BType checkExpr(Expr* e);
  void checkStmt(Stmt* s);
  void checkBlock(const std::vector<StmtPtr>& stmts);

  // Compile-time macro expansion. `expandMacro(mc)` looks up mc->macroName in
  // `macroRegistry`, clones the macro body, substitutes every `$param` VarRef
  // (named `"$" + param`) with a clone of the matching caller arg, type-checks
  // the resulting expression, and stashes the expanded tree on `mc->expanded`
  // (with `mc->resultTy`) so IRGen codegens the same tree the check verified.
  // `substMacroParams` is the recursive substitution walker that copies the
  // body and swaps VarRefs (using cloneExpr so nested MacroCall/MethodCall/etc.
  // survive). Reports a Sema error for: unknown macro, arity mismatch, or
  // missing body. Returns the expanded type (void_ on error).
  BType expandMacro(MacroCall* mc);
  static ExprPtr substMacroParams(Expr* e,
                                   const MacroDecl* m,
                                   const std::vector<ExprPtr>& args);


  bool canTouchPrivate(const std::string& structName);


  void registerMethod(const std::string& structName, FuncDecl& fn);


  const struct MethodInfo* resolveOverload(const std::string& sn,
                                           BinaryExpr::Op op, Expr* b);


  bool foldConstExpr(Expr* e, GlobalInfo& gi);


  std::string instantiateGenericFn(const std::string& name, const std::vector<BType>& args);
  // Template-pointer flavour: instantiate a CHOSEN template (from selectGenericFn)
  // so a constrained OVERLOAD instantiates the selected template, not whichever
  // won the single-template registry race. `name` is the call symbol used for
  // mangling (the same across overloads of a name, so args drive distinct symbols).
  std::string instantiateGenericFnDecl(const std::string& name, const struct FuncDecl* tmpl,
                                       const std::vector<BType>& args);

  // --- C++-style concepts + constrained generics + default type args ---
  // Concept model: a `concept C<T> { fn ... }` decl is a NAMED set of required
  // method/associated-fn signatures; a generic type-param `T: C` (or `where
  // T: C`) is a CONSTRAINT that the concrete arg must satisfy by having all the
  // req'd methods (found via `impl`, walking the base chain). Used purely as a
  // compile-time predicate  -  no runtime polymorphism. `satisfies` is the
  // predicate; `fillDefaultTypeArgs` fills trailing missing type args from a
  // template's `tparams` defaults; `checkConstraints` validates every
  // constrained param of a template against filled args. Constrained overloads:
  // `genericFnOverloads_` lets multiple generic fns of the same name coexist,
  // disambiguated by which template's constraints the inferred args satisfy.
  bool satisfies(const BType& typeArg, const std::string& conceptName);
  std::vector<BType> fillDefaultTypeArgs(const std::vector<TypeParam>& tparams,
                                         std::vector<BType> args) const;
  bool checkConstraints(const std::vector<TypeParam>& tparams,
                        const std::map<std::string, BType>& env,
                        int line, int col);
  // Pick the right generic template for a name given the (inferred/filled) type
  // args: if there's only one, return it; if there are several (constrained
  // overloads), pick the FIRST in source order whose constraints all pass, or
  // null if none qualify. Source-order-first-match is the user's lever  -  a more
  // constrained template should be declared before the looser one to win.
  const FuncDecl* selectGenericFn(const std::string& name,
                                  const std::vector<BType>& inferredArgs);
  // Sema-only overload registry: per generic-fn-name, all templates in source
  // order (parallel to the single-template `findGenericFn` registry in Types.cpp
  // for the common no-overload case). Populated during the fn-registration pass.
  std::map<std::string, std::vector<const FuncDecl*>> genericFnOverloads_;

  // Single inheritance + vtables: build StructDef::vtableSlots + hasVirtuals +
  // MethodInfo::vtableSlot after all methods are registered and after the
  // base chain is resolved (Sema.cpp layout pass 2). Up-propagates hasVirtuals
  // along the base chain (a polymorphic derived forces its bases polymorphic so
  // the vtable ptr lives at offset 0 of the root, keeping &Derived -> &Base an
  // offset-0 bitcast) AND down-propagates from a polymorphic base to its
  // deriveds (a derived inherits the base's __oxvt slot and dispatches
  // inherited/overridden virtuals through it, so it's polymorphic too  -  see
  // AST.h: hasVirtuals means "this type OR any base declares a virtual fn").
  // Validates: virtual must have &self, override must match
  // an existing base virtual slot signature, no duplicate virtual decl, an
  // override must not change the slot's signature. Recurses base-first with a
  // memo so vtableSlots inherits correctly.
  void resolveVtables(const Program& prog);
  // Layout pass 3: for every polymorphic struct, recompute StructDef::fields +
  // offsets + size + align with a synthetic `__oxvt` vtable-pointer prefix at
  // offset 0 of the polymorphic ROOT (the baseless top of the inheritance
  // chain). Derived structs inherit the root's __oxvt via base-splice and add
  // NONE of their own (single shared vtable ptr). Non-polymorphic structs keep
  // their pass-1/2 layout untouched (zero change to existing examples). Runs
  // after resolveVtables set hasVirtuals.
  void recomputeVtableLayout(const Program& prog);


  BType checkLambda(LambdaLit* lam);

  // Default arguments. `registerDefaults` validates + stores the parsed default
  // initialisers for a function (trailing-ness rule + per-default type check),
  // keyed by its call symbol. `fillDefaultArgs` is called at a call site to
  // append clones of the trailing defaults so the caller-supplied arg list
  // reaches the full parameter count; returns false (with no fill) if a missing
  // slot has no default. `hasAnyDefault` is a quick pre-check.
  void registerDefaults(const std::string& sym, const std::vector<Param>& params);
  // Type-check every stored default initialiser (after globals are registered,
  // so a default may reference a const global). Ensures a defaulted function
  // that is never called still gets its defaults validated.
  void checkStoredDefaults();
  bool hasAnyDefault(const std::string& sym) const;
  bool fillDefaultArgs(const std::string& sym, std::vector<ExprPtr>& args,
                       size_t nparams, int line, int col, const std::string& what);
};
