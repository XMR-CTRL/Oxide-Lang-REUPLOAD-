#include "Sema.h"
#include <cstdio>
#include <cassert>
#include <cmath>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <functional>


ExprPtr cloneExpr(const Expr* e);
StmtPtr cloneStmt(const Stmt* s);

// File-scope access to the active Program + Sema instance during Sema::check,
// so the free function instantiateGenericStruct (invoked transitively from
// fixType, which has no `this`) can walk prog.impls and clone the generic
// struct's methods into each instantiation. Set once at the start of
// Sema::check and cleared at the end; null outside a check() call. Safe
// because all instantiation happens synchronously inside check().
static const Program* g_progForGenStruct = nullptr;
static Sema* g_semaForGenStruct = nullptr;

// Forward decls for file-local `errAt` overloads (defined below ~line 114).
// Needed because earlier code (e.g. Sema::declare) calls `errAt` before the
// definitions appear in translation-unit order  -  static functions require a
// prior declaration in C++. Just declarations; the bodies stay where they are.
struct SemanticError;
static void errAt(std::vector<SemanticError>& errs, int line, int col, const std::string& m);
static void errAt(std::vector<SemanticError>& errs, int line, int col, const std::string& m, const std::string& hint);
static void errAt(std::vector<SemanticError>& errs, int line, const std::string& m);

// Effect system  -  copy a FuncDecl's declared `effects` list into a FuncSig,
// applying the propagation-relevant derived flag (`isPure` = empty effects).
// Called at every Sema registration site that has a backing FuncDecl (free
// fns, impl methods, trap handlers, generic clones, lemmas, ghost methods).
// Sites that register a signature with no source-level Fn (spec fns, lambdas)
// leave the FuncSig's defaults untouched (isPure=true, effectsExplicit=false,
// effects empty), which is correct: spec fns and lambdas are pure by
// definition and never wrote an `effects` clause. Dedup collapses repeated
// effect names (a fn that writes `effects { io, io }` sees one `io` in the
// sig); the propagation check queries set membership, not positions.
// `effectsExplicit` flows through unchanged so the Sema propagation/purity
// gates and the Ghost blanket-frame decision fire only for fns that literally
// wrote an `effects` clause (omitted => untracked => no enforcement, keeping
// pre-effect-system programs working).
static void applyEffects(FuncSig& sig, bool effectsExplicit,
                         const std::vector<std::string>& effects) {
  sig.effects.clear();
  std::set<std::string> seen;
  for (const auto& e : effects) {
    if (!e.empty() && seen.insert(e).second) sig.effects.push_back(e);
  }
  sig.isPure = sig.effects.empty();        // empty list OR omitted => pure-by-form
  sig.effectsExplicit = effectsExplicit;
}

VarInfo Sema::lookup(const std::string& name) {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto f = it->vars.find(name);
    if (f != it->vars.end()) return {f->second.type, f->second.isMut, true};
  }
  return {BType::void_, false, false};
}

void Sema::pushScope() { scopes_.emplace_back(); }
void Sema::popScope() { scopes_.pop_back(); }
void Sema::declare(const std::string& name, BType t, bool isMut, bool isGlobal,
                   bool allowRedecl, int line) {
  // Same-scope redeclaration check. `let x = 1; let x = 2;` in ONE scope is a
  // compile error: the silent overwrite that used to happen here would break
  // drop order (the dropVars list and the Entry both get clobbered), ownership
  // /borrow tracking, ghost-variable meaning, and SMT variable mapping. Nested-
  // scope shadowing is unaffected  -  pushScope() gives a fresh vars map, so a
  // `let x` inside `{ }`/if-body/loop-body lands in a different scope and is
  // legitimately a shadow. `allowRedecl` opts out: it is set ONLY for the
  // ghost-let pre-declare pass and the matching body-walker arm, which
  // deliberately write the same name + same type into the same scope-slot (see
  // the long comment at predeclareGhostLets). We still report nothing then.
  if (!allowRedecl) {
    auto& cur = scopes_.back().vars;
    auto it = cur.find(name);
    if (it != cur.end()) {
      errAt(errs, line, "redeclaration of '" + name + "' in the same scope");
      // Don't return: fall through to the (now-diagnosed) overwrite so downstream
      // type-checking sees the new binding, matching pre-fix behaviour for the
      // rest of the pass (one error, not a cascade).
    }
  }
  bool drop = (t.tag == BType::Tag::struct_) && structHasDrop(t.structName);
  scopes_.back().vars[name] = {t, isMut, isGlobal, drop};
}

void Sema::recordDropLocal(const std::string& name) {
  // Avoid duplicate entries if declare is somehow called twice for one name.
  auto& dv = scopes_.back().dropVars;
  if (std::find(dv.begin(), dv.end(), name) == dv.end()) dv.push_back(name);
}

bool Sema::structHasDrop(const std::string& sn) const {
  if (const StructDef* d = findStruct(sn)) {
    if (d->hasDrop) return true;
    if (d->base) return d->base->hasDrop;   // bases' dtors run too
  }
  return false;
}
bool Sema::structHasClone(const std::string& sn) const {
  if (const StructDef* d = findStruct(sn)) {
    if (d->hasClone) return true;
    if (d->base) return d->base->hasClone;
  }
  return false;
}

bool Sema::isBaseOf(const std::string& parentName, const std::string& childName) const {
  // Walk child's base chain; parentName must appear strictly above child
  // (so same-type is NOT a base  -  exact equality is the BType== path).
  if (parentName == childName) return false;
  for (const StructDef* d = findStruct(childName); d; d = d->base)
    if (d->base && d->base->name == parentName) return true;
  return false;
}

bool Sema::implicitAssignable(const BType& from, const BType& to) const {
  if (from == to) return true;
  // Pointer-to-derived upcast to pointer-to-base (&Derived -> &Base) and
  // (*Derived -> *Base): single inheritance lays the base sub-object first at
  // offset 0, so the IR lowering is a plain pointer bitcast (genCoerce handles
  // any ptr->ptr). Excludes void*-decay (that's a separate, u8 rule upstream).
  if (from.tag == BType::Tag::ptr && to.tag == BType::Tag::ptr) {
    const BType& fp = pointee(from), tp = pointee(to);
    if (fp.tag == BType::Tag::struct_ && tp.tag == BType::Tag::struct_ &&
        isBaseOf(tp.structName, fp.structName))
      return true;
  }
  return false;
}

static void errAt(std::vector<SemanticError>& errs, int line, int col, const std::string& m) {
  errs.push_back({m, line, col, ""});
}
static void errAt(std::vector<SemanticError>& errs, int line, int col, const std::string& m, const std::string& hint) {
  errs.push_back({m, line, col, hint});
}

static void errAt(std::vector<SemanticError>& errs, int line, const std::string& m) {
  errs.push_back({m, line, 0, ""});
}


std::string mangleMethod(const std::string& structName, const std::string& methodName) {
  return "__oxm_" + structName + "__" + methodName;
}


static BType fixType(BType t);


static BType substitute(BType t, const std::map<std::string, BType>& env) {
  if (t.tag == BType::Tag::struct_) {
    auto it = env.find(t.structName);
    if (it != env.end()) return it->second;
    return t;
  }
  if (t.tag == BType::Tag::array)
    return makeArrayType(substitute(arrayElem(t), env), t.count);
  if (t.tag == BType::Tag::dynarray)
    return makeDynArray(substitute(dynArrayElem(t), env));
  if (t.tag == BType::Tag::ptr)
    return makePtr(substitute(pointee(t), env));
  if (t.tag == BType::Tag::map_ || t.tag == BType::Tag::hmap_)
    return (t.tag == BType::Tag::map_) ? makeMapType(substitute(mapKeyType(t), env), substitute(mapValType(t), env))
                                  : makeHMapType(substitute(mapKeyType(t), env), substitute(mapValType(t), env));
  if (t.tag == BType::Tag::set_ || t.tag == BType::Tag::hset_)
    return (t.tag == BType::Tag::set_) ? makeSetType(substitute(setElemType(t), env))
                                  : makeHSetType(substitute(setElemType(t), env));
  if (t.tag == BType::Tag::fn_) {
    const auto& ps = fnParams(t);
    std::vector<BType> np; np.reserve(ps.size());
    for (const auto& p : ps) np.push_back(substitute(p, env));
    return makeFnType(np, substitute(fnRet(t), env));
  }
  if (t.tag == BType::Tag::generic_) {


    const auto& args = genericInstArgs(t);
    std::vector<BType> na; na.reserve(args.size());
    for (const auto& a : args) na.push_back(substitute(a, env));
    return makeGenericInst(genericInstBase(t), na, genericInstIsFn(t));
  }
  return t;
}


// Unify a template parameter type `tmpl` against an actual call-argument type
// `actual`, recording a binding for every type parameter we discover. This walks
// the shape of `tmpl` and matches it against `actual`:
//   * a type-parameter name (stored as struct_ with that name) binds to actual
//   * a generic struct Pair<A,B> against a constructed instance binds A and B
//     from the instance's argument list
// `env` accumulates the bindings; an existing binding that disagrees is a hard
// mismatch (the caller treats a non-empty env as "got something" and a mismatch
// keeps env empty so the explicit-args path can fire).
static void unifyInto(BType tmpl, BType actual, std::map<std::string, BType>& env) {
  if (tmpl.tag == BType::Tag::struct_ && tmpl.structName.empty() == false) {
    // a type parameter binds directly to the actual type at this position
    env[tmpl.structName] = actual;
    return;
  }
  if (tmpl.tag == BType::Tag::generic_) {
    const std::string& base = genericInstBase(tmpl);
    const auto& targs = genericInstArgs(tmpl);
    // ox:unsafe the actual must be a constructed instance of the same base
    if (actual.tag == BType::Tag::struct_) {
      // constructed generic structs are stored as struct_ with a mangled name;
      // recover their argument list via the registered struct definition
      const StructDef* sd = findStruct(actual.structName);
      if (sd && sd->genericOf == base && sd->genericArgs.size() == targs.size()) {
        for (size_t i = 0; i < targs.size(); i++)
          unifyInto(targs[i], sd->genericArgs[i], env);
      }
    } else if (actual.tag == BType::Tag::generic_ && genericInstBase(actual) == base) {
      const auto& aargs = genericInstArgs(actual);
      if (aargs.size() == targs.size())
        for (size_t i = 0; i < targs.size(); i++)
          unifyInto(targs[i], aargs[i], env);
    }
    return;
  }
  if (tmpl.tag == BType::Tag::ptr && actual.tag == BType::Tag::ptr)
    unifyInto(pointee(tmpl), pointee(actual), env);
  if (tmpl.tag == BType::Tag::array && actual.tag == BType::Tag::array)
    unifyInto(arrayElem(tmpl), arrayElem(actual), env);
  if (tmpl.tag == BType::Tag::dynarray && actual.tag == BType::Tag::dynarray)
    unifyInto(dynArrayElem(tmpl), dynArrayElem(actual), env);
}


// Walk a cloned expression tree and substitute any symbolic type-parameter
// occurrences inside BType fields embedded on nodes (Call::typeArgs,
// StructLit::typeArgs, CastExpr::target, SizeofExpr::target, DynNew/MapNew/
// SetNew/HMapNew/HSetNew element types, AsmExpr operand types). The environment
// maps type-parameter names to their concrete instantiation types.
static void substTypesInExpr(Expr* e, const std::map<std::string, BType>& env);

static BType substType(BType t, const std::map<std::string, BType>& env) {
  return substitute(t, env);
}

static void substTypesInStmt(Stmt* s, const std::map<std::string, BType>& env) {
  if (!s) return;
  if (auto es = dynamic_cast<ExprStmt*>(s)) { substTypesInExpr(es->expr.get(), env); return; }
  if (auto ls = dynamic_cast<LetStmt*>(s)) {
    if (ls->typeAnnotated) ls->type = substType(ls->type, env);
    substTypesInExpr(ls->init.get(), env);
    return;
  }
  if (auto rs = dynamic_cast<ReturnStmt*>(s)) { substTypesInExpr(rs->value.get(), env); return; }
  if (auto is = dynamic_cast<IfStmt*>(s)) {
    substTypesInExpr(is->cond.get(), env);
    for (auto& st : is->then) substTypesInStmt(st.get(), env);
    for (auto& st : is->else_) substTypesInStmt(st.get(), env);
    return;
  }
  if (auto ws = dynamic_cast<WhileStmt*>(s)) {
    substTypesInExpr(ws->cond.get(), env);
    for (auto& inv : ws->invariants) substTypesInExpr(inv.get(), env);
    for (auto& st : ws->body) substTypesInStmt(st.get(), env);
    return;
  }
  if (auto fs = dynamic_cast<ForStmt*>(s)) {
    fs->elemType = substType(fs->elemType, env);
    fs->elemType2 = substType(fs->elemType2, env);
    fs->iterType = substType(fs->iterType, env);
    substTypesInExpr(fs->start.get(), env);
    substTypesInExpr(fs->end.get(), env);
    substTypesInExpr(fs->step.get(), env);
    substTypesInExpr(fs->iter.get(), env);
    for (auto& inv : fs->invariants) substTypesInExpr(inv.get(), env);
    for (auto& st : fs->body) substTypesInStmt(st.get(), env);
    return;
  }
  if (auto b = dynamic_cast<Block*>(s)) {
    for (auto& st : b->stmts) substTypesInStmt(st.get(), env);
    return;
  }
  if (auto sb = dynamic_cast<SyncBlock*>(s)) {
    for (auto& st : sb->body) substTypesInStmt(st.get(), env);
    return;
  }
  if (auto d = dynamic_cast<DeferStmt*>(s)) {
    if (d->body) substTypesInStmt(d->body.get(), env);
    return;
  }
  if (auto a = dynamic_cast<AssertStmt*>(s)) {
    substTypesInExpr(a->cond.get(), env);
    // `assert <expr> by { <hints> };`  -  substitute types in each hint too.
    for (auto& h : a->byBody) substTypesInStmt(h.get(), env);
    return;
  }
  // `assume <expr>;` / `trusted assume <expr>;`  -  substitute type vars inside
  // the assumed condition (a spec expression may name a generic T). No byBody /
  // nested hints to recurse into; sourceCitation is a plain string (no T).
  if (auto as = dynamic_cast<AssumeStmt*>(s)) {
    if (as->cond) substTypesInExpr(as->cond.get(), env);
    return;
  }
}

static void substTypesInExpr(Expr* e, const std::map<std::string, BType>& env) {
  if (!e) return;
  if (auto c = dynamic_cast<Call*>(e)) {
    if (c->hasTypeArgs) for (auto& a : c->typeArgs) a = substType(a, env);
    for (auto& a : c->args) substTypesInExpr(a.get(), env);
    substTypesInExpr(c->calleeExpr.get(), env);
    return;
  }
  if (auto sl = dynamic_cast<StructLit*>(e)) {
    if (sl->hasTypeArgs) for (auto& a : sl->typeArgs) a = substType(a, env);
    for (auto& v : sl->values) substTypesInExpr(v.get(), env);
    return;
  }
  if (auto ce = dynamic_cast<CastExpr*>(e)) { ce->target = substType(ce->target, env); substTypesInExpr(ce->e.get(), env); return; }
  if (auto so = dynamic_cast<SizeofExpr*>(e)) { so->target = substType(so->target, env); return; }
  if (auto dn = dynamic_cast<DynNew*>(e)) { dn->elemType = substType(dn->elemType, env); return; }
  if (auto mn = dynamic_cast<MapNew*>(e)) { mn->keyType = substType(mn->keyType, env); mn->valType = substType(mn->valType, env); return; }
  if (auto sn = dynamic_cast<SetNew*>(e)) { sn->elemType = substType(sn->elemType, env); return; }
  if (auto hm = dynamic_cast<HMapNew*>(e)) { hm->keyType = substType(hm->keyType, env); hm->valType = substType(hm->valType, env); return; }
  if (auto hs = dynamic_cast<HSetNew*>(e)) { hs->elemType = substType(hs->elemType, env); return; }
  // Concurrency nodes: ChannelNew carries an element BType to substitute (its
  // channel type is makeChannelType(elemType), fixed later in checkExpr);
  // ChannelSend/ChannelRecv/SpawnExpr only reference sub-expressions, so walk
  // those. The binder substituted here is a quantifier binder  -  it cannot
  // appear free in a channel/spawn body in well-typed code, but recursing keeps
  // the substitution total (no node silently skipped).
  if (auto cn = dynamic_cast<ChannelNew*>(e)) { cn->elemType = substType(cn->elemType, env); return; }
  if (auto cs = dynamic_cast<ChannelSend*>(e)) { substTypesInExpr(cs->chan.get(), env); substTypesInExpr(cs->val.get(), env); return; }
  if (auto cr = dynamic_cast<ChannelRecv*>(e)) { substTypesInExpr(cr->chan.get(), env); return; }
  if (auto sp = dynamic_cast<SpawnExpr*>(e)) { if (sp->body) substTypesInExpr(sp->body.get(), env); return; }
  if (auto a = dynamic_cast<AsmExpr*>(e)) {
    for (auto& io : a->ios) io.ty = substType(io.ty, env);
    // Recurse into the implemented-spec arg expressions too  -  they may carry
    // generic type-args (e.g. `implements spec(vec[i32])`) that need the same
    // substitution as the rest of the asm block's operands.
    for (auto& arg : a->implementsArgs) substTypesInExpr(arg.get(), env);
    return;
  }
  if (auto u = dynamic_cast<UnaryExpr*>(e)) { substTypesInExpr(u->base.get(), env); return; }
  if (auto b = dynamic_cast<BinaryExpr*>(e)) { substTypesInExpr(b->lhs.get(), env); substTypesInExpr(b->rhs.get(), env); return; }
  if (auto at = dynamic_cast<AssignTarget*>(e)) { substTypesInExpr(at->base.get(), env); substTypesInExpr(at->index.get(), env); substTypesInExpr(at->value.get(), env); return; }
  if (auto mc = dynamic_cast<MethodCall*>(e)) { substTypesInExpr(mc->receiver.get(), env); for (auto& a : mc->args) substTypesInExpr(a.get(), env); return; }
  if (auto ac = dynamic_cast<AssocCall*>(e)) { for (auto& a : ac->args) substTypesInExpr(a.get(), env); return; }
  if (auto ix = dynamic_cast<Index*>(e)) { substTypesInExpr(ix->base.get(), env); substTypesInExpr(ix->index.get(), env); return; }
  if (auto fl = dynamic_cast<Field*>(e)) { substTypesInExpr(fl->base.get(), env); return; }
  if (auto al = dynamic_cast<ArrayLit*>(e)) { for (auto& el : al->elems) substTypesInExpr(el.get(), env); return; }
  if (auto ie = dynamic_cast<IncDecExpr*>(e)) { substTypesInExpr(ie->base.get(), env); return; }
  if (auto te = dynamic_cast<TernaryExpr*>(e)) { substTypesInExpr(te->cond.get(), env); substTypesInExpr(te->thenE.get(), env); substTypesInExpr(te->elseE.get(), env); return; }
  if (auto r = dynamic_cast<RangeLit*>(e)) { substTypesInExpr(r->lo.get(), env); substTypesInExpr(r->hi.get(), env); return; }
  if (auto o = dynamic_cast<OldExpr*>(e)) { substTypesInExpr(o->sub.get(), env); return; }
  if (auto q = dynamic_cast<QuantExpr*>(e)) {
    q->binderType = substType(q->binderType, env);
    substTypesInExpr(q->lo.get(), env); substTypesInExpr(q->hi.get(), env);
    substTypesInExpr(q->body.get(), env);
    return;
  }
}


static std::string mangleInst(const std::string& base, const std::vector<BType>& args) {
  std::string s = "__oxg_" + base;
  for (const auto& a : args) {
    s += "__";
    std::string sp = typeSpelling(a);
    for (char c : sp) {
      if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) s += c;
      else if (c == ' ') ;
      else { s += '_'; }
    }
  }
  return s;
}


BType instantiateGenericStruct(const std::string& base, const std::vector<BType>& args) {
  if (isAliasName(base)) {


  }
  const StructDecl* tmpl = findGenericStruct(base);
  if (!tmpl) {


    BType t; t.tag = BType::Tag::struct_; t.structName = base; return t;
  }
  // Fill trailing default type args (`<T = i64>`). Use the template's tparams;
  // a missing non-defaulted param leaves args short and the arity check below
  // fires just as before. fixType the defaults so aliases/generic struct args
  // resolve. Note: constraint validation for generic STRUCTS is done in the
  // StructLit path (which is the only user-reachable instantiation site for
  // generic structs); struct field types reaching here via fixType are not
  // constraint-checked in this first cut (documented limitation).
  std::vector<BType> filledArgs = args;
  if (filledArgs.size() < tmpl->typeParams.size()) {
    for (size_t i = filledArgs.size(); i < tmpl->typeParams.size(); i++) {
      if (!tmpl->tparams.empty() && i < tmpl->tparams.size() && tmpl->tparams[i].hasDefault)
        filledArgs.push_back(fixType(tmpl->tparams[i].defaultType));
      else break;
    }
  }
  if (filledArgs.size() != tmpl->typeParams.size()) {
    BType t; t.tag = BType::Tag::struct_; t.structName = base; return t;
  }
  std::string mangled = mangleInst(base, filledArgs);

  if (findStruct(mangled)) {
    BType t; t.tag = BType::Tag::struct_; t.structName = mangled; return t;
  }

  std::map<std::string, BType> env;
  for (size_t i = 0; i < tmpl->typeParams.size() && i < filledArgs.size(); i++)
    env[tmpl->typeParams[i]] = filledArgs[i];

  StructDef* d = registerStruct(mangled);
  d->name = mangled;
  d->genericOf = base;
  d->genericArgs = filledArgs;
  d->fields.clear();
  for (auto& f : tmpl->fields) {
    const std::string& fname = std::get<0>(f);
    bool priv = std::get<2>(f);
    BType ft = fixType(substitute(std::get<1>(f), env));
    d->fields.push_back({fname, ft, 0, priv});
  }

  d->size = 0; d->align = 1;
  int32_t off = 0;
  for (auto& f : d->fields) {
    int32_t w = fieldByteWidth(f.type);
    int32_t a = fieldAlign(f.type);
    if (a > d->align) d->align = a;
    off = (off + a - 1) & ~(a - 1);
    f.offset = off;
    off += w;
  }
  off = (off + d->align - 1) & ~(d->align - 1);
  d->size = off;

  // Clone this struct's impl methods into the instantiation, substituting the
  // type parameters with `env` so the clone carries CONCRETE field/param/ret
  // types. Each clone is registered under `methods[mangled]` (so method-call
  // resolution lands on the mangled inst) and `funcs[mangleMethod(mangled,
  // name)]` (so IRGen's call site emits the right symbol), AND pushed to
  // `monomorphMethods` so IRGen emits the body via genMethod(mangled, *clone)
  // and Sema type-checks the substituted body in the per-instantiation loop.
  //
  // Scope: methods that are themselves generic (owning `fn foo<T2>(...)` on a
  // generic struct) and ghost/virtual methods are skipped here  -  generic
  // methods on generic structs and virtual dispatch on instantiations are
  // deeper features outside this change; vtables stay owned by the
  // non-generic-impl walk. Associated fns (no self) and ordinary &self methods
  // whose signature only mentions the struct's own type params clone cleanly
  // (the example `impl Box { fn make(v: i64) -> Box<i64> }` lands here).
  if (g_progForGenStruct && g_semaForGenStruct) {
    for (auto& im : g_progForGenStruct->impls) {
      if (!im || im->structName != base) continue;
      for (auto& m : im->methods) {
        if (!m) continue;
        if (m->isGeneric || m->isGhost || m->isVirtual || m->isOverride) continue;

        auto clone = std::make_unique<FuncDecl>();
        clone->line = m->line;
        clone->isExtern = false;
        clone->isGeneric = false;
        clone->isExport = m->isExport;
        clone->isUnsafe = m->isUnsafe;
        clone->isLemma = false;
        clone->isGhost = false;
        clone->implStruct = mangled;
        clone->hasSelf = m->hasSelf;
        clone->selfByRef = m->selfByRef;
        clone->isVirtual = false;
        clone->isOverride = false;
        clone->name = m->name;
        clone->retType = fixType(substitute(m->retType, env));
        for (auto& p : m->params) {
          Param np; np.name = p.name;
          np.type = fixType(substitute(p.type, env));
          np.hasDefault = false;          // instantiated methods carry no defaults
          clone->params.push_back(std::move(np));
        }
        for (auto& s : m->body) {
          StmtPtr c = cloneStmt(s.get());
          substTypesInStmt(c.get(), env);
          clone->body.push_back(std::move(c));
        }
        for (auto& r : m->requires_) {
          auto c = cloneExpr(r.get());
          substTypesInExpr(c.get(), env);
          clone->requires_.push_back(std::move(c));
        }
        for (auto& e2 : m->ensures_) {
          auto c = cloneExpr(e2.get());
          substTypesInExpr(c.get(), env);
          clone->ensures_.push_back(std::move(c));
        }
        clone->effectsExplicit = m->effectsExplicit;
        clone->effects = m->effects;     // effect names are unparameterised strings

        g_semaForGenStruct->registerMethodPublic(mangled, *clone);
        auto& mm = g_semaForGenStruct->monomorphMethods;
        mm.push_back({mangled, std::move(clone)});
      }
    }
  }

  BType t; t.tag = BType::Tag::struct_; t.structName = mangled; return t;
}

static BType fixType(BType t) {
  if (t.tag == BType::Tag::struct_) {
    if (findEnum(t.structName)) return makeEnumType(t.structName);
    if (isAliasName(t.structName))
      return fixType(resolveAlias(t.structName).second);
    return t;
  }
  if (t.tag == BType::Tag::generic_) {

    if (genericInstIsFn(t)) {


      std::vector<BType> args; for (const auto& a : genericInstArgs(t)) args.push_back(fixType(a));
      BType ft; ft.tag = BType::Tag::struct_; ft.structName = mangleInst(genericInstBase(t), args);
      return ft;
    }
    std::vector<BType> args; for (const auto& a : genericInstArgs(t)) args.push_back(fixType(a));
    return instantiateGenericStruct(genericInstBase(t), args);
  }
  if (t.tag == BType::Tag::fn_) {
    const auto& ps = fnParams(t);
    std::vector<BType> np; for (const auto& p : ps) np.push_back(fixType(p));
    return makeFnType(np, fixType(fnRet(t)));
  }
  if (t.tag == BType::Tag::array)   return makeArrayType(fixType(arrayElem(t)), t.count);
  if (t.tag == BType::Tag::dynarray) return makeDynArray(fixType(dynArrayElem(t)));
  if (t.tag == BType::Tag::ptr)    return makePtr(fixType(pointee(t)));
  if (t.tag == BType::Tag::map_ || t.tag == BType::Tag::hmap_)
    return (t.tag == BType::Tag::map_) ? makeMapType(fixType(mapKeyType(t)), fixType(mapValType(t)))
                                  : makeHMapType(fixType(mapKeyType(t)), fixType(mapValType(t)));
  if (t.tag == BType::Tag::set_ || t.tag == BType::Tag::hset_)
    return (t.tag == BType::Tag::set_) ? makeSetType(fixType(setElemType(t)))
                                  : makeHSetType(fixType(setElemType(t)));
  return t;
}


bool Sema::foldConstExpr(Expr* e, GlobalInfo& gi) {
  if (auto l = dynamic_cast<IntLit*>(e)) {
    gi.hasConstVal = true;
    // Preserve an int-typed annotation the caller already seeded into `gi.type`
    // (e.g. `const X: u64 = 18446744073709551615` seeds u64). Without this the
    // fold would always emit `i64`, which then trips `fold type mismatch` for
    // any non-i64 int global whose literal does not also fit int64  -  even though
    // the literal fits the unsigned target perfectly (e.g. u64 all-ones). When
    // no annotation was seeded (`gi.type` is void_/non-int) we default to i64,
    // preserving the historical behaviour for plain `let`/`const` paths.
    gi.iVal = (int64_t)l->v;   // bit-pattern-identical for both i64 and u64
    if (!isInt(gi.type) || gi.type == BType::void_)
      gi.type = BType::i64;
    return true;
  }
  if (auto l = dynamic_cast<FloatLit*>(e)) {
    gi.hasConstVal = true;
    if (l->isF32) { gi.type = BType::f32; gi.fVal = (double)(float)l->v; }
    else { gi.type = BType::f64; gi.fVal = l->v; }
    return true;
  }
  if (auto l = dynamic_cast<BoolLit*>(e)) {
    gi.hasConstVal = true; gi.type = BType::bool_; gi.bVal = l->v; return true;
  }
  if (auto l = dynamic_cast<CharLit*>(e)) {
    gi.hasConstVal = true; gi.type = BType::char_; gi.cVal = l->v; return true;
  }
  if (auto l = dynamic_cast<StrLit*>(e)) {
    gi.hasConstVal = true; gi.type = BType::str; gi.sVal = l->v; return true;
  }
  if (auto s = dynamic_cast<SizeofExpr*>(e)) {


    BType tt = fixType(s->target);
    int32_t sz = fieldByteWidth(tt);
    if (sz <= 0) sz = 1;
    s->size = sz;
    gi.hasConstVal = true; gi.type = BType::i64; gi.iVal = (int64_t)sz; return true;
  }
  if (auto v = dynamic_cast<VarRef*>(e)) {

    auto it = globals.find(v->name);
    if (it != globals.end() && it->second.hasConstVal) {
      gi = it->second;
      gi.type = it->second.type;
      return true;
    }
    return false;
  }
  if (auto u = dynamic_cast<UnaryExpr*>(e)) {
    GlobalInfo tmp{};
    if (u->op == UnaryExpr::Op::neg) {
      if (foldConstExpr(u->base.get(), tmp)) {
        gi = tmp;
        if (gi.type == BType::f64) gi.fVal = -gi.fVal;
        else gi.iVal = -gi.iVal;
        return true;
      }
    }
    if (u->op == UnaryExpr::Op::bnot) {
      if (foldConstExpr(u->base.get(), tmp)) { gi = tmp; gi.iVal = ~gi.iVal; return true; }
    }
    if (u->op == UnaryExpr::Op::not_) {
      if (foldConstExpr(u->base.get(), tmp)) { gi = tmp; gi.bVal = !gi.bVal; return true; }
    }
    return false;
  }
  if (auto b = dynamic_cast<BinaryExpr*>(e)) {
    GlobalInfo l{}, r{};

    if (!foldConstExpr(b->lhs.get(), l) || !foldConstExpr(b->rhs.get(), r)) return false;
    if (isInt(l.type) && isInt(r.type)) {
      int64_t lv = l.iVal, rv = r.iVal;
      int64_t out = 0; bool isCmp = false; bool cv = false;
      switch (b->op) {
        case BinaryExpr::Op::add: out = lv + rv; break;
        case BinaryExpr::Op::sub: out = lv - rv; break;
        case BinaryExpr::Op::mul: out = lv * rv; break;
        case BinaryExpr::Op::div: out = rv ? lv / rv : 0; break;
        case BinaryExpr::Op::mod: out = rv ? lv % rv : 0; break;
        case BinaryExpr::Op::band: out = lv & rv; break;
        case BinaryExpr::Op::bor: out = lv | rv; break;
        case BinaryExpr::Op::bxor: out = lv ^ rv; break;
        case BinaryExpr::Op::shl: out = lv << rv; break;
        case BinaryExpr::Op::shr: out = lv >> rv; break;
        case BinaryExpr::Op::eq: cv = lv == rv; isCmp = true; break;
        case BinaryExpr::Op::ne: cv = lv != rv; isCmp = true; break;
        case BinaryExpr::Op::lt: cv = lv < rv; isCmp = true; break;
        case BinaryExpr::Op::gt: cv = lv > rv; isCmp = true; break;
        case BinaryExpr::Op::le: cv = lv <= rv; isCmp = true; break;
        case BinaryExpr::Op::ge: cv = lv >= rv; isCmp = true; break;
        default: return false;
      }
      gi = l;
      if (isCmp) { gi.hasConstVal = true; gi.bVal = cv; gi.type = BType::bool_; }
      else gi.iVal = out;
      return true;
    }
    if (l.type == BType::f64 && r.type == BType::f64) {
      double lv = l.fVal, rv = r.fVal, out = 0; bool isCmp = false; bool cv = false;
      switch (b->op) {
        case BinaryExpr::Op::add: out = lv + rv; break;
        case BinaryExpr::Op::sub: out = lv - rv; break;
        case BinaryExpr::Op::mul: out = lv * rv; break;
        case BinaryExpr::Op::div: out = rv ? lv / rv : 0; break;
        case BinaryExpr::Op::mod: out = std::fmod(lv, rv); break;
        case BinaryExpr::Op::eq: cv = lv == rv; isCmp = true; break;
        case BinaryExpr::Op::ne: cv = lv != rv; isCmp = true; break;
        case BinaryExpr::Op::lt: cv = lv < rv; isCmp = true; break;
        case BinaryExpr::Op::gt: cv = lv > rv; isCmp = true; break;
        case BinaryExpr::Op::le: cv = lv <= rv; isCmp = true; break;
        case BinaryExpr::Op::ge: cv = lv >= rv; isCmp = true; break;
        default: return false;
      }
      gi = l;
      if (isCmp) { gi.hasConstVal = true; gi.bVal = cv; gi.type = BType::bool_; }
      else gi.fVal = out;
      return true;
    }
    if ((l.type == BType::str && r.type == BType::str) ||
        (l.type == BType::str && r.type == BType::char_) ||
        (l.type == BType::char_ && r.type == BType::str)) {
      if (b->op != BinaryExpr::Op::add) return false;
      std::string ls = (l.type == BType::char_) ? std::string(1, (char)l.cVal) : l.sVal;
      std::string rs = (r.type == BType::char_) ? std::string(1, (char)r.cVal) : r.sVal;
      gi.hasConstVal = true; gi.type = BType::str; gi.sVal = ls + rs; return true;
    }


    if (l.type == BType::str && r.type == BType::str) {
      int c = l.sVal.compare(r.sVal);
      bool cv = false;
      switch (b->op) {
        case BinaryExpr::Op::eq: cv = (c == 0); break;
        case BinaryExpr::Op::ne: cv = (c != 0); break;
        case BinaryExpr::Op::lt: cv = (c < 0); break;
        case BinaryExpr::Op::gt: cv = (c > 0); break;
        case BinaryExpr::Op::le: cv = (c <= 0); break;
        case BinaryExpr::Op::ge: cv = (c >= 0); break;
        default: return false;
      }
      gi.hasConstVal = true; gi.type = BType::bool_; gi.bVal = cv; return true;
    }
    return false;
  }
  if (auto c = dynamic_cast<CastExpr*>(e)) {
    GlobalInfo tmp{};
    if (!foldConstExpr(c->e.get(), tmp)) return false;

    if (isInt(tmp.type) && isInt(c->target)) { gi = tmp; gi.type = c->target; gi.iVal = (int64_t)tmp.iVal; return true; }


    if (isFloat(tmp.type) && isFloat(c->target)) {
      gi = tmp; gi.type = c->target;
      if (c->target == BType::f32) gi.fVal = (double)(float)gi.fVal;
      return true;
    }

    if (isInt(tmp.type) && isFloat(c->target)) { gi = tmp; gi.type = c->target; gi.fVal = (double)tmp.iVal; if (c->target == BType::f32) gi.fVal = (double)(float)gi.fVal; return true; }

    if (isFloat(tmp.type) && isInt(c->target)) { gi = tmp; gi.type = c->target; gi.iVal = (int64_t)tmp.fVal; return true; }
    if (tmp.type == BType::char_ && isInt(c->target)) { gi = tmp; gi.type = c->target; gi.iVal = (int64_t)tmp.cVal; return true; }
    return false;
  }
  return false;
}
static int editDist(const std::string& a, const std::string& b) {
  size_t n = a.size(), m = b.size();
  std::vector<int> prev(m + 1), cur(m + 1);
  for (size_t j = 0; j <= m; j++) prev[j] = (int)j;
  for (size_t i = 1; i <= n; i++) {
    cur[0] = (int)i;
    for (size_t j = 1; j <= m; j++) {
      int cost = (a[i-1] == b[j-1]) ? 0 : 1;
      cur[j] = std::min(std::min(prev[j] + 1, cur[j-1] + 1), prev[j-1] + cost);
    }
    prev = cur;
  }
  return prev[m];
}
static std::string suggest(const std::string& name, const std::vector<std::string>& cands) {
  std::string best; int bestD = 1000;
  for (auto& c : cands) {
    if (c.empty()) continue;
    int d = editDist(name, c);
    if (d < bestD) { bestD = d; best = c; }
  }
  if (best.empty() || bestD > (int)name.size() / 2 + 2) return "";
  return best;
}


static bool isIntLitExpr(Expr* e) {
  if (dynamic_cast<IntLit*>(e)) return true;

  if (auto u = dynamic_cast<UnaryExpr*>(e))
    return u->op == UnaryExpr::Op::neg && isIntLitExpr(u->base.get());
  return false;
}


static BType coerceIntLit(Expr* e, BType litType, BType partnerType) {
  if (isIntLitExpr(e) && isInt(partnerType)) return partnerType;
  return litType;
}


static bool isFloatLitExpr(Expr* e) {
  if (dynamic_cast<FloatLit*>(e)) return true;
  if (auto u = dynamic_cast<UnaryExpr*>(e))
    return u->op == UnaryExpr::Op::neg && isFloatLitExpr(u->base.get());
  return false;
}
static BType coerceFloatLit(Expr* e, BType litType, BType partnerType) {
  if (isFloatLitExpr(e) && isFloat(partnerType)) return partnerType;
  return litType;
}


static bool litAssignable(Expr* e, BType fromType, BType toType) {
  if (fromType == toType) return true;
  if (isInt(toType) && isIntLitExpr(e)) return true;
  if (toType == BType::f64 && dynamic_cast<FloatLit*>(e)) return true;


  if (toType == BType::f32 && dynamic_cast<FloatLit*>(e)) return true;
  if (toType == BType::bool_ && dynamic_cast<BoolLit*>(e)) return true;
  if (toType == BType::char_ && dynamic_cast<CharLit*>(e)) return true;
  if (toType.tag == BType::Tag::ptr && dynamic_cast<NullLit*>(e)) return true;
  return false;
}


static BType pickCommonType(Expr* thenE, BType thenT, Expr* elseE, BType elseT,
                            std::vector<SemanticError>& errs, int line) {
  if (thenT == elseT) return thenT;
  if (litAssignable(thenE, thenT, elseT)) return elseT;
  if (litAssignable(elseE, elseT, thenT)) return thenT;
  errAt(errs, line, "ternary arms must have the same type (got " +
        typeSpelling(thenT) + " and " + typeSpelling(elseT) + ")");
  return thenT;
}


static bool isLvalueExpr(Expr* e) {
  if (dynamic_cast<VarRef*>(e)) return true;
  if (dynamic_cast<Index*>(e)) return true;
  if (dynamic_cast<Field*>(e)) return true;
  if (auto u = dynamic_cast<UnaryExpr*>(e)) return u->op == UnaryExpr::Op::deref;
  return false;
}


bool Sema::canTouchPrivate(const std::string& structName) {
  return !curImpl_.empty() && curImpl_ == structName;
}


static const char* opMethodName(BinaryExpr::Op op) {
  switch (op) {
    case BinaryExpr::Op::add: return "__add";
    case BinaryExpr::Op::sub: return "__sub";
    case BinaryExpr::Op::mul: return "__mul";
    case BinaryExpr::Op::div: return "__div";
    case BinaryExpr::Op::mod: return "__mod";
    case BinaryExpr::Op::eq:  return "__eq";
    case BinaryExpr::Op::ne:  return "__eq";
    case BinaryExpr::Op::lt:  return "__lt";
    case BinaryExpr::Op::le:  return "__le";
    case BinaryExpr::Op::gt:  return "__gt";
    case BinaryExpr::Op::ge:  return "__ge";
    case BinaryExpr::Op::band:return "__band";
    case BinaryExpr::Op::bor: return "__bor";
    case BinaryExpr::Op::bxor:return "__bxor";
    case BinaryExpr::Op::shl: return "__shl";
    case BinaryExpr::Op::shr: return "__shr";
    default: return "";
  }
}

static const char* opIMethodName(BinaryExpr::Op op) {
  switch (op) {
    case BinaryExpr::Op::add: return "__iadd";
    case BinaryExpr::Op::sub: return "__isub";
    case BinaryExpr::Op::mul: return "__imul";
    case BinaryExpr::Op::div: return "__idiv";
    case BinaryExpr::Op::mod: return "__imod";
    default: return "";
  }
}


const MethodInfo* Sema::resolveOverload(const std::string& sn,
                                        BinaryExpr::Op op, Expr* b) {
  (void)b;
  auto it = methods.find(sn);
  if (it == methods.end()) return nullptr;
  const char* nm = opMethodName(op);
  if (!nm || !nm[0]) return nullptr;
  auto mit = it->second.find(nm);
  if (mit == it->second.end()) return nullptr;
  return &mit->second;
}


void Sema::registerMethod(const std::string& structName, FuncDecl& fn) {
  BType st; st.tag = BType::Tag::struct_; st.structName = structName;
  std::string mangled = mangleMethod(structName, fn.name);
  MethodInfo mi;
  mi.retType = fn.retType;
  mi.hasSelf = fn.hasSelf;
  mi.selfByRef = fn.selfByRef;
  mi.implStruct = structName;
  mi.mangled = mangled;
  // Virtuals: a `virtual fn foo` declares a NEW vtable slot in this struct;
  // `override fn foo` reuses the matching base slot. The slot list itself is
  // built in a separate pass over impls (after all methods are registered, so
  // the base chain's slots exist). Here we just remember whether this method
  // participates in virtual dispatch (stored on MethodInfo for IRGen).
  mi.isVirtual = fn.isVirtual || fn.isOverride;
  mi.isOverride = fn.isOverride;
  for (auto& p : fn.params) mi.paramTypes.push_back(p.type);
  if (methods.count(structName) && methods[structName].count(fn.name)) {
    errAt(errs, fn.line, "redefinition of method '" + fn.name +
          "' in impl '" + structName + "'");
  }
  methods[structName][fn.name] = mi;
  FuncSig sig;
  sig.isExtern = false;
  sig.retType = fn.retType;
  applyEffects(sig, fn.effectsExplicit, fn.effects);  // effect system  -  copy + dedup

  if (fn.hasSelf)
    sig.paramTypes.push_back(fn.selfByRef ? makePtr(st) : st);
  for (auto& p : fn.params) sig.paramTypes.push_back(p.type);
  funcs[mangled] = sig;
  // Defaults for method/associated-fn calls are keyed by the mangled symbol the
  // call resolves to (mi.mangled). `fn.params` matches mi.paramTypes exactly
  // (both exclude self), so slot indices line up with the call-site args.
  registerDefaults(mangled, fn.params);
}

void Sema::registerMethodPublic(const std::string& structName, FuncDecl& fn) {
  registerMethod(structName, fn);
}

const MethodInfo* Sema::resolveMethod(const std::string& structName,
                                      const std::string& methodName) const {
  // Walk the single-inheritance base chain: most-derived match wins, then
  // base, then base's base, etc. This makes inherited (non-overridden) base
  // methods directly callable on a derived receiver.
  for (const StructDef* d = findStruct(structName); d; d = d->base) {
    auto it = methods.find(d->name);
    if (it != methods.end()) {
      auto mit = it->second.find(methodName);
      if (mit != it->second.end()) return &mit->second;
    }
  }
  return nullptr;
}


// Find the MethodInfo for a method defined DIRECTLY in struct `sn` (not
// inherited)  -  null if `sn` has no own method of that name. Used by vtable
// resolution to inspect the per-impl declarations (virtual/override + the slot
// it declares or reuses), distinct from resolveMethod (most-derived inherited).
static MethodInfo* ownMethod(Sema* self, const std::string& sn,
                              const std::string& methodName) {
  auto it = self->methods.find(sn);
  if (it == self->methods.end()) return nullptr;
  auto mit = it->second.find(methodName);
  if (mit == it->second.end()) return nullptr;
  return &mit->second;
}

// Recursively build StructDef::vtableSlots for `sn` (base's slots first, then
// this struct's own newly-declared virtuals in impl-declaration order), set
// hasVirtuals (already may be set by up-propagation), and patch each virtual
// or override method's MethodInfo::vtableSlot to its slot index. Memoized via
// the slot list being non-empty only after building (a struct with zero
// virtuals and a polymorphic-less base keeps an empty list  -  so we guard with
// `vtBuilt_`). Returns true if `sn` is polymorphic (hasVirtuals).
void Sema::resolveVtables(const Program& prog) {
  (void)prog;

  // First pass: up-propagate hasVirtuals along the base chain. A struct that
  // declares a `virtual fn` forces every base to hasVirtuals too  -  so the single
  // shared vtable ptr lives at offset 0 of the polymorphic ROOT and
  // &Derived -> &Base stays an offset-0 bitcast (single-inheritance invariant).
  for (auto& im : prog.impls) {
    StructDef* sd = findStruct(im->structName);
    if (!sd) continue;
    for (auto& m : im->methods)
      if (m->isVirtual) sd->hasVirtuals = true;
  }
  for (auto& im : prog.impls) {
    StructDef* sd = findStruct(im->structName);
    if (!sd || !sd->hasVirtuals) continue;
    for (StructDef* b = sd->base; b; b = b->base) b->hasVirtuals = true;
  }
  // Down-propagate: a derived struct whose BASE hasVirtuals is itself
  // polymorphic  -  it inherits the base's __oxvt vtable-ptr slot (at offset 0)
  // and dispatches inherited/overridden virtuals through it. Without this,
  // an override-only derived (e.g. `struct Cat: Animal {}` with `override fn
  // speak`) never had hasVirtuals set above (only `virtual fn` triggers pass 1,
  // and the walk at line 872 only goes UP to bases), so neither
  // recomputeVtableLayout nor emitVtables ever processed it: its layout dropped
  // the __oxvt field (corrupting field 0) and IRGen emitted a store of the
  // never-defined @__oxvt_<Derived> global. Iterate to a fixed point so a
  // multi-level chain (D: C: B: A where only A is virtual) propagates end-to-
  // end regardless of prog.structs ordering.
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto& sd : prog.structs) {
      if (sd->isGeneric) continue;
      StructDef* d = findStruct(sd->name);
      if (!d || d->hasVirtuals) continue;
      if (d->base && d->base->hasVirtuals) { d->hasVirtuals = true; changed = true; }
    }
  }

  // Order polymorphic structs base-first (roots before derived) so each struct's
  // vtableSlots can inherit its base's already-built slots. Single inheritance ⇒
  // depth = base-chain length gives a valid order. Iterate to a fixed point to
  // be robust to any prog.structs ordering.
  std::vector<StructDef*> order;
  for (auto& sd : prog.structs) {
    if (sd->isGeneric) continue;
    StructDef* d = findStruct(sd->name);
    if (d && d->hasVirtuals) order.push_back(d);
  }
  auto depth = [](StructDef* d) {
    int n = 0; for (StructDef* b = d->base; b; b = b->base) n++; return n;
  };
  std::stable_sort(order.begin(), order.end(),
    [&](StructDef* a, StructDef* b) { return depth(a) < depth(b); });

  for (StructDef* sd : order) {
    const std::string& sn = sd->name;
    sd->vtableSlots.clear();
    if (sd->base) {
      for (const std::string& s : sd->base->vtableSlots) sd->vtableSlots.push_back(s);
    }
    // Append this struct's OWN newly-declared virtuals in impl-declaration order.
    for (auto& im : prog.impls) {
      if (im->structName != sn) continue;
      for (auto& m : im->methods) {
        if (m->isVirtual && !m->isOverride) {
          if (m->hasSelf && !m->selfByRef) {
            errAt(errs, m->line, "virtual method '" + m->name + "' must use `&self` "
                  "(a by-value `self` virtual would move the receiver before dispatch)");
            continue;
          }
          bool dup = false;
          for (const std::string& s : sd->vtableSlots) if (s == m->name) { dup = true; break; }
          if (dup) {
            errAt(errs, m->line, "virtual method '" + m->name +
                  "' redeclares an existing vtable slot in '" + sn + "'");
            continue;
          }
          sd->vtableSlots.push_back(m->name);
          int slot = (int)sd->vtableSlots.size() - 1;
          if (MethodInfo* mi = ownMethod(this, sn, m->name)) mi->vtableSlot = slot;
        } else if (m->isOverride) {
          // An override MUST reuse an existing inherited slot of the same name
          // with a matching signature (params + ret, self excluded). It adds NO
          // new slot. Find the nearest base that owns a virtual of this name.
          const StructDef* bd = sd->base;
          bool found = false;
          while (bd) {
            if (ownMethod(this, bd->name, m->name) &&
                methods.at(bd->name).at(m->name).isVirtual) { found = true; break; }
            bd = bd->base;
          }
          if (!found) {
            errAt(errs, m->line, "override method '" + m->name +
                  "' in '" + sn + "' has no matching base virtual to override");
            continue;
          }
          const MethodInfo& bmi = methods.at(bd->name).at(m->name);
          if (m->hasSelf && !m->selfByRef) {
            errAt(errs, m->line, "override method '" + m->name + "' must use `&self`");
            continue;
          }
          bool sigOk = (m->retType == bmi.retType) &&
                       (m->params.size() == bmi.paramTypes.size());
          for (size_t k = 0; k < m->params.size() && sigOk; k++)
            if (!(m->params[k].type == bmi.paramTypes[k])) sigOk = false;
          // The override's `self`-by-ref-ness must also match a &self base
          // virtual (all virtuals are &self per the rule above, so just check).
          if (!sigOk) {
            errAt(errs, m->line, "override '" + m->name + "' signature does not match "
                  "the base virtual (params/return type mismatch)");
            continue;
          }
          if (MethodInfo* mi = ownMethod(this, sn, m->name)) {
            for (int i = 0; i < (int)sd->vtableSlots.size(); i++)
              if (sd->vtableSlots[i] == m->name) { mi->vtableSlot = i; break; }
          }
        }
      }
    }
  }
}

// Synthetic vtable-pointer field name. Marked so every user-facing field
// iteration (StructLit missing-field check, field collision check, Sema
// field-type validators) can recognize and skip it  -  the user never references
// the vtable slot by name; IRGen stores it at construction time. Lowered as a
// plain i8* (the vtable global is `[N x i8*]`, of which the object holds a ptr
// to the first element) to keep %struct.T layout simple and 8-byte self-aligned.
static const char* kVtableFieldName = "__oxvt";

void Sema::recomputeVtableLayout(const Program& prog) {
  // Only polymorphic structs need any change; a non-polymorphic struct keeps
  // its pass-1/2 layout byte-for-byte (every existing example/hv/net/GUI is
  // untouched). For a polymorphic struct, prepend a synthetic i8* vtable-ptr
  // field at offset 0 of the baseless ROOT of its inheritance chain; derived
  // structs then inherit that slot via base-splice (single shared vtable ptr,
  // at offset 0 of every object in the chain  -  so &Derived -> &Base stays an
  // offset-0 bitcast). Build base-first so a base's __oxvt exists before a
  // derived's merge reads it.

  // Map struct name -> its own raw declared fields (from the source StructDecl),
  // because StructDef::fields was mutated by layout passes 1+2 (own/merged).
  std::map<std::string, std::vector<StructField>> ownRaw;
  for (auto& sd : prog.structs) {
    if (sd->isGeneric) continue;
    std::vector<StructField> raw;
    for (auto& f : sd->fields) {
      raw.push_back({std::get<0>(f), std::get<1>(f), 0, std::get<2>(f)});
    }
    ownRaw[sd->name] = std::move(raw);
  }

  // Depth order (roots before derived). Single inheritance => depth = chain len.
  auto depth = [](StructDef* d) {
    int n = 0; for (StructDef* b = d->base; b; b = b->base) n++; return n;
  };
  std::vector<StructDef*> polymorphic;
  for (auto& sd : prog.structs) {
    if (sd->isGeneric) continue;
    StructDef* d = findStruct(sd->name);
    if (d && d->hasVirtuals) polymorphic.push_back(d);
  }
  std::stable_sort(polymorphic.begin(), polymorphic.end(),
    [&](StructDef* a, StructDef* b) { return depth(a) < depth(b); });

  for (StructDef* d : polymorphic) {
    // The baseless top of this chain carries the single __oxvt slot at index 0.
    StructDef* root = d;
    while (root->base) root = root->base;
    (void)root;   // only d matters here; the root's __oxvt is added when d==root
    // Recompute this struct's merged fields base-first.
    std::vector<StructField> merged;
    int32_t baseEnd = 0;
    int32_t align = 1;
    if (d->base) {
      // Base sub-object: its fields already include __oxvt at index 0 (up-
      // propagation marked the whole chain polymorphic, and the base was
      // processed earlier in this loop). Use the base's final merged fields.
      for (const StructField& bf : d->base->fields) merged.push_back(bf);
      baseEnd = d->base->size;
      if (d->base->align > align) align = d->base->align;
    } else {
      // d IS the baseless root: prepend the synthetic vtable-ptr field here.
      BType vt = makePtr(BType::u8);
      StructField vf; vf.name = kVtableFieldName; vf.type = vt; vf.offset = 0;
      merged.push_back(vf);
      baseEnd = 8;   // i8* is 8 bytes, 8-aligned
      if (baseEnd > align) align = baseEnd;
    }
    // Own fields (shifted past the base sub-object [+ __oxvt for the root]).
    const std::vector<StructField>& raw = ownRaw[d->name];
    int32_t off = baseEnd;
    for (const StructField& rf : raw) {
      int32_t w = fieldByteWidth(rf.type);
      int32_t a = fieldAlign(rf.type);
      if (a > align) align = a;
      off = (off + a - 1) & ~(a - 1);
      StructField f = rf;
      f.offset = off;
      merged.push_back(f);
      off += w;
    }
    int32_t total = (off + align - 1) & ~(align - 1);
    d->fields = std::move(merged);
    d->size = total;
    d->align = align;
  }
}



// Instantiate a specific generic-fn TEMPLATE at the given (already-filled)
// args. `name` is the call symbol used for mangling, which for an overloaded
// generic is the SAME across all its templates (so every overload produces a
// distinct mangled symbol via its args). This flavour takes the template
// pointer directly (from selectGenericFn) so a constrained OVERLOAD instantiates
// the CHOSEN template, not whichever won the single-template registry race.
std::string Sema::instantiateGenericFnDecl(const std::string& name, const FuncDecl* tmpl,
                                           const std::vector<BType>& args) {
  if (!tmpl) return "";
  std::vector<BType> filledArgs = args;
  if (!tmpl->tparams.empty()) filledArgs = fillDefaultTypeArgs(tmpl->tparams, std::move(filledArgs));
  if (filledArgs.size() != tmpl->typeParams.size()) return "";
  std::string mangled = mangleInst(name, filledArgs);
  if (funcs.count(mangled)) return mangled;

  std::map<std::string, BType> env;
  for (size_t i = 0; i < tmpl->typeParams.size() && i < filledArgs.size(); i++)
    env[tmpl->typeParams[i]] = filledArgs[i];

  auto clone = std::make_unique<FuncDecl>();
  clone->name = mangled;
  clone->isExtern = false;
  clone->isGeneric = false;
  clone->line = tmpl->line;
  clone->retType = fixType(substitute(tmpl->retType, env));
  for (auto& p : tmpl->params) {
    Param np; np.name = p.name; np.type = fixType(substitute(p.type, env));
    // Generic instantiations do not carry default arguments (defaults are a
    // property of the concrete call signature; expanding a generic with a
    // defaulted param is an edge case left unsupported). Param is move-only.
    np.hasDefault = false;
    clone->params.push_back(std::move(np));
  }


  for (auto& s : tmpl->body) {
    StmtPtr c = cloneStmt(s.get());
    substTypesInStmt(c.get(), env);
    clone->body.push_back(std::move(c));
  }
  // Clone + substitute the contract clauses so an instantiated generic keeps its
  // requires/ensures (with type-params substituted by the concrete args).
  for (auto& r : tmpl->requires_) { auto c = cloneExpr(r.get()); substTypesInExpr(c.get(), env); clone->requires_.push_back(std::move(c)); }
  for (auto& e : tmpl->ensures_) { auto c = cloneExpr(e.get()); substTypesInExpr(c.get(), env); clone->ensures_.push_back(std::move(c)); }

  FuncSig sig; sig.isExtern = false;
  sig.retType = clone->retType;
  for (auto& p : clone->params) sig.paramTypes.push_back(p.type);
  applyEffects(sig, clone->effectsExplicit, clone->effects);  // effect system  -  propagate to instantiation
  funcs[mangled] = sig;
  FuncDecl* raw = clone.get();
  monomorphFns.push_back(std::move(clone));
  (void)raw;
  return mangled;
}

// Name-based flavour: pick the template via the single-template registry (the
// FIRST registered for the name). Use this only when no overload selection is
// needed; the call-site path uses instantiateGenericFnDecl with a `chosen`
// template from selectGenericFn for the constrained-overload case.
std::string Sema::instantiateGenericFn(const std::string& name, const std::vector<BType>& args) {
  const FuncDecl* tmpl = findGenericFn(name);
  return instantiateGenericFnDecl(name, tmpl, args);
}


// Fill trailing missing type args from a template's `tparams` defaults (C++-
// style `template <typename T = i64>`): if `args` is shorter than `tparams`,
// append `fixType(tparams[i].defaultType)` for each missing trailing param that
// has a default. A missing param without a default is left unfilled  -  the
// existing arity check (comparing the filled size to tparams.size()) then emits
// the same arity-mismatch error it did before. The returned vector is the
// (possibly lengthened) arg list, suitable for env building and mangling.
std::vector<BType> Sema::fillDefaultTypeArgs(const std::vector<TypeParam>& tparams,
                                             std::vector<BType> args) const {
  if (args.size() < tparams.size()) {
    for (size_t i = args.size(); i < tparams.size(); i++) {
      if (!tparams[i].hasDefault) break;   // leave the rest for the arity check
      args.push_back(fixType(tparams[i].defaultType));
    }
  }
  return args;
}

// Predicate: does `typeArg` satisfy `conceptName`? A concept is a named set of
// required method / associated-fn signatures (see ConceptDef in AST.h). The
// candidate must be a struct_ type  -  its impl methods (looked up via
// resolveMethod, which walks the single-inheritance base chain) must provide a
// matching signature for every req. Exact param-type + retType match is required
// (with implicitAssignable as a fallback for pointer-derivable and int-lit
// cases); this is intentionally simple  -  concept reqs are expected to be
// concrete signatures like `fn len(&self) -> usize`, not `Self`-parameterised.
// Scalar / enum / pointer candidates have no impls, so a concept with any
// hasSelf method req is NEVER satisfied by them (a concept with only zero-arity
// associated fn reqs would be trivially satisfied; we don't special-case it  - 
// the call site is left to handle a concept with no hasSelf req if needed).
bool Sema::satisfies(const BType& typeArg, const std::string& conceptName) {
  if (conceptName.empty()) return true;   // unconstrained param: always ok
  ConceptDef* c = findConceptDef(conceptName);
  if (!c) return false;                    // unknown concept: caller reports
  if (c->reqs.empty()) return true;       // vacuous concept: any type satisfies
  // Only struct_ types can have impl methods, so non-struct candidates fail
  // any concept that has at least one hasSelf method req.
  if (typeArg.tag != BType::Tag::struct_) {
    for (auto& r : c->reqs) if (r.hasSelf) return false;
    // A concept with only associated-fn reqs (no hasSelf)  -  we don't expose
    // associated fns on non-struct types. Treat as unsatisfied for simplicity.
    return false;
  }
  const std::string& sn = typeArg.structName;
  for (const ConceptReq& r : c->reqs) {
    const MethodInfo* mi = resolveMethod(sn, r.name);
    if (!mi) return false;
    // Arity (NOT counting the receiver). For !hasSelf reqs we still allow a
    // matching hasSelf method with zero params  -  a degenerate but harmless case.
    if (mi->paramTypes.size() != r.params.size()) return false;
    // Receiver shape: if the req declares `fn foo(&self)`, the method must take
    // self by reference; `fn foo(self)` requires by-value. We don't strictly
    // enforce self-shape here (the existing impl machinery already validated
    // the method at registration); leaving it loose keeps more candidates valid.
    (void)r.selfByRef;
    if (!(mi->retType == r.retType) &&
        !implicitAssignable(mi->retType, r.retType)) return false;
    for (size_t i = 0; i < r.params.size(); i++) {
      const BType& want = r.params[i];
      const BType& got = mi->paramTypes[i];
      if (got == want) continue;
      if (implicitAssignable(got, want)) continue;   // base->derived ptr etc.
      return false;
    }
  }
  return true;
}

// Validate every constrained type-param of a template against the env (type-
// param name -> concrete arg). Called from the StructLit path and the generic-
// fn call path after defaults are filled + the env is built. Emits an error
// per violated constraint and returns false if any failed (kept going so the
// caller still monomorphises with the offending arg and errors keep coming).
bool Sema::checkConstraints(const std::vector<TypeParam>& tparams,
                            const std::map<std::string, BType>& env,
                            int line, int col) {
  bool ok = true;
  for (const TypeParam& tp : tparams) {
    if (tp.constraint.empty()) continue;
    auto it = env.find(tp.name);
    if (it == env.end()) continue;   // no binding -> arity check will fire
    if (!satisfies(it->second, tp.constraint)) {
      ConceptDef* c = findConceptDef(tp.constraint);
      std::string cText = c ? ("'" + tp.constraint + "'") : ("'" + tp.constraint +
                            "' (unknown concept)");
      errAt(errs, line, col, "type argument for '" + tp.name + "' does not satisfy " +
            "concept " + cText + " (got " + typeSpelling(it->second) + ")",
            "add the required impl methods, or relax the constraint");
      ok = false;
    }
  }
  return ok;
}

// Pick the right generic-fn template for a name across constrained overloads.
// `genericFnOverloads_` holds the per-name templates in source order; the FIRST
// whose constraints all pass (against inferredArgs) wins. If only one template
// is registered for the name, behaves exactly as before. Returning null here
// means "none of the overloads' constraints accept these args".
const FuncDecl* Sema::selectGenericFn(const std::string& name,
                                      const std::vector<BType>& inferredArgs) {
  auto it = genericFnOverloads_.find(name);
  if (it == genericFnOverloads_.end() || it->second.empty()) return nullptr;
  if (it->second.size() == 1) return it->second.front();
  for (const FuncDecl* tmpl : it->second) {
    // Build this template's env from the (already-filled) args, then check its
    // constraints. If every constrained param is satisfied or unconstrained,
    // this is the template to use.
    std::map<std::string, BType> env;
    size_t n = std::min(tmpl->typeParams.size(), inferredArgs.size());
    for (size_t i = 0; i < n; i++) env[tmpl->typeParams[i]] = inferredArgs[i];
    bool allOk = true;
    for (const TypeParam& tp : tmpl->tparams) {
      if (tp.constraint.empty()) continue;
      auto eit = env.find(tp.name);
      if (eit == env.end()) { allOk = false; break; }
      if (!satisfies(eit->second, tp.constraint)) { allOk = false; break; }
    }
    if (allOk) return tmpl;
  }
  return nullptr;
}


// Validate + store the default initialisers on a function's params. Enforces
// the C++ rule that defaulted params are TRAILING (a non-defaulted param may
// not follow a defaulted one) so positional call filling is unambiguous. The
// per-default TYPE check is deferred to checkStoredDefaults() (run after
// globals are registered), because a default may reference a const global that
// is not yet folded at function-registration time. The original Expr stays owned
// by the FuncDecl; we store independent clones so repeated call sites each get
// their own copy to append + re-check.
void Sema::registerDefaults(const std::string& sym, const std::vector<Param>& params) {
  bool seenDefault = false;
  bool anyDefault = false;
  DefaultSet ds;
  ds.types.reserve(params.size());
  ds.exprs.reserve(params.size());
  for (size_t i = 0; i < params.size(); i++) {
    const Param& p = params[i];
    ds.types.push_back(p.type);
    if (p.hasDefault) {
      seenDefault = true;
      anyDefault = true;
      ds.exprs.push_back(cloneExpr(p.defaultExpr.get()));
    } else {
      if (seenDefault) {
        errAt(errs, 0, 0,
              "parameter '" + p.name + "' has no default but follows a defaulted "
              "parameter; defaulted parameters must be trailing");
      }
      ds.exprs.push_back(nullptr);
    }
  }
  if (anyDefault) defaultArgs_[sym] = std::move(ds);
}

// Type-check every stored default in an empty local scope (only globals/consts
// are visible). Runs after globals are registered so a default may reference a
// const global. The same check is re-applied to each appended clone at every
// call site (in the per-arg loop); this pass guarantees that defaults of an
// uncalled defaulted function are still validated.
void Sema::checkStoredDefaults() {
  for (auto& kv : defaultArgs_) {
    const std::string& sym = kv.first;
    DefaultSet& ds = kv.second;
    for (size_t k = 0; k < ds.exprs.size(); k++) {
      ExprPtr& d = ds.exprs[k];
      if (!d) continue;
      pushScope();
      BType dt = checkExpr(d.get());
      popScope();
      BType pt = ds.types[k];
      bool ok = (dt == pt) || implicitAssignable(dt, pt) ||
                litAssignable(d.get(), dt, pt) ||
                (dynamic_cast<CastExpr*>(d.get()) != nullptr);
      if (!ok && pt.tag == BType::Tag::ptr && dt == pointee(pt)) ok = true;
      if (!ok)
        errAt(errs, d->line, d->col,
              "default argument " + std::to_string(k + 1) + " of '" + sym +
              "' has the wrong type (expected " + typeSpelling(pt) + ")");
    }
  }
}

bool Sema::hasAnyDefault(const std::string& sym) const {
  auto it = defaultArgs_.find(sym);
  if (it == defaultArgs_.end()) return false;
  for (auto& d : it->second.exprs) if (d) return true;
  return false;
}

// Append clones of the trailing defaults to `args` until it reaches `nparams`.
// Returns true if every missing slot had a default (call is well-formed). On a
// missing slot with no default, returns false WITHOUT filling  -  the caller then
// reports the arity mismatch itself. `what` labels the error source.
bool Sema::fillDefaultArgs(const std::string& sym, std::vector<ExprPtr>& args,
                           size_t nparams, int line, int col, const std::string& what) {
  if (args.size() >= nparams) return true;  // no fill needed
  auto it = defaultArgs_.find(sym);
  if (it == defaultArgs_.end()) return false;
  const std::vector<ExprPtr>& defs = it->second.exprs;
  for (size_t k = args.size(); k < nparams; k++) {
    if (k < defs.size() && defs[k]) {
      args.push_back(cloneExpr(defs[k].get()));
    } else {
      errAt(errs, line, col, what + " expects " +
            std::to_string(nparams) + " args, got " + std::to_string(args.size()) +
            " (no default for argument " + std::to_string(k + 1) + ")");
      return false;
    }
  }
  return true;
}


BType Sema::checkLambda(LambdaLit* lam) {
  std::string sym = "__oxfn_" + std::to_string(++lambdaSeq_);
  lam->retType = fixType(lam->retType);

  // Resolve captures against the enclosing scope BEFORE we wipe scopes. Each
  // named capture becomes a leading parameter on the lowered function:
  //   by-value `[x]`  -> param of type T          (a copy made at the lex site)
  //   by-ref   `[&x]` -> param of type *T         (the local's address is passed)
  lam->captureTypes.clear();
  lam->captureOuterTypes.clear();
  std::vector<BType> capParamTypes;     // what the lowered @__oxfn_N sees first

  // Capture-all: `[=]`/`[&]` (and a trailing `[=, &x]`/`[&, x]` mix) is expanded
  // to an explicit list of every in-scope local, like C++'s default capture. We
  // collect names with a reverse walk (inner scope shadows outer) and skip
  // globals (they are addressable as `@name`, no capture needed) and any name
  // already named explicitly by the user in the same list (the explicit form
  // overrides the default mode for that one variable).
  bool haveDefault = false;
  bool defaultByRef = false;
  std::set<std::string> explicitNames;
  for (auto& cap : lam->captures)
    if (cap.name != "=" && cap.name != "&") explicitNames.insert(cap.name);

  std::vector<LambdaCapture> expanded;
  for (auto& cap : lam->captures) {
    if (cap.name != "=" && cap.name != "&") {
      expanded.push_back(cap);
      continue;
    }
    haveDefault = true;
    defaultByRef = (cap.name == "&");
    // Pull every visible local across all live scopes. Reverse so inner
    // shadows outer; first hit wins, matching the lookup() resolution rule.
    std::set<std::string> seen;
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      for (auto& kv : it->vars) {
        if (kv.second.isGlobal) continue;       // globals need no capture
        if (seen.count(kv.first)) continue;     // shadowed by an inner scope
        seen.insert(kv.first);
        if (explicitNames.count(kv.first)) continue;   // user gave explicit mode
        // An individual name uses the default capture mode; an explicit entry
        // (if any) for the same name already in `expanded` wins.
        expanded.push_back({kv.first, defaultByRef});
      }
    }
  }
  // Preserve order: explicit captures first (as written), then the default
  // sweep. We already appended default-sweep names after each default token,
  // but if explicit captures follow a default token they'd be misordered. Re-
  // sort so all explicit (non-default) entries keep their relative order and
  // all default-swept names come after, deduped.
  if (haveDefault) {
    std::vector<LambdaCapture> ordered;
    std::set<std::string> placed;
    for (auto& c : expanded)
      if (explicitNames.count(c.name)) { ordered.push_back(c); placed.insert(c.name); }
    for (auto& c : expanded)
      if (!explicitNames.count(c.name) && !placed.count(c.name)) {
        ordered.push_back(c); placed.insert(c.name);
      }
    lam->captures = std::move(ordered);
  }

  for (auto& cap : lam->captures) {
    if (cap.name.empty()) continue;
    VarInfo vi = lookup(cap.name);
    if (!vi.found) {
      auto git = globals.find(cap.name);
      if (git != globals.end()) { vi.type = git->second.type; vi.found = true; }
    }
    if (!vi.found) {
      errAt(errs, lam->line, lam->col,
            "lambda capture '" + cap.name + "' is not in scope");
      continue;
    }
    BType ot = vi.type;
    lam->captureOuterTypes.push_back(ot);
    BType pt = cap.byRef ? makePtr(ot) : ot;
    lam->captureTypes.push_back(pt);
    capParamTypes.push_back(pt);
  }

  // Full lowered param list = captures first, then real params.
  std::vector<BType> pts;
  for (auto& p : lam->params) { p.type = fixType(p.type); pts.push_back(p.type); }

  // The *value type* of the lambda:
  //   - non-capturing: a plain fn type (the value is just a function pointer, as
  //     before).
  //   - capturing:     a closure struct __oxclosure_N { fn: *(params)->ret,
  //                                                     cap0: T0, ... } whose
  //     value is constructed at the lex site by an implicit struct literal.
  if (lam->captures.empty()) {
    lam->fnType = makeFnType(pts, lam->retType);
    lam->closureStructName = "";

    FuncSig sig; sig.isExtern = false; sig.retType = lam->retType;
    for (auto& p : lam->params) sig.paramTypes.push_back(p.type);
    funcs[sym] = sig;
    lam->loweredName = sym;

    curFunc_ = &funcs[sym];
    curImpl_.clear();
    std::vector<Scope> saved = scopes_;
    std::set<std::string> savedMoved = movedVars_;
    movedVars_.clear();
    while (scopes_.size() > 1) scopes_.pop_back();
    pushScope();
    for (auto& p : lam->params) declare(p.name, p.type, true, false, false, lam->line);
    checkBlock(lam->body);
    popScope();
    movedVars_ = savedMoved;
    scopes_ = saved;
    curFunc_ = nullptr;
    return lam->fnType;
  }

  // ---- capturing lambda ----
  // Lowered fn signature: ret @sym(*cap0..capN, param0..paramM). Note the fn
  // type recorded for indirect-call arg-counting must reflect BOTH captures and
  // params, BUT callers go through the closure struct path, not the plain
  // fn-ptr path, so we keep fnType describing the closure's .fn field type.
  std::vector<BType> fullParams = capParamTypes;
  for (auto& p : lam->params) fullParams.push_back(p.type);

  FuncSig sig; sig.isExtern = false; sig.retType = lam->retType;
  for (auto& t : fullParams) sig.paramTypes.push_back(t);
  funcs[sym] = sig;
  lam->loweredName = sym;

  // The .fn field type: a fn type taking (cap.., param..) -> ret.
  lam->fnType = makeFnType(fullParams, lam->retType);

  // Synthesize a closure struct:  { fn: <FnType>, cap0: T0, ... }.
  std::string cname = "__oxclosure_" + std::to_string(lambdaSeq_);
  lam->closureStructName = cname;
  StructDef* sd = registerStruct(cname);
  sd->fields.clear();
  sd->fields.push_back({"fn", lam->fnType, 0, false});
  for (size_t i = 0; i < lam->captures.size(); i++)
    sd->fields.push_back({lam->captures[i].name.empty() ? ("_axes" + std::to_string(i)) :
                            lam->captures[i].name,
                          lam->captureTypes[i], 0, false});
  // Recompute layout the same way the struct-decl path does.
  sd->size = 0; sd->align = 1;
  int32_t off = 0;
  for (auto& f : sd->fields) {
    int32_t w = fieldByteWidth(f.type);
    int32_t a = fieldAlign(f.type);
    if (a > sd->align) sd->align = a;
    if (a < 1) a = 1;
    off = (off + a - 1) & ~(a - 1);
    f.offset = off;
    off += w;
  }
  off = (off + sd->align - 1) & ~(sd->align - 1);
  sd->size = off;
  sd->isOpaque = false;

  // Type-check the body in a scope that exposes the captured names and params.
  curFunc_ = &funcs[sym];
  curImpl_.clear();
  std::vector<Scope> saved = scopes_;
  std::set<std::string> savedMoved = movedVars_;
  movedVars_.clear();
  while (scopes_.size() > 1) scopes_.pop_back();
  pushScope();
  // Captures: by-ref binds the name to the pointee type with its address being
  // the passed-in pointer (transparent read/write, like C++ [&, name]). By-value
  // binds the name to the local copy of type T (mutable inside the lambda body).
  for (size_t i = 0; i < lam->captures.size(); i++) {
    if (lam->captures[i].name.empty()) continue;
    BType bindType = lam->captures[i].byRef
                       ? pointee(lam->captureTypes[i])   // body sees T, addr=ptr
                       : lam->captureTypes[i];
    declare(lam->captures[i].name, bindType, true, false, false, lam->line);
  }
  for (auto& p : lam->params) declare(p.name, p.type, true, false, false, lam->line);
  checkBlock(lam->body);
  popScope();
  scopes_ = saved;
  movedVars_ = savedMoved;
  curFunc_ = nullptr;

  // Return value type = the closure struct.
  BType ct; ct.tag = BType::Tag::struct_; ct.structName = cname;
  return ct;
}

// Compile-time macro expansion entry point (called from checkExpr's MacroCall
// arm). Resolves mc->macroName in macroRegistry, clones the body, substitutes
// the $param markers with the caller's args, type-checks the result, and stashes
// the expanded tree on mc->expanded (plus mc->resultTy) for IRGen. Returns the
// expanded type (void_ on error). Nested `expand` inside the macro body
// recurses via the type-check of the substituted tree (its inner MacroCall
// nodes reach checkExpr -> expandMacro again).
BType Sema::expandMacro(MacroCall* mc) {
  const MacroDecl* m = findMacro(mc->macroName);
  if (!m) {
    errAt(errs, mc->line, mc->col, "unknown macro '" + mc->macroName + "'");
    // Still type-check the args (so downstream errors in the args surface too).
    for (auto& a : mc->args) (void)checkExpr(a.get());
    return BType::void_;
  }
  if (!m->bodyExpr) {
    errAt(errs, mc->line, mc->col, "macro '" + mc->macroName + "' has no body");
    return BType::void_;
  }
  // Arity check: the number of caller args must match the formal param count.
  if (mc->args.size() != m->paramNames.size()) {
    errAt(errs, mc->line, mc->col,
          "macro '" + mc->macroName + "' expects " +
          std::to_string(m->paramNames.size()) + " arg(s), got " +
          std::to_string(mc->args.size()));
    for (auto& a : mc->args) (void)checkExpr(a.get());
    return BType::void_;
  }
  // Clone the body and substitute the $param markers (VarRefs named "$" + param)
  // with clones of the caller args. Inner MacroCall nodes survive the clone+walk
  // and re-expand during the type-check below.
  //
  // Macros may have leading `let` bindings in `bodyStmts` (e.g.
  // `macro max3(a,b,c) { let ab = imax($a,$b); imax(ab,$c) }`). We must clone
  // each binding, substitute $params in its init expression, push a scope,
  // type-check the let (which declares the bound name), and stash the
  // substituted stmt on `mc->expandedStmts` so IRGen can emit allocas for it.
  // The trailing `bodyExpr` is then type-checked in that same scope so the
  // VarRefs to the let-bound names resolve.
  pushScope();
  for (const auto& bs : m->bodyStmts) {
    if (!bs) continue;
    StmtPtr cloned = cloneStmt(bs.get());
    if (!cloned) continue;
    // Substitute $param markers inside the cloned stmt's init expression.
    if (auto ls = dynamic_cast<LetStmt*>(cloned.get())) {
      if (ls->init) ls->init = substMacroParams(ls->init.get(), m, mc->args);
    }
    (void)checkStmt(cloned.get());
    mc->expandedStmts.push_back(std::move(cloned));
  }
  mc->expanded = substMacroParams(m->bodyExpr.get(), m, mc->args);
  if (!mc->expanded) {
    errAt(errs, mc->line, mc->col,
          "macro '" + mc->macroName + "' body could not be expanded (internal error)");
    popScope();
    return BType::void_;
  }
  BType ty = checkExpr(mc->expanded.get());
  mc->resultTy = ty;
  popScope();
  return ty;
}

// Recursive substitution: deep-copy `e` (via cloneExpr) and replace every
// `$param` VarRef with a clone of the corresponding arg. cloneExpr already
// copies all the Expr variants Oxide macros can use (binary/unary/call/method/
// index/field/literals/ternary/...), so after cloning we only need to splice in
// the arg clones wherever a VarRef matches a `$param` name. We do it in-place
// on the freshly-cloned tree: cloneExpr returns owning ExprPtr, and we recurse
// over the owned sub-expressions, replacing whole subtrees (not patching in
// place  -  a VarRef inside, e.g., a BinaryExpr's lhs is owned by that BinaryExpr,
// so we reassign its ExprPtr).
ExprPtr Sema::substMacroParams(Expr* e, const MacroDecl* m,
                                const std::vector<ExprPtr>& args) {
  ExprPtr out = cloneExpr(e);
  if (!out) return nullptr;

  // Helper that walks an owned ExprPtr slot and substitutes in place.
  std::function<void(ExprPtr&)> walk = [&](ExprPtr& slot) {
    if (!slot) return;
    // A `$param` VarRef -> the matching arg clone. (cloneExpr already copied
    // the VarRef; we replace the whole node with a fresh clone of the arg.)
    if (auto v = dynamic_cast<VarRef*>(slot.get())) {
      for (size_t k = 0; k < m->paramNames.size(); k++) {
        if (v->name == "$" + m->paramNames[k]) {
          slot = cloneExpr(args[k].get());
          if (slot) { slot->line = v->line; slot->col = v->col; }
          return;
        }
      }
      // A non-`$param` VarRef (e.g. a `let`-bound name inside the macro body)
      // is left as-is; it'll be resolved by the enclosing scope at type-check.
      return;
    }
    // ox:why Otherwise recurse into known sub-expression slots. Recurse over every
    // owned Expr pointer the cloned node carries; if cloneExpr cloned it, we
    // walk it.
    if (auto u = dynamic_cast<UnaryExpr*>(slot.get())) { walk(u->base); return; }
    if (auto c = dynamic_cast<CastExpr*>(slot.get())) { walk(c->e); return; }
    if (auto t = dynamic_cast<TernaryExpr*>(slot.get())) {
      walk(t->cond); walk(t->thenE); walk(t->elseE); return;
    }
    if (auto b = dynamic_cast<BinaryExpr*>(slot.get())) {
      walk(b->lhs); walk(b->rhs); return;
    }
    if (auto c = dynamic_cast<Call*>(slot.get())) {
      walk(c->calleeExpr);
      for (auto& a : c->args) walk(a);
      return;
    }
    if (auto mc = dynamic_cast<MethodCall*>(slot.get())) {
      walk(mc->receiver);
      for (auto& a : mc->args) walk(a);
      return;
    }
    if (auto ac = dynamic_cast<AssocCall*>(slot.get())) {
      for (auto& a : ac->args) walk(a);
      return;
    }
    if (auto ix = dynamic_cast<Index*>(slot.get())) { walk(ix->base); walk(ix->index); return; }
    if (auto fl = dynamic_cast<Field*>(slot.get())) { walk(fl->base); return; }
    if (auto a = dynamic_cast<ArrayLit*>(slot.get())) {
      for (auto& el : a->elems) walk(el);
      return;
    }
    if (auto s = dynamic_cast<StructLit*>(slot.get())) {
      for (auto& v : s->values) walk(v);
      return;
    }
    if (auto r = dynamic_cast<RangeLit*>(slot.get())) { walk(r->lo); walk(r->hi); return; }
    if (auto o = dynamic_cast<OldExpr*>(slot.get())) { walk(o->sub); return; }
    if (auto q = dynamic_cast<QuantExpr*>(slot.get())) {
      walk(q->lo); walk(q->hi); walk(q->body);
      return;
    }
    if (auto mc2 = dynamic_cast<MacroCall*>(slot.get())) {
      // A nested `expand` inside the macro body: leave it intact  -  it re-expands
      // during the type-check of this substituted tree (checkExpr's MacroCall
      // arm calls expandMacro again, with the INNER macro's own args already
      // substituted by this same walker). So just walk its args.
      for (auto& a : mc2->args) walk(a);
      return;
    }
    // Leaves (literals, sizeof, dyn/map/set-new, null, asm) have no sub-expr to
    // walk; their $param-free structure is already cloned.
  };
  walk(out);
  return out;
}

BType Sema::checkExpr(Expr* e) {
  if (auto lam = dynamic_cast<LambdaLit*>(e)) {


    return checkLambda(lam);
  }
  if (auto l = dynamic_cast<IntLit*>(e)) return BType::i64;
  if (auto l = dynamic_cast<FloatLit*>(e)) return l->isF32 ? BType::f32 : BType::f64;
  if (auto l = dynamic_cast<BoolLit*>(e)) return BType::bool_;
  if (auto l = dynamic_cast<StrLit*>(e)) return BType::str;
  if (auto l = dynamic_cast<CharLit*>(e)) return BType::char_;
  if (dynamic_cast<NullLit*>(e)) return makePtr(BType::void_);
  if (auto s = dynamic_cast<SizeofExpr*>(e)) {


    BType tt = fixType(s->target);
    s->target = tt;
    if (tt == BType::void_)
      errAt(errs, s->line, s->col, "sizeof(void) is not allowed");


    if (tt.tag == BType::Tag::struct_ && !findStruct(tt.structName))
      errAt(errs, s->line, s->col,
            "sizeof an unknown type '" + tt.structName + "'",
            "declare the struct before asking for its size");
    if (tt.tag == BType::Tag::struct_ && findStruct(tt.structName) &&
        findStruct(tt.structName)->isOpaque)
      errAt(errs, s->line, s->col,
            "sizeof an opaque struct '" + tt.structName + "' is not meaningful",
            "use the handle pointer type, e.g. sizeof(*" + tt.structName + ")");
    int32_t sz = fieldByteWidth(tt);


    if (sz <= 0) sz = 1;
    s->size = sz;
    return BType::i64;
  }
  if (auto a = dynamic_cast<AsmExpr*>(e)) {


    int outCount = 0;
    for (auto& io : a->ios) {
      BType t = checkExpr(io.val.get());
      io.ty = t;
      if (io.isOutput) {
        // Multi-output asm! support (contract 5): every output / inout target
        // must be an lvalue (assignable memory the asm writes back into). We
        // validate this here so a non-lvalue output like `out("{rax}") 5` is a
        // compile error, not a silent IRGen mis-compile (genAddr would return an
        // empty address and the store would be dropped). Mirrors the lvalue rule
        // used by plain assignment (`x = ...`) and `&<lvalue>`.
        if (!isLvalueExpr(io.val.get()))
          errAt(errs, a->line, a->col,
                "asm! output target must be an assignable lvalue (variable, "
                "index, field, or *deref)");
        outCount++;
        a->outputTypes.push_back(t);
      }
    }
    // resultTy: 1 output  -> the output's type (the asm! expression yields it);
    //           0 outputs -> void (side-effect only, e.g. a barrier);
    //           >1 output -> void (results go to the output targets, not the
    //                       expression value  -  see IRGen's aggregate extractvalue
    //                       path and smtAsmTerm's per-output uninterpreted
    //                       functions asm_<fn>_<seq>_outN).
    if (outCount == 1) a->resultTy = a->outputTypes[0];
    else a->resultTy = BType::void_;
    // Effect system  -  `asm!` performs the `asm` effect (inline assembly). A
    // pure caller (curFunc_->isPure) may not use asm!; a non-pure caller that
    // forgot `asm` in its effects is NOT caught here (asm! has no FuncDecl to
    // carry effects)  -  the user is expected to declare `asm`. For v1 we gate
    // purity only; the non-pure path leaves it to the user.
    checkPurityViolation(a->line, a->col, "asm", "(inline assembly)");
    // Verified-asm link: `asm!(...) implements <spec_fn>(<args>)`. Validate the
    // link here so a mis-link is a compile error, not a silent SMT-elision:
    //   1. <spec_fn> must resolve to a registered name in `funcs` (the asm spec
    //      decl is registered with isExtern=true + isAsmSpec=true earlier in
    //      `check`). Unresolved => error.
    //   2. The slot MUST have isAsmSpec=true  -  a plain `spec fn` or a normal
    //      `fn` is NOT a hardware-instruction spec (only an `asm spec fn` is)
    //      and cannot vet an asm block. => error.
    //   3. The implementsArgs count must equal the spec's param count. => error.
    //   4. Each implementsArg's checked type must be comparable to the spec's
    //      corresponding param type (allow Int<->Int and the usual numeric
    //      coercions; a structural mismatch like u32 vs str is an error).
    //   5. If the asm has a single output (resultTy != void), its type must
    //      match the spec's return type  -  the spec's `ensures result == ...`
    //      references `result`, so the link is only sound when the asm's value
    //      and the spec's `result` are the same sort. (A void / multi-output
    //      asm has resultTy=void; the spec's `result` then binds to a fresh
    //      opaque symbol at SMT time  -  fine, the ensures just constrains that
    //      symbol rather than an observable value.)
    // The requires/ensures DISCHARGE itself happens in the SMT encoder (the
    // Ghost section emits the spec's universal axiom; the WP path asserts each
    // ensures as a hypothesis at the asm's point and discharges each requires
    // as a caller proof obligation)  -  Sema only vetoes structurally-broken
    // links here. Each implementsArg is also type-checked recursively (so an
    // undefined name in the arg list is caught), and its checked type is NOT
    // stored: the SMT encoder re-lowers the arg via smtExpr at emit time.
    if (a->hasImplements) {
      auto it = funcs.find(a->implementsSpec);
      if (it == funcs.end()) {
        errAt(errs, a->line, a->col,
              "asm! implements '" + a->implementsSpec +
              "'  -  no such asm spec function (declare `asm spec " +
              a->implementsSpec + "(...) -> ... requires ... ensures ...;`)");
      } else if (!it->second.isAsmSpec) {
        errAt(errs, a->line, a->col,
              "asm! implements '" + a->implementsSpec +
              "'  -  not an `asm spec fn` (only an asm spec can vet an asm block; "
              "a plain `spec fn` or regular `fn` cannot)");
      } else {
        const FuncSig& sig = it->second;
        // Check + count the implementsArgs. We type-check each arg so nested
        // undefined refs are caught here (the resulting type is used only for
        // the compatibility check  -  it is not stored; the SMT encoder
        // re-lowers each arg via smtExpr at emit time).
        std::vector<BType> argTypes;
        for (auto& arg : a->implementsArgs) {
          if (!arg) { argTypes.push_back(BType::void_); continue; }
          argTypes.push_back(checkExpr(arg.get()));
        }
        if (argTypes.size() != sig.paramTypes.size()) {
          errAt(errs, a->line, a->col,
                "asm! implements '" + a->implementsSpec + "'  -  argument count " +
                std::to_string(argTypes.size()) + " does not match spec param " +
                "count " + std::to_string(sig.paramTypes.size()));
        } else {
          // Per-arg structural check. Allow Int<->Int + the usual numeric
          // coercions (a u32 implementing an i64 spec arg is fine  -  both
          // lower to Int in SMT; a bool arg to an i64 spec param is NOT,
          // different sorts). Use the same `comparableTypes` predicate the
          // call-arg check uses when it exists, else fall back to isInt ==
          // isInt / isNumeric == isNumeric / exact equality.
          for (size_t i = 0; i < argTypes.size(); ++i) {
            BType at = argTypes[i], pt = sig.paramTypes[i];
            bool ok = (at == pt) || (isInt(at) && isInt(pt)) ||
                      (isNumeric(at) && isNumeric(pt)) ||
                      (at == BType::void_ && pt == BType::void_);
            if (!ok) errAt(errs, a->line, a->col,
                           "asm! implements '" + a->implementsSpec +
                           "'  -  argument " + std::to_string(i + 1) + " type " +
                           typeSpelling(at) + " is not compatible with spec " +
                           "param type " + typeSpelling(pt));
          }
        }
        // Result-type check: only meaningful for a single-output asm
        // (resultTy != void). For a void / multi-output asm the spec's
        // `result` binds to a fresh opaque symbol at SMT time, so a mismatch
        // is not a structural error there.
        if (a->resultTy != BType::void_ && sig.retType != BType::void_ &&
            !(a->resultTy == sig.retType ||
              (isInt(a->resultTy) && isInt(sig.retType)) ||
              (isNumeric(a->resultTy) && isNumeric(sig.retType)))) {
          errAt(errs, a->line, a->col,
                "asm! implements '" + a->implementsSpec + "'  -  asm result " +
                "type " + typeSpelling(a->resultTy) + " does not match spec " +
                "return type " + typeSpelling(sig.retType));
        }
      }
    }
    return a->resultTy;
  }
  // Compile-time macro invocation: `expand name(args...)`. Sema expands the
  // macro here (clone body + substitute $params + type-check), stashing the
  // expanded tree on `mc->expanded` so IRGen codegens the verified tree. This
  // is the ONLY place expansion happens, so nested `expand` inside a macro body
  // recurses naturally via the type-check of the substituted body (its inner
  // MacroCall nodes reach this same arm). Returns the expanded type; void_ on
  // error.
  if (auto mc = dynamic_cast<MacroCall*>(e)) {
    return expandMacro(mc);
  }
  if (auto c = dynamic_cast<CastExpr*>(e)) {
    BType ft = checkExpr(c->e.get());
    BType tt = fixType(c->target);
    c->target = tt;
    bool ok = false;
    if (ft == tt) ok = true;
    else if (isInt(ft) && isInt(tt)) ok = true;
    else if (isNumeric(ft) && isNumeric(tt)) ok = true;
    else if ((ft.tag == BType::Tag::ptr && tt.tag == BType::Tag::ptr)) ok = true;
    else if ((ft.tag == BType::Tag::ptr || ft.tag == BType::Tag::fn_ ||
              ft == BType::usize || ft == BType::u64 ||
              ft == BType::i64 || ft == BType::i32) &&
             (tt.tag == BType::Tag::ptr || tt.tag == BType::Tag::fn_ ||
              tt == BType::usize || tt == BType::u64 ||
              tt == BType::i64 || tt == BType::i32))
      ok = true;
    else if (ft == BType::char_ && isInt(tt)) ok = true;
    else if (isInt(ft) && tt == BType::char_) ok = true;
    if (!ok) errAt(errs, c->line, c->col, "cast from " + typeSpelling(ft) +
                   " to " + typeSpelling(tt) + " is not allowed");
    return tt;
  }
  if (auto v = dynamic_cast<VarRef*>(e)) {
    VarInfo info = lookup(v->name);
    if (info.found) {
      // ox:unsafe Use-after-move: a move-only local that has been moved-out of may not be
      // read. Re-assignment (AssignTarget/IncDec) re-validates the name below.
      if (movedVars_.count(v->name))
        errAt(errs, v->line, v->col, "use of moved value '" + v->name +
              "' (it was consumed by a move; re-assign or drop it explicitly)",
              "move-only types need a `clone()` to be copied");
      return info.type;
    }

    auto [ed, ord] = resolveEnumVariant(v->name);
    if (ed) {

      v->line = v->line;

      return makeEnumType(ed->name);
    }
    // a bare function name, used as a value (not a call), yields a function
    // pointer of that function's type. This lets you pass free functions as
    // fn(...) -> ... arguments.
    auto fit = funcs.find(v->name);
    if (fit != funcs.end() && !fit->second.isExtern) {
      return makeFnType(fit->second.paramTypes, fit->second.retType);
    }
    std::vector<std::string> cands;
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
      for (auto& kv : it->vars) cands.push_back(kv.first);
    auto s = suggest(v->name, cands);
    errAt(errs, v->line, v->col, "use of undeclared variable '" + v->name + "'",
          s.empty() ? "" : "did you mean '" + s + "'?");
    return info.type;
  }
  if (auto u = dynamic_cast<UnaryExpr*>(e)) {
    BType bt = checkExpr(u->base.get());
    switch (u->op) {
      case UnaryExpr::Op::neg: {


        BType rt = bt;
        if (rt.tag == BType::Tag::ptr && pointee(rt).tag == BType::Tag::struct_)
          rt = pointee(rt);
        if (rt.tag == BType::Tag::struct_ &&
            methods.count(rt.structName) && methods[rt.structName].count("__neg")) {
          const MethodInfo& mi = methods[rt.structName].at("__neg");
          if (!mi.paramTypes.empty())
            errAt(errs, u->line, u->col, "__neg must take no arguments");
          u->methodOverload = true;
          u->overloadStruct = rt.structName;
          u->overloadMethod = "__neg";
          u->overloadRecvType = rt;
          u->recvByRef = mi.selfByRef;
          return mi.retType;
        }
        if (!isNumeric(bt))
          errAt(errs, u->line, "unary '-' requires a number");
        return bt;
      }
      case UnaryExpr::Op::not_:
        if (bt != BType::bool_) errAt(errs, u->line, "'!' requires a bool");
        return BType::bool_;
      case UnaryExpr::Op::bnot:
        if (!isInt(bt)) errAt(errs, u->line, "'~' requires an int");
        return BType::i64;
      case UnaryExpr::Op::addr:
        if (bt == BType::void_) {
          errAt(errs, u->line, "'&' requires an lvalue");
          return makePtr(BType::i64);
        }
        return makePtr(bt);
      case UnaryExpr::Op::deref:
        if (bt.tag != BType::Tag::ptr) {
          errAt(errs, u->line, "'*' requires a pointer");
          return BType::i64;
        }
        return pointee(bt);
    }
  }
  if (auto b = dynamic_cast<BinaryExpr*>(e)) {
    BType lt = checkExpr(b->lhs.get());
    BType rt = checkExpr(b->rhs.get());

    lt = coerceIntLit(b->lhs.get(), lt, rt);
    rt = coerceIntLit(b->rhs.get(), rt, lt);

    lt = coerceFloatLit(b->lhs.get(), lt, rt);
    rt = coerceFloatLit(b->rhs.get(), rt, lt);


    if (b->op != BinaryExpr::Op::land && b->op != BinaryExpr::Op::lor) {
      BType recvT = lt;
      if (recvT.tag == BType::Tag::ptr && pointee(recvT).tag == BType::Tag::struct_)
        recvT = pointee(recvT);
      if (recvT.tag == BType::Tag::struct_) {
        const MethodInfo* mi = resolveOverload(recvT.structName, b->op, b);
        if (mi) {
          bool isCmp = (b->op >= BinaryExpr::Op::eq && b->op <= BinaryExpr::Op::ge);
          bool wantParam = (b->op == BinaryExpr::Op::ne) || !isCmp;
          if (isCmp) {


          }

          size_t expect = 1;
          (void)wantParam;
          if (mi->paramTypes.size() != expect) {
            errAt(errs, b->line, b->col,
                  "'" + std::string(opMethodName(b->op)) + "' on '" + recvT.structName +
                  "' must take " + std::to_string(expect) + " argument(s), got " +
                  std::to_string(mi->paramTypes.size()));
          } else if (rt != mi->paramTypes[0] &&
                     !litAssignable(b->rhs.get(), rt, mi->paramTypes[0]) &&
                     !dynamic_cast<CastExpr*>(b->rhs.get())) {
            errAt(errs, b->rhs->line, "operator overload '" +
                  std::string(opMethodName(b->op)) + "': rhs type " +
                  typeSpelling(rt) + " does not match expected " +
                  typeSpelling(mi->paramTypes[0]));
          }
          b->methodOverload = true;
          b->overloadStruct = recvT.structName;
          b->overloadMethod = opMethodName(b->op);
          b->overloadRecvType = recvT;
          b->recvByRef = mi->selfByRef;


          if (b->op >= BinaryExpr::Op::eq && b->op <= BinaryExpr::Op::ge)
            return BType::bool_;
          return mi->retType;
        }
      }
    }
    switch (b->op) {
      case BinaryExpr::Op::add: case BinaryExpr::Op::sub:
      case BinaryExpr::Op::mul: case BinaryExpr::Op::div:
      case BinaryExpr::Op::mod:


        if (b->op == BinaryExpr::Op::add || b->op == BinaryExpr::Op::sub) {
          auto isPtrIntMix = [&](const BType& a, const BType& d) {
            return a.tag == BType::Tag::ptr && isInt(d);
          };
          if (isPtrIntMix(lt, rt) || isPtrIntMix(rt, lt)) {
            BType pt = (lt.tag == BType::Tag::ptr) ? lt : rt;
            b->isPtrArith = true;
            b->ptrArithPointee = pointee(pt);
            return pt;
          }
        }
        // --- Matrix multiply routing (Bug 1) ---
        // When `*` is applied to two array-typed operands, treat it as a
        // matrix-multiply and compute the result type as a 2-D array
        // array<array<elem, rhsCols>, lhsRows>.  We can't transform the
        // BinaryExpr node into a MatMulExpr here (checkExpr is const-ish on
        // the tree), so we compute the matrix-mul result type directly and
        // piggy-back on the existing MatMul runtime in IRGen by setting the
        // BinaryExpr's isMatMul flag (see AST.h).
        if (b->op == BinaryExpr::Op::mul &&
            lt.tag == BType::Tag::array && rt.tag == BType::Tag::array) {
          // Walk to scalar element types.
          BType lelem = lt;
          while (lelem.tag == BType::Tag::array) lelem = arrayElem(lelem);
          BType relem = rt;
          while (relem.tag == BType::Tag::array) relem = arrayElem(relem);
          BType elemType = (lelem == BType::f32 && relem == BType::f32) ? BType::f32 : BType::f64;
          // Determine dimensions: lhs is array<array<elem,cols>,rows>,
          // rhs is array<array<elem,cols2>,rows2>.  Result is rows x cols2.
          int lhsRows = lt.count;
          BType ltInner = arrayElem(lt);      // array<elem, cols>
          (void)ltInner;                      // cols of lhs not needed for result
          int rhsCols = 0;
          BType rtInner = arrayElem(rt);      // array<elem, cols2>
          rhsCols = rtInner.count;
          b->isMatMul = true;
          return makeArrayType(makeArrayType(elemType, rhsCols), lhsRows);
        }
        if (isNumeric(lt) && lt == rt) return lt;
        if (lt == BType::str && rt == BType::str && b->op == BinaryExpr::Op::add) return BType::str;

        if (b->op == BinaryExpr::Op::add &&
            ((lt == BType::str && rt == BType::char_) ||
             (lt == BType::char_ && rt == BType::str)))
          return BType::str;
        errAt(errs, b->line, "arithmetic operands must be the same numeric type");
        return lt == BType::void_ ? BType::i64 : lt;
      case BinaryExpr::Op::band: case BinaryExpr::Op::bor:
      case BinaryExpr::Op::bxor: case BinaryExpr::Op::shl:
      case BinaryExpr::Op::shr:
        if (isInt(lt) && lt == rt) return lt;
        errAt(errs, b->line, "bitwise operands must be the same int type");
        return BType::i64;
      case BinaryExpr::Op::eq: case BinaryExpr::Op::ne:
        if (lt == rt && lt != BType::void_) return BType::bool_;
        if (isNumeric(lt) && isNumeric(rt) && lt == rt) return BType::bool_;

        if (lt.tag == BType::Tag::enum_ && isIntLitExpr(b->rhs.get())) return BType::bool_;
        if (rt.tag == BType::Tag::enum_ && isIntLitExpr(b->lhs.get())) return BType::bool_;
        if (lt.tag == BType::Tag::ptr && rt.tag == BType::Tag::ptr) return BType::bool_;
        errAt(errs, b->line, "comparison operands must match");
        return BType::bool_;
      case BinaryExpr::Op::lt: case BinaryExpr::Op::gt:
      case BinaryExpr::Op::le: case BinaryExpr::Op::ge:
        if (lt == rt && lt != BType::void_) return BType::bool_;
        if (isNumeric(lt) && isNumeric(rt) && lt == rt) return BType::bool_;
        if (lt.tag == BType::Tag::ptr && rt.tag == BType::Tag::ptr) return BType::bool_;
        errAt(errs, b->line, "comparison operands must match");
        return BType::bool_;
      case BinaryExpr::Op::land: case BinaryExpr::Op::lor:
        if (lt != BType::bool_ || rt != BType::bool_)
          errAt(errs, b->line, "logical operands must be bool");
        return BType::bool_;
    }
  }
  if (auto t = dynamic_cast<TernaryExpr*>(e)) {


    BType ct = checkExpr(t->cond.get());
    if (ct != BType::bool_) errAt(errs, t->line, "ternary condition must be bool");
    BType tt = checkExpr(t->thenE.get());
    BType et = checkExpr(t->elseE.get());
    BType res = pickCommonType(t->thenE.get(), tt, t->elseE.get(), et, errs, t->line);
    t->resultTy = res;
    return res;
  }
  if (auto d = dynamic_cast<IncDecExpr*>(e)) {


    BType lvt = BType::void_;
    bool isMut = false, found = false;
    if (d->kind == AssignTarget::Kind::var) {
      VarInfo info = lookup(d->name);
      if (!info.found)
        errAt(errs, d->line, "increment/decrement of undeclared variable '" + d->name + "'");
      else if (!info.isMut)
        errAt(errs, d->line, "cannot ++/-- immutable variable '" + d->name + "' (use mut)");
      lvt = info.type; isMut = info.isMut; found = info.found;
    } else if (d->kind == AssignTarget::Kind::index) {
      BType baseT = checkExpr(d->base.get());
      if (baseT.tag == BType::Tag::ptr && (pointee(baseT).tag == BType::Tag::array ||
          pointee(baseT).tag == BType::Tag::dynarray || pointee(baseT).tag == BType::Tag::ptr))
        baseT = pointee(baseT);
      if (baseT.tag == BType::Tag::array) { lvt = arrayElem(baseT); found = true; }
      else if (baseT.tag == BType::Tag::dynarray) { lvt = dynArrayElem(baseT); found = true; }
      else if (baseT.tag == BType::Tag::ptr) { lvt = pointee(baseT); found = true; }
      else errAt(errs, d->line, "increment/decrement of indexed non-array/non-pointer");
      BType idxT = checkExpr(d->index.get());
      if (!isInt(idxT)) errAt(errs, d->line, "array index must be an int");
      (void)isMut;
    } else if (d->kind == AssignTarget::Kind::deref) {
      BType baseT = checkExpr(d->base.get());
      if (baseT.tag != BType::Tag::ptr)
        errAt(errs, d->line, "increment/decrement of '*p' requires a pointer");
      else { lvt = pointee(baseT); found = true; }
    } else if (d->kind == AssignTarget::Kind::field) {
      BType baseT = checkExpr(d->base.get());
      if (baseT.tag == BType::Tag::ptr && pointee(baseT).tag == BType::Tag::struct_)
        baseT = pointee(baseT);
      if (baseT.tag != BType::Tag::struct_) {
        errAt(errs, d->line, "increment/decrement of a field requires a struct value");
      } else {
        StructDef* sd = findStruct(baseT.structName);
        if (!sd) errAt(errs, d->line, "unknown struct '" + baseT.structName + "'");
        else if (structFieldIndex(sd, d->field) < 0)
          errAt(errs, d->line, "struct '" + baseT.structName + "' has no field '" + d->field + "'");
        else {
          int32_t fi = structFieldIndex(sd, d->field);
          if (sd->fields[fi].isPrivate && !canTouchPrivate(baseT.structName))
            errAt(errs, d->line, d->col,
                  "field '" + d->field + "' of '" + baseT.structName + "' is private");
          lvt = sd->fields[fi].type; found = true;
        }
      }
    }
    if (found && lvt.tag != BType::Tag::void_) {
      bool ok = isInt(lvt) || lvt == BType::char_ || lvt == BType::usize ||
                lvt == BType::bool_ || lvt.tag == BType::Tag::ptr;
      if (!ok)
        errAt(errs, d->line, "++/-- requires an int, char, usize, bool, or pointer operand");
    }
    d->valueTy = found ? lvt : BType::i64;
    return d->valueTy;
  }
  if (auto a = dynamic_cast<AssignTarget*>(e)) {

    BType lvt = BType::void_;
    bool isMut = false, found = false;
    if (a->kind == AssignTarget::Kind::var) {
      VarInfo info = lookup(a->name);
      if (!info.found)
        errAt(errs, a->line, "assignment to undeclared variable '" + a->name + "'");
      else if (!info.isMut)
        errAt(errs, a->line, "cannot assign to immutable variable '" + a->name + "' (use mut)");
      lvt = info.type; isMut = info.isMut; found = info.found;
      // A move-only local being assigned-to is re-validated (it is live again,
      // holding a fresh value). Its prior value (if it had a destructor) is
      // dropped first by IRGen's assignment path before the store.
      movedVars_.erase(a->name);
    } else if (a->kind == AssignTarget::Kind::index) {
      BType baseT = checkExpr(a->base.get());

      if (baseT.tag == BType::Tag::ptr && (pointee(baseT).tag == BType::Tag::array ||
          pointee(baseT).tag == BType::Tag::dynarray || pointee(baseT).tag == BType::Tag::ptr))
        baseT = pointee(baseT);
      if (baseT.tag == BType::Tag::array) {
        lvt = arrayElem(baseT); found = true;
      } else if (baseT.tag == BType::Tag::dynarray) {
        lvt = dynArrayElem(baseT); found = true;
      } else if (baseT.tag == BType::Tag::ptr) {
        lvt = pointee(baseT); found = true;
      } else if (baseT.tag == BType::Tag::map_ || baseT.tag == BType::Tag::hmap_) {
        // `m[k] = v` on a map / hmap  -  the index is the KEY, the result element
        // type is the value type. IRGen routes the store to map_set/hmap_set.
        lvt = mapValType(baseT); found = true;
      } else {
        errAt(errs, a->line, "indexed assignment requires an array or pointer value");
      }
      BType idxT = checkExpr(a->index.get());
      if (baseT.tag == BType::Tag::map_ || baseT.tag == BType::Tag::hmap_) {
        // a map key is not an int offset: validate against the key type instead.
        BType keyT = mapKeyType(baseT);
        if (idxT != keyT && !litAssignable(a->index.get(), idxT, keyT) &&
            !dynamic_cast<CastExpr*>(a->index.get()))
          errAt(errs, a->index->line, a->index->col,
                "map key type " + typeSpelling(idxT) + " does not match expected " +
                typeSpelling(keyT));
      } else if (!isInt(idxT)) {
        errAt(errs, a->line, "array index must be an int");
      }
      (void)isMut;
    } else if (a->kind == AssignTarget::Kind::deref) {
      BType baseT = checkExpr(a->base.get());
      if (baseT.tag != BType::Tag::ptr) {
        errAt(errs, a->line, "deref assignment requires a pointer ('*p = v')");
      } else {
        lvt = pointee(baseT); found = true;
      }
    } else if (a->kind == AssignTarget::Kind::field) {
      BType baseT = checkExpr(a->base.get());

      if (baseT.tag == BType::Tag::ptr && pointee(baseT).tag == BType::Tag::struct_)
        baseT = pointee(baseT);
      if (baseT.tag != BType::Tag::struct_) {
        errAt(errs, a->line, "field assignment requires a struct value");
      } else {
        StructDef* d = findStruct(baseT.structName);
        if (!d) {
          errAt(errs, a->line, "unknown struct '" + baseT.structName + "'");
        } else if (structFieldIndex(d, a->field) < 0) {
          errAt(errs, a->line, "struct '" + baseT.structName + "' has no field '" + a->field + "'");
          lvt = BType::void_; found = true;
        } else {
          int32_t fi = structFieldIndex(d, a->field);
          if (d->fields[fi].isPrivate && !canTouchPrivate(baseT.structName)) {
            errAt(errs, a->line, a->col, "field '" + a->field + "' of '" + baseT.structName +
                  "' is private", "mutate it through the struct's impl methods");
          }
          lvt = d->fields[fi].type;
          found = true;
        }
      }
    }
    BType rtype = checkExpr(a->value.get());


    if (found && lvt.tag != BType::Tag::void_) {
      BType recvT = lvt;
      if (recvT.tag == BType::Tag::ptr && pointee(recvT).tag == BType::Tag::struct_)
        recvT = pointee(recvT);
      if (recvT.tag == BType::Tag::struct_) {
        const char* mname = a->isCompound
            ? opIMethodName(a->compound)
            : "__assign";
        auto sit = methods.find(recvT.structName);
        if (sit != methods.end() && mname && mname[0] &&
            sit->second.count(mname)) {
          const MethodInfo& mi = sit->second.at(mname);
          if (mi.paramTypes.size() != 1)
            errAt(errs, a->line, a->col,
                  std::string("'") + mname + "' on '" + recvT.structName +
                  "' must take 1 argument, got " + std::to_string(mi.paramTypes.size()));
          else if (rtype != mi.paramTypes[0] &&
                   !litAssignable(a->value.get(), rtype, mi.paramTypes[0]) &&
                   !dynamic_cast<CastExpr*>(a->value.get()))
            errAt(errs, a->value->line,
                  std::string("operator overload '") + mname + "': rhs type " +
                  typeSpelling(rtype) + " does not match expected " +
                  typeSpelling(mi.paramTypes[0]));
          a->methodOverload = true;
          a->overloadStruct = recvT.structName;
          a->overloadMethod = mname;
          a->overloadRecvType = recvT;
          return mi.retType;
        }
      }
    }
    // Move discipline for plain assignment: if the RHS is a move-only struct
    // value rooted at a local, that local is consumed by the move. (Compound
    // ops like += don't transfer ownership  -  they mutate in place.)
    if (found) {
      BType moveT = lvt;
      if (moveT.tag == BType::Tag::ptr && pointee(moveT).tag == BType::Tag::struct_)
        moveT = pointee(moveT);
      if (!a->isCompound) noteMovedFrom(a->value.get(), moveT);
    }

    if (a->isCompound) {

      if (found && lvt != BType::void_ && rtype != lvt)
        errAt(errs, a->line, "compound assignment type mismatch");


      if (found && lvt == BType::str && a->compound != BinaryExpr::Op::add)
        errAt(errs, a->line, a->col,
              "only '+=' is supported on a str (concatenation)");
      return lvt == BType::void_ ? rtype : lvt;
    }
    if (found && lvt != BType::void_ && rtype != lvt)
      errAt(errs, a->line, "assignment type mismatch");
    return lvt == BType::void_ ? rtype : lvt;
  }
  if (auto c = dynamic_cast<Call*>(e)) {

    // A call produced by postfix application (`(expr)(args)`, `f()()`,
    // `arr[i]()`, …) arrives here with `fnPtr` set and a fully-formed
    // `calleeExpr` already in place (the parser set it). Such a call is NEVER
    // a bare-name function call, so skip the whole name-resolution path below
    // and type-check it purely against the callee's value type.
    if (c->fnPtr && c->calleeExpr) {
      BType ft = checkExpr(c->calleeExpr.get());
      c->calleeFnType = ft;
      // If the callee is a capturing closure struct, the *callable* fn type is
      // its first field (synthesized by checkLambda). A bare fn-pointer type is
      // callable directly.
      const std::vector<BType>* ps = nullptr;
      BType ret = BType::void_;
      size_t ncaps = 0;
      if (ft.tag == BType::Tag::fn_) {
        ps = &fnParams(ft); ret = fnRet(ft); ncaps = 0;
      } else if (ft.tag == BType::Tag::struct_ &&
                 ft.structName.rfind("__oxclosure_", 0) == 0) {
        StructDef* sd = findStruct(ft.structName);
        if (sd && !sd->fields.empty()) {
          BType fnField = sd->fields[0].type;
          ps = &fnParams(fnField); ret = fnRet(fnField);
          ncaps = sd->fields.size() - 1;
        }
      }
      if (!ps) {
        errAt(errs, c->line, c->col, "called expression is not a function");
        for (auto& a : c->args) checkExpr(a.get());
        return BType::void_;
      }
      size_t nReal = (ps->size() >= ncaps) ? ps->size() - ncaps : 0;
      if (c->args.size() != nReal)
        errAt(errs, c->line, c->col, "indirect call expects " + std::to_string(nReal) +
              " arg(s), got " + std::to_string(c->args.size()));
      for (size_t k = 0; k < c->args.size(); k++) {
        BType at = checkExpr(c->args[k].get());
        BType pt = (k < nReal) ? (*ps)[k + ncaps] : BType::void_;
        bool ok = (at == pt) || implicitAssignable(at, pt) || litAssignable(c->args[k].get(), at, pt);
        if (!ok && pt.tag == BType::Tag::ptr && at == pointee(pt) && isLvalueExpr(c->args[k].get())) ok = true;
        if (!ok)
          errAt(errs, c->args[k]->line, c->args[k]->col,
                "argument " + std::to_string(k + 1) + " of indirect call has the wrong type (expected " +
                typeSpelling(pt) + ")");
      }
      return ret;
    }

    static const std::map<std::string, std::pair<BType, int>> builtins = {
      {"abs",   {BType::i64, 1}},
      {"imin",  {BType::i64, 2}},
      {"imax",  {BType::i64, 2}},
      {"fmin",  {BType::f64, 2}},
      {"fmax",  {BType::f64, 2}},
      {"sqrt",  {BType::f64, 1}},
      {"itos",  {BType::str, 1}},
      {"stoi",  {BType::i64, 1}},
      {"stod",  {BType::f64, 1}},
      {"ftos",  {BType::str, 1}},
      {"char_to_str", {BType::str, 1}},
      {"substr",   {BType::str, 3}},
      {"index_of", {BType::i64, 2}},
      {"read_line", {BType::str, 0}},
      {"read_file", {BType::str, 1}},
      {"file_open",  {BType::i64, 2}},
      {"file_close", {BType::i64, 1}},
      {"file_read",  {BType::str, 1}},
      {"file_write", {BType::i64, 2}},
      {"file_exists",{BType::bool_, 1}},
      {"len",   {BType::i64, 1}},
      {"push",  {BType::void_, 2}},
      {"sort",  {BType::void_, 1}},
      {"map_len",      {BType::i64,  1}},
      {"map_contains",  {BType::bool_, 2}},
      {"map_set",       {BType::void_, 3}},
      {"map_get",       {BType::void_, 2}},
      {"map_keys",      {BType::void_, 1}},
      {"set_len",       {BType::i64,  1}},
      {"set_contains",  {BType::bool_, 2}},
      {"set_insert",    {BType::void_, 2}},
      {"set_remove",    {BType::void_, 2}},
      {"set_to_vec",   {BType::void_, 1}},
      {"hmap_len",      {BType::i64,  1}},
      {"hmap_contains", {BType::bool_, 2}},
      {"hmap_set",      {BType::void_, 3}},
      {"hmap_get",      {BType::void_, 2}},
      {"hmap_keys",     {BType::void_, 1}},
      {"hset_len",       {BType::i64,  1}},
      {"hset_contains",  {BType::bool_, 2}},
      {"hset_insert",    {BType::void_, 2}},
      {"hset_remove",    {BType::void_, 2}},
      {"hset_to_vec",   {BType::void_, 1}},
      {"print",  {BType::void_, 0}},
      // ---- extended stdlib (math) ----
      {"pow", {BType::f64, 2}}, {"floor", {BType::f64, 1}}, {"ceil", {BType::f64, 1}},
      {"round", {BType::f64, 1}}, {"trunc", {BType::f64, 1}}, {"lround", {BType::i64, 1}},
      {"sin", {BType::f64, 1}}, {"cos", {BType::f64, 1}}, {"tan", {BType::f64, 1}},
      {"asin", {BType::f64, 1}}, {"acos", {BType::f64, 1}}, {"atan", {BType::f64, 1}},
      {"atan2", {BType::f64, 2}}, {"log", {BType::f64, 1}}, {"log2", {BType::f64, 1}},
      {"log10", {BType::f64, 1}}, {"exp", {BType::f64, 1}}, {"exp2", {BType::f64, 1}},
      {"hypot", {BType::f64, 2}}, {"fmod", {BType::f64, 2}}, {"gcd", {BType::f64, 2}},
      {"isnan", {BType::i64, 1}}, {"isinf", {BType::i64, 1}}, {"finite", {BType::i64, 1}},
      {"deg2rad", {BType::f64, 1}}, {"rad2deg", {BType::f64, 1}},
      {"pi", {BType::f64, 0}}, {"e", {BType::f64, 0}},
      {"clampf", {BType::f64, 3}}, {"clamp", {BType::i64, 3}}, {"clampi", {BType::i64, 3}},
      // ---- strings ----
      {"lower", {BType::str, 1}}, {"upper", {BType::str, 1}}, {"reverse", {BType::str, 1}},
      {"trim", {BType::str, 1}}, {"repeat", {BType::str, 2}},
      {"starts_with", {BType::bool_, 2}}, {"ends_with", {BType::bool_, 2}},
      {"contains_str", {BType::bool_, 2}}, {"find", {BType::i64, 2}},
      {"replace", {BType::str, 3}}, {"itoa_base", {BType::str, 2}},
      // ---- vec / container helpers ----
      {"pop", {BType::bool_, 1}}, {"pop_last", {BType::void_, 1}},
      {"clear", {BType::void_, 1}}, {"remove_at", {BType::void_, 2}},
      {"insert_at", {BType::void_, 3}}, {"reverse_vec", {BType::void_, 1}},
      {"reverse", {BType::void_, 1}}, {"contains_elem", {BType::bool_, 2}},
      {"index_of_elem", {BType::i64, 2}}, {"extend", {BType::void_, 2}},
      {"map_delete", {BType::void_, 2}},
      // polymorphic vec reduces: element-typed return; placeholder i64 here,
      // the real return is recomputed below from the vec's element type.
      {"sum", {BType::i64, 1}}, {"vmin", {BType::i64, 1}}, {"vmax", {BType::i64, 1}},
      {"first", {BType::i64, 1}}, {"last", {BType::i64, 1}},
      // join/split have an overloaded-ish return: str/vec[str]; table marks them
      // recognized, the real return is computed below.
      {"join", {BType::str, 2}}, {"split", {BType::void_, 2}},
      // ---- time + random ----
      {"seed", {BType::void_, 1}}, {"rand", {BType::i64, 0}},
      {"rand_range", {BType::i64, 2}}, {"time_ns", {BType::i64, 0}},
      {"clock_ms", {BType::i64, 0}}, {"time_epoch", {BType::i64, 0}},
    };
    if (c->isPrint) {
      for (auto& arg : c->args) checkExpr(arg.get());
      if (freestanding)
        errAt(errs, c->line, c->col, "print requires the hosted runtime (--freestanding omits it)",
              "extern your own output routine, or build hosted (drop --freestanding / --no-rt)");
      // Effect system  -  `print` has effect `io`. A pure caller (curFunc_->isPure)
      // may not print; a non-pure caller must declare `io` (TODO via the stdlib
      // effect registry). For v1 we gate purity only; the non-pure caller's
      // `io` declaration is checked at the user-function-call path for callees
      // with effects, but `print` is a builtin with no FuncDecl, so this is the
      // purity gate's responsibility.
      checkPurityViolation(c->line, c->col, "io", "via call to built-in 'print'");
      return BType::void_;
    }


    if (findGenericFn(c->callee)) {
      std::vector<BType> args;
      if (c->hasTypeArgs) {
        for (auto& a : c->typeArgs) args.push_back(fixType(a));
      } else {
        const FuncDecl* tmpl = findGenericFn(c->callee);
        std::map<std::string, BType> env;
        size_t nargs = std::min(tmpl->params.size(), c->args.size());
        for (size_t ai = 0; ai < nargs; ai++) {
          BType actual = checkExpr(c->args[ai].get());
          unifyInto(tmpl->params[ai].type, actual, env);
        }
        // ox:proof also check any leftover args (exists for stability of error messages)
        for (size_t ai = nargs; ai < c->args.size(); ai++) checkExpr(c->args[ai].get());
        for (const auto& tp : tmpl->typeParams) {
          auto it = env.find(tp);
          args.push_back(it == env.end() ? BType::i64 : it->second);
        }
      }
      // Default type args (trailing). For explicit type-args calls we fill here
      // using the first registered template's tparams (selectGenericFn below may
      // pick a different overload, but overloads must agree on arity/defaults for
      // default-fill to be meaningful; keep the simple fill for the common case).
      if (!genericFnOverloads_[c->callee].empty()) {
        args = fillDefaultTypeArgs(genericFnOverloads_[c->callee].front()->tparams, std::move(args));
      }
      // Constrained overloads: pick the template whose constraints accept the
      // (inferred/filled) args. With a single template this returns it unchanged.
      const FuncDecl* chosen = args.empty() ? findGenericFn(c->callee)
                                             : selectGenericFn(c->callee, args);
      if (!chosen) {
        errAt(errs, c->line, c->col, "no overload of generic '" + c->callee +
              "' satisfies the concept constraints of the given arguments");
        for (auto& a : c->args) checkExpr(a.get());
        return BType::void_;
      }
      // Validate this template's constraints against the chosen bindings.
      std::map<std::string, BType> cenv;
      size_t nb = std::min(chosen->typeParams.size(), args.size());
      for (size_t i = 0; i < nb; i++) cenv[chosen->typeParams[i]] = args[i];
      checkConstraints(chosen->tparams, cenv, c->line, c->col);

      std::string mangled = instantiateGenericFnDecl(c->callee, chosen, args);
      if (mangled.empty()) {
        errAt(errs, c->line, c->col, "generic instantiation of '" + c->callee + "' failed");
        for (auto& a : c->args) checkExpr(a.get());
        return BType::void_;
      }

      c->callee = mangled;
      c->hasTypeArgs = false;
      c->typeArgs.clear();
    } else if (c->hasTypeArgs) {
      errAt(errs, c->line, c->col,
            "'" + c->callee + "' is not a generic function");
      for (auto& a : c->args) checkExpr(a.get());
      return BType::void_;
    }


    if (!funcs.count(c->callee) && !c->fnPtr) {
      VarInfo vi = lookup(c->callee);
      if (!vi.found) {
        auto it = globals.find(c->callee); if (it != globals.end()) { vi.type = it->second.type; vi.found = true; }
      }
      if (vi.found && vi.type.tag == BType::Tag::fn_) {
        c->fnPtr = true;
        auto v = std::make_unique<VarRef>(); v->name = c->callee; v->line = c->line; v->col = c->col;
        c->calleeExpr = std::move(v);
        const auto& ps = fnParams(vi.type);
        BType ret = fnRet(vi.type);
        if (c->args.size() != ps.size() &&
            !(ps.size() == 0 && false)) {
        }
        if (c->args.size() != ps.size())
          errAt(errs, c->line, c->col, "indirect call expects " + std::to_string(ps.size()) +
                " args, got " + std::to_string(c->args.size()));
        for (size_t k = 0; k < c->args.size(); k++) {
          BType at = checkExpr(c->args[k].get());
          BType pt = (k < ps.size()) ? ps[k] : BType::void_;
          bool ok = (at == pt) || implicitAssignable(at, pt) || litAssignable(c->args[k].get(), at, pt);
          if (!ok && pt.tag == BType::Tag::ptr && at == pointee(pt) && isLvalueExpr(c->args[k].get())) ok = true;
          if (!ok)
            errAt(errs, c->args[k]->line, "argument " + std::to_string(k + 1) +
                  " of indirect call has the wrong type (expected " + typeSpelling(pt) + ")");
        }
        return ret;
      }
      if (vi.found && vi.type.tag == BType::Tag::struct_ &&
          vi.type.structName.rfind("__oxclosure_", 0) == 0) {
        // Capture-closure call. The number of USER arguments equals the real
        // param count of the lambda = (closure.fn fields - captures) ... but
        // closures aren't generic, so read the fn field's param count from the
        // closure struct's synthesized definition.
        StructDef* sd = findStruct(vi.type.structName);
        c->fnPtr = true;   // route through IRGen's closure-call branch
        auto v = std::make_unique<VarRef>(); v->name = c->callee; v->line = c->line; v->col = c->col;
        c->calleeExpr = std::move(v);
        size_t ncaps = sd ? (sd->fields.size() - 1) : 0;
        BType fnField = sd ? sd->fields[0].type : BType::void_;
        const auto& fps = fnParams(fnField);
        BType ret = fnRet(fnField);
        size_t nReal = (fps.size() >= ncaps) ? fps.size() - ncaps : 0;
        if (c->args.size() != nReal)
          errAt(errs, c->line, c->col, "closure call expects " + std::to_string(nReal) +
                " arg(s), got " + std::to_string(c->args.size()) +
                " (captures are passed implicitly, not at the call site)");
        for (size_t k = 0; k < c->args.size(); k++) {
          BType at = checkExpr(c->args[k].get());
          BType pt = (k < nReal) ? fps[k + ncaps] : BType::void_;
          bool ok = (at == pt) || implicitAssignable(at, pt) || litAssignable(c->args[k].get(), at, pt);
          if (!ok && pt.tag == BType::Tag::ptr && at == pointee(pt) && isLvalueExpr(c->args[k].get())) ok = true;
          if (!ok)
            errAt(errs, c->args[k]->line, "argument " + std::to_string(k + 1) +
                  " of closure call has the wrong type (expected " + typeSpelling(pt) + ")");
        }
        return ret;
      }
    }


    if (freestanding && !funcs.count(c->callee)) {
      static const std::set<std::string> rtBuiltins = {
        "len","push","sort","read_line","read_file","file_open","file_close","file_read",
        "file_write","file_exists","itos","stoi","stod","ftos","char_to_str",
        "substr","index_of","abs",
        "map_len","map_contains","map_set","map_get","map_keys",
        "set_len","set_contains","set_insert","set_remove","set_to_vec",
        "hmap_len","hmap_contains","hmap_set","hmap_get","hmap_keys",
        "hset_len","hset_contains","hset_insert","hset_remove","hset_to_vec"
      };
      if (rtBuiltins.count(c->callee))
        errAt(errs, c->line, c->col,
              "'" + c->callee + "' requires the hosted runtime (--freestanding omits it)",
              "declare an `extern fn " + c->callee + "(...) -> ...;` with your own implementation");
    }


    auto checkElemKey = [&](int argIdx, BType want) {
      BType got = checkExpr(c->args[argIdx].get());
      if (got != want && !litAssignable(c->args[argIdx].get(), got, want) &&
          !dynamic_cast<CastExpr*>(c->args[argIdx].get()))
        errAt(errs, c->args[argIdx]->line, c->args[argIdx]->col,
              c->callee + " element type does not match (expected " +
              typeSpelling(want) + ")");
    };

    auto keyOk = [&](const BType& k) {
      return isScalar(k) || k.tag == BType::Tag::enum_;
    };
    auto needContainer = [&](int aidx, BType::Tag want, const char* kind) -> BType {
      BType t = checkExpr(c->args[aidx].get());
      // `map_*`/`map_len`/... accept the ordered map OR its hash-table twin
      // (and likewise `set_*`/`set_len` accept `set` or `hset`): the two kinds
      // share one call surface, so the bare `map_set`/`map_get`/... spellings
      // work on a `hmap`/`hset` too (IRGen dispatches by the operand's tag).
      bool ok = (t.tag == want);
      if (!ok) {
        if (want == BType::Tag::map_ && t.tag == BType::Tag::hmap_) ok = true;
        if (want == BType::Tag::hmap_ && t.tag == BType::Tag::map_) ok = true;
        if (want == BType::Tag::set_ && t.tag == BType::Tag::hset_) ok = true;
        if (want == BType::Tag::hset_ && t.tag == BType::Tag::set_) ok = true;
      }
      if (!ok)
        errAt(errs, c->args[aidx]->line, c->args[aidx]->col,
              c->callee + " expects a " + kind + " as argument " +
              std::to_string(aidx + 1));
      return t;
    };
    if (!funcs.count(c->callee)) {
      const std::string& cc = c->callee;
      if (cc == "map_len" || cc == "hmap_len" || cc == "set_len" || cc == "hset_len") {
        if (c->args.size() != 1) {
          errAt(errs, c->line, c->col, cc + " expects 1 argument");
          for (auto& a : c->args) checkExpr(a.get());
          return BType::i64;
        }
        if (cc == "map_len") (void)needContainer(0, BType::Tag::map_, "map");
        else if (cc == "hmap_len") (void)needContainer(0, BType::Tag::hmap_, "hash map");
        else if (cc == "set_len") (void)needContainer(0, BType::Tag::set_, "set");
        else (void)needContainer(0, BType::Tag::hset_, "hash set");
        return BType::i64;
      }
      if (cc == "map_contains" || cc == "hmap_contains" ||
          cc == "set_contains" || cc == "hset_contains") {
        if (c->args.size() != 2) {
          errAt(errs, c->line, c->col, cc + " expects 2 arguments (container, key)");
          for (auto& a : c->args) checkExpr(a.get());
          return BType::bool_;
        }
        BType ct = (cc=="map_contains") ? needContainer(0, BType::Tag::map_, "map")
                 : (cc=="hmap_contains") ? needContainer(0, BType::Tag::hmap_, "hash map")
                 : (cc=="set_contains") ? needContainer(0, BType::Tag::set_, "set")
                 : needContainer(0, BType::Tag::hset_, "hash set");
        BType want = (ct.tag == BType::Tag::map_ || ct.tag == BType::Tag::hmap_)
                     ? mapKeyType(ct) : setElemType(ct);
        checkElemKey(1, want);
        return BType::bool_;
      }
      if (cc == "map_set" || cc == "hmap_set") {
        if (c->args.size() != 3) {
          errAt(errs, c->line, c->col, cc + " expects 3 arguments (map, key, value)");
          for (auto& a : c->args) checkExpr(a.get());
          return BType::void_;
        }
        BType ct = (cc=="map_set") ? needContainer(0, BType::Tag::map_, "map")
                                   : needContainer(0, BType::Tag::hmap_, "hash map");
        checkElemKey(1, mapKeyType(ct));
        checkElemKey(2, mapValType(ct));
        return BType::void_;
      }
      if (cc == "set_insert" || cc == "set_remove" || cc == "hset_insert" || cc == "hset_remove") {
        if (c->args.size() != 2) {
          errAt(errs, c->line, c->col, cc + " expects 2 arguments (set, element)");
          for (auto& a : c->args) checkExpr(a.get());
          return BType::void_;
        }
        BType ct = (cc=="set_insert"||cc=="set_remove") ? needContainer(0, BType::Tag::set_, "set")
                                                       : needContainer(0, BType::Tag::hset_, "hash set");
        checkElemKey(1, setElemType(ct));
        return BType::void_;
      }
      if (cc == "map_get" || cc == "hmap_get") {
        if (c->args.size() != 2) {
          errAt(errs, c->line, c->col, cc + " expects 2 arguments (map, key)");
          for (auto& a : c->args) checkExpr(a.get());
          return BType::i64;
        }
        BType ct = (cc=="map_get") ? needContainer(0, BType::Tag::map_, "map")
                                   : needContainer(0, BType::Tag::hmap_, "hash map");
        checkElemKey(1, mapKeyType(ct));
        if (ct.tag == BType::Tag::map_ || ct.tag == BType::Tag::hmap_) return mapValType(ct);
        return BType::i64;
      }
      if (cc == "map_keys" || cc == "hmap_keys") {
        if (c->args.size() != 1) {
          errAt(errs, c->line, c->col, cc + " expects 1 argument (map)");
          for (auto& a : c->args) checkExpr(a.get());
          return makeDynArray(BType::i64);
        }
        BType ct = (cc=="map_keys") ? needContainer(0, BType::Tag::map_, "map")
                                    : needContainer(0, BType::Tag::hmap_, "hash map");
        if (ct.tag == BType::Tag::map_ || ct.tag == BType::Tag::hmap_)
          return makeDynArray(mapKeyType(ct));
        return makeDynArray(BType::i64);
      }
      if (cc == "set_to_vec" || cc == "hset_to_vec") {
        if (c->args.size() != 1) {
          errAt(errs, c->line, c->col, cc + " expects 1 argument (set)");
          for (auto& a : c->args) checkExpr(a.get());
          return makeDynArray(BType::i64);
        }
        BType ct = (cc=="set_to_vec") ? needContainer(0, BType::Tag::set_, "set")
                                      : needContainer(0, BType::Tag::hset_, "hash set");
        if (ct.tag == BType::Tag::set_ || ct.tag == BType::Tag::hset_)
          return makeDynArray(setElemType(ct));
        return makeDynArray(BType::i64);
      }
    }


    if (!funcs.count(c->callee) && c->callee == "mmio_load") {
      if (c->args.size() != 1) {
        errAt(errs, c->line, c->col, "mmio_load expects 1 argument (a *T pointer)");
        for (auto& a : c->args) checkExpr(a.get());
        return BType::i64;
      }
      BType pt = checkExpr(c->args[0].get());
      if (pt.tag != BType::Tag::ptr)
        errAt(errs, c->args[0]->line, c->args[0]->col,
              "mmio_load requires a pointer (use &T / *T), got " + typeSpelling(pt));
      else if (!isScalar(pointee(pt)) && pointee(pt).tag != BType::Tag::ptr)
        errAt(errs, c->args[0]->line, c->args[0]->col,
              "mmio_load requires a *T where T is a scalar (got *" + typeSpelling(pointee(pt)) + ")");
      return pt.tag == BType::Tag::ptr ? pointee(pt) : BType::i64;
    }
    if (!funcs.count(c->callee) && c->callee == "mmio_store") {
      if (c->args.size() != 2) {
        errAt(errs, c->line, c->col, "mmio_store expects 2 arguments (*T, value)");
        for (auto& a : c->args) checkExpr(a.get());
        return BType::void_;
      }
      BType pt = checkExpr(c->args[0].get());
      if (pt.tag != BType::Tag::ptr)
        errAt(errs, c->args[0]->line, c->args[0]->col,
              "mmio_store requires a pointer (use &T / *T), got " + typeSpelling(pt));
      BType vt = checkExpr(c->args[1].get());
      BType want = (pt.tag == BType::Tag::ptr) ? pointee(pt) : BType::void_;
      if (want != BType::void_ && vt != want &&
          !litAssignable(c->args[1].get(), vt, want) &&
          !dynamic_cast<CastExpr*>(c->args[1].get()))
        errAt(errs, c->args[1]->line, c->args[1]->col,
              "mmio_store value type does not match pointer pointee");
      return BType::void_;
    }
    if (!funcs.count(c->callee) && c->callee == "memset") {
      if (c->args.size() != 3) {
        errAt(errs, c->line, c->col, "memset expects 3 arguments (*T, u8 fill, i64 byte count)");
        for (auto& a : c->args) checkExpr(a.get());
        return BType::void_;
      }
      BType pt = checkExpr(c->args[0].get());
      if (pt.tag != BType::Tag::ptr)
        errAt(errs, c->args[0]->line, c->args[0]->col,
              "memset requires a pointer as the first argument, got " + typeSpelling(pt));
      BType fv = checkExpr(c->args[1].get());
      if (!isInt(fv)) errAt(errs, c->args[1]->line, c->args[1]->col, "memset fill must be an integer (u8)");
      BType cv = checkExpr(c->args[2].get());
      if (!isInt(cv)) errAt(errs, c->args[2]->line, c->args[2]->col, "memset count must be an integer (i64)");
      return BType::void_;
    }
    if (!funcs.count(c->callee) && c->callee == "memcpy") {
      if (c->args.size() != 3) {
        errAt(errs, c->line, c->col, "memcpy expects 3 arguments (*dst, *src, i64 byte count)");
        for (auto& a : c->args) checkExpr(a.get());
        return BType::void_;
      }
      BType dpt = checkExpr(c->args[0].get());
      BType spt = checkExpr(c->args[1].get());
      if (dpt.tag != BType::Tag::ptr)
        errAt(errs, c->args[0]->line, c->args[0]->col,
              "memcpy requires a pointer as the first argument, got " + typeSpelling(dpt));
      if (spt.tag != BType::Tag::ptr)
        errAt(errs, c->args[1]->line, c->args[1]->col,
              "memcpy requires a pointer as the second argument, got " + typeSpelling(spt));
      BType cv = checkExpr(c->args[2].get());
      if (!isInt(cv)) errAt(errs, c->args[2]->line, c->args[2]->col, "memcpy count must be an integer (i64)");
      return BType::void_;
    }


    if (!funcs.count(c->callee) && c->callee == "str_ptr") {
      if (c->args.size() != 1) {
        errAt(errs, c->line, c->col, "str_ptr expects 1 argument (a str)");
        for (auto& a : c->args) checkExpr(a.get());
        return makePtr(BType::u8);
      }
      BType st = checkExpr(c->args[0].get());
      if (st != BType::str)
        errAt(errs, c->args[0]->line, c->args[0]->col,
              "str_ptr requires a str, got " + typeSpelling(st));
      return makePtr(BType::u8);
    }

    if (!funcs.count(c->callee) && c->callee == "len") {
      if (c->args.size() != 1) {
        errAt(errs, c->line, c->col, "len expects 1 argument");
        for (auto& a : c->args) checkExpr(a.get());
        return BType::i64;
      }
      BType t = checkExpr(c->args[0].get());
      if (t.tag != BType::Tag::array && t.tag != BType::Tag::dynarray &&
          t != BType::str &&
          t.tag != BType::Tag::map_ && t.tag != BType::Tag::set_ &&
          t.tag != BType::Tag::hmap_ && t.tag != BType::Tag::hset_)
        errAt(errs, c->args[0]->line, c->args[0]->col,
              "len requires an array, string, map, or set");
      return BType::i64;
    }
    if (!funcs.count(c->callee) && c->callee == "push") {
      if (c->args.size() != 2) {
        errAt(errs, c->line, c->col, "push expects 2 arguments (vec, value)");
        for (auto& a : c->args) checkExpr(a.get());
        return BType::void_;
      }
      BType vt = checkExpr(c->args[0].get());
      if (vt.tag != BType::Tag::dynarray) {
        errAt(errs, c->args[0]->line, c->args[0]->col, "push requires a vec as the first argument");
      } else {
        BType et = checkExpr(c->args[1].get());
        BType want = dynArrayElem(vt);
        if (et != want && !litAssignable(c->args[1].get(), et, want) &&
            !dynamic_cast<CastExpr*>(c->args[1].get()))
          errAt(errs, c->args[1]->line, c->args[1]->col, "push value type does not match vec element type");
      }
      return BType::void_;
    }
    if (!funcs.count(c->callee) && c->callee == "sort") {
      if (c->args.size() != 1) {
        errAt(errs, c->line, c->col, "sort expects 1 argument (a vec)");
        for (auto& a : c->args) checkExpr(a.get());
        return BType::void_;
      }
      BType vt = checkExpr(c->args[0].get());
      if (vt.tag != BType::Tag::dynarray) {
        errAt(errs, c->args[0]->line, c->args[0]->col, "sort requires a vec as the argument");
      } else {
        BType et = dynArrayElem(vt);


        if (!isScalar(et) && et.tag != BType::Tag::enum_)
          errAt(errs, c->args[0]->line, c->args[0]->col,
                "sort requires a vec of scalar/str/enum elements (got " +
                typeSpelling(et) + ")");
      }
      return BType::void_;
    }
    auto bit = builtins.find(c->callee);
    if (bit != builtins.end() && !funcs.count(c->callee)) {

      for (auto& arg : c->args) {
        BType at = checkExpr(arg.get());
        (void)at;
      }

      if (c->callee == "abs") {
        BType t = c->args.empty() ? BType::void_ : checkExpr(c->args[0].get());
        if (!isNumeric(t))
          errAt(errs, c->line, c->col, "abs requires a numeric argument",
                 "abs(int) or abs(f64)  -  abs returns the same type");
      } else if (c->callee == "sqrt") {
        BType t = c->args.empty() ? BType::void_ : checkExpr(c->args[0].get());
        if (t != BType::f64) errAt(errs, c->line, "sqrt requires f64");
      } else if (c->callee == "itos") {
        BType t = c->args.empty() ? BType::void_ : checkExpr(c->args[0].get());
        if (t != BType::i64) errAt(errs, c->line, "itos requires i64");
      } else if (c->callee == "stoi" || c->callee == "stod") {
        BType t = c->args.empty() ? BType::void_ : checkExpr(c->args[0].get());
        if (t != BType::str) errAt(errs, c->line, c->callee + " requires str");
      } else if (c->callee == "ftos") {
        BType t = c->args.empty() ? BType::void_ : checkExpr(c->args[0].get());
        if (t != BType::f64) errAt(errs, c->line, "ftos requires f64");
      } else if (c->callee == "char_to_str") {
        BType t = c->args.empty() ? BType::void_ : checkExpr(c->args[0].get());
        if (t != BType::char_ && !isIntLitExpr(c->args[0].get()))
          errAt(errs, c->line, "char_to_str requires char");
      } else if (c->callee == "substr") {
        if (c->args.size() != 3) {
          errAt(errs, c->line, "substr expects 3 args (str, start, len)");
        } else {
          BType t0 = checkExpr(c->args[0].get());
          BType t1 = checkExpr(c->args[1].get());
          BType t2 = checkExpr(c->args[2].get());
          if (t0 != BType::str) errAt(errs, c->args[0]->line, "substr requires str");
          if (!isInt(t1) && !isIntLitExpr(c->args[1].get()))
            errAt(errs, c->args[1]->line, "substr start must be an int");
          if (!isInt(t2) && !isIntLitExpr(c->args[2].get()))
            errAt(errs, c->args[2]->line, "substr len must be an int");
        }
      } else if (c->callee == "index_of") {
        if (c->args.size() != 2) {
          errAt(errs, c->line, "index_of expects 2 args (str, char)");
        } else {
          BType t0 = checkExpr(c->args[0].get());
          BType t1 = checkExpr(c->args[1].get());
          if (t0 != BType::str) errAt(errs, c->args[0]->line, "index_of requires str");
          if (t1 != BType::char_ && !isIntLitExpr(c->args[1].get()))
            errAt(errs, c->args[1]->line, "index_of char must be a char");
        }
      } else if (c->callee == "read_file" || c->callee == "file_exists") {
        BType t = c->args.empty() ? BType::void_ : checkExpr(c->args[0].get());
        if (t != BType::str) errAt(errs, c->line, c->callee + " requires str");
      } else {
        if ((int)c->args.size() != bit->second.second)
          errAt(errs, c->line, c->callee + " expects " +
                std::to_string(bit->second.second) + " args, got " +
                std::to_string(c->args.size()));
      }


      if (c->callee == "abs" && !c->args.empty()) {
        BType at = checkExpr(c->args[0].get());
        if (isNumeric(at)) return at;
      }
      // Polymorphic vec reduces / accessors: the return type is the vec's
      // element type (for sum it's promoted to f64 on a float vec, i64 on int).
      if ((c->callee == "sum" || c->callee == "vmin" || c->callee == "vmax" ||
           c->callee == "first" || c->callee == "last") && !c->args.empty()) {
        BType vt = checkExpr(c->args[0].get());
        if (vt.tag == BType::Tag::dynarray) {
          BType et = dynArrayElem(vt);
          if (c->callee == "sum" || c->callee == "vmin" || c->callee == "vmax" ||
              c->callee == "first" || c->callee == "last")
            return et;
        }
      }
      // split returns a vec[str] (a fresh dynarray of str), regardless of the
      // table placeholder.
      if (c->callee == "split") return makeDynArray(BType::str);
      return bit->second.first;
    }
    auto it = funcs.find(c->callee);
    if (it == funcs.end()) {
      std::vector<std::string> fc;
      for (auto& kv : funcs) fc.push_back(kv.first);
      static const char* builtin[] = {"abs","imin","imax","fmin","fmax","sqrt",
        "itos","stoi","stod","ftos","char_to_str","substr","index_of",
        "read_line","read_file","file_open","file_close",
        "file_read","file_write","file_exists","print"};
      for (auto b : builtin) fc.push_back(b);
      auto s = suggest(c->callee, fc);
      errAt(errs, c->line, c->col, "call to undeclared function '" + c->callee + "'",
            s.empty() ? "" : "did you mean '" + s + "'?");
      for (auto& arg : c->args) checkExpr(arg.get());
      return BType::void_;
    }
    // ox:proof Lemma / ghost fn call-site restriction. A lemma (FuncSig::isLemma) or a
    // ghost fn (FuncSig::isGhost) is a PROOF-ONLY helper callable ONLY from a
    // spec/proof context (a `proof { ... }` block or another lemma's body),
    // never from runtime code. `inSpecContext_` is raised by the lemma body-
    // check loop and the ProofBlockStmt arm of checkStmt; when it's 0 the call
    // is in a runtime frame and must be rejected. We still type-check the args
    // (so the error message is the lemma-call diagnostic, not a cascade) and
    // return the fn's declared return type so downstream typing proceeds.
    if ((it->second.isLemma || it->second.isGhost) && inSpecContext_ == 0) {
      const char* kind = it->second.isLemma ? "lemma" : "ghost fn";
      errAt(errs, c->line, c->col,
            std::string(kind) + " '" + c->callee +
            "' is a proof-only helper  -  callable only from a `proof { ... }`"
            " block or another lemma/spec context, not from runtime code");
      for (auto& arg : c->args) checkExpr(arg.get());
      return it->second.retType;
    }
    // Effect system  -  propagation + purity gate at the user-function-call
    // resolution point. The callee's FuncSig (`it->second`) carries the effects
    // it copied from its FuncDecl. Two cases:
    //   (1) extern callee (`it->second.isExtern`): an extern fn has no Oxide-
    //       side body and no declared effects (parseFunc skips the contract
    //       tail loop for extern decls), so we conservatively treat the call as
    //       the built-in `io` effect. A pure caller may not call it; a non-pure
    //       caller must declare `io` (handled by synthesising an `io`-effect
    //       callee Façade below).
    //   (2) ordinary callee: forward to checkEffectPropagation, which reports
    //       the first callee effect the caller forgot to declare.
    if (it->second.isExtern) {
      // Extern: io effect (unless the extern is itself marked pure  -  but
      // externs skip the effects clause, so isPure is its default true; an
      // extern declared `effects { }` is pure-by-declaration and exempt). We
      // treat only the pure-default + no-explicit-effects externs as `io`; an
      // extern that explicitly declares other effects via a hand-written
      // FuncSig is not produced by the parser today, so this path is the real
      // one. For a pure caller this is a purity violation; for a non-pure
      // caller that forgot `io`, it's a propagation miss.
      if (it->second.effects.empty()) {
        // ox:why Build a one-effect `io` façade so the propagation helper reports a
        // consistent diagnostic for both pure and non-pure callers. Mark the
        // façade `effectsExplicit` so the propagation gate (which requires
        // BOTH caller and callee to have written an `effects` clause) admits
        // it  -  the extern's io effect is a compiler-known builtin, not a
        // user-declared effect, but it should still propagate into a caller
        // that opted into effect tracking.
        FuncSig ioCallee = it->second;
        ioCallee.effects = {"io"};
        ioCallee.isPure = false;
        ioCallee.effectsExplicit = true;
        if (curFunc_ && curFunc_->isPure) {
          checkPurityViolation(c->line, c->col, "io",
                               "via call to extern '" + c->callee + "'");
        } else {
          checkEffectPropagation(c->line, c->col, c->callee, ioCallee);
        }
      } else {
        // Extern that somehow carries declared effects (forward-compat): fall
        // through to the ordinary callee propagation path.
        checkEffectPropagation(c->line, c->col, c->callee, it->second);
      }
    } else {
      checkEffectPropagation(c->line, c->col, c->callee, it->second);
    }
    // Default arguments: fill trailing defaulted slots from the callee's stored
    // defaults so the user may omit them at the call site. If a slot has no
    // default, fillDefaultArgs reports the arity error and we skip the redundant
    // mismatch message below.
    size_t np = it->second.paramTypes.size();
    bool filled = false;
    if (c->args.size() < np && hasAnyDefault(c->callee))
      filled = fillDefaultArgs(c->callee, c->args, np, c->line, c->col,
                               "function '" + c->callee + "'");
    if (c->args.size() != np && !filled)
      errAt(errs, c->line, "function '" + c->callee + "' expects " +
            std::to_string(np) + " args, got " +
            std::to_string(c->args.size()));
    for (size_t k = 0; k < c->args.size(); k++) {
      BType at = checkExpr(c->args[k].get());
      BType pt = (k < it->second.paramTypes.size()) ? it->second.paramTypes[k] : BType::void_;
      bool ok = (at == pt) || implicitAssignable(at, pt);
      if (!ok) ok = litAssignable(c->args[k].get(), at, pt);
      if (!ok) ok = (dynamic_cast<CastExpr*>(c->args[k].get()) != nullptr);

      if (!ok && pt.tag == BType::Tag::ptr && at == pointee(pt) && isLvalueExpr(c->args[k].get()))
        ok = true;
      // C-style void* decay: any addressable value may be passed where an
      // opaque `&u8` (byte pointer) param is expected  -  we take its address
      // and bitcast to i8*. Mirrors C's implicit `T* -> void*` decay, so a
      // typed struct lvalue flows straight into an `extern fn f(p: &u8)`.
      if (!ok && pt.tag == BType::Tag::ptr && pointee(pt) == BType::u8 &&
          isLvalueExpr(c->args[k].get()) &&
          (at.tag == BType::Tag::struct_ || at.tag == BType::Tag::array ||
           isInt(at) || at == BType::char_ || at == BType::bool_ ||
           at.tag == BType::Tag::ptr))
        ok = true;
      // explicit `&typed_value` (a pointer arg, e.g. `&wc`) also decays to a
      // `&u8` param  -  like C `T* -> void*` on a pointer argument.
      if (!ok && pt.tag == BType::Tag::ptr && pointee(pt) == BType::u8 &&
          at.tag == BType::Tag::ptr)
        ok = true;
      if (!ok)
        errAt(errs, c->args[k]->line, "argument " + std::to_string(k + 1) +
              " of '" + c->callee + "' has wrong type");
    }
    return it->second.retType;
  }
  if (auto ix = dynamic_cast<Index*>(e)) {
    BType baseT = checkExpr(ix->base.get());


    if (baseT.tag == BType::Tag::ptr && pointee(baseT).tag == BType::Tag::array)
      baseT = pointee(baseT);
    BType idxT = checkExpr(ix->index.get());


    BType recvT = baseT;
    if (recvT.tag == BType::Tag::ptr && pointee(recvT).tag == BType::Tag::struct_)
      recvT = pointee(recvT);
    if (recvT.tag == BType::Tag::struct_ &&
        methods.count(recvT.structName) && methods[recvT.structName].count("__index")) {
      const MethodInfo& mi = methods[recvT.structName].at("__index");
      if (mi.paramTypes.size() != 1) {
        errAt(errs, ix->line, ix->col, "__index must take 1 argument, got " +
              std::to_string(mi.paramTypes.size()));
      } else if (idxT != mi.paramTypes[0] &&
                 !litAssignable(ix->index.get(), idxT, mi.paramTypes[0]) &&
                 !dynamic_cast<CastExpr*>(ix->index.get())) {
        errAt(errs, ix->index->line, "__index argument type " + typeSpelling(idxT) +
              " does not match expected " + typeSpelling(mi.paramTypes[0]));
      }
      ix->methodOverload = true;
      ix->overloadStruct = recvT.structName;
      ix->overloadMethod = "__index";
      ix->overloadRecvType = recvT;
      ix->recvByRef = mi.selfByRef;
      return mi.retType;
    }
    if (baseT.tag == BType::Tag::map_ || baseT.tag == BType::Tag::hmap_) {
      // `m[k]` read on a map / hmap  -  the index is the KEY (not an int offset),
      // and the result is the value type. Validate the key type (allowing a
      // literal coercion). IRGen reads via the runtime (ox_map_get /
      // ox_hmap_get) instead of an addressable load (entries are in the table).
      BType keyT = mapKeyType(baseT);
      if (idxT != keyT && !litAssignable(ix->index.get(), idxT, keyT) &&
          !dynamic_cast<CastExpr*>(ix->index.get()))
        errAt(errs, ix->index->line, ix->index->col,
              "map key type " + typeSpelling(idxT) + " does not match expected " +
              typeSpelling(keyT));
      return mapValType(baseT);
    }
    if (!isInt(idxT)) errAt(errs, ix->line, "array index must be an int");
    if (baseT.tag == BType::Tag::array) return arrayElem(baseT);
    if (baseT.tag == BType::Tag::dynarray) return dynArrayElem(baseT);
    if (baseT.tag == BType::Tag::ptr) return pointee(baseT);
    if (baseT == BType::str) return BType::char_;
    errAt(errs, ix->line, "cannot index a non-array value");
    return BType::i64;
  }
  if (auto dn = dynamic_cast<DynNew*>(e)) {


    dn->elemType = fixType(dn->elemType);
    if (fieldByteWidth(dn->elemType) <= 0) {
      errAt(errs, e->line, 0, "vec element type has no known size (use a scalar, struct, array, or nested vec)",
            "a vec element must have a fixed compile-time byte size");
    }
    return makeDynArray(dn->elemType);
  }
  if (auto mn = dynamic_cast<MapNew*>(e)) {


    mn->keyType = fixType(mn->keyType);
    mn->valType = fixType(mn->valType);
    if (!isScalar(mn->keyType) && mn->keyType.tag != BType::Tag::enum_)
      errAt(errs, e->line, 0, "map keys must be scalar/str/enum (got " + typeSpelling(mn->keyType) + ")");
    else if (mn->keyType == BType::void_)
      errAt(errs, e->line, 0, "map keys may not be void");
    if (fieldByteWidth(mn->valType) <= 0 && mn->valType.tag != BType::Tag::map_ &&
        mn->valType.tag != BType::Tag::hmap_ && mn->valType.tag != BType::Tag::dynarray &&
        mn->valType.tag != BType::Tag::set_ && mn->valType.tag != BType::Tag::hset_)
      errAt(errs, e->line, 0, "map value type has no known size (got " + typeSpelling(mn->valType) + ")",
            "values may be scalars, str, structs, fixed arrays, vec, or another collection");
    return makeMapType(mn->keyType, mn->valType);
  }
  if (auto hmn = dynamic_cast<HMapNew*>(e)) {

    hmn->keyType = fixType(hmn->keyType);
    hmn->valType = fixType(hmn->valType);
    if (!isScalar(hmn->keyType) && hmn->keyType.tag != BType::Tag::enum_)
      errAt(errs, e->line, 0, "hmap keys must be scalar/str/enum (got " + typeSpelling(hmn->keyType) + ")");
    else if (hmn->keyType == BType::void_)
      errAt(errs, e->line, 0, "hmap keys may not be void");
    if (fieldByteWidth(hmn->valType) <= 0 && hmn->valType.tag != BType::Tag::map_ &&
        hmn->valType.tag != BType::Tag::hmap_ && hmn->valType.tag != BType::Tag::dynarray &&
        hmn->valType.tag != BType::Tag::set_ && hmn->valType.tag != BType::Tag::hset_)
      errAt(errs, e->line, 0, "hmap value type has no known size (got " + typeSpelling(hmn->valType) + ")",
            "values may be scalars, str, structs, fixed arrays, vec, or another collection");
    return makeHMapType(hmn->keyType, hmn->valType);
  }
  if (auto sn = dynamic_cast<SetNew*>(e)) {

    sn->elemType = fixType(sn->elemType);
    if (!isScalar(sn->elemType) && sn->elemType.tag != BType::Tag::enum_)
      errAt(errs, e->line, 0, "set elements must be scalar/str/enum (got " + typeSpelling(sn->elemType) + ")");
    else if (sn->elemType == BType::void_)
      errAt(errs, e->line, 0, "set elements may not be void");
    return makeSetType(sn->elemType);
  }
  if (auto hsn = dynamic_cast<HSetNew*>(e)) {

    hsn->elemType = fixType(hsn->elemType);
    if (!isScalar(hsn->elemType) && hsn->elemType.tag != BType::Tag::enum_)
      errAt(errs, e->line, 0, "hset elements must be scalar/str/enum (got " + typeSpelling(hsn->elemType) + ")");
    else if (hsn->elemType == BType::void_)
      errAt(errs, e->line, 0, "hset elements may not be void");
    return makeHSetType(hsn->elemType);
  }
  // `Channel<T>::new()`  -  construct a new buffered channel. The element type
  // comes from the parsed `ChannelNew::elemType` (set by the Parser from the
  // `[T]`/`<T>` argument). We fix it and require a scalar/str/enum element
  // (the runtime stores elements boxed by their byte width; non-scalar
  // elements are rejected up front to match vec/set). The channel value
  // itself is pointer-width (i8*).
  if (auto cn = dynamic_cast<ChannelNew*>(e)) {
    cn->elemType = fixType(cn->elemType);
    if (!isScalar(cn->elemType) && cn->elemType.tag != BType::Tag::enum_)
      errAt(errs, e->line, 0, "channel elements must be scalar/str/enum (got " +
            typeSpelling(cn->elemType) + ")");
    else if (cn->elemType == BType::void_)
      errAt(errs, e->line, 0, "channel elements may not be void");
    return makeChannelType(cn->elemType);
  }
  // `chan <- val`  -  send `val` into the channel `chan`. `chan` must have a
  // `channel_` type (produced by `Channel<T>::new()` or a `let` of one); the
  // value must be assignable to the channel's element type. A send produces no
  // value (void_), so it is used as a statement-expression.
  if (auto cs = dynamic_cast<ChannelSend*>(e)) {
    BType ct = checkExpr(cs->chan.get());
    if (ct.tag != BType::Tag::channel_) {
      errAt(errs, e->line, 0, "send target is not a channel (got " +
            typeSpelling(ct) + ")");
      (void)checkExpr(cs->val.get());
      return BType::void_;
    }
    BType et = channelElemType(ct);
    BType vt = checkExpr(cs->val.get());
    if (vt != et && !implicitAssignable(vt, et)) {
      errAt(errs, e->line, 0, "channel send value type " + typeSpelling(vt) +
            " does not match channel element type " + typeSpelling(et));
    }
    return BType::void_;
  }
  // ox:unsafe `<- chan`  -  receive one value from `chan`. `chan` must be a channel; the
  // result type is the channel's element type. We cache `elemType` on the node
  // so IRGen knows what to load after @ox_chan_recv (the recv runtime returns
  // the value via a typed register matching the channel's element width).
  if (auto cr = dynamic_cast<ChannelRecv*>(e)) {
    BType ct = checkExpr(cr->chan.get());
    if (ct.tag != BType::Tag::channel_) {
      errAt(errs, e->line, 0, "receive source is not a channel (got " +
            typeSpelling(ct) + ")");
      cr->elemType = BType::i64;
      return BType::i64;
    }
    cr->elemType = channelElemType(ct);
    return cr->elemType;
  }
  // `spawn <body>`  -  type-check the body expression in the current scope (the
  // closure captures locals by ref/val like a lambda). resultTy is the body's
  // type for the bare-expression form; the parser pre-set void_ for a
  // multi-statement block, which we leave as the thread's (void) result. The
  // spawn expression itself is a thread handle (i8* currentThread), so we
  // return i8* rather than the body type: the spawn synthesises a joinable
  // thread, NOT the body's value (the body runs detached). This matches
  // @ox_thread_create returning i8*.
  if (auto sp = dynamic_cast<SpawnExpr*>(e)) {
    if (sp->body) {
      BType bt = checkExpr(sp->body.get());
      // For the bare-expression form the parser leaves resultTy as the default
      // (void_); record the body type so IRGen/the frontend can report it, but
      // the spawn VALUE is the thread handle (i8*), not the body value.
      if (sp->resultTy == BType::void_) sp->resultTy = bt;
    }
    // Thread handle: pointer-width.
    return BType::i64;
  }
  if (auto fl = dynamic_cast<Field*>(e)) {
    BType baseT = checkExpr(fl->base.get());

    if (baseT.tag == BType::Tag::ptr && pointee(baseT).tag == BType::Tag::struct_)
      baseT = pointee(baseT);
    if (baseT.tag != BType::Tag::struct_) {
      errAt(errs, fl->line, "field access requires a struct value");
      return BType::i64;
    }
    StructDef* d = findStruct(baseT.structName);
    if (!d) {
      errAt(errs, fl->line, "unknown struct '" + baseT.structName + "'");
      return BType::i64;
    }
    if (d->isOpaque) {
      errAt(errs, fl->line, "opaque struct '" + baseT.structName +
            "' has no fields (declared `extern struct`; only `*" +
            baseT.structName + "` handles are usable)");
      return BType::i64;
    }
    int32_t fi = structFieldIndex(d, fl->field);
    if (fi < 0) {
      errAt(errs, fl->line, "struct '" + baseT.structName + "' has no field '" + fl->field + "'");
      return BType::i64;
    }
    // The synthetic __oxvt vtable-ptr is not a user-acceessable field.
    if (d->fields[fi].name == kVtableFieldName) {
      errAt(errs, fl->line, "field '" + fl->field + "' is the vtable slot of a "
            "polymorphic struct and cannot be accessed directly");
      return BType::i64;
    }
    if (d->fields[fi].isPrivate && !canTouchPrivate(baseT.structName)) {
      errAt(errs, fl->line, fl->col, "field '" + fl->field + "' of '" + baseT.structName +
            "' is private", "access it through the struct's impl methods");
      return d->fields[fi].type;
    }
    return d->fields[fi].type;
  }
  if (auto al = dynamic_cast<ArrayLit*>(e)) {
    if (al->elems.empty()) {
      errAt(errs, al->line, "array literal must have at least one element");
      return makeArrayType(BType::i64, 0);
    }
    BType et = checkExpr(al->elems[0].get());
    for (size_t i = 1; i < al->elems.size(); i++) {
      BType t = checkExpr(al->elems[i].get());


      t = coerceIntLit(al->elems[i].get(), t, et);
      t = coerceFloatLit(al->elems[i].get(), t, et);
      if (t != et && !(isInt(et) && isIntLitExpr(al->elems[i].get())) &&
          !litAssignable(al->elems[i].get(), t, et))
        errAt(errs, al->elems[i]->line, "array literal elements must have the same type");
    }
    return makeArrayType(et, (int32_t)al->elems.size());
  }
  if (auto sl = dynamic_cast<StructLit*>(e)) {


    if (sl->hasTypeArgs) {
      std::vector<BType> args; for (auto& a : sl->typeArgs) args.push_back(fixType(a));
      // ox:why Fill trailing defaults from the template's tparams so the constraint
      // check sees the same bindings the monomorphiser will use.
      const StructDecl* tmpl = findGenericStruct(sl->name);
      if (tmpl) {
        size_t i = args.size();
        while (i < tmpl->typeParams.size() && i < tmpl->tparams.size() &&
               tmpl->tparams[i].hasDefault) {
          args.push_back(fixType(tmpl->tparams[i].defaultType));
          i++;
        }
        // Validate concept constraints on the (filled) type args.
        std::map<std::string, BType> env;
        for (size_t k = 0; k < tmpl->typeParams.size() && k < args.size(); k++)
          env[tmpl->typeParams[k]] = args[k];
        checkConstraints(tmpl->tparams, env, sl->line, sl->col);
      }
      BType inst = instantiateGenericStruct(sl->name, args);
      sl->name = inst.structName;
      sl->hasTypeArgs = false;
    } else if (!findStruct(sl->name)) {
      // Bare `Name { ... }` (no explicit `<...>` type args) naming a generic
      // struct template whose params ALL have defaults (C++-style
      // `template <typename T = i64>`). Fill every trailing default and
      // instantiate, so `Tagged { payload: Named::new(5) }` works the same as
      // `Tagged<Named> { ... }`. Mixed (some defaulted, some not) is left to
      // the explicit-args path: a bare literal with a missing non-defaulted
      // param falls through to the "unknown struct" report below, which is the
      // honest diagnosis (we never manufactured partial type args).
      const StructDecl* tmpl = findGenericStruct(sl->name);
      if (tmpl) {
        bool allDefaulted = !tmpl->tparams.empty();
        for (const auto& tp : tmpl->tparams) if (!tp.hasDefault) { allDefaulted = false; break; }
        if (allDefaulted) {
          std::vector<BType> args;
          for (const auto& tp : tmpl->tparams) args.push_back(fixType(tp.defaultType));
          std::map<std::string, BType> env;
          for (size_t k = 0; k < tmpl->typeParams.size() && k < args.size(); k++)
            env[tmpl->typeParams[k]] = args[k];
          checkConstraints(tmpl->tparams, env, sl->line, sl->col);
          BType inst = instantiateGenericStruct(sl->name, args);
          sl->name = inst.structName;
        }
      }
    }
    StructDef* d = findStruct(sl->name);
    if (!d) {
      errAt(errs, sl->line, "unknown struct '" + sl->name + "'");
      for (auto& v : sl->values) checkExpr(v.get());
      return sl->name.empty() ? BType::void_ : (BType{BType::Tag::struct_, 0, 0, sl->name});
    }
    if (d->isOpaque) {
      errAt(errs, sl->line, sl->line, "cannot construct an opaque struct '" + sl->name +
            "' (declared `extern struct`; only `*" + sl->name +
            "` handles are usable)", "declare a normal `struct` to build values");
      for (auto& v : sl->values) checkExpr(v.get());
      return BType{BType::Tag::struct_, 0, 0, sl->name};
    }

    std::vector<char> seen(d->fields.size(), 0);
    for (size_t i = 0; i < sl->fieldNames.size() && i < sl->values.size(); i++) {
      int32_t fi = structFieldIndex(d, sl->fieldNames[i]);
      if (fi < 0) {
        errAt(errs, sl->line, "struct '" + sl->name + "' has no field '" + sl->fieldNames[i] + "'");
        checkExpr(sl->values[i].get());
        continue;
      }
      // The synthetic __oxvt vtable-ptr field is set by IRGen at construction,
      // never by the user  -  naming it in a literal is rejected (the field does
      // not belong to the user's view of the struct).
      if (d->fields[fi].name == kVtableFieldName) {
        errAt(errs, sl->line, "field '" + sl->fieldNames[i] + "' is the vtable slot "
              "of a polymorphic struct and cannot be set in a literal");
        checkExpr(sl->values[i].get());
        continue;
      }
      if (d->fields[fi].isPrivate && !canTouchPrivate(sl->name)) {
        errAt(errs, sl->line, sl->line, "field '" + sl->fieldNames[i] + "' of '" + sl->name +
              "' is private", "construct the value through the struct's impl, e.g. " +
              sl->name + "::new(...)");

      }
      if (seen[fi]) errAt(errs, sl->line, "field '" + sl->fieldNames[i] + "' set more than once");
      seen[fi] = 1;
      BType vt = checkExpr(sl->values[i].get());
      BType ft = d->fields[fi].type;
      if (vt != ft && !litAssignable(sl->values[i].get(), vt, ft) &&
          !dynamic_cast<CastExpr*>(sl->values[i].get()))
        errAt(errs, sl->values[i]->line, "field '" + sl->fieldNames[i] + "' type mismatch");
    }
    if (sl->fieldNames.size() < d->fields.size()) {


      for (size_t i = 0; i < d->fields.size(); i++)
        if (!seen[i]) {
          bool priv = d->fields[i].isPrivate;
          if (priv && !canTouchPrivate(sl->name)) continue;
          // The __oxvt vtable-ptr slot is auto-initialized by IRGen, never "user-
          // missing".
          if (d->fields[i].name == kVtableFieldName) continue;
          errAt(errs, sl->line, "struct literal '" + sl->name + "' is missing field '" +
                d->fields[i].name + "'");
          break;
        }
    }
    return BType{BType::Tag::struct_, 0, 0, sl->name};
  }
  if (auto mc = dynamic_cast<MethodCall*>(e)) {
    BType rt = checkExpr(mc->receiver.get());

    if (rt.tag == BType::Tag::ptr && pointee(rt).tag == BType::Tag::struct_)
      rt = pointee(rt);
    if (rt.tag != BType::Tag::struct_) {
      errAt(errs, mc->line, mc->col, "method call requires a struct receiver");
      for (auto& a : mc->args) checkExpr(a.get());
      return BType::void_;
    }
    const std::string& sn = rt.structName;
    const MethodInfo* mip = resolveMethod(sn, mc->callee);   // walks base chain
    if (!mip) {
      // build candidate list from this struct + its bases for diagnostics
      std::vector<std::string> cands;
      for (const StructDef* d = findStruct(sn); d; d = d->base) {
        auto sit = methods.find(d->name);
        if (sit != methods.end()) for (auto& kv : sit->second) cands.push_back(kv.first);
      }
      auto s = suggest(mc->callee, cands);
      errAt(errs, mc->line, mc->col,
            "struct '" + sn + "' has no method '" + mc->callee + "'",
            s.empty() ? "" : "did you mean '" + s + "'?");
      for (auto& a : mc->args) checkExpr(a.get());
      return BType::void_;
    }
    const MethodInfo& mi = *mip;
    if (!mi.hasSelf) {
      errAt(errs, mc->line, mc->col,
            "'" + mc->callee + "' is an associated function (call it as " + sn +
            "::" + mc->callee + ", not via a receiver)");
      for (auto& a : mc->args) checkExpr(a.get());
      return mi.retType;
    }
    size_t mnp = mi.paramTypes.size();
    bool mfilled = false;
    if (mc->args.size() < mnp && hasAnyDefault(mi.mangled))
      mfilled = fillDefaultArgs(mi.mangled, mc->args, mnp, mc->line, mc->col,
                                 "method");
    if (mc->args.size() != mnp && !mfilled)
      errAt(errs, mc->line, mc->col, "method '" + sn + "::" + mc->callee +
            "' expects " + std::to_string(mnp) + " args, got " +
            std::to_string(mc->args.size()));
    for (size_t k = 0; k < mc->args.size(); k++) {
      BType at = checkExpr(mc->args[k].get());
      BType pt = (k < mi.paramTypes.size()) ? mi.paramTypes[k] : BType::void_;
      bool ok = (at == pt) || implicitAssignable(at, pt) ||
                litAssignable(mc->args[k].get(), at, pt) ||
                dynamic_cast<CastExpr*>(mc->args[k].get());
      if (!ok && pt.tag == BType::Tag::ptr && at == pointee(pt) &&
          isLvalueExpr(mc->args[k].get())) ok = true;
      // void* decay  -  see the same comment in the Call path above.
      if (!ok && pt.tag == BType::Tag::ptr && pointee(pt) == BType::u8 &&
          isLvalueExpr(mc->args[k].get()) &&
          (at.tag == BType::Tag::struct_ || at.tag == BType::Tag::array ||
           isInt(at) || at == BType::char_ || at == BType::bool_ ||
           at.tag == BType::Tag::ptr))
        ok = true;
      // explicit &T pointer arg decays to &u8.
      if (!ok && pt.tag == BType::Tag::ptr && pointee(pt) == BType::u8 &&
          at.tag == BType::Tag::ptr)
        ok = true;
    }
    mc->receiverByRef = mi.selfByRef;
    mc->recvType = rt;
    // A by-value `self` receiver MOVES the receiver when the struct has a
    // destructor (the callee owns and drops it; the caller may not use the
    // receiver afterward). A borrowed `&self` receiver does not transfer
    // ownership. Mirrors the IRGen by-value-receiver markMovedOut in
    // emitMethodCall, on the Sema side so use-after-move is reported.
    if (mi.hasSelf && !mi.selfByRef && structHasDrop(sn))
      noteMovedFrom(mc->receiver.get(), rt);
    return mi.retType;
  }
  if (auto ac = dynamic_cast<AssocCall*>(e)) {

    std::string sn = ac->typeName;
    if (isAliasName(sn)) sn = resolveAlias(sn).second.structName;
    // A generic struct template with all-default type args: `Box::make(7)` has
    // ac->typeName == "Box" and there is no concrete StructDef "Box" (only the
    // instantiations). The front impl-walk registered the template's methods
    // under the template name AND mirrored the default-instantiation's clones
    // (whose `mangled` points at the emitted body). So when `findStruct(sn)`
    // fails but `findGenericStruct(sn)` succeeded and `methods[sn]` holds the
    // callable entries, treat it as resolved; the default-instantiation clone
    // is the one an arg-less associated call targets (a caller wanting a
    // different instantiation writes `Box::<u32>::make` etc.  -  out of scope
    // here). If neither path applies, surface the original error.
    if (!findStruct(sn)) {
      const StructDecl* tmpl = findGenericStruct(sn);
      if (tmpl) {
        bool allDefaulted = !tmpl->tparams.empty();
        for (const auto& tp : tmpl->tparams) if (!tp.hasDefault) { allDefaulted = false; break; }
        if (allDefaulted && methods.count(sn) && methods.at(sn).count(ac->callee)) {
          // fall through  -  `methods.find(sn)` below finds the default-inst clone.
        } else {
          errAt(errs, ac->line, ac->col, "associated call on unknown type '" + ac->typeName + "'");
          for (auto& a : ac->args) checkExpr(a.get());
          return BType::void_;
        }
      } else {
        errAt(errs, ac->line, ac->col, "associated call on unknown type '" + ac->typeName + "'");
        for (auto& a : ac->args) checkExpr(a.get());
        return BType::void_;
      }
    }
    auto sit = methods.find(sn);
    // resolveMethod walks the base chain via findStruct(structName); for a
    // generic struct template there is no StructDef "Box" (only the
    // instantiations), so it returns null even though methods[sn] holds the
    // associated fn. Fall back to the direct table entry in that case.
    const MethodInfo* mip = resolveMethod(sn, ac->callee);   // base-chain fallback
    if (!mip && sit != methods.end()) {
      auto mit = sit->second.find(ac->callee);
      if (mit != sit->second.end()) mip = &mit->second;
    }
    if (!mip) {
      std::vector<std::string> cands;
      if (sit != methods.end()) for (auto& kv : sit->second) cands.push_back(kv.first);
      auto s = suggest(ac->callee, cands);
      errAt(errs, ac->line, ac->col,
            "type '" + sn + "' has no associated function '" + ac->callee + "'",
            s.empty() ? "" : "did you mean '" + s + "'?");
      for (auto& a : ac->args) checkExpr(a.get());
      return BType::void_;
    }
    const MethodInfo& mi = *mip;
    if (mi.hasSelf) {
      errAt(errs, ac->line, ac->col,
            "'" + ac->callee + "' is a method (call it via a receiver, not " +
            sn + "::" + ac->callee + ")");
      for (auto& a : ac->args) checkExpr(a.get());
      return mi.retType;
    }
    size_t anp = mi.paramTypes.size();
    bool afilled = false;
    if (ac->args.size() < anp && hasAnyDefault(mi.mangled))
      afilled = fillDefaultArgs(mi.mangled, ac->args, anp, ac->line, ac->col,
                                 "associated function");
    if (ac->args.size() != anp && !afilled)
      errAt(errs, ac->line, ac->col, "'" + sn + "::" + ac->callee +
            "' expects " + std::to_string(anp) + " args, got " +
            std::to_string(ac->args.size()));
    for (size_t k = 0; k < ac->args.size(); k++) {
      BType at = checkExpr(ac->args[k].get());
      BType pt = (k < mi.paramTypes.size()) ? mi.paramTypes[k] : BType::void_;
      bool ok = (at == pt) || implicitAssignable(at, pt) ||
                litAssignable(ac->args[k].get(), at, pt) ||
                dynamic_cast<CastExpr*>(ac->args[k].get());
      if (!ok && pt.tag == BType::Tag::ptr && at == pointee(pt) &&
          isLvalueExpr(ac->args[k].get())) ok = true;
      // void* decay  -  see the same comment in the Call path above.
      if (!ok && pt.tag == BType::Tag::ptr && pointee(pt) == BType::u8 &&
          isLvalueExpr(ac->args[k].get()) &&
          (at.tag == BType::Tag::struct_ || at.tag == BType::Tag::array ||
           isInt(at) || at == BType::char_ || at == BType::bool_ ||
           at.tag == BType::Tag::ptr))
        ok = true;
      // explicit &T pointer arg decays to &u8.
      if (!ok && pt.tag == BType::Tag::ptr && pointee(pt) == BType::u8 &&
          at.tag == BType::Tag::ptr)
        ok = true;
    }
    return mi.retType;
  }
  if (auto rng = dynamic_cast<RangeLit*>(e)) {
    // A range literal is ONLY legal as the direct iterable of `for x in <range>`,
    // where the ForStmt branch handles it before reaching checkExpr. Anywhere
    // else (let, arg, return, expression) it is a compile error: a range is not
    // a first-class value you can store, pass, or return. We still type-check
    // the operands so the operands' own errors surface, and return i64 so any
    // downstream context doesn't see a void hole.
    (void)checkExpr(rng->lo.get());
    (void)checkExpr(rng->hi.get());
    errAt(errs, e->line,
          "range expression 'a..b' is only valid as the iterable of 'for x in ...'");
    return BType::i64;
  }
  // --- Contract spec expressions ---
  // `old(x)` is valid ONLY inside an `ensures` clause (the only place where a
  // pre/post correspondence makes sense). Elsewhere it is a hard error. The
  // sub-expression is checked in the current scope (params visible); the whole
  // node is boolean-valued.
  if (auto o = dynamic_cast<OldExpr*>(e)) {
    if (!inEnsures_) {
      errAt(errs, o->line,
            "'old(...)' is only legal inside an 'ensures' clause");
      (void)checkExpr(o->sub.get());
      return BType::bool_;
    }
    // `old(x)` carries the type of x (i64 if x is i64), so it can appear as a
    // comparison operand in `ensures result > old(x)`. Returning bool_ here
    // would force `i64 vs bool_` and trip "comparison operands must match".
    return checkExpr(o->sub.get());
  }
  // declared in an ephemeral scope for the body check only. The range operands
  // are integer expressions checked normally. The whole node is boolean.
  if (auto q = dynamic_cast<QuantExpr*>(e)) {
    (void)checkExpr(q->lo.get());
    (void)checkExpr(q->hi.get());
    // ox:why Declare the binder in a fresh scope so the body can reference it; the
    // binder's type defaults to i64 (quantifiers are over integers here).
    if (q->binderType == BType::void_) q->binderType = BType::i64;
    pushScope();
    declare(q->binder, q->binderType, true, false, false, q->line);
    (void)checkExpr(q->body.get());
    popScope();
    return BType::bool_;
  }
  // --- Advanced math expressions ---
  // PowerExpr: `base ** exponent` (also the lowering target for the Unicode
  // superscript glyphs ²/³ and the `pow2`/`pow3` ASCII fallbacks, which the
  // Parser emits as PowerExpr with a constant IntLit exponent). The result is
  // always a float (f64 by default; f32 only when BOTH operands are f32, so
  // `2.0f ** 3` stays f32 but `2 ** 3.0` widens to f64). Integer bases with an
  // integer exponent are still typed f64 here to match the pow/math.h lowering
  // in IRGen  -  exponentiation is a real-valued operation, not integer power.
  // Operands must be numeric; bool/str/struct/ptr bases are rejected.
  if (auto p = dynamic_cast<PowerExpr*>(e)) {
    BType bt = checkExpr(p->base.get());
    BType et = checkExpr(p->exponent.get());
    bt = coerceIntLit(p->base.get(), bt, et);
    et = coerceIntLit(p->exponent.get(), et, bt);
    bt = coerceFloatLit(p->base.get(), bt, et);
    et = coerceFloatLit(p->exponent.get(), et, bt);
    if (!isNumeric(bt) || !isNumeric(et)) {
      errAt(errs, p->line, p->col,
            "'**' (power) operands must be numeric, got " +
            typeSpelling(bt) + " and " + typeSpelling(et));
      p->resultType = BType::f64;
      return BType::f64;
    }
    // ox:why Result type: f32 iff both sides are f32; otherwise f64. This mirrors the
    // BinaryExpr arithmetic widening rule (numeric operands of the same width
    // keep that width) but forces a float result even for int**int.
    BType rt = (bt == BType::f32 && et == BType::f32) ? BType::f32 : BType::f64;
    p->resultType = rt;
    return rt;
  }
  // MatrixLit: `[[a, b], [c, d]]`  -  a row-major 2D matrix literal. Every
  // element must share a single numeric element type (inferred from the first
  // element, with int/float literal coercion applied to the rest), and every
  // row must have the same column count. The element type is cached on the
  // node (m->elemType) for IRGen/SMT and the expression's BType is
  // array<array<elemType, cols>, rows> so it composes with the existing array
  // machinery. An empty matrix or a row of width 0 is a hard error (we can't
  // form a zero-width array type), but we still recurse to surface operand
  // errors and return a default f64 array so downstream context isn't left
  // with a void hole.
  if (auto m = dynamic_cast<MatrixLit*>(e)) {
    if (m->rows.empty()) {
      errAt(errs, m->line, m->col, "matrix literal must have at least one row");
      m->elemType = BType::f64;
      return makeArrayType(makeArrayType(BType::f64, 0), 0);
    }
    // Determine the column count from the first row.
    size_t cols = m->rows[0].size();
    if (cols == 0) {
      errAt(errs, m->line, m->col, "matrix literal row must have at least one element");
      m->elemType = BType::f64;
      return makeArrayType(makeArrayType(BType::f64, 0), (int32_t)m->rows.size());
    }
    // Element type is inferred from the (0,0) element; the rest are coerced
    // to it, exactly like ArrayLit's element uniformity check.
    BType et = checkExpr(m->rows[0][0].get());
    for (size_t r = 0; r < m->rows.size(); r++) {
      if (m->rows[r].size() != cols) {
        errAt(errs, m->line, m->col,
              "matrix literal rows must all have the same number of columns "
              "(row " + std::to_string(r) + " has " +
              std::to_string(m->rows[r].size()) + ", expected " +
              std::to_string(cols) + ")");
      }
      for (size_t c = 0; c < m->rows[r].size(); c++) {
        BType t = checkExpr(m->rows[r][c].get());
        t = coerceIntLit(m->rows[r][c].get(), t, et);
        t = coerceFloatLit(m->rows[r][c].get(), t, et);
        if (c == 0 && r == 0) { et = t; continue; }
        if (t != et &&
            !(isInt(et) && isIntLitExpr(m->rows[r][c].get())) &&
            !litAssignable(m->rows[r][c].get(), t, et)) {
          errAt(errs, m->rows[r][c]->line, m->rows[r][c]->col,
                "matrix literal elements must have the same type (got " +
                typeSpelling(t) + ", expected " + typeSpelling(et) + ")");
        }
      }
    }
    if (!isNumeric(et)) {
      errAt(errs, m->line, m->col,
            "matrix literal elements must be numeric, got " + typeSpelling(et));
      et = BType::f64;
    }
    m->elemType = et;
    return makeArrayType(makeArrayType(et, (int32_t)cols), (int32_t)m->rows.size());
  }
  // SolveExpr: `A \ b`  -  MATLAB-style left-division, i.e. solve the linear
  // system A*x = b for x. The lhs is expected to be a matrix (a 2D MatrixLit
  // or an array-of-array value) and the rhs is a vector or scalar. We type-
  // check both sides; the result is a float (f64 by default, f32 only when
  // both sides carry f32 elements) matching `**`/matrix widening. Operands
  // that are not arrays/numeric are rejected so a stray `a \ b` on two scalars
  // surfaces a clear error rather than a silent IRGen mis-compile (the linear
  // solve is only defined on matrix lhs). We still recurse into both sides so
  // their own sub-errors are reported.
  if (auto s = dynamic_cast<SolveExpr*>(e)) {
    BType lt = checkExpr(s->lhs.get());
    BType rt = checkExpr(s->rhs.get());
    bool lhsOk = (lt.tag == BType::Tag::array) || isNumeric(lt);
    bool rhsOk = (rt.tag == BType::Tag::array) || isNumeric(rt);
    if (!lhsOk) {
      errAt(errs, s->line, s->col,
            "'\\' (solve) left operand must be a matrix or numeric, got " +
            typeSpelling(lt));
    }
    if (!rhsOk) {
      errAt(errs, s->line, s->col,
            "'\\' (solve) right operand must be a vector/numeric, got " +
            typeSpelling(rt));
    }
    // Result type: f32 iff both sides' element types are f32; else f64. For an
    // array lhs we peek through the (possibly nested) array element type to
    // pick f32 vs f64; for a numeric operand we use it directly.
    BType lelem = lt;
    if (lt.tag == BType::Tag::array) {
      // Walk down nested arrays to the scalar element type.
      while (lelem.tag == BType::Tag::array) lelem = arrayElem(lelem);
    }
    BType relem = rt;
    if (rt.tag == BType::Tag::array) {
      while (relem.tag == BType::Tag::array) relem = arrayElem(relem);
    }
    BType rt2 = (lelem == BType::f32 && relem == BType::f32) ? BType::f32 : BType::f64;
    // Result is a column vector: array<array<elem, 1>, lhsRows>.  For a
    // matrix lhs `A` of shape m×n and rhs `b` of shape m (or m×1), the
    // solution x has shape n×1 (column vector).  We pick up the dimension
    // from the lhs outer array count (number of rows of A == number of
    // equations == m); the solve result has the same number of rows as A
    // has columns, but since we model square systems here we use m as the
    // vector length for the 2-D column-vector wrapper.
    int lhsRows = 1;
    if (lt.tag == BType::Tag::array) lhsRows = lt.count;
    s->resultType = makeArrayType(makeArrayType(rt2, 1), lhsRows);
    return s->resultType;
  }
  // MathSymExpr: a standalone Unicode math symbol used as a bare value.
  // The Parser builds a MathSymExpr only for `pi`/`π` (the circle constant);
  // `√` and `∫` are routed to Call/IntegrateExpr nodes by the parser, and the
  // superscript glyphs `²`/`³` are handled as SuperscriptExpr/PowerExpr in
  // parsePostfix, so the ONLY MathSymExpr that should reach here has
  // `text == "pi"` (ASCII form) or `text == "\u03c0"` (the raw `π` bytes, if a
  // non-normalising path produced them).  These are the supported math-symbol
  // constants; we resolve the result type to f64 so IRGen can emit a call to
  // `ox_pi()`.
  //
  // Any OTHER text here is a programmer bug waiting to happen: previously we
  // silently accepted it and IRGen's fallback routed every unknown glyph to
  // `ox_e()` (Euler's number, 2.71828…) with NO error, so `τ`, `Σ`, `∞`, etc.
  // would silently evaluate to e.  Now we whitelist the supported symbols and
  // emit a hard compile error for unknown ones so a typo like `τ` is caught at
  // Sema instead of silently miscompiling.  (We still return f64 on the error
  // path so downstream passes don't cascade; the compile already failed.)
  if (auto ms = dynamic_cast<MathSymExpr*>(e)) {
    if (ms->text == "pi" || ms->text == "\u03c0") {
      ms->resultType = BType::f64;
      return BType::f64;
    }
    errAt(errs, ms->line, ms->col,
          "unknown math symbol: '" + ms->text + "'",
          "supported Unicode math constants are: \u03c0 (pi), \u221a (sqrt), "
          "\u222b (integrate), \u00b2 (x\u00b2 / pow2), \u00b3 (x\u00b3 / pow3); "
          "use the matching ASCII keyword (pi/sqrt/integrate/pow2/pow3) if your "
          "editor can't type the glyph");
    ms->resultType = BType::f64;
    return BType::f64;
  }
  // IntegrateExpr: ∫(lo, hi, body, samples).  Numeric integration requires a
  // function of exactly ONE f64 parameter returning f64  -  the IRGen path lowers
  // the integrand to `double (double)*` and hands it to `@ox_integrate_trapz`,
  // so any other signature would silently miscompile (wrong bitcast) or crash at
  // runtime.  This arm enforces that contract at Sema time:
  //   - When the body is a Call to / bare VarRef naming a top-level fn in the
  //     `funcs` table, we verify that fn's signature is `(f64) -> f64`.
  //   - When the body is anything else (a lambda / fn-ptr value / expression),
  //     Sema does not have a structural fn-type to check today, so we fall back
  //     to requiring the body's value type to BE f64 (a single-arg f64 caller is
  //     expected to type as f64 itself); IRGen's bitcast path for the generic
  //     value case uses the resolved value type, so this is consistent.
  // In all cases we still recurse into lo/hi/body first for sub-error
  // reporting, then emit an `errAt` if the contract is violated.  The result is
  // always f64 (the runtime returns double; the f32-result shortcut was never
  // actually wired through IRGen  -  see the IRGen arm which unconditionally
  // returns f64  -  so we collapse that branch here too).
  if (auto ie = dynamic_cast<IntegrateExpr*>(e)) {
    BType loT = checkExpr(ie->lo.get());
    BType hiT = checkExpr(ie->hi.get());
    BType bodyT = checkExpr(ie->body.get());
    (void)loT; (void)hiT; // validated for sub-errors
    // Resolve the integrand's function signature when possible.  The Parser
    // builds `∫ f from a to b` (body = VarRef) and `∫ f(x) from a to b` (body
    // = Call to `f`), so both forms must be handled.  We look the name up in
    // Sema's own `funcs` table (the same table IRGen consults).
    std::string bodyFnName;
    int bodyLine = ie->body ? ie->body->line : ie->line;
    int bodyCol  = ie->body ? ie->body->col  : ie->col;
    if (auto c = dynamic_cast<Call*>(ie->body.get())) {
      bodyFnName = c->callee;
      // ox:why Prefer the Call node's own position so the diagnostic points at the
      // integrand as written, not the outer `∫` glyph.
      bodyLine = c->line; bodyCol = c->col;
    } else if (auto v = dynamic_cast<VarRef*>(ie->body.get())) {
      bodyFnName = v->name;
      bodyLine = v->line; bodyCol = v->col;
    }
    bool sigOk = false;
    if (!bodyFnName.empty()) {
      auto it = funcs.find(bodyFnName);
      if (it != funcs.end()) {
        const FuncSig& sig = it->second;
        if (sig.paramTypes.size() == 1 &&
            sig.paramTypes[0] == BType::f64 &&
            sig.retType == BType::f64) {
          sigOk = true;
        } else {
          // Build a readable signature string like `(f64, i64) -> f32`.
          std::string psp;
          for (size_t i = 0; i < sig.paramTypes.size(); ++i) {
            if (i) psp += ", ";
            psp += typeSpelling(sig.paramTypes[i]);
          }
          errAt(errs, bodyLine, bodyCol,
                "integrate integrand '" + bodyFnName + "' must have signature "
                "(f64) -> f64, but is declared (" + psp + ") -> " +
                typeSpelling(sig.retType),
                "numeric integration calls the function as double(*)(double); "
                "any other arity/types would miscompile the bitcast or crash");
        }
      }
      // If the name is NOT in `funcs` we let the normal undefined-reference /
      // VarRef path surface its own error (don't double-report here).
    } else if (ie->body) {
      // Lambda / fn-ptr value path: we have no FuncSig to inspect, so require
      // the value itself to be f64 (an f64-returning single-arg caller types as
      // f64 in this AST).  If it isn't, report at the body node.
      if (bodyT == BType::f64) {
        sigOk = true;
      } else {
        errAt(errs, bodyLine, bodyCol,
              "integrate integrand must return f64 for numeric integration, "
              "but the integrand expression has type " + typeSpelling(bodyT),
              "the integrand is called as double(*)(double); derive it from a "
              "fn(x: f64) -> f64");
      }
    } else {
      // No integrand at all (Parser allows a degenerate form on error recovery).
      errAt(errs, ie->line, ie->col,
            "integrate requires an integrand body before 'from'");
    }
    // Result type: the IRGen arm always returns f64 (the runtime returns
    // double), regardless of the f32 shortcut the old comment claimed.  Keep
    // that here so Sema and IRGen agree  -  the f32 branch was dead code.
    (void)bodyT; // only used for the lambda/fn-ptr diagnostic above
    BType rt2 = BType::f64;
    ie->resultType = rt2;
    // On a signature mismatch we still return a sane type so downstream passes
    // don't cascade (the compile already failed via errAt).
    (void)sigOk;
    return rt2;
  }
  // MatMulExpr: a * b where both sides are matrices.  The Sema pass is
  // supposed to create this from a BinaryExpr `*` when both operands are
  // matrix-typed, but that routing is deferred (the current Pipeline just
  // uses the multiplicative runtime `ox_mat_mul`).  When a MatMulExpr does
  // arrive here (e.g. from manual AST construction) we type-check both
  // sides and carry the element type.
  if (auto mm = dynamic_cast<MatMulExpr*>(e)) {
    BType lt = checkExpr(mm->lhs.get());
    BType rt = checkExpr(mm->rhs.get());
    // Walk down nested arrays to find the scalar element type.
    BType lelem = lt;
    while (lelem.tag == BType::Tag::array) lelem = arrayElem(lelem);
    BType relem = rt;
    while (relem.tag == BType::Tag::array) relem = arrayElem(relem);
    // Result element: f32 iff both sides are f32; else f64.
    mm->elemType = (lelem == BType::f32 && relem == BType::f32) ? BType::f32 : BType::f64;
    // Result is a 2-D array: array<array<elem, rhsCols>, lhsRows>.
    // A is m×n, B is n×k, result is m×k.
    int lhsRows = lt.count;
    int rhsCols = arrayElem(rt).count;
    return makeArrayType(makeArrayType(mm->elemType, rhsCols), lhsRows);
  }
  // SuperscriptExpr: x² or x³  -  the parser turns postfix superscript
  // glyphs into a SuperscriptExpr with a pre-parsed integer exponent.
  // The result type matches the base type (i64 base → i64 result, f64
  // base → f64 result), matching the semantics of `ox_ipow`/`ox_pow_f64`.
  if (auto ss = dynamic_cast<SuperscriptExpr*>(e)) {
    BType baseType = checkExpr(ss->base.get());
    if (!isNumeric(baseType)) {
      errAt(errs, ss->line, ss->col,
            "superscript base must be numeric, got " + typeSpelling(baseType));
    }
    ss->resultType = isNumeric(baseType) ? baseType : BType::f64;
    return ss->resultType;
  }
  return BType::void_;
}

// Walk an init expression down to its root VarRef, looking through Field access
// (a.b.c -> a) and deref (*p -> p's root). Returns the root var name or "".
static std::string moveRootVar(Expr* e) {
  if (!e) return "";
  if (auto v = dynamic_cast<VarRef*>(e)) return v->name;
  if (auto f = dynamic_cast<Field*>(e)) return moveRootVar(f->base.get());
  if (auto u = dynamic_cast<UnaryExpr*>(e)) {
    if (u->op == UnaryExpr::Op::deref) return moveRootVar(u->base.get());
  }
  return "";
}

void Sema::noteMovedFrom(Expr* init, const BType& initType) {
  // Any struct with a destructor is move-only on implicit copy (let/assign/pass-
  // by-value): the source local is CONSUMED by the move, and use-after-move is a
  // compile error in checkExpr/VarRef. A `clone()` method is an explicit
  // opt-in copy the user calls by name (a.clone()); it does NOT re-enable
  // implicit copying  -  exactly like Rust, where `Clone` does not make a
  // `let b = a` copy (only `Copy` does, and Oxide has no `Copy` trait). A plain
  // (no-Drop) struct is a cheap bitwise copy with no ownership transfer.
  if (initType.tag != BType::Tag::struct_) return;
  const std::string& sn = initType.structName;
  if (!structHasDrop(sn)) return;   // no destructor -> no move discipline
  std::string root = moveRootVar(init);
  if (!root.empty()) movedVars_.insert(root);
}

// ox:unsafe Type-check a list of contract spec expressions. Each must be boolean; a non-
// bool result is a Sema error. When `ensures` is true, `inEnsures_` is set for
// the duration so `old(x)` is admitted (and rejected elsewhere by checkExpr).
// Caller has already pushed the scope the names should resolve in (the function
// param scope for requires/ensures; the loop body scope for invariants).
void Sema::checkContractExprs(const std::vector<ExprPtr>& exprs, bool ensures) {
  bool saved = inEnsures_;
  for (auto& e : exprs) {
    if (!e) continue;
    inEnsures_ = ensures;
    BType t = checkExpr(e.get());
    if (t != BType::bool_)
      errAt(errs, e->line, "contract clause must be a boolean expression (got " +
            typeSpelling(t) + ")");
  }
  inEnsures_ = saved;
}

// Check a function's `requires` and `ensures` clauses in its parameter scope.
// Requires reference params only; ensures may also use `old(x)` and the special
// name `result` (the function's return value). Called once per defined (non-
// extern) function, right after its params are declared and before its body.
void Sema::checkFuncContracts(const FuncDecl& fn) {
  checkContractExprs(fn.requires_, false);
  // In the ensures pass, declare `result` as a binding of the return type so an
  // `ensures result >= 0` clause can name the returned value. Lives only for the
  // ensures check (popped right after); declared mutable is immaterial here.
  if (!fn.ensures_.empty() && fn.retType != BType::void_) {
    pushScope();
    declare("result", fn.retType, true, false, false, fn.line);
    checkContractExprs(fn.ensures_, true);
    popScope();
  } else {
    checkContractExprs(fn.ensures_, true);
  }
}

// --- Effect system ---
// checkEffectPropagation: enforce the rule "a function that calls a fn with
// effect E must itself declare E." `curFunc_` is the CALLER's FuncSig (set by
// the body-check loop right before checkBlock runs); `callee` is the callee's
// FuncSig looked up from `funcs`. For each effect on the callee not present in
// the caller's effects, emit a Sema error naming the missing effect and the
// call that introduced it. When curFunc_ is null (no active function frame  - 
// e.g. during global initialiser or top-level contract checks) we skip: the
// propagation rule only makes sense inside a function body. Built-in/stdlib
// callables have an empty `effects` (no FuncDecl behind them) and thus nothing
// to propagate; their purity is handled separately for `print`/`asm`/extern.
void Sema::checkEffectPropagation(int line, int col,
                                  const std::string& calleeName,
                                  const FuncSig& callee) {
  if (!curFunc_) return;                          // not inside a function body
  if (!curFunc_->effectsExplicit) return;         // caller omitted `effects`  -  untracked
  if (!callee.effectsExplicit) return;            // callee omitted `effects`  -  its effects unknown
  if (callee.effects.empty()) return;             // pure callee  -  nothing to propagate
  // Caller's declared effect set (membership lookup).
  std::set<std::string> callerEffects(curFunc_->effects.begin(),
                                      curFunc_->effects.end());
  for (const auto& eff : callee.effects) {
    if (callerEffects.count(eff)) continue;   // declared  -  OK
    // Missing. A pure caller hits this branch for the callee's FIRST effect
    // (its effect set is empty); a non-pure caller hits it only for effects
    // it genuinely forgot. Tailor the hint to which case it is.
    if (curFunc_->isPure) {
      errAt(errs, line, col,
            "pure function performs effect '" + eff + "' (via call to '" +
                calleeName + "') but declares no effects",
            "remove the call, or add `effects { " + eff + " }` to the caller");
    } else {
      errAt(errs, line, col,
            "function performs effect '" + eff + "' (via call to '" +
                calleeName + "') but does not declare it in its effects",
            "add `effects { " + eff + " }` to the caller's effects clause");
    }
    return;   // one diagnostic per call is enough; further misses repeat
  }
}

// checkPurityViolation: the per-effect purity gate for `asm!`, `print`, and
// calls to `extern fn`s. Each of those is a side-effect a pure function
// (curFunc_->isPure) is forbidden from performing, and none of them flows
// through the user-function-call path that checkEffectPropagation covers
// (asm! is an expr, print is a built-in, an extern has no FuncDecl effects).
// `eff` is the effect name; `detail` is human context for the message
// (e.g. the extern name, or "(inline assembly)"). No-op when curFunc_ is null
// or the caller isn't pure  -  the propagation path handles non-pure callers.
void Sema::checkPurityViolation(int line, int col, const char* eff,
                                const std::string& detail) {
  if (!curFunc_ || !curFunc_->isPure) return;          // only pure callers are gated
  if (!curFunc_->effectsExplicit) return;             // caller omitted `effects`  -  untracked
  errAt(errs, line, col,
        "pure function performs effect '" + std::string(eff) + "' (" + detail +
            ") but declares no effects",
        "remove the operation, or add `effects { " + std::string(eff) +
            " }` to the caller");
}

// ox:proof T3-1  -  pre-declare every `ghost let` in a function body before contract
// clauses are type-checked. Contract resolution (checkFuncContracts, above)
// runs BEFORE the body walk (`checkBlock`), so a `ghost let g: i64;` in the
// body is NOT yet in scope when an `ensures result == g` clause is checked  - 
// Sema would reject `g` as undeclared. This pre-scanner walks the body's
// nesting shapes (Block / If / While / For / Defer  -  the shapes the normal
// body walker recurses into) and `declare`s each ghost let's name + type into
// the current scope (the function's param scope, pushed by the caller). The
// body walk later re-declares the same name when it reaches the `ghost let`;
// the re-declare is a harmless overwrite of the same scope-slot with the same
// type. Type-fixing (`fixType`) is applied here so a `ghost let g: MyAlias;`
// resolves the alias identically in the clause and the body.
//
// Scope note: we deliberately do NOT push a new scope here. Ghost lets
// "hoist" to the function's param scope for contract-visibility purposes  - 
// the SMT encoder resolves them via nameMap keys (`ghost_<fn>_<name>`) that
// are function-scoped, not lexically nested, so a contract clause and the body
// must agree on the one function-level symbol. Lexically, a `ghost let`
// inside an inner `{ }` block still Sema-scopes to the fn param scope here;
// the body walker's later declare hits the same slot. This is a deliberate
// T3-1 simplification: a ghost let in a conditionally-executed branch is
// still visible to the contract  -  sound, since ghost lets are spec-only and
// the SMT side models them as function-level uninterpreted consts anyway.
// Forward: the per-statement dispatcher is a private static Sema helper; it
// needs to call back into `predeclareGhostLets` for nested vector recursion
// and `declare` for ghost-let registration, so it's a member function too.
// Declared in Sema.h as `predeclareGhostLetsStmt`.

void Sema::predeclareGhostLets(const std::vector<StmtPtr>& stmts) {
  for (auto& s : stmts) {
    if (s) predeclareGhostLetsStmt(s.get());
  }
}

void Sema::predeclareGhostLetsStmt(Stmt* s) {
  if (!s) return;
  if (auto gl = dynamic_cast<GhostLetStmt*>(s)) {
    BType t = gl->typeAnnotated ? fixType(gl->type) : BType::i64;
    // ox:proof allowRedecl=true: the body walker later re-declares the same ghost let
    // name + type into the same scope-slot. That intentional overlap is the
    // whole point of the pre-declare pass (see predeclareGhostLets comment).
    declare(gl->name, t, gl->isMut, false, true, gl->line);
    return;   // don't recurse into a ghost let's init  -  spec-only
  }
  if (auto bl = dynamic_cast<Block*>(s)) {
    predeclareGhostLets(bl->stmts);
  } else if (auto sb = dynamic_cast<SyncBlock*>(s)) {
    predeclareGhostLets(sb->body);
  } else if (auto is = dynamic_cast<IfStmt*>(s)) {
    predeclareGhostLets(is->then);
    predeclareGhostLets(is->else_);
  } else if (auto ws = dynamic_cast<WhileStmt*>(s)) {
    predeclareGhostLets(ws->body);
  } else if (auto fs = dynamic_cast<ForStmt*>(s)) {
    predeclareGhostLets(fs->body);
  } else if (auto ds = dynamic_cast<DeferStmt*>(s)) {
    // DeferStmt holds a single StmtPtr (often a Block). We deliberately do
    // NOT recurse into it  -  ghost lets inside a `defer` are spec-exotic
    // (rare in practice) and the contract-visibility benefit there is
    // marginal. The body walker will still declare them when it reaches
    // the defer. Honest under-approximation, T3-1 scope.
    (void)ds;
  } else if (auto a = dynamic_cast<AssertStmt*>(s)) {
    // ox:proof `assert <expr> by { <hints> };`  -  pre-declare any ghost lets inside
    // the proof-hint block so they are visible to the SMT/IRGen pre-pass.
    predeclareGhostLets(a->byBody);
  } else if (auto cs = dynamic_cast<CalcStmt*>(s)) {
    // ox:proof calcD  -  pre-declare any ghost lets inside a calc hint block so they are
    // visible to the SMT/IRGen pre-pass. Each step's `hints` is a statement
    // list (same shape as AssertStmt::byBody above); we fold every step's
    // hints together so a ghost let declared in step i is visible to step i+1.
    for (auto& step : cs->steps) predeclareGhostLets(step.hints);
  }
}

void Sema::checkStmt(Stmt* s) {
  if (auto es = dynamic_cast<ExprStmt*>(s)) {
    checkExpr(es->expr.get());
    return;
  }
  if (auto ls = dynamic_cast<LetStmt*>(s)) {
    // ox:proof T2  -  a `ghost let` is a spec-only binding. It still has an Oxide type at
    // declaration time (the user wrote `: T`), so we register the name with
    // that type so downstream contracts and ghost clauses can reference it;
    // but we do NOT type-check its initializer expression for v1  -  a ghost
    // let's RHS may name other ghost lets or spec fns that aren't fully in the
    // runtime scope, and failing the build on that would bar honest use. The
    // Ghost encoder (src/Ghost.cpp) resolves names lazily at SMT-emit time.
    bool isGhostLet = (dynamic_cast<GhostLetStmt*>(s) != nullptr);
    if (isGhostLet) {
      if (!ls->typeAnnotated) {
        errAt(errs, ls->line, "ghost let requires an explicit ': T' annotation");
      } else {
        ls->type = fixType(ls->type);
      }
      // ox:proof allowRedecl=true: this ghost let was already pre-declared into the fn
      // param scope by predeclareGhostLetsStmt, with the same name + type.
      // The pre-declare + body-walk overlap is intentional (contract clauses
      // need the ghost name visible before the body walk reaches it).
      declare(ls->name, ls->type, ls->isMut, false, true, ls->line);
      return;
    }
    if (ls->typeAnnotated) ls->type = fixType(ls->type);
    BType t = ls->typeAnnotated ? ls->type : BType::void_;
    if (ls->init) {
      BType it = checkExpr(ls->init.get());
      if (!ls->typeAnnotated) {
        t = it;
      } else if (t != it) {
        bool ok = implicitAssignable(it, t) ||
                  litAssignable(ls->init.get(), it, t) ||
                  dynamic_cast<CastExpr*>(ls->init.get());


        if (!ok && t.tag == BType::Tag::array) {
          if (auto al = dynamic_cast<ArrayLit*>(ls->init.get())) {
            bool fits = true;
            BType want = arrayElem(t);
            for (auto& el : al->elems) {
              BType et = checkExpr(el.get());
              if (et != want && !litAssignable(el.get(), et, want) &&
                  !dynamic_cast<CastExpr*>(el.get())) { fits = false; break; }
            }
            if (fits) {


              for (auto& el : al->elems) {
                if (auto* ce = dynamic_cast<CastExpr*>(el.get())) { (void)ce; continue; }
                auto cast = std::make_unique<CastExpr>();
                cast->target = want;
                cast->e = std::move(el);
                el = std::move(cast);
              }
              ok = true;
            }
          }
        }
        if (!ok) errAt(errs, ls->line, "let initializer type does not match annotation");
      }
    }
    if (t == BType::void_) {
      errAt(errs, ls->line, "cannot infer type for '" + ls->name + "'");
      t = BType::i64;
    }
    if (t.tag == BType::Tag::array && t.count <= 0 && !ls->typeAnnotated) {

    }
    ls->type = t;
    ls->typeAnnotated = true;
    // RAII: if the bound type has a destructor, register it in this scope so
    // IRGen runs `drop` at scope exit (reverse declaration order) and on every
    // early-exit path. Globals are never in a drop scope.
    declare(ls->name, t, ls->isMut, false, false, ls->line);
    if (t.tag == BType::Tag::struct_ && structHasDrop(t.structName))
      recordDropLocal(ls->name);
    // Move discipline: binding from a move-only struct value consumes its root
    // local (use-after-move is then a compile error in checkExpr/VarRef).
    if (ls->init) noteMovedFrom(ls->init.get(), t);
    return;
  }
  if (auto rs = dynamic_cast<ReturnStmt*>(s)) {
    if (inDefer_ > 0)
      errAt(errs, rs->line, "'return' inside a 'defer' is not allowed");
    BType rt = rs->value ? checkExpr(rs->value.get()) : BType::void_;
    if (!curFunc_) return;
    // A capturing lambda is a closure struct, NOT a plain function pointer.
    // Just like C++, a capturing closure is not convertible to a bare `fn`
    // pointer type: returning one from a function declared `-> fn(...) -> ...`
    // would otherwise emit invalid IR (a struct value typed as i8*). Flag it
    // up-front with a clear diagnostic instead.
    if (rt.tag == BType::Tag::struct_ &&
        rt.structName.rfind("__oxclosure_", 0) == 0 &&
        curFunc_->retType.tag == BType::Tag::fn_) {
      errAt(errs, rs->line,
            "cannot return a capturing lambda as a plain 'fn' type; a capturing "
            "closure is not convertible to a function pointer (use it as a "
            "local, or declare the return as the lambda value type)");
    }
    if (rt != curFunc_->retType) {
      if (curFunc_->retType == BType::void_ && rs->value)
        errAt(errs, rs->line, "void function cannot return a value");
      else if (curFunc_->retType != BType::void_ && !rs->value)
        errAt(errs, rs->line, "non-void function must return a value");
      else if (!implicitAssignable(rt, curFunc_->retType) &&
               !litAssignable(rs->value.get(), rt, curFunc_->retType) &&
               !dynamic_cast<CastExpr*>(rs->value.get()))
        errAt(errs, rs->line, "return type does not match function signature");
    }
    return;
  }
  if (auto is = dynamic_cast<IfStmt*>(s)) {
    BType ct = checkExpr(is->cond.get());
    if (ct != BType::bool_) errAt(errs, is->line, "if condition must be bool");
    pushScope(); checkBlock(is->then); popScope();
    pushScope(); checkBlock(is->else_); popScope();
    return;
  }
  if (auto ws = dynamic_cast<WhileStmt*>(s)) {
    BType ct = checkExpr(ws->cond.get());
    if (ct != BType::bool_) errAt(errs, ws->line, "while condition must be bool");
    loopDepth_++;
    pushScope();
    // Loop invariants are checked inside the body scope (they may name the loop
    // state/condition vars). Each must be boolean.
    for (auto& inv : ws->invariants) {
      BType it = checkExpr(inv.get());
      if (it != BType::bool_) errAt(errs, inv ? inv->line : ws->line,
                                    "loop invariant must be a boolean expression");
    }
    checkBlock(ws->body);
    popScope();
    loopDepth_--;
    return;
  }
  if (auto fs = dynamic_cast<ForStmt*>(s)) {
    if (fs->isForeach) {
      // Integer range iterable: `for x in a..b` / `a..=b`. The element type is
      // i64 (range operands are integers). We type-check lo/hi directly here and
      // DO NOT call checkExpr on the RangeLit itself  -  that path rejects a range
      // used outside `for`, so reaching it here is the one legal site.
      if (auto rng = dynamic_cast<RangeLit*>(fs->iter.get())) {
        BType lt = checkExpr(rng->lo.get());
        if (!isInt(lt) && lt.tag != BType::Tag::usize &&
            lt.tag != BType::Tag::char_ && lt.tag != BType::Tag::enum_)
          errAt(errs, fs->line, "range lower bound must be an integer");
        BType ht = checkExpr(rng->hi.get());
        if (!isInt(ht) && ht.tag != BType::Tag::usize &&
            ht.tag != BType::Tag::char_ && ht.tag != BType::Tag::enum_)
          errAt(errs, fs->line, "range upper bound must be an integer");
        fs->elemType = BType::i64;
        pushScope();
        declare(fs->varName, BType::i64, true, false, false, fs->line);
        checkContractExprs(fs->invariants, false);
        loopDepth_++;
        checkBlock(fs->body);
        loopDepth_--;
        popScope();
        return;
      }
      BType it = checkExpr(fs->iter.get());
      BType et = BType::i64;
      if (it.tag == BType::Tag::map_ || it.tag == BType::Tag::hmap_) {
        // Map iteration. The single loop var binds to each key (the same shape
        // as hand-rolling `for k in map_keys(m)`); `for k, v in m` additionally
        // binds `v` to the value at key k.
        et = mapKeyType(it);
        fs->elemType = et;
        fs->isMapIter = true;
        fs->iterType = it;    // carry full type (map_ vs hmap_) for IRGen routing
        if (!fs->varName2.empty()) {
          fs->elemType2 = mapValType(it);
        }
        pushScope();
        declare(fs->varName, et, true, false, false, fs->line);
        if (!fs->varName2.empty()) declare(fs->varName2, mapValType(it), true, false, false, fs->line);
        checkContractExprs(fs->invariants, false);
        loopDepth_++;
        checkBlock(fs->body);
        loopDepth_--;
        popScope();
        return;
      }
      if (it.tag == BType::Tag::array)       et = arrayElem(it);
      else if (it.tag == BType::Tag::dynarray) et = dynArrayElem(it);
      else if (it == BType::str)             et = BType::char_;
      else errAt(errs, fs->line, "for-in requires an array, vec, or string");
      fs->elemType = et;
      // A second loop variable is only legal over a map (a single-element
      // iterable has no value slot). Reject here, not silently mis-handle.
      if (!fs->varName2.empty())
        errAt(errs, fs->line, "a second loop variable requires a map iterable (got " +
              typeSpelling(it) + ")");
      pushScope();
      declare(fs->varName, et, true, false, false, fs->line);
      checkContractExprs(fs->invariants, false);
      loopDepth_++;
      checkBlock(fs->body);
      loopDepth_--;
      popScope();
      return;
    }
    BType st = checkExpr(fs->start.get());
    pushScope();
    declare(fs->varName, BType::i64, true, false, false, fs->line);
    BType ct = fs->end ? checkExpr(fs->end.get()) : BType::bool_;
    if (ct != BType::bool_) errAt(errs, fs->line, "for condition must be bool");
    if (fs->step) checkExpr(fs->step.get());
    checkContractExprs(fs->invariants, false);
    loopDepth_++;
    checkBlock(fs->body);
    loopDepth_--;
    popScope();
    (void)st;
    return;
  }
  if (dynamic_cast<BreakStmt*>(s) || dynamic_cast<ContinueStmt*>(s)) {
    if (inDefer_ > 0)
      errAt(errs, s->line, "'break'/'continue' inside a 'defer' is not allowed");
    else if (loopDepth_ == 0) errAt(errs, s->line, "break/continue outside a loop");
    return;
  }
  if (auto b = dynamic_cast<Block*>(s)) {
    pushScope(); checkBlock(b->stmts); popScope();
    return;
  }
  if (auto sb = dynamic_cast<SyncBlock*>(s)) {
    // `sync { ... }`  -  semantically a plain block in a fresh Sema scope (its
    // own `let` names don't escape). IRGen wraps the body with @ox_sync_begin/
    // @ox_sync_end; Sema only needs to check the inner statements normally.
    pushScope(); checkBlock(sb->body); popScope();
    return;
  }
  if (auto a = dynamic_cast<AssertStmt*>(s)) {
    // `assert <expr>`  -  a runtime contract checkpoint. The expression is a spec
    // expression (so `forall`/`implies` are usable inside it), but `old(x)` is
    // NOT permitted (assert is a body statement, not an ensures clause).
    if (a->cond) {
      BType t = checkExpr(a->cond.get());
      if (t != BType::bool_)
        errAt(errs, a->line, "assert expression must be boolean (got " +
              typeSpelling(t) + ")");
    }
    // `assert <expr> by { <hints> };`  -  check each hint statement through the
    // normal statement-checking machinery (recurses into checkStmt per hint).
    // The hints are proof statements (assert, instantiate, lemma calls as
    // expr-stmts, calc blocks). To match ProofBlockStmt / CalcStmt semantics,
    // the byBody gets its own Sema scope (a `let` in a hint does not leak into
    // the enclosing scope) AND `inSpecContext_` is raised so lemma / spec-fn
    // calls and `instantiate` pragmas type-check inside the hint block.
    if (!a->byBody.empty()) {
      pushScope();
      int savedSpec = inSpecContext_;
      inSpecContext_ = 1;          // a proof-hint block is a spec context
      for (auto& h : a->byBody)
        checkStmt(h.get());
      inSpecContext_ = savedSpec;  // restore (compose with any outer frame)
      popScope();
    }
    return;
  }
  if (auto as = dynamic_cast<AssumeStmt*>(s)) {
    // `assume <expr>;` (non-trusted) and `trusted assume <expr>;`  -  a
    // hypothesis assumption. The condition is a spec expression (so
    // `forall`/`implies` are usable inside it, exactly like `assert`), but
    // `old(x)` is NOT permitted (an assume is a body statement, not an
    // ensures clause). It has NO runtime effect: IRGen skips it (no LLVM IR)
    // and it never appears in a runtime gate. Sema only verifies the condition
    // type-checks as boolean so a typo fails the build rather than silently
    // producing a bogus SMT premise. The SMT emitter (Driver.cpp's
    // smtEncodeStmt assumeStmt arm) emits `(assert <cond>)` so the assumed
    // fact becomes a premise for downstream discharge; a `trusted` assume is
    // additionally recorded in the proof report's trust audit. `sourceCitation`
    // is a plain string  -  no type checking needed.
    if (as->cond) {
      BType t = checkExpr(as->cond.get());
      if (t != BType::bool_)
        errAt(errs, as->line, "assume expression must be boolean (got " +
              typeSpelling(t) + ")");
    }
    return;
  }
  if (auto ds = dynamic_cast<DeferStmt*>(s)) {
    // The deferred statement runs at the ENCLOSING scope's exit, so it sees the
    // locals live at the point of `defer`. Type-check its body in the current
    // scope: a Block body pushes/ pops its own Sema scope (its own `let` names
    // don't persist), which matches IRGen not introducing a runtime scope for a
    // defer body's own locals either  -  those run inline at exit.
    if (!ds->body) return;
    inDefer_++;
    checkStmt(ds->body.get());
    inDefer_--;
    return;
  }
  if (auto is = dynamic_cast<InstantiateStmt*>(s)) {
    // ox:proof fixB  -  `instantiate` pragma: a ghost / spec-only statement that guides Z3
    // quantifier instantiation. It has NO runtime effect: IRGen skips it (no
    // LLVM IR), and it never appears in a runtime gate. Sema only verifies that
    // the constituent expressions type-check in the current scope so a typo
    // fails the build rather than silently producing a bogus SMT pattern.
    //
    // The quantifier `q` is a QuantExpr (forall/exists with binder, lo, hi,
    // body). We mirror the checkExpr QuantExpr arm: type-check the range bounds
    // in the current scope, then push an ephemeral scope declaring the binder
    // (default i64) so the body's VarRefs resolve. The body itself doesn't need
    // to be boolean here (an instantiate pattern can wrap any predicate), so we
    // accept whatever type it yields  -  only checking it type-checks without
    // raising undeclared-variable errors.
    if (!is->q) {
      errAt(errs, s->line, "instantiate pragma requires a quantifier (forall/exists)");
      return;
    }
    if (is->q->lo) (void)checkExpr(is->q->lo.get());
    if (is->q->hi) (void)checkExpr(is->q->hi.get());
    if (!is->q->body) {
      errAt(errs, s->line, "instantiate quantifier is missing its body predicate");
    } else {
      // Default the binder to i64 (quantifiers are over integers here) and
      // scope it only for the body check, exactly like checkExpr's QuantExpr.
      if (is->q->binderType == BType::void_) is->q->binderType = BType::i64;
      pushScope();
      declare(is->q->binder, is->q->binderType, true, false, false, s->line);
      (void)checkExpr(is->q->body.get());
      popScope();
    }
    // Ground form: `on k = <expr>`  -  witness is the value to substitute for the
    // bound variable. It must be an integer-typed expression visible in the
    // current (runtime) scope  -  the body's ephemeral binder scope is gone, so a
    // witness referencing the quantifier's own binder would correctly fail here.
    if (is->isGround) {
      if (!is->witness) {
        errAt(errs, s->line, "instantiate ground form requires a witness expression (`on k = <expr>`)");
      } else {
        BType wt = checkExpr(is->witness.get());
        if (wt != BType::i64 && wt != BType::i32 && wt != BType::u64 &&
            wt != BType::u32 && wt != BType::i16 && wt != BType::u16 &&
            wt != BType::i8  && wt != BType::u8)
          errAt(errs, s->line, "instantiate witness must be an integer expression (got " +
                typeSpelling(wt) + ")");
      }
    } else {
      // Pattern form: `on <t1>, <t2>, ...`  -  each pattern term is an array /
      // index expression whose `(select arr idx)` becomes Z3's :pattern. They
      // live in the current scope (not the binder scope) and only need to
      // type-check; any well-formed expression is a valid trigger candidate.
      if (is->patternTerms.empty()) {
        errAt(errs, s->line, "instantiate pattern form requires at least one pattern term (`on <t1>, ...`)");
      }
      for (auto& pt : is->patternTerms) {
        if (pt) (void)checkExpr(pt.get());
      }
    }
    return;
  }
  if (auto ps = dynamic_cast<ProofStmt*>(s)) {
    // ox:proof Fix C  -  `proof` statement: a ghost / spec-only induction-proof pragma
    // consumed by the SMT emitter (Driver.cpp's smtEncodeStmt arm). It has NO
    // runtime effect: IRGen skips it (no LLVM IR) and it never appears in a
    // runtime gate. Sema only verifies the constituent expressions type-check
    // so a typo fails the build rather than silently producing bogus SMT.
    //
    // Layout (see ProofStmt in AST.h):
    //   theorem    : QuantExpr (the universally-quantified theorem, `forall k`)
    //   inductionVar : the bound variable name (must equal theorem->binder)
    //   baseCase   : ground boolean (no bound variable)  -  the k=base instance
    //   ih         : boolean (induction hypothesis, references the bound var)
    //   goal       : boolean (the step goal, references the bound var)
    //
    // The bound variable (inductionVar) is a BOUND variable, scoped ONLY inside
    // ih and goal  -  it is NOT declared in the regular Sema scope (so a later
    // runtime `let k = ...` is NOT shadowed/colliding) and we must NOT
    // type-check it as a regular variable. We push an ephemeral scope declaring
    // the binder only while checking ih/goal, exactly like the QuantExpr arm of
    // checkExpr and the InstantiateStmt arm above do for their body.
    if (!ps->theorem) {
      errAt(errs, s->line, "proof pragma requires a theorem (forall <var>. ...)");
      return;
    }
    auto* q = ps->theorem.get();
    if (!q->isForall) {
      errAt(errs, s->line, "proof theorem must be a `forall` quantifier (got `exists`)");
      return;
    }
    if (ps->inductionVar.empty()) {
      errAt(errs, s->line, "proof requires an induction variable");
      return;
    }
    if (q->binder != ps->inductionVar) {
      errAt(errs, s->line, "proof induction variable '" + ps->inductionVar +
            "' does not match theorem binder '" + q->binder + "'");
      return;
    }
    // ox:unsafe Range bounds must be present (a forall over `k in lo..hi`) and type-check
    // in the current OUTER scope (they are ground relative to k).
    if (!q->lo || !q->hi) {
      errAt(errs, s->line, "proof theorem quantifier requires range bounds (lo..hi)");
      return;
    }
    (void)checkExpr(q->lo.get());
    (void)checkExpr(q->hi.get());
    // ox:proof Default the binder to i64 (proofs are over integer induction) so ih/goal
    // resolve the bound variable as an integer.
    if (q->binderType == BType::void_) q->binderType = BType::i64;

    // Base case is GROUND (no bound variable): type-check it in the current
    // scope, before introducing the binder.
    if (!ps->baseCase) {
      errAt(errs, s->line, "proof requires a base case expression");
    } else {
      BType bt = checkExpr(ps->baseCase.get());
      if (bt != BType::bool_)
        errAt(errs, s->line, "proof base case must be boolean (got " +
              typeSpelling(bt) + ")");
    }

    // IH and goal may reference the bound variable. Push an ephemeral scope
    // declaring the binder only for these two checks  -  it does NOT leak into
    // the regular Sema scope, so the induction variable is NOT type-checked as
    // a regular runtime variable (it is a ghost bound variable).
    pushScope();
    declare(q->binder, q->binderType, true, false, false, s->line);
    if (!ps->ih) {
      errAt(errs, s->line, "proof requires an induction hypothesis expression");
    } else {
      BType it = checkExpr(ps->ih.get());
      if (it != BType::bool_)
        errAt(errs, s->line, "proof induction hypothesis must be boolean (got " +
              typeSpelling(it) + ")");
    }
    if (!ps->goal) {
      errAt(errs, s->line, "proof requires a goal expression");
    } else {
      BType gt = checkExpr(ps->goal.get());
      if (gt != BType::bool_)
        errAt(errs, s->line, "proof goal must be boolean (got " +
              typeSpelling(gt) + ")");
    }
    popScope();
    return;
  }

  // ox:proof Lemma-functions `proof { <stmts> }` block form  -  a proof/spec context.
  // The body is a sequence of statements (typically lemma calls `add_comm(x,
  // y);` parsed as ExprStmt holding a Call, and `assert`s). We raise
  // `inSpecContext_` for the duration of the body walk so the Call arm of
  // checkExpr admits calls to isLemma/isGhost fns (which a runtime frame
  // rejects). The body statements are otherwise type-checked like a regular
  // block (pushScope/popScope for proper scoping). IRGen skips the whole
  // ProofBlockStmt (no codegen  -  proof-only). SMT semantics live in
  // Driver.cpp's smtEncodeStmt proofBlockStmt arm (assumes lemma ensures
  // with args substituted + asserts discharge as hypotheses).
  if (auto pb = dynamic_cast<ProofBlockStmt*>(s)) {
    pushScope();
    int savedSpec = inSpecContext_;
    inSpecContext_ = 1;            // a proof block is a spec context
    for (auto& st : pb->body) checkStmt(st.get());
    inSpecContext_ = savedSpec;    // restore (compose with any outer frame)
    popScope();
    return;
  }
  if (auto cs = dynamic_cast<CalcStmt*>(s)) {
    // ox:proof calcD  -  `calc { ... }` equational-reasoning block. A ghost / spec-only
    // proof statement consumed by the SMT emitter (Driver.cpp's smtEncodeStmt
    // calcStmt arm). It has NO runtime effect: IRGen skips it (no LLVM IR), and
    // it never appears in a runtime gate. Sema only verifies the constituent
    // expressions + hint statements type-check so a typo fails the build rather
    // than silently producing a bogus SMT discharge  -  same contract as the
    // induction-form ProofStmt above and the block-form ProofBlockStmt arm.
    //
    // The step expressions are spec expressions (parsed with inSpec_ set in the
    // Parser)  -  they may use `forall`/`implies`, and they may call lemma/spec
    // fns. We raise `inSpecContext_` for the duration of the walk so checkExpr's
    // Call arm admits calls to isLemma/isGhost fns (which a runtime frame
    // rejects)  -  exactly mirroring the ProofBlockStmt arm. The block gets its
    // own Sema scope (push/pop) so any `let` introduced by a hint statement
    // does NOT leak into the enclosing scope (consistent with how ProofBlockStmt
    // scopes its body, and how a calc is conceptually a self-contained proof
    // block).
    //
    // The step expressions need not be boolean  -  the relation operators (`==`,
    // `<=`, ...) connect expressions of ANY common type (integers, etc.). We
    // only require each step's expression type-checks in the current scope.
    // The hints are full statements checked via the regular `checkStmt` walk.
    if (cs->steps.empty()) {
      errAt(errs, s->line, "calc block must contain at least one expression step");
      return;
    }
    pushScope();
    int savedSpec = inSpecContext_;
    inSpecContext_ = 1;            // a calc block is a proof/spec context
    for (size_t i = 0; i < cs->steps.size(); ++i) {
      auto& step = cs->steps[i];
      if (!step.expr) {
        errAt(errs, s->line, "calc step " + std::to_string(i) +
              " is missing its expression");
        continue;
      }
      // ox:proof Type-check the step expression. Each step may name lemma/spec fns and
      // use `forall`/`implies`; the inSpecContext_ flag above admits those.
      (void)checkExpr(step.expr.get());
      // ox:unsafe The relation must be present on every step EXCEPT the last (the last
      // step terminates the chain  -  `steps.back().relation` is empty). The
      // Parser guarantees this structurally, but an honest diagnostic here
      // surfaces malformed ASTs produced by a buggy reconstruct/recovery path.
      bool isLast = (i + 1 == cs->steps.size());
      if (!isLast && step.relation.empty()) {
        errAt(errs, s->line, "calc step " + std::to_string(i) +
              " is missing the relation operator to the next step");
      } else if (isLast && !step.relation.empty()) {
        errAt(errs, s->line, "calc last step must not have a trailing relation "
              "operator (got '" + step.relation + "')");
      }
      // Validate the relation string is one of the allowed operators. The
      // Parser only ever emits the six legal values, but checking here catches
      // hand-built ASTs (and keeps the SMT emitter honest about what it reads).
      if (!step.relation.empty() &&
          step.relation != "==" && step.relation != "!=" &&
          step.relation != "<=" && step.relation != ">=" &&
          step.relation != "<"  && step.relation != ">") {
        errAt(errs, s->line, "calc step " + std::to_string(i) +
              " has invalid relation '" + step.relation +
              "' (expected one of ==, !=, <=, >=, <, >)");
      }
      // ox:proof The hint statements are ordinary proof statements (assert / lemma-call
      // ExprStmt / instantiate / further calc). Each is checked through the
      // normal statement-checking machinery, recursion and all.
      for (auto& h : step.hints) {
        if (h) checkStmt(h.get());
      }
    }
    inSpecContext_ = savedSpec;    // restore (compose with any outer frame)
    popScope();
    return;
  }
}

void Sema::checkBlock(const std::vector<StmtPtr>& stmts) {
  for (auto& s : stmts) checkStmt(s.get());
}

bool Sema::check(Program& prog) {

  // ox:why Thread prog (+ this) through file-scope pointers so the free function
  // instantiateGenericStruct can clone generic-struct impl methods into each
  // instantiation (see g_progForGenStruct / g_semaForGenStruct above). Cleared
  // at the end of check().
  g_progForGenStruct = &prog;
  g_semaForGenStruct = this;

  // --- Concepts (C++20-concepts-style) ---
  // Register every `concept` decl FIRST, before any struct/fn registration or
  // instantiation, so constraint checks during monomorphisation can resolve a
  // concept by name. registerConceptDef MOVES the ConceptDecl out of the program
  // into the owning concept table (Types.cpp); after this prog.concepts holds
  // null unique_ptrs, which is fine  -  they are not consulted again.
  for (auto& c : prog.concepts) {
    if (c) registerConceptDef(std::move(c));
  }

  // Compile-time macros: register every `macro name(params) { body }` decl in
  // the name->MacroDecl* registry up front so the first `expand name(...)` call
  //  -  which may appear textually before the macro decl, though Oxide is order-
  // independent at module scope here  -  resolves. Macros are purely compile-time:
  // no entry in `funcs`, no codegen. A duplicate name is a hard error (the
  // second decl reports it; the first stays registered).
  for (auto& m : prog.macros) {
    if (!m) continue;
    if (macroRegistry.count(m->name))
      errAt(errs, m->line, "redefinition of macro '" + m->name + "'");
    else
      macroRegistry.emplace(m->name, m.get());
  }

  for (auto& ed : prog.enums) {
    EnumDef* d = registerEnum(ed->name);
    if (!d->variants.empty() && d->name != ed->name) {
      errAt(errs, ed->line, "redefinition of enum '" + ed->name + "'");
    }
    d->variants = ed->variants;

    std::set<std::string> seen;
    for (auto& v : ed->variants) {
      if (seen.count(v))
        errAt(errs, ed->line, "duplicate variant '" + v + "' in enum '" + ed->name + "'");
      seen.insert(v);
    }
  }


  for (auto& td : prog.typedefs) {
    BType resolved = fixType(td->target);
    // Feature 4  -  if the typedef carries a `where <expr>` clause, register
    // it as a RANGE TYPE and stamp the resolved BType with `hasRange` +
    // `rangeTypeName` so callers that resolve the alias know to emit the
    // constraint. The alias table holds the PRE-range resolved base type
    // (unchanged for runtime/IRGen); the range metadata lives in the
    // separate range-table for the SMT encoder to consult.
    if (td->rangeExpr) {
      resolved.hasRange = true;
      resolved.rangeTypeName = td->name;
      registerRangeType(td->name, resolved, td->rangeExpr.get());
    }
    registerAlias(td->name, resolved);
  }


  for (auto& sd : prog.structs) {
    if (sd->isGeneric) {


      registerGenericStruct(sd.get());


      continue;
    }
    StructDef* d = registerStruct(sd->name);
    if (sd->isOpaque) {
      // `extern struct Name;`  -  opaque, no fields, only used as *Name (handle ptr).
      d->isOpaque = true;
      d->fields.clear();
      d->size = 0;
      d->align = 1;
      continue;
    }
    if (!d->fields.empty() && d->name != sd->name) {
      errAt(errs, sd->line, "redefinition of struct '" + sd->name + "'");
    }
    d->fields.clear();
    d->baseName = sd->baseName;
    d->base = nullptr;
    d->hasVirtuals = false;
    for (auto& f : sd->fields) {


      const std::string& fname = std::get<0>(f);
      BType ft = fixType(std::get<1>(f));
      bool priv = std::get<2>(f);
      d->fields.push_back({fname, ft, 0, priv});
    }

    // Layout pass 1: the derived struct's OWN fields, laid out from offset 0 as a
    // baseless struct. (base fields are spliced in later, in a second pass over
    // all structs, once every struct is registered so `findStruct(baseName)`
    // resolves  -  see the base-resolve loop below.) This produces the offsets of
    // the derived's own fields relative to the start of the own-field block; the
    // second pass shifts them past the base sub-object.
    d->size = 0; d->align = 1;
    int32_t off = 0;
    for (auto& f : d->fields) {
      int32_t w = fieldByteWidth(f.type);
      int32_t a = fieldAlign(f.type);
      if (a > d->align) d->align = a;
      off = (off + a - 1) & ~(a - 1);
      f.offset = off;
      off += w;
    }
    off = (off + d->align - 1) & ~(d->align - 1);
    d->size = off;
  }

  // Single inheritance  -  layout pass 2: resolve `base` and splice base fields in
  // FIRST (base sub-object at offset 0, so a Derived* → Base* upcast is a no-op
  // bitcast). Every consumer  -  the LLVM `%struct.T = type {...}` header and the
  // `getelementptr i32 fi` field indices  -  reads `StructDef::fields` and
  // `structFieldIndex`, so a single merged, base-first list keeps them all
  // consistent. Validates: base exists & is non-generic & non-opaque; no cycles;
  // no field-name collisions across the base chain; inheritance is single (one
  // base chain, no diamond  -  multiple inheritance is a later item).
  for (auto& sd : prog.structs) {
    if (sd->isGeneric) continue;
    StructDef* d = findStruct(sd->name);
    if (!d || sd->baseName.empty()) continue;

    // Resolve & validate the base chain (walk up, detecting cycles/vanishing).
    StructDef* base = findStruct(sd->baseName);
    if (!base) {
      errAt(errs, sd->line, "struct '" + sd->name + "' inherits from unknown "
            "struct '" + sd->baseName + "'");
      continue;
    }
    if (base->isOpaque) {
      errAt(errs, sd->line, "struct '" + sd->name + "' cannot inherit from "
            "opaque extern struct '" + sd->baseName + "'");
      continue;
    }
    // Cycle detection: walk the chain currently being built; if we revisit sd,
    // it's a cycle (A: B, B: A).
    {
      std::set<const StructDef*> seen;
      for (const StructDef* b = base; b; b = b->base) {
        if (b == d || !seen.insert(b).second) {
          errAt(errs, sd->line, "inheritance cycle through '" + sd->baseName + "'");
          base = nullptr;
          break;
        }
      }
      if (!base) continue;
    }
    d->base = base;

    // Splice: base fields first (carrying their base-relative offsets, which are
    // already 0-based and correct since base is at offset 0), then this struct's
    // own fields shifted by the base's padded size.
    std::vector<StructField> merged;
    merged.reserve(base->fields.size() + d->fields.size());
    int32_t baseEnd = base->size;   // already alignment-padded in base's pass 1
    // collision check across the whole chain
    for (const StructField& bf : base->fields) {
      for (const StructField& mf : merged)
        if (mf.name == bf.name)
          errAt(errs, sd->line, "field '" + bf.name + "' in '" + sd->name +
                "' collides with an inherited field from '" + sd->baseName + "'");
      merged.push_back(bf);
    }
    for (auto& f : d->fields) {
      for (const StructField& mf : merged)
        if (mf.name == f.name)
          errAt(errs, sd->line, "field '" + f.name + "' is redeclared (also "
                "inherited from a base of '" + sd->name + "')");
      f.offset = baseEnd + f.offset;   // shift own fields past the base sub-object
      merged.push_back(f);
    }
    d->fields = std::move(merged);
    if (base->align > d->align) d->align = base->align;
    int32_t total = baseEnd + d->size;   // d->size still holds the own-fields block size
    total = (total + d->align - 1) & ~(d->align - 1);
    d->size = total;
  }


  for (auto& im : prog.impls) {
    const std::string& sn = im->structName;
    StructDef* sd = findStruct(sn);
    if (!sd) {
      // Generic struct template (`impl Box { ... }` where `struct Box<T = ...>`).
      // The template's StructDef isn't in the concrete table (only its
      // instantiations are), so `findStruct("Box")` is null here. We still want
      // to (1) fixType the method signatures so a concrete `Box<i64>` ret/param
      // type resolves to its mangled instantiation, (2) register the methods
      // under the TEMPLATE name `sn` so an associated call `Box::make(...)` with
      // no explicit type args resolves, and (3) eagerly instantiate the struct
      // with its defaults (when every param has a default) so the cloned method
      // bodies exist for IRGen and the per-mangled method table is populated.
      // The `sd->hasDrop/hasClone` and rule-of-five handling below needs a
      // concrete StructDef and is silently skipped for the template (an
      // instantiation that needs a drop gets its own clone via the lazy path in
      // instantiateGenericStruct  -  drop/clone on generic structs is a deeper
      // item left to that path's caller).
      const StructDecl* tmpl = findGenericStruct(sn);
      if (tmpl) {
        for (auto& m : im->methods) {
          m->retType = fixType(m->retType);
          for (auto& p : m->params) p.type = fixType(p.type);
          m->implStruct = sn;
          registerMethod(sn, *m);
        }
        // Eager default instantiation  -  registers methods[defaultMangled] +
        // funcs + monomorphMethods clones (see instantiateGenericStruct).
        bool allDefaulted = !tmpl->tparams.empty();
        for (const auto& tp : tmpl->tparams) if (!tp.hasDefault) { allDefaulted = false; break; }
        if (allDefaulted) {
          std::vector<BType> dargs;
          for (const auto& tp : tmpl->tparams) dargs.push_back(fixType(tp.defaultType));
          BType inst = instantiateGenericStruct(sn, dargs);
          // Mirror the default instantiation's method entries under the
          // TEMPLATE name so `Box::make(7)` (ac->typeName == "Box") finds a
          // MethodInfo whose `mangled` points at the emitted clone. The clone
          // is keyed by the mangled inst name; copying the whole map slice
          // keeps the per-method `mangled` field intact.
          const std::string& mangled = inst.structName;
          auto it = methods.find(mangled);
          if (it != methods.end()) {
            for (const auto& kv : it->second) methods[sn][kv.first] = kv.second;
          }
        }
      } else {
        errAt(errs, im->line, "impl for unknown struct '" + sn + "'");
      }
      continue;
    }
    for (auto& m : im->methods) {
      m->retType = fixType(m->retType);
      for (auto& p : m->params) p.type = fixType(p.type);
      m->implStruct = sn;
      registerMethod(sn, *m);

      // Rule-of-five, Rust-style opt-in. The two special method names are
      // detected BY NAME (no new AST node  -  they are ordinary methods with
      // a conventional signature):
      //   impl T { fn drop(&mut self) {...} }   -  destructor; runs at scope exit
      //     and on every early-exit path. Marks the type move-only.
      //   impl T { fn clone(&self) -> T {...} }  -  explicit copy; the ONLY way
      //     to copy a move-only type (assignment/let-copy/pass-by-value of a
      //     hasDrop type without `clone` is a compile error).
      // A `drop` must take `&mut self` and return void; `clone` must return T.
      if (m->name == "drop") {
        if (!m->hasSelf || !m->selfByRef || !m->params.empty() || m->retType != BType::void_)
          errAt(errs, m->line, "drop must be `fn drop(&mut self)` taking no args, returning void");
        sd->hasDrop = true;
      } else if (m->name == "clone") {
        // `clone(&self) -> T`  -  borrows self by reference (does NOT consume or
        // move the receiver), returns a fresh independent copy. A by-ref receiver
        // means clone produces NO spurious destructor and leaves the source valid
        //  -  matching Rust's `Clone::clone(&self) -> Self`.
        if (!m->hasSelf || !m->selfByRef || !m->params.empty() ||
            m->retType.tag != BType::Tag::struct_ || m->retType.structName != sn)
          errAt(errs, m->line, "clone must be `fn clone(&self) -> " + sn + "`");
        sd->hasClone = true;
      }
    }
  }

  // Virtual dispatch metadata: after all methods are registered and the base
  // chain is resolved, build StructDef::vtableSlots + hasVirtuals (up-propagated
  // along the base chain) and patch MethodInfo::vtableSlot. Validates virtual/
  // override declarations. Runs BEFORE the layout pass 3 that may prepend the
  // __oxvt vtable-ptr field to polymorphic-root structs  -  so layout sees the
  // final hasVirtuals (see resolveVtables + the layout recompute below).
  resolveVtables(prog);
  // Layout pass 3: now that hasVirtuals + vtableSlots are known, prepend the
  // synthetic __oxvt vtable-ptr field at the baseless root of every polymorphic
  // chain, recomputing offsets/sizes/aligns. Non-polymorphic structs unchanged.
  recomputeVtableLayout(prog);

  // ox:proof T1  -  pre-register every `spec fn` name in the funcs table BEFORE the
  // free-fn body-check loop that follows. This is critical: a function's
  // `requires` clause may call a spec fn (e.g. `requires range_ok(a, b)`),
  // and the contract-checker resolves Call names through `funcs`. If spec
  // fns were registered AFTER the body-check loop (as they used to be),
  // such a `requires` would report "call to undeclared function"  -  the spec
  // fn exists in the AST but not yet in the funcs map. Spec fns take no body
  // type-check here: their body is a single spec expression that the Ghost
  // encoder (src/Ghost.cpp) lowers to SMT directly; we just expose the
  // CALLABLE signature so contract spec clauses can NAME it. isExtern=true
  // marks the slot as a declared name with no runtime body (same model used
  // for ghost fns below), which is what we want  -  a spec fn never codegens.
  // Names use the bare user-written name (not mangled); the encoder resolves
  // by walking prog.specFns directly, so the funcs-table name is only for
  // Sema's call resolution in contract spec expressions.
  for (auto& sf : prog.specFns) {
    if (!sf) continue;
    if (funcs.count(sf->name)) {
      errAt(errs, sf->line, "spec fn '" + sf->name +
            "' redeclares an existing function name");
      continue;
    }
    sf->retType = fixType(sf->retType);
    for (auto& p : sf->params) p.type = fixType(p.type);
    FuncSig sig;
    sig.isExtern = true;          // declared (callable), no runtime body
    sig.retType = sf->retType;
    for (auto& p : sf->params) sig.paramTypes.push_back(p.type);
    sig.isAsmSpec = sf->isAsmSpec; // verified-asm: mark so an `implements`
                                   // link check in checkExpr recognizes this
                                   // slot as a hardware-instruction spec.
    funcs[sf->name] = sig;
    specFnNames_.insert(sf->name); // expose to IRGen for contract-gate elision
  }

  // ox:unsafe fix2  -  pre-register every `trap [handler] name(...)` signature in the
  // funcs table BEFORE the free-fn body-check loop, so a caller (e.g. `main`
  // unit-testing a handler, or another trap handler dispatching) resolves the
  // call. A trap handler IS a FuncDecl, so the registration shape is identical
  // to a free fn: fixType the return + params, build a FuncSig, insert under
  // the bare name. `isExtern` mirrors the handler's own `isExtern` flag (true
  // for a `trap name(...);` prototype stub, false for a `trap handler ...{ body }`),
  // so call-resolution and IRGen's userDefinedFns_ see the right model. A
  // body-less prototype registered as non-extern would let `main` CALL it but
  // IRGen would try to codegen a body it doesn't have  -  extern is correct.
  // Body type-checking happens in the dedicated trap-handler loop below (after
  // the free-fn body-check loop), so contract clauses resolve spec fns/consts
  // that were registered above.
  for (auto& th : prog.trapHandlers) {
    if (!th) continue;
    if (funcs.count(th->name)) {
      errAt(errs, th->line, "trap handler '" + th->name +
            "' redeclares an existing function name");
      continue;
    }
    if (th->isGeneric) {
      // ox:unsafe Generic trap handlers: register as a generic fn template (so a caller
      // with explicit type args can instantiate), mirroring free fns. Body
      // type-checking is deferred to per-instantiation (out of scope for v1).
      registerGenericFn(th.get());
      genericFnOverloads_[th->name].push_back(th.get());
      continue;
    }
    th->retType = fixType(th->retType);
    FuncSig sig;
    sig.isExtern = th->isExtern;
    sig.retType = th->retType;
    applyEffects(sig, th->effectsExplicit, th->effects);  // effect system  -  trap handler effects
    for (auto& p : th->params) { p.type = fixType(p.type); sig.paramTypes.push_back(p.type); }
    funcs[th->name] = sig;
    // ox:unsafe `hasMain` is declared just below; a trap handler named `main` is an oddity
    // but the free-fn loop's hasMain check covers the normal case. We set it in
    // the trap-handler body-check loop below where hasMain is already in scope.
    registerDefaults(th->name, th->params);
  }

  // ox:proof Lemma functions  -  pre-register every `lemma fn name(...)` signature in the
  // funcs table (like trap handlers and ghost fns) BEFORE the free-fn body-check
  // loop, so a `proof { name(args) }` call (and a lemma calling another lemma)
  // resolves. A lemma IS a FuncDecl, so the registration shape is identical to
  // a free fn: fixType the return + params, build a FuncSig with `isLemma=true`,
  // insert under the bare name. `isLemma=true` on FuncSig lets the Call arm of
  // checkExpr admit the call ONLY when `inSpecContext_ > 0` (a proof/spec
  // context) and reject it from runtime code  -  the SAME rule as a ghost fn
  // (FuncSig::isGhost). `isExtern` is false (a lemma always has a body  -  the
  // Parser's parseLemma reuses parseFunc(false)). Body type-checking happens in
  // the dedicated lemma body-check loop below (after the free-fn body-check
  // loop) under inSpecContext_=1 so a lemma may call other lemmas/spec fns.
  for (auto& lm : prog.lemmas) {
    if (!lm) continue;
    if (funcs.count(lm->name)) {
      errAt(errs, lm->line, "lemma '" + lm->name +
            "' redeclares an existing function name");
      continue;
    }
    if (lm->isGeneric) {
      // Generic lemmas: register as a generic fn template. Body type-checking
      // is deferred to per-instantiation (mirrors generic trap handlers).
      registerGenericFn(lm.get());
      genericFnOverloads_[lm->name].push_back(lm.get());
      continue;
    }
    lm->retType = fixType(lm->retType);
    FuncSig sig;
    sig.isExtern = false;          // a lemma always has a body
    sig.isLemma = true;            // proof-only call restriction
    sig.retType = lm->retType;
    applyEffects(sig, lm->effectsExplicit, lm->effects);  // effect system  -  lemmas are pure (effects default empty)
    for (auto& p : lm->params) { p.type = fixType(p.type); sig.paramTypes.push_back(p.type); }
    funcs[lm->name] = sig;
    registerDefaults(lm->name, lm->params);
  }

  bool hasMain = false;
  for (auto& fn : prog.funcs) {
    if (fn->isGeneric) {


      registerGenericFn(fn.get());
      genericFnOverloads_[fn->name].push_back(fn.get());
      if (fn->name == "main" && !fn->isExtern) hasMain = true;


      // Leave the template's return and parameter types symbolic. These are
      // resolved per instantiation in instantiateGenericFn (substitute then
      // fixType). Calling fixType here would eagerly construct generic struct
      // argument types with symbolic type-parameter names baked into the mangled
      // name, which afterward defeats substitute (the keys A,B no longer appear)
      // and yields fields that keep their symbolic element type.
      continue;
    }
    if (funcs.count(fn->name)) {
      errAt(errs, fn->line, "redefinition of function '" + fn->name + "'");
    }
    FuncSig sig;
    sig.isExtern = fn->isExtern;
    sig.isExport = fn->isExport;
    sig.isGhost = fn->isGhost;          // T2  -  ghost fn (spec-only call)
    fn->retType = fixType(fn->retType);
    sig.retType = fn->retType;
    applyEffects(sig, fn->effectsExplicit, fn->effects);  // effect system  -  free fn effects + isPure
    for (auto& p : fn->params) { p.type = fixType(p.type); sig.paramTypes.push_back(p.type); }
    funcs[fn->name] = sig;
    if (fn->name == "main" && !fn->isExtern) hasMain = true;
    registerDefaults(fn->name, fn->params);
  }


  pushScope();
  for (auto& vd : prog.globals) {
    GlobalInfo gi{};
    gi.isConst = vd->isConst;
    gi.isExtern = vd->isExtern;
    gi.isMut = vd->isMut;
    BType t = BType::void_;
    if (vd->typeAnnotated) {
      vd->type = fixType(vd->type);
      t = vd->type;
    }
    if (vd->isExtern) {
      if (!vd->typeAnnotated) errAt(errs, vd->line, "extern global '" + vd->name + "' needs a type");
      if (vd->init) errAt(errs, vd->line, "extern global '" + vd->name + "' may not have an initializer");
      gi.type = vd->type;
    } else if (vd->init) {
      BType it = checkExpr(vd->init.get());
      if (!vd->typeAnnotated) {
        t = it;
      } else if (t != it) {
        bool ok = implicitAssignable(it, t) ||
                  litAssignable(vd->init.get(), it, t) ||
                  dynamic_cast<CastExpr*>(vd->init.get());
        if (!ok) errAt(errs, vd->line, "global '" + vd->name + "': initializer type does not match annotation");
      }
      gi.type = t;


      GlobalInfo folded{};
      folded.type = t;
      if (foldConstExpr(vd->init.get(), folded)) {
        gi.hasConstVal = true;
        gi.type = folded.type;
        gi.iVal = folded.iVal; gi.fVal = folded.fVal;
        gi.bVal = folded.bVal; gi.cVal = folded.cVal; gi.sVal = folded.sVal;
        if (gi.type != t && t != BType::void_)
          errAt(errs, vd->line, "global '" + vd->name + "': fold type mismatch");
      } else if (!vd->isConst) {


        errAt(errs, vd->line, 0, "global '" + vd->name +
              "' initializer must be a compile-time constant (move runtime init into main)",
              "use a constant expression, or a const, or set it from main");
      } else {

        errAt(errs, vd->line, 0, "const '" + vd->name +
              "' initializer is not a compile-time constant",
              "const requires a literal, folded arithmetic, or a reference to a const");
      }
    } else {
      if (!vd->typeAnnotated) errAt(errs, vd->line, "global '" + vd->name + "' has no type and no initializer");
      gi.type = t;
    }
    if (gi.type == BType::void_) {
      errAt(errs, vd->line, "cannot infer type for global '" + vd->name + "'");
      gi.type = BType::i64;
    }
    if (vd->isConst) vd->isMut = false;
    vd->type = gi.type;
    vd->typeAnnotated = true;
    if (globals.count(vd->name))
      errAt(errs, vd->line, "redefinition of global '" + vd->name + "'");
    globals[vd->name] = gi;

    declare(vd->name, gi.type, gi.isMut, true, false, vd->line);
  }

  // Now that globals/consts are registered + folded, type-check every stored
  // default argument (a default may reference a const global). The same check
  // re-runs on appended clones at call sites; this validates uncalled defaults.
  checkStoredDefaults();

  for (auto& fn : prog.funcs) {
    if (fn->isExtern) continue;
    if (fn->isGeneric) continue;
    // ox:proof T2  -  a `ghost fn` is not runtime code; its body may mention spec-only
    // names (ghost lets, spec fns) that aren't in the runtime scope. For v1
    // we skip body type-checking of a ghost fn and only register its name +
    // signature so contract contexts and the Ghost encoder can name it. Sema
    // enforcement that a ghost fn is CALLED only from a spec context is out of
    // scope for this minimal patch (TODO: add when a full spec-context marker
    // threads through Sema).
    if (fn->isGhost) {
      // Registered already in the funcs loop earlier (line ~3515); nothing
      // else to do for a spec-only fn in v1.
      continue;
    }
    curFunc_ = &funcs[fn->name];

    movedVars_.clear();
    while (scopes_.size() > 1) scopes_.pop_back();
    pushScope();
    for (auto& p : fn->params) declare(p.name, p.type, true, false, false, fn->line);
    predeclareGhostLets(fn->body);
    checkFuncContracts(*fn);
    checkBlock(fn->body);
    popScope();
    curFunc_ = nullptr;
  }

  // T1/T3  -  module-level decls (regions, refines, spec fns) are consumed by
  // the Ghost encoder (src/Ghost.cpp). Spec fns are already registered into
  // `funcs` ABOVE the free-fn body-check loop (so contract clauses can name
  // them); regions and refines resolve their names lazily at SMT-emit time, so
  // NO Sema pass over them is needed for v1. (This block used to register the
  // spec fns here, but that was too late for body-check-time `requires`
  // resolution  -  it was hoisted to the top of this pass.)

  for (auto& im : prog.impls) {
    const std::string& sn = im->structName;
    if (!findStruct(sn)) continue;
    curImpl_ = sn;
    BType st; st.tag = BType::Tag::struct_; st.structName = sn;
    for (auto& m : im->methods) {
      // ox:proof The isGhost flag is set by parseFunc for `ghost fn`. A ghost method
      // does NOT get body type-checked (its body is spec-only); only register
      // its signature so contract/spec contexts can call it (mirrors free-fn
      // ghost handling above).
      if (m->isGhost) {
        // FuncSig only carries retType/paramTypes/isExtern  -  method-specific
        // hasSelf/selfByRef live on MethodInfo (a separate lookup path used by
        // method-call resolution, not the funcs map a spec context routes
        // through). So a ghost method's funcs entry needs only the plain
        // signature; the method-call lookup will never dispatch a ghost fn
        // (Sema rejects runtime calls to a isGhost fn), and contract/spec
        // contexts that name the ghost fn only need the sig table. Mirrors
        // how free `ghost fn` is handled at the top of this pass.
        FuncSig sig;
        sig.isExtern = true;
        sig.isGhost = true;          // proof-only call restriction (same rule)
        sig.retType = m->retType;
        applyEffects(sig, m->effectsExplicit, m->effects);   // effect system  -  ghost methods are pure (effects default empty)
        for (auto& p : m->params) sig.paramTypes.push_back(p.type);
        funcs[mangleMethod(sn, m->name)] = sig;
        continue;
      }
      curFunc_ = &funcs[mangleMethod(sn, m->name)];
      movedVars_.clear();
      while (scopes_.size() > 1) scopes_.pop_back();
      pushScope();


      declare("self", m->selfByRef ? makePtr(st) : st, true, false, false, m->line);
      for (auto& p : m->params) declare(p.name, p.type, true, false, false, m->line);
      predeclareGhostLets(m->body);
      checkFuncContracts(*m);
      checkBlock(m->body);
      popScope();
      curFunc_ = nullptr;
    }
    curImpl_.clear();
  }


  {
    size_t checked = 0;
    while (checked < monomorphFns.size()) {
      size_t n = monomorphFns.size();
      for (; checked < n; checked++) {
        FuncDecl& fn = *monomorphFns[checked];
        curFunc_ = &funcs[fn.name];
        movedVars_.clear();
        while (scopes_.size() > 1) scopes_.pop_back();
        pushScope();
        for (auto& p : fn.params) declare(p.name, p.type, true, false, false, fn.line);
        predeclareGhostLets(fn.body);
        checkFuncContracts(fn);
        checkBlock(fn.body);
        popScope();
        curFunc_ = nullptr;
      }
    }
  }

  // Generic-struct method clones  -  body type-check each instantiation under its
  // mangled struct type, mirroring the monomorphFns loop above but with a
  // `self` binding when the method is a &self/self method. The clone's params
  // and retType are already substituted to the concrete type args by
  // instantiateGenericStruct, so `t: T` becomes `t: Named` and a method call
  // `t.to_str()` resolves against Named's impl as expected (this is what makes
  // `describe<T: Printable>(t: T) { ... t.to_str() ... }` type-check once
  // instantiated). Grown iteratively like monomorphFns: an instantiation's
  // body may reference another generic instantiation, which appends more
  // methods to monomorphMethods  -  the while-loop drives that fixed point.
  {
    size_t checked = 0;
    while (checked < monomorphMethods.size()) {
      size_t n = monomorphMethods.size();
      for (; checked < n; checked++) {
        auto& cm = monomorphMethods[checked];
        FuncDecl& fn = *cm.fn;
        const std::string& mangled = cm.structName;
        curImpl_ = mangled;
        BType st; st.tag = BType::Tag::struct_; st.structName = mangled;
        curFunc_ = &funcs[mangleMethod(mangled, fn.name)];
        movedVars_.clear();
        while (scopes_.size() > 1) scopes_.pop_back();
        pushScope();
        if (fn.hasSelf) declare("self", fn.selfByRef ? makePtr(st) : st, true, false, false, fn.line);
        for (auto& p : fn.params) declare(p.name, p.type, true, false, false, fn.line);
        predeclareGhostLets(fn.body);
        checkFuncContracts(fn);
        checkBlock(fn.body);
        popScope();
        curFunc_ = nullptr;
        curImpl_.clear();
      }
    }
  }

  // ox:proof Lemma functions  -  type-check each lemma's body (its proof) and contract
  // clauses, mirroring the free-fn body-check loop above but with two key
  // differences: (1) the body is checked under `inSpecContext_=1` so a lemma
  // may CALL other lemmas / spec fns (a runtime frame would reject these  - 
  // the SAME rule as `ghost fn`); (2) a lemma is never codegen'd (IRGen skips
  // isLemma decls) and never has its body monomorphised, so we type-check the
  // (non-generic) lemmas directly here. Contract clauses (requires/ensures)
  // are checked via checkFuncContracts exactly like a regular fn  -  a lemma's
  // own ensures must type-check (and is later discharged by Ghost/Driver).
  // Generic lemmas are skipped (per-instantiation body-check is out of scope
  // for v1, mirroring generic trap handlers/ghost fns).
  for (auto& lm : prog.lemmas) {
    if (!lm) continue;
    if (lm->isGeneric) continue;
    curFunc_ = &funcs[lm->name];
    movedVars_.clear();
    while (scopes_.size() > 1) scopes_.pop_back();
    pushScope();
    for (auto& p : lm->params) declare(p.name, p.type, true, false, false, lm->line);
    predeclareGhostLets(lm->body);
    int savedSpec = inSpecContext_;
    inSpecContext_ = 1;            // a lemma is a proof/spec context
    checkFuncContracts(*lm);
    checkBlock(lm->body);
    inSpecContext_ = savedSpec;    // restore (compose with any outer frame)
    popScope();
    curFunc_ = nullptr;
  }

  popScope();

  if (requireMain && !hasMain) errAt(errs, 1, "program must define a 'main' function");

  // Clear the file-scope pointers threaded through instantiateGenericStruct.
  g_progForGenStruct = nullptr;
  g_semaForGenStruct = nullptr;
  return errs.empty();
}


ExprPtr cloneExpr(const Expr* e) {
  if (!e) return nullptr;
  if (auto l = dynamic_cast<const IntLit*>(e))    { auto n = std::make_unique<IntLit>();   n->v=l->v; n->line=l->line; n->col=l->col; return n; }
  if (auto l = dynamic_cast<const FloatLit*>(e))  { auto n = std::make_unique<FloatLit>(); n->v=l->v; n->isF32=l->isF32; n->line=l->line; n->col=l->col; return n; }
  if (auto l = dynamic_cast<const BoolLit*>(e))   { auto n = std::make_unique<BoolLit>();   n->v=l->v; n->line=l->line; n->col=l->col; return n; }
  if (auto l = dynamic_cast<const StrLit*>(e))    { auto n = std::make_unique<StrLit>();    n->v=l->v; n->line=l->line; n->col=l->col; return n; }
  if (auto l = dynamic_cast<const CharLit*>(e))   { auto n = std::make_unique<CharLit>();   n->v=l->v; n->line=l->line; n->col=l->col; return n; }
  if (auto v = dynamic_cast<const VarRef*>(e))    { auto n = std::make_unique<VarRef>();    n->name=v->name; n->line=v->line; n->col=v->col; return n; }
  if (dynamic_cast<const NullLit*>(e))            { auto n = std::make_unique<NullLit>();   n->line=e->line; n->col=e->col; return n; }
  if (auto s = dynamic_cast<const SizeofExpr*>(e)){ auto n = std::make_unique<SizeofExpr>(); n->target=s->target; n->size=s->size; n->line=s->line; n->col=s->col; return n; }
  if (auto a = dynamic_cast<const AsmExpr*>(e)) {
    auto n = std::make_unique<AsmExpr>(); n->asmText=a->asmText; n->clobbers=a->clobbers;
    n->sideEffect=a->sideEffect; n->hasMemory=a->hasMemory; n->resultTy=a->resultTy;
    n->outputTypes=a->outputTypes;
    n->line=a->line; n->col=a->col;
    for (auto& io : a->ios) { AsmIO c; c.isOutput=io.isOutput; c.isInOut=io.isInOut; c.constraint=io.constraint; c.ty=io.ty; c.val=cloneExpr(io.val.get()); n->ios.push_back(std::move(c)); }
    // ox:why Copy the verified-asm `implements` link so a cloned AsmExpr (e.g. a
    // generic-instantiation or macro-substituted copy) keeps its binding to the
    // original `asm spec fn`. The implementsArgs are cloned recursively (each
    // is an span-expression that must survive the substitution); the expiring
    // pointer `a->implementsArgs` is dropped, the clones move into `n`.
    n->hasImplements = a->hasImplements;
    n->implementsSpec = a->implementsSpec;
    for (auto& arg : a->implementsArgs) n->implementsArgs.push_back(cloneExpr(arg.get()));
    return n;
  }
  if (auto c = dynamic_cast<const CastExpr*>(e)) { auto n = std::make_unique<CastExpr>();  n->e=cloneExpr(c->e.get()); n->target=c->target; n->line=c->line; n->col=c->col; return n; }
  if (auto u = dynamic_cast<const UnaryExpr*>(e)){ auto n = std::make_unique<UnaryExpr>(); n->op=u->op; n->base=cloneExpr(u->base.get()); n->methodOverload=u->methodOverload; n->overloadStruct=u->overloadStruct; n->overloadMethod=u->overloadMethod; n->overloadRecvType=u->overloadRecvType; n->recvByRef=u->recvByRef; n->line=u->line; n->col=u->col; return n; }
  if (auto b = dynamic_cast<const BinaryExpr*>(e)){ auto n = std::make_unique<BinaryExpr>(); n->op=b->op; n->lhs=cloneExpr(b->lhs.get()); n->rhs=cloneExpr(b->rhs.get()); n->methodOverload=b->methodOverload; n->overloadStruct=b->overloadStruct; n->overloadMethod=b->overloadMethod; n->overloadRecvType=b->overloadRecvType; n->recvByRef=b->recvByRef; n->isPtrArith=b->isPtrArith; n->ptrArithPointee=b->ptrArithPointee; n->line=b->line; n->col=b->col; return n; }
  if (auto a = dynamic_cast<const AssignTarget*>(e)) {
    auto n = std::make_unique<AssignTarget>(); n->kind=a->kind; n->name=a->name; n->base=cloneExpr(a->base.get()); n->index=cloneExpr(a->index.get()); n->field=a->field;
    n->value=cloneExpr(a->value.get()); n->compound=a->compound; n->isCompound=a->isCompound; n->methodOverload=a->methodOverload; n->overloadStruct=a->overloadStruct; n->overloadMethod=a->overloadMethod; n->overloadRecvType=a->overloadRecvType; n->recvByRef=a->recvByRef; n->line=a->line; n->col=a->col; return n;
  }
  if (auto c = dynamic_cast<const Call*>(e)) { auto n = std::make_unique<Call>(); n->callee=c->callee; n->isPrint=c->isPrint; n->typeArgs=c->typeArgs; n->hasTypeArgs=c->hasTypeArgs; n->fnPtr=c->fnPtr; n->calleeExpr=cloneExpr(c->calleeExpr.get()); n->calleeFnType=c->calleeFnType; n->line=c->line; n->col=c->col; for (auto& a : c->args) n->args.push_back(cloneExpr(a.get())); return n; }
  if (auto mc = dynamic_cast<const MethodCall*>(e)) { auto n = std::make_unique<MethodCall>(); n->callee=mc->callee; n->receiver=cloneExpr(mc->receiver.get()); n->receiverByRef=mc->receiverByRef; n->recvType=mc->recvType; n->line=mc->line; n->col=mc->col; for (auto& a : mc->args) n->args.push_back(cloneExpr(a.get())); return n; }
  if (auto ac = dynamic_cast<const AssocCall*>(e)) { auto n = std::make_unique<AssocCall>(); n->typeName=ac->typeName; n->callee=ac->callee; n->line=ac->line; n->col=ac->col; for (auto& a : ac->args) n->args.push_back(cloneExpr(a.get())); return n; }
  // Compile-time macro invocation. Cloned with args intact; the `expanded` /
  // `resultTy` stash is deliberately NOT copied (it refers to a Sema-specific
  // expansion that the cloned copy will re-acquire on its own checkExpr pass).
  if (auto mc = dynamic_cast<const MacroCall*>(e)) {
    auto n = std::make_unique<MacroCall>();
    n->macroName = mc->macroName; n->line = mc->line; n->col = mc->col;
    for (auto& a : mc->args) n->args.push_back(cloneExpr(a.get()));
    return n;
  }
  if (auto ix = dynamic_cast<const Index*>(e)) { auto n = std::make_unique<Index>(); n->base=cloneExpr(ix->base.get()); n->index=cloneExpr(ix->index.get()); n->methodOverload=ix->methodOverload; n->overloadStruct=ix->overloadStruct; n->overloadMethod=ix->overloadMethod; n->overloadRecvType=ix->overloadRecvType; n->recvByRef=ix->recvByRef; n->line=ix->line; n->col=ix->col; return n; }
  if (auto fl = dynamic_cast<const Field*>(e)) { auto n = std::make_unique<Field>(); n->base=cloneExpr(fl->base.get()); n->field=fl->field; n->line=fl->line; n->col=fl->col; return n; }
  if (auto al = dynamic_cast<const ArrayLit*>(e)) { auto n = std::make_unique<ArrayLit>(); n->line=al->line; n->col=al->col; for (auto& el : al->elems) n->elems.push_back(cloneExpr(el.get())); return n; }
  if (auto sl = dynamic_cast<const StructLit*>(e)) { auto n = std::make_unique<StructLit>(); n->name=sl->name; n->fieldNames=sl->fieldNames; n->typeArgs=sl->typeArgs; n->hasTypeArgs=sl->hasTypeArgs; n->line=sl->line; n->col=sl->col; for (auto& v : sl->values) n->values.push_back(cloneExpr(v.get())); return n; }
  if (auto dn = dynamic_cast<const DynNew*>(e)) { auto n = std::make_unique<DynNew>(); n->elemType=dn->elemType; n->line=dn->line; n->col=dn->col; return n; }
  if (auto mn = dynamic_cast<const MapNew*>(e)) { auto n = std::make_unique<MapNew>(); n->keyType=mn->keyType; n->valType=mn->valType; n->line=mn->line; n->col=mn->col; return n; }
  if (auto sn = dynamic_cast<const SetNew*>(e)) { auto n = std::make_unique<SetNew>(); n->elemType=sn->elemType; n->line=sn->line; n->col=sn->col; return n; }
  if (auto hm = dynamic_cast<const HMapNew*>(e)) { auto n = std::make_unique<HMapNew>(); n->keyType=hm->keyType; n->valType=hm->valType; n->line=hm->line; n->col=hm->col; return n; }
  if (auto hs = dynamic_cast<const HSetNew*>(e)) { auto n = std::make_unique<HSetNew>(); n->elemType=hs->elemType; n->line=hs->line; n->col=hs->col; return n; }
  // Concurrency node clones. ChannelNew carries only an element BType (no
  // sub-expr); the send/recv/spawn forms clone their sub-expressions via the
  // normal cloneExpr recursion. A SyncBlock is a Stmt with a body vector.
  if (auto cn = dynamic_cast<const ChannelNew*>(e)) { auto n = std::make_unique<ChannelNew>(); n->elemType=cn->elemType; n->line=cn->line; n->col=cn->col; return n; }
  if (auto cs = dynamic_cast<const ChannelSend*>(e)) { auto n = std::make_unique<ChannelSend>(); n->chan=cloneExpr(cs->chan.get()); n->val=cloneExpr(cs->val.get()); n->line=cs->line; n->col=cs->col; return n; }
  if (auto cr = dynamic_cast<const ChannelRecv*>(e)) { auto n = std::make_unique<ChannelRecv>(); n->chan=cloneExpr(cr->chan.get()); n->elemType=cr->elemType; n->line=cr->line; n->col=cr->col; return n; }
  if (auto sp = dynamic_cast<const SpawnExpr*>(e)) { auto n = std::make_unique<SpawnExpr>(); n->body=cloneExpr(sp->body.get()); n->resultTy=sp->resultTy; n->line=sp->line; n->col=sp->col; return n; }
  if (auto r = dynamic_cast<const RangeLit*>(e)) { auto n = std::make_unique<RangeLit>(); n->lo=cloneExpr(r->lo.get()); n->hi=cloneExpr(r->hi.get()); n->inclusive=r->inclusive; n->line=r->line; n->col=r->col; return n; }
  if (auto lam = dynamic_cast<const LambdaLit*>(e)) { auto n = std::make_unique<LambdaLit>(); n->captures=lam->captures; n->retType=lam->retType; n->loweredName=lam->loweredName; n->fnType=lam->fnType; n->captureTypes=lam->captureTypes; n->captureOuterTypes=lam->captureOuterTypes; n->closureStructName=lam->closureStructName; for (auto& p : lam->params) { Param cp; cp.name=p.name; cp.type=p.type; cp.hasDefault=false; n->params.push_back(std::move(cp)); } for (auto& s : lam->body) n->body.push_back(cloneStmt(s.get())); n->line=lam->line; n->col=lam->col; return n; }
  if (auto t = dynamic_cast<const TernaryExpr*>(e)) {
    auto n = std::make_unique<TernaryExpr>(); n->cond=cloneExpr(t->cond.get());
    n->thenE=cloneExpr(t->thenE.get()); n->elseE=cloneExpr(t->elseE.get());
    n->resultTy=t->resultTy; n->line=t->line; n->col=t->col; return n;
  }
  if (auto d = dynamic_cast<const IncDecExpr*>(e)) {
    auto n = std::make_unique<IncDecExpr>(); n->isInc=d->isInc; n->isPost=d->isPost;
    n->kind=d->kind; n->name=d->name; n->base=cloneExpr(d->base.get());
    n->index=cloneExpr(d->index.get()); n->field=d->field; n->valueTy=d->valueTy;
    n->line=d->line; n->col=d->col; return n;
  }
  if (auto o = dynamic_cast<const OldExpr*>(e)) {
    auto n = std::make_unique<OldExpr>(); n->sub=cloneExpr(o->sub.get());
    n->line=o->line; n->col=o->col; return n;
  }
  if (auto q = dynamic_cast<const QuantExpr*>(e)) {
    auto n = std::make_unique<QuantExpr>(); n->isForall=q->isForall; n->binder=q->binder;
    n->binderType=q->binderType; n->lo=cloneExpr(q->lo.get()); n->hi=cloneExpr(q->hi.get());
    n->inclusive=q->inclusive; n->body=cloneExpr(q->body.get());
    n->line=q->line; n->col=q->col; return n;
  }
  // --- Advanced math node clones ---
  // PowerExpr: deep-clone base + exponent and carry the Sema-assigned
  // resultType so a cloned power (e.g. a generic-instantiation or macro-
  // substituted copy) keeps its resolved float width without re-running
  // checkExpr.
  if (auto p = dynamic_cast<const PowerExpr*>(e)) {
    auto n = std::make_unique<PowerExpr>();
    n->base = cloneExpr(p->base.get());
    n->exponent = cloneExpr(p->exponent.get());
    n->resultType = p->resultType;
    n->line = p->line; n->col = p->col;
    return n;
  }
  // MatrixLit: clone every element of every row. elemType is carried as-is;
  // the row structure (rows x cols) is preserved by cloning each inner
  // vector in order.
  if (auto m = dynamic_cast<const MatrixLit*>(e)) {
    auto n = std::make_unique<MatrixLit>();
    for (const auto& row : m->rows) {
      std::vector<ExprPtr> crow;
      crow.reserve(row.size());
      for (const auto& el : row) crow.push_back(cloneExpr(el.get()));
      n->rows.push_back(std::move(crow));
    }
    n->elemType = m->elemType;
    n->line = m->line; n->col = m->col;
    return n;
  }
  // SolveExpr: deep-clone lhs + rhs and carry the Sema-assigned resultType.
  if (auto s = dynamic_cast<const SolveExpr*>(e)) {
    auto n = std::make_unique<SolveExpr>();
    n->lhs = cloneExpr(s->lhs.get());
    n->rhs = cloneExpr(s->rhs.get());
    n->resultType = s->resultType;
    n->line = s->line; n->col = s->col;
    return n;
  }
  // MatMulExpr: deep-clone lhs + rhs and carry elemType.
  if (auto mm = dynamic_cast<const MatMulExpr*>(e)) {
    auto n = std::make_unique<MatMulExpr>();
    n->lhs = cloneExpr(mm->lhs.get());
    n->rhs = cloneExpr(mm->rhs.get());
    n->elemType = mm->elemType;
    n->line = mm->line; n->col = mm->col;
    return n;
  }
  // IntegrateExpr: deep-clone lo + hi + body and carry samples + resultType.
  if (auto ie = dynamic_cast<const IntegrateExpr*>(e)) {
    auto n = std::make_unique<IntegrateExpr>();
    n->lo = cloneExpr(ie->lo.get());
    n->hi = cloneExpr(ie->hi.get());
    n->body = cloneExpr(ie->body.get());
    n->samples = ie->samples;
    n->resultType = ie->resultType;
    n->line = ie->line; n->col = ie->col;
    return n;
  }
  // MathSymExpr: clone text + resultType.
  if (auto ms = dynamic_cast<const MathSymExpr*>(e)) {
    auto n = std::make_unique<MathSymExpr>();
    n->text = ms->text;
    n->resultType = ms->resultType;
    n->line = ms->line; n->col = ms->col;
    return n;
  }
  // SuperscriptExpr: deep-clone base + exponent and carry text + resultType.
  if (auto ss = dynamic_cast<const SuperscriptExpr*>(e)) {
    auto n = std::make_unique<SuperscriptExpr>();
    n->base = cloneExpr(ss->base.get());
    n->text = ss->text;
    n->exponent = cloneExpr(ss->exponent.get());
    n->resultType = ss->resultType;
    n->line = ss->line; n->col = ss->col;
    return n;
  }
  return nullptr;
}

StmtPtr cloneStmt(const Stmt* s) {
  if (!s) return nullptr;
  if (auto es = dynamic_cast<const ExprStmt*>(s)) { auto n = std::make_unique<ExprStmt>(); n->expr=cloneExpr(es->expr.get()); n->line=es->line; n->col=es->col; return n; }
  if (auto ls = dynamic_cast<const LetStmt*>(s)) { auto n = std::make_unique<LetStmt>(); n->isMut=ls->isMut; n->name=ls->name; n->type=ls->type; n->typeAnnotated=ls->typeAnnotated; n->init=cloneExpr(ls->init.get()); n->line=ls->line; n->col=ls->col; return n; }
  if (auto rs = dynamic_cast<const ReturnStmt*>(s)) { auto n = std::make_unique<ReturnStmt>(); n->value=cloneExpr(rs->value.get()); n->line=rs->line; n->col=rs->col; return n; }
  if (auto is = dynamic_cast<const IfStmt*>(s)) { auto n = std::make_unique<IfStmt>(); n->cond=cloneExpr(is->cond.get()); for (auto& t : is->then) n->then.push_back(cloneStmt(t.get())); for (auto& e : is->else_) n->else_.push_back(cloneStmt(e.get())); n->line=is->line; n->col=is->col; return n; }
  if (auto ws = dynamic_cast<const WhileStmt*>(s)) { auto n = std::make_unique<WhileStmt>(); n->cond=cloneExpr(ws->cond.get()); for (auto& b : ws->body) n->body.push_back(cloneStmt(b.get())); for (auto& inv : ws->invariants) n->invariants.push_back(cloneExpr(inv.get())); n->line=ws->line; n->col=ws->col; return n; }
  if (auto fs = dynamic_cast<const ForStmt*>(s)) { auto n = std::make_unique<ForStmt>(); n->varName=fs->varName; n->varName2=fs->varName2; n->start=cloneExpr(fs->start.get()); n->end=cloneExpr(fs->end.get()); n->step=cloneExpr(fs->step.get()); n->inclusiveEnd=fs->inclusiveEnd; n->isForeach=fs->isForeach; n->iter=cloneExpr(fs->iter.get()); n->elemType=fs->elemType; n->elemType2=fs->elemType2; n->iterType=fs->iterType; n->isMapIter=fs->isMapIter; for (auto& b : fs->body) n->body.push_back(cloneStmt(b.get())); for (auto& inv : fs->invariants) n->invariants.push_back(cloneExpr(inv.get())); n->line=fs->line; n->col=fs->col; return n; }
  if (dynamic_cast<const BreakStmt*>(s)) { auto n = std::make_unique<BreakStmt>(); n->line=s->line; n->col=s->col; return n; }
  if (dynamic_cast<const ContinueStmt*>(s)) { auto n = std::make_unique<ContinueStmt>(); n->line=s->line; n->col=s->col; return n; }
  if (auto b = dynamic_cast<const Block*>(s)) { auto n = std::make_unique<Block>(); n->line=b->line; n->col=b->col; for (auto& st : b->stmts) n->stmts.push_back(cloneStmt(st.get())); return n; }
  if (auto sb = dynamic_cast<const SyncBlock*>(s)) { auto n = std::make_unique<SyncBlock>(); n->line=sb->line; n->col=sb->col; for (auto& st : sb->body) n->body.push_back(cloneStmt(st.get())); return n; }
  if (auto d = dynamic_cast<const DeferStmt*>(s)) { auto n = std::make_unique<DeferStmt>(); n->line=d->line; n->col=d->col; n->body=cloneStmt(d->body.get()); return n; }
  if (auto a = dynamic_cast<const AssertStmt*>(s)) { auto n = std::make_unique<AssertStmt>(); n->cond=cloneExpr(a->cond.get()); for (auto& h : a->byBody) n->byBody.push_back(cloneStmt(h.get())); n->line=a->line; n->col=a->col; return n; }
  // `assume <expr>;` / `trusted assume <expr>;`  -  clone the condition + the
  // isTrusted flag + the sourceCitation string. No nested hints/byBody.
  if (auto as = dynamic_cast<const AssumeStmt*>(s)) { auto n = std::make_unique<AssumeStmt>(); n->cond=cloneExpr(as->cond.get()); n->isTrusted=as->isTrusted; n->sourceCitation=as->sourceCitation; n->line=as->line; n->col=as->col; return n; }
  if (auto is = dynamic_cast<const InstantiateStmt*>(s)) {
    auto n = std::make_unique<InstantiateStmt>(); n->isGround=is->isGround; n->line=is->line; n->col=is->col;
    if (is->q) {
      auto q = std::make_unique<QuantExpr>(); q->isForall=is->q->isForall; q->binder=is->q->binder;
      q->binderType=is->q->binderType; q->lo=cloneExpr(is->q->lo.get()); q->hi=cloneExpr(is->q->hi.get());
      q->inclusive=is->q->inclusive; q->body=cloneExpr(is->q->body.get()); q->line=is->q->line; q->col=is->q->col;
      n->q=std::move(q);
    }
    n->witness=cloneExpr(is->witness.get());
    for (auto& pt : is->patternTerms) n->patternTerms.push_back(cloneExpr(pt.get()));
    return n;
  }
  if (auto cs = dynamic_cast<const CalcStmt*>(s)) {
    // calcD  -  Clone a `calc { ... }` block. Each step's expression is cloned via
    // cloneExpr; the relation string is copied verbatim (it's a literal "==",
    // "!=", "<=", ">=", "<", ">", or ""  -  no AST sharing). Each hint statement is
    // cloned via cloneStmt so a calc's hint blocks survive a proof-inlining
    // clone exactly. Spec-only statements like ProofStmt/ProofBlockStmt have no
    // clone arm (they never get cloned  -  proof-only, never inlined into a
    // runtime body); calc DOES get one for parity with InstantiateStmt so a
    // calc nested inside a hint block that is later inlined copies cleanly.
    auto n = std::make_unique<CalcStmt>(); n->line=cs->line; n->col=cs->col;
    for (auto& step : cs->steps) {
      CalcStep ns;
      ns.expr = cloneExpr(step.expr.get());
      ns.relation = step.relation;
      for (auto& h : step.hints) ns.hints.push_back(cloneStmt(h.get()));
      n->steps.push_back(std::move(ns));
    }
    return n;
  }
  return nullptr;
}
