#pragma once

#include "AST.h"
#include "Sema.h"

#include <map>
#include <set>
#include <string>
#include <vector>
#include <sstream>
#include <functional>


class IRGen {
public:
  IRGen(Sema& sema);
  void generate(Program& prog);
  std::string takeIR() { return out_.str(); }


  void setTargetTriple(const std::string& t) { targetTriple_ = t; }

private:
  Sema& sema_;
  std::string targetTriple_;
  std::ostringstream out_;
  std::ostringstream globals_;


  std::ostringstream lambdas_;
  std::map<std::string, StructDef*> structDefs_;
  std::set<std::string> userDefinedFns_;


  std::set<std::string> usedVec_;
  bool usedVec_blob_ = false;
  std::set<std::string> usedSort_;
  bool usedSort_blob_ = false;
  bool usedMap_ = false;
  bool usedSet_ = false;
  bool usedHMap_ = false;
  bool usedHSet_ = false;

  // Concurrency runtime usage flags. Each gates the emission of the matching
  // `@ox_*` declare so an untouched feature adds nothing to the IR / link line.
  // `usedChan_` is a set of element-type suffixes (i64/f64/i1/str/i8) matching
  // the vec suffix scheme  -  a program using `Channel[i64]` registers "i64" and
  // gets `declare ... @ox_chan_new_i64 / @ox_chan_send_i64 / @ox_chan_recv_i64`.
  bool usedSpawn_ = false;   // `spawn` -> @ox_thread_create/@ox_thread_join
  bool usedSync_ = false;    // `sync { ... }` -> @ox_sync_begin/@ox_sync_end
  std::set<std::string> usedChan_;   // suffix set for channel element types
  bool usedChan_blob_ = false;       // channels of an unsuffixed (blob) element

  // Advanced-math runtime usage flags. Each gates emission of the matching
  // `@ox_*` declare so an untouched operator adds nothing to the IR / link
  // line. Set in genExpr when the corresponding AST node is lowered.
  bool usedPowI_ = false;       // `**`/^ on integers -> @ox_ipow
  bool usedPowF_ = false;       // `**`/^ on f64 -> @ox_pow_f64 (libc pow)
  bool usedSquare_ = false;     // postfix ² -> @ox_square
  bool usedMat_ = false;        // matrix literal -> @ox_mat_new / @ox_mat_print / ...
  bool usedMatMul_ = false;     // matrix * matrix -> @ox_mat_mul
  bool usedSolve_ = false;      // A \ b linear solve -> @ox_mat_solve
  bool usedIntegrate_ = false;  // numeric integration -> @ox_integrate_trapz

  // Extended stdlib: the set of oxide-runtime extern names actually referenced
  // by the program (math/strings/vec-helpers/time/random/...). A declare is
  // emitted only for a name in this set, so unused stdlib contributes nothing
  // to the IR and links nothing. `declareStd(name)` looks up here.
  std::set<std::string> usedExt_;


  bool dynSetPending_ = false;
  bool dynSetBlob_ = false;
  std::string dynSetHandle_, dynSetIdx_, dynSetSx_;
  BType dynSetEt_ = BType::void_;

  // `m[k] = v` on a map/hmap: there is no slot to store into (the entry lives
  // in a runtime table), so we defer the @ox_map_set/@ox_hmap_set call until the
  // RHS value is evaluated (mirroring dynSetPending_). keyScratch_ holds the
  // spilled key; mapValT_ is the value type (for spilling v); mapIsH_ selects
  // the hash vs ordered runtime.
  bool mapSetPending_ = false;
  bool mapIsH_ = false;
  std::string keyScratch_;
  BType mapValT_ = BType::void_;
  std::string mapHandle_;

  std::string typeStr(BType t);

  std::string elemSuffix(const BType& t);

  std::string elemIrType(const BType& t);

  std::string vecSlotType(const std::string& sx);
  BType vecSlotBType(const std::string& sx);


  std::string curFnName_;
  BType curFnRet_ = BType::void_;
  std::string curBlock_;
  int labelSeq_ = 0;
  int strSeq_ = 0;
  bool terminated_ = false;
  // --- Function ``ensures'' exit-block mode ---
  // When a function has any `ensures` clauses, returns are rewritten to store
  // into a result slot and branch to a single `exit` block, where the ensures
  // gates run before the final ret. `curExitBlock_` is "" when NOT in this mode
  // (the common case: no ensures -> returns stay inline, unchanged behaviour).
  // `curResultSlot_` is the alloca holding the coerced return value; `oldSnapshots_`
  // maps a local name to its entry-time snapshot slot, read by OldExpr lowering.
  std::string curExitBlock_;
  std::string curResultSlot_;
  std::map<std::string, std::pair<std::string, BType>> oldSnapshots_;
  // True while lowering an `ensures` clause (so OldExpr reads from snapshots and
  // `result` reads from the result slot, not the live vars).
  bool inEnsuresGate_ = false;
  // For break/continue: the scope index of the loop's body scope (the scope
  // pushed for the body block). break/continue unwind every scope from the
  // innermost live down to and including bodyScope, running their destructors,
  // before jumping to the loop's break/continue target.
  struct LoopCtx { std::string cont; std::string brk; size_t bodyScope = 0; };
  std::vector<LoopCtx> loops_;
  std::vector<std::map<std::string, std::pair<std::string, BType>>> scopes_;
  // A drop-list entry is EITHER a local to be destroyed at scope exit (kernel
  // `kind == Var`: emitDropFor(name) once) OR a `defer <stmt>` (kernel
  // `kind == Defer`: generate `deferBody` inline at exit). Both interleave in a
  // single per-scope reverse declaration-order list, so RAII drops and defers run
  // together in the natural "last scheduled first" order on every exit path.
  struct DropEntry {
    enum K { Var, Defer } kind = Var;
    std::string name;       // Var: the local name (resolved to storage via scopes_)
    Stmt* deferBody = nullptr;  // Defer: AST sub-tree generated inline at exit
  };
  // Mirrors Sema's per-scope dropVars: the declaration order of locally-declared
  // names whose type has a destructor, plus any `defer` statements in this scope.
  // Each is emitted at scope exit in REVERSE declaration order and on every
  // early-exit path (return/break/continue). Parallel to scopes_; pushed/popped
  // in lockstep by pushScope/popScope.
  std::vector<std::vector<DropEntry>> scopeDropVars_;
  // During a `return X`, X might be (root at) a move-only local being moved
  // into the return slot  -  that local must NOT be dropped at the return site
  // (the caller takes ownership). emitScopeDrops skips any name in this set.
  // Cleared after the ret. Same mechanism could suppress a moved var on break.
  std::set<std::string> suppressDrop_;

  void emit(const std::string& s);
  void beginBlock(const std::string& name);
  void branch(const std::string& condVal, const std::string& t, const std::string& f);
  void jump(const std::string& t);
  void ensureTerminated();

  std::string freshLabel(const std::string& hint);
  std::string freshGlobal(const std::string& hint);
  std::string freshLocal(const std::string& hint);
  int freshInt();

  std::pair<std::string, BType> findVar(const std::string& name);
  void pushScope();
  void popScope();
  void declareVar(const std::string& name, const std::string& storage, BType t);
  void collectStruct(const BType& t);

  // --- RAII destructor emission ---
  // If storage `vname` holds a struct-typed value whose type (or any base, once
  // inheritance exists) has an `impl T { fn drop(&mut self) }`, emit a call to
  // that drop method, passing &storage. Returns true if a drop was emitted.
  bool emitDropFor(const std::string& storage, const BType& type);
  // Call emitDropFor for every drop-having local in scope `scopeIndex` in
  // REVERSE declaration order. Used at normal scope exit and on early-exit
  // paths that leave multiple scopes (return/break/continue): call once per
  // scope from the innermost affected out to the target.
  void emitScopeDrops(size_t scopeIndex);
  // Emit drops for all scopes from the innermost live scope down to and
  // INCLUDING `toScopeIndex` (the scope an early-exit target lives in), each in
  // reverse declaration order. Used by return/break/continue to run every
  // intervening destructor before jumping to the target block.
  void emitScopeUnwind(size_t toScopeIndex);
  // Generate the deferred statement's IR inline. Used by emitScopeDrops for a
  // Defer entry (the statement runs at scope exit, LIFO with the RAII drops).
  void emitDefer(Stmt* body);
  // Emit drops for the current (innermost) scope in reverse declaration order,
  // then pop it. Convenience for the many `pushScope(); genBlock(...); popScope();`
  // sites where the block body's locals may need destructors at exit.
  void popScopeWithDrops();
  // Remove `name` from the current scope's dropVars if present  -  used to exclude
  // a borrowed `self` (a &self/&mut self arg is NOT owned by the method and must
  // never be auto-dropped, else a `drop` method recurses into itself forever).
  void excludeDrop(const std::string& name);
  // Is struct `sn` (or any base) move-only  -  has a drop but no clone? Such a
  // type, when copied into a new binding/arg/return, is MOVED: the source slot
  // is consumed. We then remove the source local from its drop-list so the
  // scope-exit destructor does NOT run on it (the new owner will drop it).
  bool isMoveOnlyStruct(const std::string& sn) const;
  // Single-inheritance upcast helper for arg coercion (mirrors Sema::isBaseOf).
  bool isBaseOf(const std::string& parentName, const std::string& childName) const;
  // Is an addressable arg of storage (pointee) type `bt` addressable+coercible
  // to param pointer type `pt` (exact match, or &Derived -> &Base upcast)?
  bool argPtrAccepts(const BType& bt, const BType& pt) const;
  // Remove `name` from its owning scope's dropVars (a move-out of that local),
  // and never drop it. Idempotent.
  void markMovedOut(const std::string& name);
  // If `pt` (the formal param type) is a move-only struct and `arg` is rooted at
  // a local VarRef, that local is moved INTO the function (the callee now owns
  // the value). Mark the source moved-out so it is NOT dropped again by the
  // caller's scope exit (the callee drops its owned copy). No-op otherwise.
  void maybeMarkArgMoved(Expr* arg, const BType& pt);


  std::string globalInit(const BType& t, const struct GlobalInfo& gi, bool folded);

  void emitGlobalsAndExterns();


  std::pair<std::string, BType> genAddr(Expr* e);


  void boundsCheck(const std::string& idx, int32_t count);

  // --- Formal-verification contract gates (runtime enforcement) ---
  // Lower a boolean contract expression to a runtime trap-gate: evaluate the
  // expression to a condition value, and on false call @ox_contract_fail(tag,
  // line) + unreachable; on true continue in a fresh ok block. `tag` identifies
  // the contract kind (0=assert,1=requires,2=ensures,3=invariant). Mirrors the
  // boundsCheck pattern but for an arbitrary boolean predicate. If `cond` is
  // null (e.g. a parse error earlier) this is a no-op.
  void genContractGate(Expr* cond, int tag, int line);
  // ox:proof The gate for a quantifier, expanded into a bounded runtime loop over the
  // integer range (a quantifier is checked by actually iterating its range and
  // testing the body at each element  -  a real runtime witness, not a static
  // proof). Returns the i1 result value name. `negate` flips forall<->exists
  // failure sense: forall fails when ANY element's body is false; exists fails
  // when ALL elements' bodies are false. Used by genContractGate for a top-
  // level quantifier; nested quantifiers are handled recursively.
  std::string genQuantifierGate(const QuantExpr* q, int tag, int line);
  // Contract kind tags passed to @ox_contract_fail.
  enum ContractTag { CT_ASSERT = 0, CT_REQUIRES = 1, CT_ENSURES = 2, CT_INVARIANT = 3 };
  // Pre-declare @ox_contract_fail (mirrors how @ox_bounds_fail is declared in
  // emitHeaderAndRuntime). Idempotent via declaredIntrinsics_.
  void ensureContractFailDecl();


  void strBoundsCheck(const std::string& strPtr, const std::string& idx);


  std::pair<std::string, BType> genExpr(Expr* e);


  std::string genCoerce(const std::string& v, BType fromT, BType toT);

  void printValue(const std::string& val, const BType& t, const std::string& prefix = "");
  std::string strConst(const std::string& s);

  std::pair<std::string, BType> lowerBuiltin(Call* c);


  std::string spillScratch(const std::string& v, const BType& t);


  std::string loadScratch(const std::string& ptr, const BType& t);


  std::pair<std::string, BType> emitMethodCall(const std::string& structName,
                                                const std::string& methodName,
                                                const BType& recvType, bool recvByRef,
                                                Expr* receiver,
                                                const std::vector<ExprPtr>& args);


  std::pair<std::string, BType> emitOverloadCall(const std::string& structName,
                                                 const std::string& methodName,
                                                 const BType& recvType, bool recvByRef,
                                                 Expr* receiver,
                                                 const std::vector<ExprPtr>& args,
                                                 bool negateResult);
  void genStmt(Stmt* s);
  void genBlock(const std::vector<StmtPtr>& stmts);
  void genFunc(FuncDecl& fn);


  void genLambda(const LambdaLit* lam);
  std::set<std::string> emittedLambdas_;


  std::set<std::string> declaredIntrinsics_;
  void ensureIntrinsic(const std::string& decl, const std::string& name);


  void genMethod(const std::string& structName, FuncDecl& fn);
  void emitHeaderAndRuntime();

  // --- Virtual dispatch / vtables ---
  // Sema marks polymorphic structs (StructDef::hasVirtuals), gives them an
  // `__oxvt` i8* field at index 0 (shared across an inheritance chain  -  the
  // baseless root's slot, inherited by derived), and builds StructDef::
  // vtableSlots + MethodInfo::vtableSlot. IRGen:
  //   • emits a vtable GLOBAL per polymorphic struct (emitVtables), an array
  //     of i8* fn pointers  -  one per vtableSlots entry  -  initialized with the
  //     MOST-DERIVED implementation (the method resolveMethod finds);
  //   • stores &@__oxvt_<S> into the __oxvt slot at every StructLit;
  //   • dispatches a virtual method call indirectly (load vtable ptr, load the
  //     slot's fn ptr, bitcast, call) instead of a direct @mangled call.
  // Vtable globals emitted so far (dedup, in case collect/emit runs twice).
  std::set<std::string> emittedVtables_;
  void emitVtables();

  // ox:unsafe fix2  -  bare-metal trap vector table (VMCS host RIP / IDT-style dispatch).
  // When the program is compiled in freestanding/bare-metal mode (--freestanding)
  // AND declares at least one `trap handler name(...) { body }` with a compiled
  // body, IRGen emits a `.trap_table` global: an array of `i8*` function pointers
  //  -  one per handler, in declaration order  -  placed in the `.trap_table` ELF
  // section so VMX startup code can locate it and wire each entry into the VMCS
  // host-RIP field (or an IDT-style vector). Body-less `trap name(...);`
  // prototypes (isExtern) and generic handlers (no codegen) are skipped, matching
  // the genFunc loop above. Returns having emitted nothing when freestanding is
  // false (hosted test harnesses don't need a trap table) or no handler qualifies.
  void emitTrapTable(Program& prog);
  // True if struct `sn` (or any base via single inheritance) is polymorphic.
  bool isPolymorphic(const std::string& sn) const;
  // The vtable global symbol for struct `sn`.
  static std::string vtableGlobalSym(const std::string& sn) { return "__oxvt_" + sn; }
  // Field index of the __oxvt slot in sn (the index Sema placed it at, 0 for the
  // baseless root and the same index inherited by derived via base-splice).
  static int vtableFieldIndex(const StructDef* d);
};
