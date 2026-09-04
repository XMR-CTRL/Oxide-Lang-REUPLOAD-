#include "IRGen.h"

#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <functional>

IRGen::IRGen(Sema& sema) : sema_(sema) {}


static std::string esc(const std::string& s);


static bool isLvalueExpr(Expr* e) {
  if (dynamic_cast<VarRef*>(e)) return true;
  if (dynamic_cast<Index*>(e)) return true;
  if (dynamic_cast<Field*>(e)) return true;
  if (auto u = dynamic_cast<UnaryExpr*>(e)) return u->op == UnaryExpr::Op::deref;
  return false;
}

// The root VarRef of a move: `return a` / `return a.b` / `return *p`. Used to
// suppress dropping a move-only local that is being moved into the return slot
// (the caller takes ownership; dropping it here would double-free).
static std::string genMoveRootVar(Expr* e) {
  if (!e) return "";
  if (auto v = dynamic_cast<VarRef*>(e)) return v->name;
  if (auto f = dynamic_cast<Field*>(e)) return genMoveRootVar(f->base.get());
  if (auto u = dynamic_cast<UnaryExpr*>(e)) {
    if (u->op == UnaryExpr::Op::deref) return genMoveRootVar(u->base.get());
  }
  return "";
}

std::string IRGen::typeStr(BType t) {
  switch (t.tag) {
    case BType::Tag::i64: return "i64";
    case BType::Tag::f64: return "double";
    case BType::Tag::f32: return "float";
    case BType::Tag::bool_: return "i1";
    case BType::Tag::void_: return "void";
    case BType::Tag::str: return "i8*";
    case BType::Tag::array: return "[" + std::to_string(t.count) + " x " + typeStr(arrayElem(t)) + "]";
    case BType::Tag::dynarray: return "i8*";
    case BType::Tag::map_: return "i8*";
    case BType::Tag::set_: return "i8*";
    case BType::Tag::hmap_: return "i8*";
    case BType::Tag::hset_: return "i8*";
    case BType::Tag::channel_: return "i8*";
    case BType::Tag::struct_: return "%struct." + t.structName;
    case BType::Tag::ptr: return typeStr(pointee(t)) + "*";
    case BType::Tag::char_: return "i8";
    case BType::Tag::i8: return "i8";
    case BType::Tag::i16: return "i16";
    case BType::Tag::i32: return "i32";
    case BType::Tag::u8: return "i8";
    case BType::Tag::u16: return "i16";
    case BType::Tag::u32: return "i32";
    case BType::Tag::u64: return "i64";
    case BType::Tag::usize: return "i64";
    case BType::Tag::enum_: return "i64";
    case BType::Tag::fn_: return "i8*";
    case BType::Tag::generic_: return "i64";
  }
  return "i64";
}


static std::string intIrTy(const BType& t) {
  switch (t.tag) {
    case BType::Tag::i8: case BType::Tag::u8: case BType::Tag::char_: return "i8";
    case BType::Tag::i16: case BType::Tag::u16: return "i16";
    case BType::Tag::i32: case BType::Tag::u32: return "i32";
    case BType::Tag::i64: case BType::Tag::u64: case BType::Tag::usize:
    case BType::Tag::enum_: return "i64";
    default: return "i64";
  }
}

std::string IRGen::genCoerce(const std::string& v, BType fromT, BType toT) {
  std::string fr = typeStr(fromT), tt = typeStr(toT);
  if (fr == tt) return v;


  if ((fromT.tag == BType::Tag::ptr || fromT.tag == BType::Tag::fn_) &&
      (toT.tag == BType::Tag::ptr || toT.tag == BType::Tag::fn_)) {


    if (pointee(fromT) == BType::void_ && v == "null")
      return "null";
    if (fr == tt) return v;
    // void* decay: any typed pointer &T bitcasts to &u8 (C `T* -> void*`).
    if (pointee(toT) == BType::u8) {
      std::string r = freshLocal("cast");
      out_ << "  " << r << " = bitcast " << fr << " " << v << " to " << tt << "\n";
      return r;
    }
    std::string r = freshLocal("cast");
    out_ << "  " << r << " = bitcast " << fr << " " << v << " to " << tt << "\n";
    return r;
  }

  // C-style void* decay at call sites: an addressable struct/array value is
  // passed where an opaque `&u8` is expected. `v` is the value's storage
  // address (e.g. `%struct.X*` or `[N x T]*`); bitcast it to the byte-pointer
  // `toT` (i8*, also covers `null`-void pointee handled above). Pairs with the
  // auto-addressof branches in the Call/MethodCall/AssocCall paths.
  if ((fromT.tag == BType::Tag::struct_ || fromT.tag == BType::Tag::array) &&
      toT.tag == BType::Tag::ptr && pointee(toT) == BType::u8) {
    std::string r = freshLocal("decay");
    out_ << "  " << r << " = bitcast " << fr << "* " << v << " to " << tt << "\n";
    return r;
  }

  bool fi = isInt(fromT), ti = isInt(toT);
  bool fp = fromT.tag == BType::Tag::ptr || fromT.tag == BType::Tag::fn_;
  bool tp = toT.tag == BType::Tag::ptr || toT.tag == BType::Tag::fn_;
  if (fi && ti) {
    int wb = bitWidth(fromT), wt = bitWidth(toT);
    std::string r = freshLocal("cast");
    if (wb == wt) return v;
    if (wb > wt) {
      out_ << "  " << r << " = trunc " << fr << " " << v << " to " << tt << "\n";
    } else {
      if (isSignedInt(fromT))
        out_ << "  " << r << " = sext " << fr << " " << v << " to " << tt << "\n";
      else
        out_ << "  " << r << " = zext " << fr << " " << v << " to " << tt << "\n";
    }
    return r;
  }

  if (fi && tp) {
    std::string r = freshLocal("cast");
    out_ << "  " << r << " = inttoptr " << fr << " " << v << " to " << tt << "\n";
    return r;
  }
  if (fp && ti) {
    std::string r = freshLocal("cast");
    out_ << "  " << r << " = ptrtoint " << fr << " " << v << " to " << tt << "\n";
    return r;
  }

  if (fi && isFloat(toT)) {
    std::string r = freshLocal("cast");
    out_ << "  " << r << " = " << (isSignedInt(fromT) ? "sitofp " : "uitofp ")
         << fr << " " << v << " to " << tt << "\n";
    return r;
  }
  if (isFloat(fromT) && ti) {
    std::string r = freshLocal("cast");
    out_ << "  " << r << " = " << (isSignedInt(toT) ? "fptosi " : "fptoui ")
         << fr << " " << v << " to " << tt << "\n";
    return r;
  }


  if (isFloat(fromT) && isFloat(toT)) {
    std::string r = freshLocal("cast");
    out_ << "  " << r << " = " << (toT == BType::f32 ? "fptrunc " : "fpext ")
         << fr << " " << v << " to " << tt << "\n";
    return r;
  }
  return v;
}


std::string IRGen::elemSuffix(const BType& t) {
  switch (t.tag) {
    case BType::Tag::i64: case BType::Tag::enum_:
      return "i64";
    case BType::Tag::f64: return "f64";
    case BType::Tag::bool_: return "i1";
    case BType::Tag::str: return "str";
    case BType::Tag::char_: return "i8";
    default: return "";
  }
}
std::string IRGen::elemIrType(const BType& t) { return typeStr(t); }


std::string IRGen::vecSlotType(const std::string& sx) {
  if (sx == "i64") return "i64";
  if (sx == "f64") return "double";
  if (sx == "i1")  return "i1";
  if (sx == "i8")  return "i8";
  return "i8*";
}

BType IRGen::vecSlotBType(const std::string& sx) {
  if (sx == "i64") return BType::i64;
  if (sx == "f64") return BType::f64;
  if (sx == "i1")  return BType::bool_;
  if (sx == "i8")  return BType::char_;
  return BType::str;
}

std::string IRGen::freshLabel(const std::string& hint) {
  return hint + std::to_string(labelSeq_++);
}
std::string IRGen::freshGlobal(const std::string& hint) {
  return "@" + hint + std::to_string(strSeq_++);
}
int IRGen::freshInt() { return labelSeq_++; }
std::string IRGen::freshLocal(const std::string& hint) {
  return "%" + hint + std::to_string(labelSeq_++);
}

std::pair<std::string, BType> IRGen::findVar(const std::string& name) {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto f = it->find(name);
    if (f != it->end()) return {f->second.first, f->second.second};
  }
  return {"", BType::void_};
}
void IRGen::pushScope() { scopes_.emplace_back(); scopeDropVars_.emplace_back(); }
void IRGen::popScope() { scopes_.pop_back(); scopeDropVars_.pop_back(); }
void IRGen::declareVar(const std::string& name, const std::string& storage, BType t) {
  scopes_.back()[name] = {storage, t};
  // Track drop-having LOCALS (not globals  -  '@'-storage lives for the program's
  // whole lifetime and is never auto-dropped) for scope-exit / early-exit dtors.
  if (t.tag == BType::Tag::struct_ && !storage.empty() && storage[0] != '@') {
    const StructDef* d = findStruct(t.structName);
    bool drop = false;
    for (const StructDef* dd = d; dd && !drop; dd = dd->base)
      drop = dd->hasDrop;
    if (drop) {
      auto& dv = scopeDropVars_.back();
      if (dv.end() == std::find_if(dv.begin(), dv.end(),
            [&](const DropEntry& e) { return e.kind == DropEntry::Var && e.name == name; }))
        dv.push_back({DropEntry::Var, name, nullptr});
    }
  }
}

void IRGen::collectStruct(const BType& t) {
  if (t.tag == BType::Tag::struct_) {
    if (StructDef* d = findStruct(t.structName)) {
      structDefs_[t.structName] = d;
      for (auto& f : d->fields) collectStruct(f.type);
    }
  } else if (t.tag == BType::Tag::array) {
    collectStruct(arrayElem(t));
  } else if (t.tag == BType::Tag::ptr) {
    collectStruct(pointee(t));
  } else if (t.tag == BType::Tag::dynarray) {
    collectStruct(dynArrayElem(t));
  } else if (t.tag == BType::Tag::map_ || t.tag == BType::Tag::hmap_) {
    collectStruct(mapKeyType(t)); collectStruct(mapValType(t));
  } else if (t.tag == BType::Tag::set_ || t.tag == BType::Tag::hset_) {
    collectStruct(setElemType(t));
  }
}

// Emit a call to `impl T { fn drop(&mut self) {} }` for the struct value stored
// at `storage` (a `%struct.T*` slot). Walks the base chain (most-derived first,
// like C++) and calls the drop method for every level that has one. For single
// inheritance the base sub-object is at offset 0, so &derived is a valid &base;
// we bitcast the storage pointer to each level's struct pointer. Returns true if
// at least one drop call was emitted (so callers know whether the exit path has
// side effects and thus may not be elided).
bool IRGen::emitDropFor(const std::string& storage, const BType& type) {
  if (type.tag != BType::Tag::struct_) return false;
  // Collect the chain from most-derived to base, keeping levels that have a drop.
  std::vector<const StructDef*> chain;
  for (const StructDef* d = findStruct(type.structName); d; d = d->base) chain.push_back(d);

  bool any = false;
  // Most-derived dtor runs first (C++ destruction order). `storage` is the
  // %struct.<most-derived>* slot; each level's drop takes %struct.<level>*,
  // which  -  for single inheritance (base-first layout, base at offset 0)  -  is a
  // plain bitcast of that pointer to the level's struct pointer type.
  for (const StructDef* d : chain) {
    if (!d->hasDrop) continue;
    const auto& mit = sema_.methods.find(d->name);
    if (mit == sema_.methods.end() || !mit->second.count("drop")) continue;
    const std::string& mangled = mit->second.at("drop").mangled;
    BType st; st.tag = BType::Tag::struct_; st.structName = d->name;
    collectStruct(st);
    std::string ptr = storage;
    if (d->name != type.structName) {
      std::string bc = freshLocal("dropbc");
      BType hostT; hostT.tag = BType::Tag::struct_; hostT.structName = type.structName;
      out_ << "  " << bc << " = bitcast " << typeStr(hostT) << "* " << storage
           << " to " << typeStr(st) << "*\n";
      ptr = bc;
    }
    out_ << "  call void @" << mangled << "(" << typeStr(st) << "* " << ptr << ")\n";
    any = true;
  }
  return any;
}

// Emit drops for every drop-having local in scope `scopeIndex`, reverse
// declaration order. Each drop is only emitted if the local's binding is still
// live in that scope (scopeDropVars_ mirrors scopes_).
void IRGen::emitScopeDrops(size_t scopeIndex) {
  if (scopeIndex >= scopeDropVars_.size()) return;
  const auto& dv = scopeDropVars_[scopeIndex];
  const auto& vars = scopes_[scopeIndex];
  for (auto it = dv.rbegin(); it != dv.rend(); ++it) {
    if (it->kind == DropEntry::Defer) {
      // `defer { ... }` / `defer stmt;`  -  generate its IR inline, now (at scope
      // exit). The defer's locals/tokens run in the enclosing scope's context,
      // which is still live: emitScopeDrops runs before the scope is popped.
      emitDefer(it->deferBody);
      continue;
    }
    if (suppressDrop_.count(it->name)) continue;   // moved into a return value  -  caller owns it
    auto f = vars.find(it->name);
    if (f == vars.end()) continue;
    (void)emitDropFor(f->second.first, f->second.second);
  }
}

// Run destructors for all scopes from the innermost live scope out to and
// including `toScopeIndex` (the scope an early-exit target lives in), then jump.
// Used at return/break/continue: the jump leaves every scope between the current
// point and the target, so every drop-having local in those scopes must be
// dropped before leaving. Scopes below toScopeIndex stay live.
void IRGen::emitScopeUnwind(size_t toScopeIndex) {
  for (size_t i = scopeDropVars_.size(); i-- > toScopeIndex; )
    emitScopeDrops(i);
}

// Generate a `defer` body's IR inline, right now (we are at scope exit). The
// body lives in the enclosing scope's context, whose bindings are still on the
// scopes_ stack. We reuse genStmt for the actual emission: a Block body gets its
// own transient pushScope/popScopeWithDrops (so any locals IT declares drop when
// the deferred block ends, in correct RAII order). A bare-statement defer is
// emitted as-is. We save/restore `terminated_` so a deferred statement (which by
// Sema ban can't be return/break/continue) cannot falsely mark the enclosing
// exit-path as terminated and skip subsequent drops/defers in this scope.
void IRGen::emitDefer(Stmt* body) {
  if (!body) return;
  bool saved = terminated_;
  terminated_ = false;
  genStmt(body);
  terminated_ = saved;
}

void IRGen::popScopeWithDrops() {
  // If the block just ended in a terminator (return/break/continue/unreachable),
  // its drop-having locals were already destroyed along that exit path via
  // emitScopeUnwind  -  don't drop them a second time here. The emit side never
  // appears in the IR of a block whose genBlock set terminated_.
  if (!terminated_ && !scopeDropVars_.empty())
    emitScopeDrops(scopeDropVars_.size() - 1);
  popScope();
}

void IRGen::excludeDrop(const std::string& name) {
  if (scopeDropVars_.empty()) return;
  auto& dv = scopeDropVars_.back();
  auto it = std::find_if(dv.begin(), dv.end(),
      [&](const DropEntry& e) { return e.kind == DropEntry::Var && e.name == name; });
  if (it != dv.end()) dv.erase(it);
}

bool IRGen::isMoveOnlyStruct(const std::string& sn) const {
  // Any struct with a destructor is move-only on implicit copy (let/assign/pass-
  // by-value): `let b = a` MOVEs a into b (a's slot is dead, only b drops the
  // value -> no double-free). A `clone()` method is an explicit copy the user
  // invokes by name; it does NOT re-enable implicit copying (Rust `Clone` does
  // not make `let b = a` copy  -  only `Copy` does, and Oxide has no `Copy`).
  // Despite the name, this gate is "isNoImplicitCopy"  -  kept for call-site
  // compatibility.
  const StructDef* d = findStruct(sn);
  if (!d) return false;
  for (const StructDef* dd = d; dd; dd = dd->base)
    if (dd->hasDrop) return true;
  return false;
}

bool IRGen::isBaseOf(const std::string& parentName,
                     const std::string& childName) const {
  // Single-inheritance upcast question for arg coercion: is `parentName` an
  // ancestor of `childName` (strictly above  -  same-type is not a base). Mirrors
  // Sema::isBaseOf; replicated here so IRGen's arg-addressing guard doesn't
  // reach into Sema for every call site.
  if (parentName == childName) return false;
  for (const StructDef* d = findStruct(childName); d; d = d->base)
    if (d->base && d->base->name == parentName) return true;
  return false;
}

// `argPtrAccepts(bt, pt)` decides whether an addressable arg whose storage type
// is `bt` (the pointee) can be auto-addressed and coerced to param type `pt`
// (a &T), where `bt`/pointee(pt) may differ by a single-inheritance upcast.
bool IRGen::argPtrAccepts(const BType& bt, const BType& pt) const {
  if (pt.tag != BType::Tag::ptr) return false;
  const BType& want = pointee(pt);
  if (bt == want) return true;
  // void* decay handled by the caller's `decay` flag separately.
  if (bt.tag == BType::Tag::struct_ && want.tag == BType::Tag::struct_ &&
      isBaseOf(want.structName, bt.structName))
    return true;   // &Derived upcast to &Base  -  genCoerce will bitcast
  return false;
}

void IRGen::markMovedOut(const std::string& name) {
  // Remove from whichever live scope currently owns it (reverse so an inner
  // shadow wins). A move makes the source slot dead  -  its scope-exit drop is
  // suppressed by simply taking it out of the drop-list. The destination (a new
  // let binding, a function arg now owned by the callee, or the return slot
  // owned by the caller) will drop the value at ITS scope exit instead.
  for (auto it = scopeDropVars_.rbegin(); it != scopeDropVars_.rend(); ++it) {
    auto& dv = *it;
    auto p = std::find_if(dv.begin(), dv.end(),
        [&](const DropEntry& e) { return e.kind == DropEntry::Var && e.name == name; });
    if (p != dv.end()) { dv.erase(p); return; }
  }
}

void IRGen::maybeMarkArgMoved(Expr* arg, const BType& pt) {
  if (pt.tag != BType::Tag::struct_ || !isMoveOnlyStruct(pt.structName)) return;
  std::string root = genMoveRootVar(arg);
  if (!root.empty()) markMovedOut(root);
}


static long long keyCategory(const BType& t) {
  if (t == BType::str || t.tag == BType::Tag::ptr ||
      t.tag == BType::Tag::dynarray) return 3;
  if (isFloat(t)) return 2;
  if (isSignedInt(t)) return 0;
  return 1;
}


static std::string zeroVal(const BType& t) {
  switch (t.tag) {
    case BType::Tag::f64: return "0.0";
    case BType::Tag::f32: return "0.0";
    case BType::Tag::bool_: return "0";
    case BType::Tag::array: case BType::Tag::struct_:
      return "zeroinitializer";
    case BType::Tag::str: case BType::Tag::ptr: case BType::Tag::dynarray:
    case BType::Tag::map_: case BType::Tag::set_:
    case BType::Tag::hmap_: case BType::Tag::hset_:
      return "null";
    default: return "0";
  }
}

std::string IRGen::globalInit(const BType& t, const GlobalInfo& gi, bool folded) {

  if (folded) {
    if (t == BType::f64) {
      uint64_t bits; double dv = gi.fVal; std::memcpy(&bits, &dv, sizeof(bits));
      std::ostringstream s; s << "0x" << std::hex << std::setfill('0') << std::setw(16) << bits;
      return s.str();
    }
    if (t == BType::bool_) return gi.bVal ? "1" : "0";
    if (t == BType::str) {


      std::string g = freshGlobal("str");
      std::string data = esc(gi.sVal);
      size_t n = gi.sVal.size() + 1;
      globals_ << g << " = private constant [" << n << " x i8] c\"" << data << "\\00\"\n";
      std::string r = freshLocal("sg");


      std::ostringstream s;
      s << "getelementptr ([" << n << " x i8], [" << n << " x i8]* " << g
        << ", i32 0, i32 0)";
      return s.str();
    }
    if (t == BType::char_) return std::to_string((unsigned)gi.cVal);
    if (t.tag == BType::Tag::ptr) return "null";

    return std::to_string(gi.iVal);
  }

  if (t.tag == BType::Tag::array || t.tag == BType::Tag::struct_)
    return "zeroinitializer";
  if (t == BType::f64) return "0.0";
  if (t == BType::bool_) return "0";
  if (t == BType::str || t.tag == BType::Tag::ptr) return "null";
  return "0";
}

void IRGen::emitGlobalsAndExterns() {

  bool any = false;
  for (auto& kv : sema_.funcs) {
    if (!kv.second.isExtern) continue;
    if (!any) out_ << "; extern functions\n";
    any = true;
    out_ << "declare " << typeStr(kv.second.retType) << " @" << kv.first << "(";
    for (size_t i = 0; i < kv.second.paramTypes.size(); i++) {
      if (i) out_ << ", ";
      out_ << typeStr(kv.second.paramTypes[i]);
    }
    out_ << ")\n";
  }
  if (any) out_ << "\n";


  bool anyG = false;
  for (auto& kv : sema_.globals) {
    const std::string& nm = kv.first;
    const GlobalInfo& gi = kv.second;
    collectStruct(gi.type);
    if (!anyG) out_ << "; top-level globals\n";
    anyG = true;
    std::string ty = typeStr(gi.type);
    std::string sym = "@" + nm;
    if (gi.isExtern) {
      out_ << sym << " = external global " << ty << "\n";
    } else if (gi.isConst) {
      std::string init = globalInit(gi.type, gi, gi.hasConstVal);
      out_ << sym << " = private constant " << ty << " " << init << "\n";
    } else {


      std::string init = gi.hasConstVal ? globalInit(gi.type, gi, true) : globalInit(gi.type, gi, false);
      out_ << sym << " = global " << ty << " " << init << "\n";
    }
  }
  if (anyG) out_ << "\n";
}

std::pair<std::string, BType> IRGen::genAddr(Expr* e) {
  if (auto v = dynamic_cast<VarRef*>(e)) {
    auto [store, t] = findVar(v->name);
    if (store.empty()) return {"", BType::void_};
    return {store, t};
  }
  if (auto u = dynamic_cast<UnaryExpr*>(e)) {


    if (u->op == UnaryExpr::Op::deref) {
      auto [pv, pvt] = genExpr(u->base.get());
      if (pvt.tag != BType::Tag::ptr) return {"", BType::void_};
      return {pv, pointee(pvt)};
    }
  }
  if (auto ix = dynamic_cast<Index*>(e)) {
    auto [base, bt] = genAddr(ix->base.get());


    if (!base.empty() && bt.tag == BType::Tag::ptr &&
        pointee(bt).tag == BType::Tag::array) {


      std::string lp = freshLocal("adrix");
      out_ << "  " << lp << " = load " << typeStr(bt) << ", " << typeStr(bt) << "* " << base << "\n";
      base = lp; bt = pointee(bt);
    }


    if (bt.tag == BType::Tag::dynarray) {
      std::string handle;
      if (base.empty()) {
        auto [hv, hvt] = genExpr(ix->base.get());
        handle = hv;
      } else {
        std::string hp = freshLocal("adh");
        out_ << "  " << hp << " = load i8*, i8** " << base << "\n";
        handle = hp;
      }
      BType et = dynArrayElem(bt);
      auto [idx, it2] = genExpr(ix->index.get());
      std::string sx = elemSuffix(et);
      if (!sx.empty()) {
        usedVec_.insert(sx);
        std::string slot = vecSlotType(sx);
        std::string raw = freshLocal("agget");
        out_ << "  " << raw << " = call " << slot << " @ox_vec_get_" << sx
             << "(i8* " << handle << ", i64 " << idx << ")\n";
        std::string r = genCoerce(raw, vecSlotBType(sx), et);
        return {r, et};
      }

      int32_t esz = fieldByteWidth(et);
      usedVec_blob_ = true;
      std::string ep = freshLocal("vbp");
      out_ << "  " << ep << " = call i8* @ox_vec_blob_ptr(i8* " << handle
           << ", i64 " << idx << ", i64 " << (esz > 0 ? esz : 8) << ")\n";
      std::string tp = freshLocal("vbpc");
      out_ << "  " << tp << " = bitcast i8* " << ep << " to " << typeStr(et) << "*\n";
      return {tp, et};
    }
    if (base.empty()) {

      auto [val, vt] = genExpr(ix->base.get());
      if (vt.tag != BType::Tag::array) return {"", BType::void_};
      std::string a = freshLocal("arrtmp");
      out_ << "  " << a << " = alloca " << typeStr(vt) << "\n";
      out_ << "  store " << typeStr(vt) << " " << val << ", " << typeStr(vt) << "* " << a << "\n";
      base = a; bt = vt;
    }
    if (bt.tag != BType::Tag::array) return {"", BType::void_};
    auto [idx2, it2b] = genExpr(ix->index.get());
    boundsCheck(idx2, bt.count);
    std::string elemPtr = freshLocal("ep");
    out_ << "  " << elemPtr << " = getelementptr inbounds " << typeStr(bt) << ", "
         << typeStr(bt) << "* " << base << ", i64 0, i64 " << idx2 << "\n";
    return {elemPtr, arrayElem(bt)};
  }
  if (auto fl = dynamic_cast<Field*>(e)) {
    auto [base, bt] = genAddr(fl->base.get());
    if (base.empty()) {
      auto [val, vt] = genExpr(fl->base.get());


      if (vt.tag == BType::Tag::ptr && pointee(vt).tag == BType::Tag::struct_) {
        std::string lp = freshLocal("adrf");
        out_ << "  " << lp << " = load " << typeStr(pointee(vt)) << ", "
             << typeStr(pointee(vt)) << "* " << val << "\n";
        std::string a = freshLocal("sttmp");
        out_ << "  " << a << " = alloca " << typeStr(pointee(vt)) << "\n";
        out_ << "  store " << typeStr(pointee(vt)) << " " << lp << ", "
             << typeStr(pointee(vt)) << "* " << a << "\n";
        base = a; bt = pointee(vt);
      } else {
        if (vt.tag != BType::Tag::struct_) return {"", BType::void_};
        std::string a = freshLocal("sttmp");
        out_ << "  " << a << " = alloca " << typeStr(vt) << "\n";
        out_ << "  store " << typeStr(vt) << " " << val << ", " << typeStr(vt) << "* " << a << "\n";
        base = a; bt = vt;
      }
    } else if (bt.tag == BType::Tag::ptr && pointee(bt).tag == BType::Tag::struct_) {


      std::string lp = freshLocal("adrf");
      out_ << "  " << lp << " = load " << typeStr(bt) << ", " << typeStr(bt) << "* " << base << "\n";
      base = lp; bt = pointee(bt);
    }
    if (bt.tag != BType::Tag::struct_) return {"", BType::void_};
    StructDef* d = findStruct(bt.structName);
    if (!d) return {"", BType::void_};
    int32_t fi = structFieldIndex(d, fl->field);
    if (fi < 0) return {"", BType::void_};
    std::string fp = freshLocal("fp");
    out_ << "  " << fp << " = getelementptr inbounds " << typeStr(bt) << ", "
         << typeStr(bt) << "* " << base << ", i64 0, i32 " << fi << "\n";
    return {fp, d->fields[fi].type};
  }
  return {"", BType::void_};
}

void IRGen::boundsCheck(const std::string& idx, int32_t count) {

  std::string neg = freshLocal("ic");
  out_ << "  " << neg << " = icmp slt i64 " << idx << ", 0\n";
  std::string ovf = freshLocal("ic");
  out_ << "  " << ovf << " = icmp sge i64 " << idx << ", " << count << "\n";
  std::string bad = freshLocal("bad");
  out_ << "  " << bad << " = or i1 " << neg << ", " << ovf << "\n";
  std::string okBB = freshLabel("idx_ok");
  std::string failBB = freshLabel("idx_fail");
  branch(bad, failBB, okBB);
  beginBlock(failBB);
  out_ << "  call void @ox_bounds_fail(i64 " << idx << ", i64 " << count << ")\n";
  out_ << "  unreachable\n";
  terminated_ = false;
  beginBlock(okBB);
}

void IRGen::strBoundsCheck(const std::string& strPtr, const std::string& idx) {

  std::string len = freshLocal("slen");
  out_ << "  " << len << " = call i64 @ox_strlen(i8* " << strPtr << ")\n";
  std::string neg = freshLocal("ic");
  out_ << "  " << neg << " = icmp slt i64 " << idx << ", 0\n";
  std::string ovf = freshLocal("ic");
  out_ << "  " << ovf << " = icmp sge i64 " << idx << ", " << len << "\n";
  std::string bad = freshLocal("bad");
  out_ << "  " << bad << " = or i1 " << neg << ", " << ovf << "\n";
  std::string okBB = freshLabel("sidx_ok");
  std::string failBB = freshLabel("sidx_fail");
  branch(bad, failBB, okBB);
  beginBlock(failBB);
  out_ << "  call void @ox_bounds_fail(i64 " << idx << ", i64 " << len << ")\n";
  out_ << "  unreachable\n";
  terminated_ = false;
  beginBlock(okBB);
}


// Fresh-SSA-name helper for genExpr and the contract gates. Defined here (at the
// head of the codegen body) so the contract gate helpers that follow can call
// it; the original `tmp` lived after boundsCheck and is now hoisted.
static int g_tmp = 0;
static std::string tmp() {
  return std::string("%t") + std::to_string(g_tmp++);
}

// ox:unsafe Pre-declare the contract-trap runtime symbol. Idempotent (declaredIntrinsics_
// dedups). Mirrors how @ox_bounds_fail is declared in emitHeaderAndRuntime, but
// gated on actual contract use so freestanding programs with no contract that
// would otherwise link against the trap are unaffected (a stray declare of an
// undefined extern is harmless to assemble under clang; the symbol is only
// REFERENCED, and thus required at link time, when a gate is emitted).
void IRGen::ensureContractFailDecl() {
  const std::string name = "ox_contract_fail";
  if (declaredIntrinsics_.count(name)) return;
  declaredIntrinsics_.insert(name);
  // Declared as a side emission into globals_ so it lands in the header region
  // alongside the other runtime declares (emitted before bodies).
  globals_ << "declare void @ox_contract_fail(i32, i32)\n";
}

// Lower a contract clause `cond` (a boolean spec expression, possibly containing
// old(...) and quantifiers) to a runtime trap-gate: on FALSE, call
// @ox_contract_fail(tag, line) and unreachable; on true, continue. The condition
// is evaluated with genExpr, which for a bool-typed expr returns an i1 register
// directly (cmp results are i1; bool var loads are also widened to i1 here  - 
// see genExpr's VarRef path). So we branch on the result as-is, exactly the way
// genIf/genWhile do with their condition (IRGen.cpp:4026,4053). No icmp-ne-0
// coercion: that would feed an i1 to an i64 compare ("expected i64")  -  the bug
// this guard replaced. The fail block is emitted inline; a fresh ok block
// resumes codegen after the gate.
void IRGen::genContractGate(Expr* cond, int tag, int line) {
  if (!cond) return;
  // ox:proof T1  -  elide the runtime gate if the clause references a `spec fn`. A spec fn
  // has NO runtime body (it's an SMT abstraction), so a runtime gate that
  // emitted a `call @spec_fn_name` would produce an undefined-symbol linker
  // error. Walk the clause expression for any `Call` whose callee is a known
  // spec-fn name (per Sema::specFnNames_); if any is present, skip the gate
  // entirely. Sound: the runtime gate is a best-effort check; the real
  // discharge is in SMT, and skipping a runtime gate is an under-approximation
  // (the SMT path already covers the contract).
  {
    std::function<bool(const Expr*)> mentionsSpecFn;
    mentionsSpecFn = [&](const Expr* e) -> bool {
      if (!e) return false;
      if (auto c = dynamic_cast<const Call*>(e)) {
        if (sema_.isSpecFnName(c->callee)) return true;
        for (auto& a : c->args) if (mentionsSpecFn(a.get())) return true;
      }
      if (auto c = dynamic_cast<const MethodCall*>(e)) {
        if (mentionsSpecFn(c->receiver.get())) return true;
        for (auto& a : c->args) if (mentionsSpecFn(a.get())) return true;
      }
      if (auto c = dynamic_cast<const AssocCall*>(e)) {
        for (auto& a : c->args) if (mentionsSpecFn(a.get())) return true;
      }
      if (auto u = dynamic_cast<const UnaryExpr*>(e))
        return mentionsSpecFn(u->base.get());
      if (auto b = dynamic_cast<const BinaryExpr*>(e))
        return mentionsSpecFn(b->lhs.get()) || mentionsSpecFn(b->rhs.get());
      if (auto t = dynamic_cast<const TernaryExpr*>(e))
        return mentionsSpecFn(t->cond.get()) ||
               mentionsSpecFn(t->thenE.get()) ||
               mentionsSpecFn(t->elseE.get());
      if (auto o = dynamic_cast<const OldExpr*>(e))
        return mentionsSpecFn(o->sub.get());
      if (auto q = dynamic_cast<const QuantExpr*>(e))
        return mentionsSpecFn(q->lo.get()) || mentionsSpecFn(q->hi.get()) ||
               mentionsSpecFn(q->body.get());
      if (auto ce = dynamic_cast<const CastExpr*>(e))
        return mentionsSpecFn(ce->e.get());
      if (auto i = dynamic_cast<const Index*>(e))
        return mentionsSpecFn(i->base.get()) || mentionsSpecFn(i->index.get());
      if (auto f = dynamic_cast<const Field*>(e))
        return mentionsSpecFn(f->base.get());
      // A MacroCall's spec-fn references live in the Sema-expanded tree.
      if (auto mx = dynamic_cast<const MacroCall*>(e)) {
        if (mentionsSpecFn(mx->expanded.get())) return true;
        for (auto& a : mx->args) if (mentionsSpecFn(a.get())) return true;
      }
      return false;
    };
    if (mentionsSpecFn(cond)) return;
  }
  ensureContractFailDecl();
  auto [v, vt] = genExpr(cond);
  (void)vt;   // bool-typed genExpr already yields an i1; see comment above.
  std::string okBB = freshLabel("ct_ok");
  std::string failBB = freshLabel("ct_fail");
  branch(v, okBB, failBB);
  beginBlock(failBB);
  out_ << "  call void @ox_contract_fail(i32 " << tag << ", i32 " << line << ")\n";
  out_ << "  unreachable\n";
  terminated_ = false;
  beginBlock(okBB);
}

// ox:proof Lower a quantifier `forall i in lo..hi implies P` / `exists ...` to a runtime
// loop that computes the boolean result as an i1 name: forall = AND of P over
// the range; exists = OR over the range. The binder i is bound to a stack slot
// updated each iteration. Returns the i1 result name (or "" on a degenerate
// range). This is a runtime WITNESS, not a static proof  -  it checks the actual
// elements at run time. Range endpoints are evaluated once before the loop.
//
// CFG:
//   loopBB:  if i < upper goto bodyBB else doneBB
//   bodyBB:  P = body; (forall: if P goto cont else miss)
//            (exists:  if P goto miss else cont)
//   missBB:  acc := target; goto tailBB
//   contBB:  i := i+1; goto loopBB
//   doneBB:  goto tailBB   (acc holds the init value for the no-early-exit case)
//   tailBB:  result = load acc
std::string IRGen::genQuantifierGate(const QuantExpr* q, int /*tag*/, int /*line*/) {
  if (!q) return "";
  ensureContractFailDecl();
  auto [loV, loT] = genExpr(q->lo.get());
  auto [hiV, hiT] = genExpr(q->hi.get());
  std::string loI = genCoerce(loV, loT, BType::i64);
  std::string hiI = genCoerce(hiV, hiT, BType::i64);
  std::string upper = hiI;
  if (q->inclusive) {
    std::string up = tmp();
    out_ << "  " << up << " = add i64 " << hiI << ", 1\n";
    upper = up;
  }
  bool isForall = q->isForall;
  std::string iSlot = freshLocal("qi");
  out_ << "  " << iSlot << " = alloca i64\n";
  out_ << "  store i64 " << loI << ", i64* " << iSlot << "\n";
  std::string accSlot = freshLocal("qacc");
  out_ << "  " << accSlot << " = alloca i1\n";
  out_ << "  store i1 " << (isForall ? "1" : "0") << ", i1* " << accSlot << "\n";

  // Bind the binder name to iSlot in the current scope so body VarRefs resolve.
  declareVar(q->binder, iSlot, BType::i64);

  std::string loopBB = freshLabel("qloop");
  std::string bodyBB = freshLabel("qbody");
  std::string missBB = freshLabel("qmiss");
  std::string contBB = freshLabel("qcont");
  std::string doneBB = freshLabel("qdone");
  std::string tailBB = freshLabel("qtail");

  jump(loopBB);
  beginBlock(loopBB);
  {
    std::string i = tmp();
    out_ << "  " << i << " = load i64, i64* " << iSlot << "\n";
    std::string cnd = freshLocal("qc");
    out_ << "  " << cnd << " = icmp slt i64 " << i << ", " << upper << "\n";
    branch(cnd, bodyBB, doneBB);
  }
  beginBlock(bodyBB);
  auto [pv, pvt] = genExpr(q->body.get());
  (void)pvt;   // bool-typed genExpr already yields an i1 (see genContractGate).
  std::string pI1 = pv;
  if (isForall) {
    // ox:proof forall: P true -> continue; P false -> miss (acc=0)
    branch(pI1, contBB, missBB);
  } else {
    // ox:proof exists: P true -> miss (acc=1); P false -> continue
    branch(pI1, missBB, contBB);
  }
  beginBlock(missBB);
  out_ << "  store i1 " << (isForall ? "0" : "1") << ", i1* " << accSlot << "\n";
  jump(tailBB);
  beginBlock(contBB);
  {
    std::string i = tmp();
    out_ << "  " << i << " = load i64, i64* " << iSlot << "\n";
    std::string ni = tmp();
    out_ << "  " << ni << " = add i64 " << i << ", 1\n";
    out_ << "  store i64 " << ni << ", i64* " << iSlot << "\n";
    jump(loopBB);
  }
  beginBlock(doneBB);
  jump(tailBB);
  beginBlock(tailBB);
  std::string res = tmp();
  out_ << "  " << res << " = load i1, i1* " << accSlot << "\n";
  return res;
}

void IRGen::emitHeaderAndRuntime() {

  for (auto& kv : structDefs_) {
    std::string def = "type { ";
    bool first = true;
    for (auto& f : kv.second->fields) {
      if (!first) def += ", ";
      first = false;
      def += typeStr(f.type);
    }
    def += " }";
    out_ << "%struct." << kv.first << " = " << def << "\n";
  }
  if (!structDefs_.empty()) out_ << "\n";


  auto rd = [&](const char* name, const char* declText) {
    if (userDefinedFns_.count(name)) return;
    out_ << declText;
  };

  rd("ox_puts",       "declare i32 @ox_puts(i8*)\n");
  rd("ox_puti",       "declare i32 @ox_puti(i64)\n");
  rd("ox_putf",       "declare i32 @ox_putf(double)\n");
  rd("ox_newline",    "declare i32 @ox_newline()\n");
  rd("ox_putc",       "declare i32 @ox_putc(i64)\n");

  rd("ox_abs_i64",    "declare i64 @ox_abs_i64(i64)\n");
  out_ << "declare double @llvm.fabs.f64(double)\n";
  rd("ox_sqrt",       "declare double @ox_sqrt(double)\n");
  rd("ox_imin",       "declare i64 @ox_imin(i64, i64)\n");
  rd("ox_imax",       "declare i64 @ox_imax(i64, i64)\n");
  rd("ox_fmin2",      "declare double @ox_fmin2(double, double)\n");
  rd("ox_fmax2",      "declare double @ox_fmax2(double, double)\n");

  rd("ox_itos",       "declare i8* @ox_itos(i64)\n");
  rd("ox_stoi",       "declare i64 @ox_stoi(i8*)\n");
  rd("ox_stod",       "declare double @ox_stod(i8*)\n");

  rd("ox_strlen",     "declare i64 @ox_strlen(i8*)\n");
  rd("ox_strcmp",     "declare i64 @ox_strcmp(i8*, i8*)\n");

  rd("ox_substr",     "declare i8* @ox_substr(i8*, i64, i64)\n");
  rd("ox_strchr",     "declare i64 @ox_strchr(i8*, i64)\n");
  rd("ox_char_str",   "declare i8* @ox_char_str(i64)\n");
  rd("ox_ftos",       "declare i8* @ox_ftos(double)\n");

  rd("ox_sb_new",     "declare i8* @ox_sb_new()\n");
  rd("ox_sb_puts",    "declare void @ox_sb_puts(i8*, i8*)\n");
  rd("ox_sb_finish",  "declare i8* @ox_sb_finish(i8*)\n");

  rd("ox_read_line",  "declare i8* @ox_read_line()\n");
  rd("ox_read_file",  "declare i8* @ox_read_file(i8*)\n");
  rd("ox_file_open",  "declare i64 @ox_file_open(i8*, i8*)\n");
  rd("ox_file_close", "declare i64 @ox_file_close(i64)\n");
  rd("ox_file_read",  "declare i8* @ox_file_read(i64)\n");
  rd("ox_file_write", "declare i64 @ox_file_write(i64, i8*)\n");
  rd("ox_file_exists","declare i1 @ox_file_exists(i8*)\n");

  rd("ox_bounds_fail", "declare void @ox_bounds_fail(i64, i64)\n");

  // ---- extended stdlib declares (gated by `usedExt_`) ----
  // Each line is emitted only if the program actually calls that runtime
  // entry, so untouched stdlib names add nothing to the IR. The set is
  // populated in lowerBuiltin when a call resolves to one of these.
  auto ds = [&](const char* name, const char* declText) {
    if (!usedExt_.count(name) && !userDefinedFns_.count(name)) return;
    out_ << declText;
  };
  // math (f64-dominant)
  ds("ox_pow",       "declare double @ox_pow(double, double)\n");
  ds("ox_floor",     "declare double @ox_floor(double)\n");
  ds("ox_ceil",      "declare double @ox_ceil(double)\n");
  ds("ox_round",     "declare double @ox_round(double)\n");
  ds("ox_lround",    "declare i64   @ox_lround(double)\n");
  ds("ox_trunc",     "declare double @ox_trunc(double)\n");
  ds("ox_sin",       "declare double @ox_sin(double)\n");
  ds("ox_cos",       "declare double @ox_cos(double)\n");
  ds("ox_tan",       "declare double @ox_tan(double)\n");
  ds("ox_asin",      "declare double @ox_asin(double)\n");
  ds("ox_acos",      "declare double @ox_acos(double)\n");
  ds("ox_atan",      "declare double @ox_atan(double)\n");
  ds("ox_atan2",     "declare double @ox_atan2(double, double)\n");
  ds("ox_log",       "declare double @ox_log(double)\n");
  ds("ox_log2",      "declare double @ox_log2(double)\n");
  ds("ox_log10",     "declare double @ox_log10(double)\n");
  ds("ox_exp",       "declare double @ox_exp(double)\n");
  ds("ox_exp2",      "declare double @ox_exp2(double)\n");
  ds("ox_hypot",     "declare double @ox_hypot(double, double)\n");
  ds("ox_fmod",      "declare double @ox_fmod(double, double)\n");
  ds("ox_gcd",       "declare double @ox_gcd(i64, i64)\n");
  ds("ox_isnan",     "declare i64   @ox_isnan(double)\n");
  ds("ox_isinf",     "declare i64   @ox_isinf(double)\n");
  ds("ox_finite",    "declare i64   @ox_finite(double)\n");
  ds("ox_deg2rad",   "declare double @ox_deg2rad(double)\n");
  ds("ox_rad2deg",   "declare double @ox_rad2deg(double)\n");
  ds("ox_pi",        "declare double @ox_pi()\n");
  ds("ox_e",         "declare double @ox_e()\n");
  ds("ox_clampf",    "declare double @ox_clampf(double, double, double)\n");
  ds("ox_clampi",    "declare i64   @ox_clampi(i64, i64, i64)\n");
  // strings
  ds("ox_tolower",   "declare i8* @ox_tolower(i8*)\n");
  ds("ox_toupper",   "declare i8* @ox_toupper(i8*)\n");
  ds("ox_str_reverse","declare i8* @ox_str_reverse(i8*)\n");
  ds("ox_str_repeat","declare i8* @ox_str_repeat(i8*, i64)\n");
  ds("ox_starts_with","declare i64 @ox_starts_with(i8*, i8*)\n");
  ds("ox_ends_with", "declare i64 @ox_ends_with(i8*, i8*)\n");
  ds("ox_str_contains","declare i64 @ox_str_contains(i8*, i8*)\n");
  ds("ox_find",      "declare i64 @ox_find(i8*, i8*)\n");
  ds("ox_trim",      "declare i8* @ox_trim(i8*)\n");
  ds("ox_replace",   "declare i8* @ox_replace(i8*, i8*, i8*)\n");
  ds("ox_split",     "declare i8* @ox_split(i8*, i8*)\n");
  ds("ox_str_join",  "declare i8* @ox_str_join(i8*, i8*)\n");
  ds("ox_itoa_base", "declare i8* @ox_itoa_base(i64, i64)\n");
  ds("ox_stoi_base", "declare i64 @ox_stoi_base(i8*, i64, i8*)\n");
  // vec helpers
  ds("ox_vec_clear",     "declare void @ox_vec_clear(i8*)\n");
  ds("ox_vec_reverse",   "declare void @ox_vec_reverse(i8*, i64)\n");
  ds("ox_vec_remove_at", "declare void @ox_vec_remove_at(i8*, i64, i64)\n");
  ds("ox_vec_insert_at", "declare void @ox_vec_insert_at(i8*, i64, i64, i8*)\n");
  ds("ox_vec_contains",  "declare i64 @ox_vec_contains(i8*, i64, i8*, i64)\n");
  ds("ox_vec_index_of",  "declare i64 @ox_vec_index_of(i8*, i64, i8*, i64)\n");
  ds("ox_vec_extend",    "declare void @ox_vec_extend(i8*, i8*, i64)\n");
  ds("ox_vec_pop_blob",  "declare void @ox_vec_pop_blob(i8*)\n");
  ds("ox_vec_first_i64", "declare i64  @ox_vec_first_i64(i8*)\n");
  ds("ox_vec_last_i64",  "declare i64  @ox_vec_last_i64(i8*)\n");
  ds("ox_vec_pop_i64",   "declare i64  @ox_vec_pop_i64(i8*)\n");
  ds("ox_vec_first_f64", "declare double @ox_vec_first_f64(i8*)\n");
  ds("ox_vec_last_f64",  "declare double @ox_vec_last_f64(i8*)\n");
  ds("ox_vec_pop_f64",   "declare i64  @ox_vec_pop_f64(i8*)\n");
  ds("ox_vec_first_i1",  "declare i1  @ox_vec_first_i1(i8*)\n");
  ds("ox_vec_last_i1",   "declare i1  @ox_vec_last_i1(i8*)\n");
  ds("ox_vec_pop_i1",    "declare i64 @ox_vec_pop_i1(i8*)\n");
  ds("ox_vec_first_i8",  "declare i8  @ox_vec_first_i8(i8*)\n");
  ds("ox_vec_last_i8",   "declare i8  @ox_vec_last_i8(i8*)\n");
  ds("ox_vec_first_str", "declare i8* @ox_vec_first_str(i8*)\n");
  ds("ox_vec_last_str",  "declare i8* @ox_vec_last_str(i8*)\n");
  ds("ox_vec_sum_i64",   "declare i64  @ox_vec_sum_i64(i8*)\n");
  ds("ox_vec_min_i64",   "declare i64  @ox_vec_min_i64(i8*)\n");
  ds("ox_vec_max_i64",   "declare i64  @ox_vec_max_i64(i8*)\n");
  ds("ox_vec_sum_f64",   "declare double @ox_vec_sum_f64(i8*)\n");
  ds("ox_vec_min_f64",   "declare double @ox_vec_min_f64(i8*)\n");
  ds("ox_vec_max_f64",   "declare double @ox_vec_max_f64(i8*)\n");
  // map / set helpers
  ds("ox_map_delete",    "declare void @ox_map_delete(i8*, i8*)\n");
  ds("ox_map_clear",     "declare void @ox_map_clear(i8*)\n");
  ds("ox_set_clear",     "declare void @ox_set_clear(i8*)\n");
  ds("ox_map_val_ptr",   "declare i8* @ox_map_val_ptr(i8*, i64)\n");
  // hmap / hset helpers (parallel to the map/set helpers above; the hash-table
  // counterparts share the same call shapes, gated the same way via usedExt_).
  ds("ox_hmap_delete",   "declare void @ox_hmap_delete(i8*, i8*)\n");
  ds("ox_hmap_clear",    "declare void @ox_hmap_clear(i8*)\n");
  ds("ox_hset_clear",    "declare void @ox_hset_clear(i8*)\n");
  ds("ox_hmap_val_ptr",  "declare i8* @ox_hmap_val_ptr(i8*, i64)\n");
  // time + random
  ds("ox_seed",      "declare void @ox_seed(i64)\n");
  ds("ox_rand",      "declare i64  @ox_rand()\n");
  ds("ox_rand_range","declare i64  @ox_rand_range(i64, i64)\n");
  ds("ox_time_ns",   "declare i64  @ox_time_ns()\n");
  ds("ox_clock_ms",  "declare i64  @ox_clock_ms()\n");
  ds("ox_time_epoch","declare i64  @ox_time_epoch()\n");


  if (!usedVec_.empty() || usedVec_blob_) out_ << "declare i64  @ox_vec_len(i8*)\n";
  for (const auto& sx : usedVec_) {

    std::string et = (sx == "i64") ? "i64" : (sx == "f64") ? "double" :
                     (sx == "i1") ? "i1" : (sx == "i8") ? "i8" : "i8*";
    out_ << "declare i8*  @ox_vec_new_" << sx << "()\n";
    out_ << "declare void @ox_vec_push_" << sx << "(i8*, " << et << ")\n";
    out_ << "declare " << et << " @ox_vec_get_" << sx << "(i8*, i64)\n";
    out_ << "declare void @ox_vec_set_" << sx << "(i8*, i64, " << et << ")\n";
    out_ << "declare void @ox_vec_print_" << sx << "(i8*)\n";
  }

  if (usedVec_blob_) {
    out_ << "declare i8*  @ox_vec_blob_new(i64)\n";
    out_ << "declare void @ox_vec_blob_push(i8*, i64, i8*)\n";
    out_ << "declare void @ox_vec_blob_get(i8*, i64, i64, i8*)\n";
    out_ << "declare void @ox_vec_blob_set(i8*, i64, i64, i8*)\n";
    out_ << "declare i8*  @ox_vec_blob_ptr(i8*, i64, i64)\n";

    out_ << "declare void @llvm.memcpy.p0i8.p0i8.i64(i8*, i8*, i64, i1)\n";
  }

  for (const auto& sx : usedSort_) {
    if (sx == "i64")      out_ << "declare void @ox_sort_i64(i8*)\n";
    else if (sx == "f64") out_ << "declare void @ox_sort_f64(i8*)\n";
    else if (sx == "i1")  out_ << "declare void @ox_sort_i1(i8*)\n";
    else if (sx == "i8")  out_ << "declare void @ox_sort_i8(i8*)\n";
    else if (sx == "str") out_ << "declare void @ox_sort_str(i8*)\n";
  }
  if (usedSort_blob_) out_ << "declare void @ox_sort_blob(i8*, i64, i64)\n";


  if (usedMap_) {
    out_ << "declare i8*  @ox_map_new(i64, i64, i64)\n";
    out_ << "declare i64  @ox_map_len(i8*)\n";
    out_ << "declare i64  @ox_map_contains(i8*, i8*)\n";
    out_ << "declare void @ox_map_set(i8*, i8*, i8*)\n";
    out_ << "declare i64  @ox_map_get(i8*, i8*, i8*)\n";
    out_ << "declare i8*  @ox_map_key_ptr(i8*, i64)\n";

    out_ << "declare void @llvm.memcpy.p0i8.p0i8.i64(i8*, i8*, i64, i1)\n";
  }

  if (usedSet_) {
    out_ << "declare i8*  @ox_set_new(i64, i64)\n";
    out_ << "declare i64  @ox_set_len(i8*)\n";
    out_ << "declare i64  @ox_set_contains(i8*, i8*)\n";
    out_ << "declare void @ox_set_insert(i8*, i8*)\n";
    out_ << "declare void @ox_set_remove(i8*, i8*)\n";
    out_ << "declare i8*  @ox_set_ptr(i8*, i64)\n";
    out_ << "declare void @llvm.memcpy.p0i8.p0i8.i64(i8*, i8*, i64, i1)\n";
  }

  // hmap/hset: hash-table-backed (insertion-ordered) counterparts of map/set.
  // Identical call shapes so the routing below mirrors the map/set paths 1:1.
  if (usedHMap_) {
    out_ << "declare i8*  @ox_hmap_new(i64, i64, i64)\n";
    out_ << "declare i64  @ox_hmap_len(i8*)\n";
    out_ << "declare i64  @ox_hmap_contains(i8*, i8*)\n";
    out_ << "declare void @ox_hmap_set(i8*, i8*, i8*)\n";
    out_ << "declare i64  @ox_hmap_get(i8*, i8*, i8*)\n";
    out_ << "declare i8*  @ox_hmap_key_ptr(i8*, i64)\n";
    out_ << "declare void @llvm.memcpy.p0i8.p0i8.i64(i8*, i8*, i64, i1)\n";
  }
  if (usedHSet_) {
    out_ << "declare i8*  @ox_hset_new(i64, i64)\n";
    out_ << "declare i64  @ox_hset_len(i8*)\n";
    out_ << "declare i64  @ox_hset_contains(i8*, i8*)\n";
    out_ << "declare void @ox_hset_insert(i8*, i8*)\n";
    out_ << "declare void @ox_hset_remove(i8*, i8*)\n";
    out_ << "declare i8*  @ox_hset_ptr(i8*, i64)\n";
    out_ << "declare void @llvm.memcpy.p0i8.p0i8.i64(i8*, i8*, i64, i1)\n";
  }

  // --- Concurrency runtime declares (spawn / sync / channels) ---
  // Gated by the usedSpawn_/usedSync_/usedChan_ flags set when the matching
  // AST node is lowered in genExpr/genStmt. Each is a small set of opaque i8*
  // handle helpers; the suffix-templated channel pair (per element type)
  // mirrors `vec` (i64/f64/i1/str/i8) so the LLVM types line up with the
  // channel's element width.
  if (usedSpawn_) {
    out_ << "declare i8*  @ox_thread_create(i8*, i8*)\n";   // (fnptr, arg) -> handle
    out_ << "declare void @ox_thread_join(i8*)\n";          // wait on handle
  }
  if (usedSync_) {
    out_ << "declare void @ox_sync_begin()\n";
    out_ << "declare void @ox_sync_end()\n";
  }
  for (const auto& sx : usedChan_) {
    // Element IR type mirroring vecSlotType's mapping.
    std::string et = (sx == "i64") ? "i64" : (sx == "f64") ? "double" :
                     (sx == "i1") ? "i1" : (sx == "i8") ? "i8" : "i8*";
    out_ << "declare i8*  @ox_chan_new_" << sx << "(i64)\n";      // (bufcap) -> handle
    out_ << "declare void @ox_chan_send_" << sx << "(i8*, " << et << ")\n";
    out_ << "declare " << et << " @ox_chan_recv_" << sx << "(i8*)\n";
  }
  if (usedChan_blob_) {
    // Blob-element channel: pass the element by pointer + byte width (the
    // runtime memcpy's the bytes in/out exactly like vec_blob). Used only for
    // channel element types without a scalar suffix (none surface today as Sema
    // restricts channel elements to scalar/str/enum, but the path stays sound
    // for any future struct-element channel).
    out_ << "declare i8*  @ox_chan_new_blob(i64, i64)\n";        // (bufcap, elemSize)
    out_ << "declare void @ox_chan_send_blob(i8*, i8*, i64)\n";  // (ch, valPtr, elemSize)
    out_ << "declare void @ox_chan_recv_blob(i8*, i8*, i64)\n"; // (ch, outPtr, elemSize)
    out_ << "declare void @llvm.memcpy.p0i8.p0i8.i64(i8*, i8*, i64, i1)\n";
  }

  // --- Advanced math runtime declares (power / matrix / matmul / solve / integrate) ---
  // Gated by the usedPow*/usedSquare_/usedMat_/usedMatMul_/usedSolve_/
  // usedIntegrate_ flags set when the matching AST node is lowered in genExpr.
  // Each is emitted only if the program actually uses the operator, so an
  // untouched feature adds nothing to the IR / link line.
  // NOTE: a prior edit to this block left raw newlines INSIDE the C string
  // literals (the closing quote was on the line AFTER the `out_ <<`, with a
  // real line break in between), which is illegal in C++ (error C2001: newline
  // in constant) and broke the whole build. Fixed by writing each declare on a
  // single line with a proper `\n` escape inside the string  -  the resulting IR
  // keeps one declare per line exactly as before.
  if (usedPowI_)         out_ << "declare i64    @ox_ipow(i64, i64)\n";            // base^exp (int)
  if (usedPowF_)         out_ << "declare double @ox_pow_f64(double, double)\n";  // a^b (f64)
  if (usedSquare_) {
    out_ << "declare i64    @ox_square_i64(i64)\n";        // x^2 (int)
    out_ << "declare double @ox_square_f64(double)\n";    // x^2 (double)
  }
  if (usedMat_) {
    out_ << "declare i8*  @ox_mat_new(i64, i64)\n";        // (rows, cols) -> handle
    out_ << "declare void @ox_mat_set(i8*, i64, i64, double)\n";  // (h, r, c, val)
    out_ << "declare double @ox_mat_get(i8*, i64, i64)\n";       // (h, r, c) -> val
    out_ << "declare i64  @ox_mat_rows(i8*)\n";           // (h) -> rows
    out_ << "declare i64  @ox_mat_cols(i8*)\n";           // (h) -> cols
    out_ << "declare void @ox_mat_print(i8*)\n";          // (h) -> prints [[a, b], ...]
    out_ << "declare void @ox_mat_free(i8*)\n";           // (h) -> release buffer
  }
  if (usedMatMul_)       out_ << "declare i8* @ox_mat_mul(i8*, i8*)\n";          // A * B -> handle
  if (usedSolve_)        out_ << "declare i8* @ox_mat_solve(i8*, i8*)\n";        // A \ b -> handle
  if (usedIntegrate_)    out_ << "declare double @ox_integrate_trapz(i8*, double, double, i64)\n"; // fptr, lo, hi, N -> area

  out_ << "\n";
}

static std::string esc(const std::string& s) {
  std::string r;
  for (unsigned char c : s) {
    if (c == '\\') { r += "\\\\"; }
    else if (c == '"') { r += "\\22"; }
    else if (c >= 32 && c < 127) r += (char)c;
    else {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\%02X", c);
      r += buf;
    }
  }
  return r;
}

std::pair<std::string, BType> IRGen::genExpr(Expr* e) {

  // DIANOSTIC: detect runaway recursion. Print first ~40 entries with depth,
  // and abort if genExpr recurses deeper than a sane cap.

  if (auto l = dynamic_cast<IntLit*>(e)) return {std::to_string(l->v), BType::i64};
  if (auto l = dynamic_cast<FloatLit*>(e)) {
    if (l->isF32) {
      // Single-precision literal (2.0f): fold to the nearest f32 and emit a
      // decimal `float` constant. LLVM float hex constants must use the 16-digit
      // zero-extended form, which is error-prone, so decimal is both clearer and
      // unambiguous. The value is already nearest-f32 via (float).
      float f = (float)l->v;
      std::ostringstream s;
      s.setf(std::ios::showpoint); s << std::setprecision(9) << (double)f;
      return {s.str(), BType::f32};
    }
    uint64_t bits;
    std::memcpy(&bits, &l->v, sizeof(bits));
    std::ostringstream s;
    s << "0x" << std::hex << std::setfill('0') << std::setw(16) << bits;
    return {s.str(), BType::f64};
  }
  if (auto l = dynamic_cast<BoolLit*>(e))
    return {l->v ? std::string("1") : std::string("0"), BType::bool_};
  if (auto l = dynamic_cast<StrLit*>(e)) {
    std::string g = freshGlobal("str");
    std::string data = esc(l->v);
    size_t n = l->v.size() + 1;
    globals_ << g << " = private constant [" << n << " x i8] c\"" << data << "\\00\"\n";
    std::string r = tmp();
    out_ << "  " << r << " = getelementptr inbounds [" << n << " x i8], [" << n << " x i8]* "
         << g << ", i64 0, i64 0\n";
    return {r, BType::str};
  }
  if (auto l = dynamic_cast<CharLit*>(e))
    return {std::to_string((unsigned)l->v), BType::char_};
  if (dynamic_cast<NullLit*>(e)) {


    return {"null", makePtr(BType::void_)};
  }
  if (auto s = dynamic_cast<SizeofExpr*>(e)) {


    std::string sz = (s->size > 0) ? std::to_string(s->size) : std::to_string(fieldByteWidth(s->target));
    return {sz, BType::i64};
  }
  if (auto a = dynamic_cast<AsmExpr*>(e)) {


    auto esc = [&](const std::string& s) {
      std::string o;
      for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '"') o += "\\22";
        else if (c == '\n') o += "\\0A";
        else if (c == '\t') o += "\\09";
        else o += c;

      }
      return o;
    };

    std::string cons;
    std::vector<std::string> argTypes, argVals;


    std::vector<std::pair<size_t, Expr*>> inoutPairs;
    size_t outCounter = 0;

    for (auto& io : a->ios) {
      if (!io.isOutput) continue;
      auto [v, vt] = genExpr(io.val.get());
      (void)v; (void)vt;
      if (!cons.empty()) cons += ",";
      cons += "=" + io.constraint;
      if (io.isInOut) inoutPairs.push_back({outCounter, io.val.get()});
      ++outCounter;
    }

    for (auto& io : a->ios) {
      if (io.isOutput) continue;
      auto [v, vt] = genExpr(io.val.get());
      std::string argT = typeStr(vt);
      argTypes.push_back(argT);
      argVals.push_back(v);
      if (!cons.empty()) cons += ",";
      cons += io.constraint;
    }
    for (auto& [oidx, iexpr] : inoutPairs) {
      auto [v, vt] = genExpr(iexpr);
      std::string argT = typeStr(vt);
      argTypes.push_back(argT);
      argVals.push_back(v);
      if (!cons.empty()) cons += ",";
      cons += std::to_string(oidx);
    }

    if (!a->clobbers.empty()) {

      size_t start = 0;
      while (start <= a->clobbers.size()) {
        size_t comma = a->clobbers.find(',', start);
        std::string tok = (comma == std::string::npos)
                          ? a->clobbers.substr(start)
                          : a->clobbers.substr(start, comma - start);

        auto lb = tok.find_first_not_of(" \t");
        auto rb = tok.find_last_not_of(" \t");
        if (lb != std::string::npos) {
          std::string reg = tok.substr(lb, rb - lb + 1);

          if (reg.front() == '{' && reg.back() == '}') reg = reg.substr(1, reg.size() - 2);
          if (!cons.empty()) cons += ",";
          cons += "~{" + reg + "}";
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
      }
    }
    if (a->hasMemory) {
      if (!cons.empty()) cons += ",";
      cons += "~{memory}";
    }


    size_t nOut = a->outputTypes.size();
    std::string r;
    if (nOut >= 1) r = freshLocal("asm");
    out_ << "  ";
    if (nOut >= 1) out_ << r << " = ";
    std::string retIr;
    if (nOut == 0) retIr = "void";
    else if (nOut == 1) retIr = typeStr(a->outputTypes[0]);
    else {
      retIr = "{";
      for (size_t i = 0; i < nOut; i++) {
        if (i) retIr += ", ";
        retIr += typeStr(a->outputTypes[i]);
      }
      retIr += "}";
    }
    out_ << "call " << retIr << " asm ";
    if (a->sideEffect) out_ << "sideeffect ";
    out_ << "\"" << esc(a->asmText) << "\", \"" << cons << "\"(";
    for (size_t i = 0; i < argTypes.size(); i++) {
      if (i) out_ << ", ";
      out_ << argTypes[i] << " " << argVals[i];
    }
    out_ << ")\n";

    size_t outIdx = 0;
    for (auto& io : a->ios) {
      if (!io.isOutput) continue;

      std::string ov;
      BType ot = a->outputTypes[outIdx];
      if (nOut == 1) ov = r;
      else {
        ov = freshLocal("asmout");
        out_ << "  " << ov << " = extractvalue " << retIr << " " << r << ", " << outIdx << "\n";
      }
      auto [addr, addrTy] = genAddr(io.val.get());
      if (!addr.empty()) {
        std::string v = genCoerce(ov, ot, addrTy);
        out_ << "  store " << typeStr(addrTy) << " " << v << ", " << typeStr(addrTy)
             << "* " << addr << "\n";
      }
      outIdx++;
    }

    if (nOut == 1) return {r, a->outputTypes[0]};
    return {"", BType::void_};
  }
  if (auto c = dynamic_cast<CastExpr*>(e)) {
    auto [v, vt] = genExpr(c->e.get());
    return {genCoerce(v, vt, c->target), c->target};
  }
  if (auto v = dynamic_cast<VarRef*>(e)) {
    // `result` inside an ensures gate refers to the function's stored return
    // value (the coerced value placed into curResultSlot_ before the exit branch).
    if (inEnsuresGate_ && v->name == "result" && !curResultSlot_.empty()) {
      std::string r = tmp();
      out_ << "  " << r << " = load " << typeStr(curFnRet_) << ", "
           << typeStr(curFnRet_) << "* " << curResultSlot_ << "\n";
      return {r, curFnRet_};
    }
    auto [store, t] = findVar(v->name);
    if (store.empty()) {

      auto [ed, ord] = resolveEnumVariant(v->name);
      if (ed) return {std::to_string(ord), makeEnumType(ed->name)};

      auto fit = sema_.funcs.find(v->name);
      if (fit != sema_.funcs.end() && !fit->second.isExtern) {

        return {"@" + v->name, makeFnType(fit->second.paramTypes, fit->second.retType)};
      }
      return {"0", BType::i64};
    }
    std::string r = tmp();
    out_ << "  " << r << " = load " << typeStr(t) << ", " << typeStr(t) << "* " << store << "\n";
    return {r, t};
  }
  if (auto u = dynamic_cast<UnaryExpr*>(e)) {


    if (u->methodOverload) {
      std::vector<ExprPtr> noargs;
      return emitOverloadCall(u->overloadStruct, u->overloadMethod,
                              u->overloadRecvType, u->recvByRef, u->base.get(),
                              noargs, false);
    }
    auto [b, bt] = genExpr(u->base.get());
    bool fp = (bt == BType::f64);
    if (u->op == UnaryExpr::Op::addr || u->op == UnaryExpr::Op::deref) {
      if (u->op == UnaryExpr::Op::addr) {
        auto [p, pte] = genAddr(u->base.get());
        (void)pte;
        if (p.empty()) return {"null", makePtr(BType::i8)};

        return {p, makePtr(bt)};
      }

      auto [pv, pvt] = genExpr(u->base.get());
      if (pvt.tag != BType::Tag::ptr) return {"0", BType::i64};
      BType et = pointee(pvt);
      std::string r = freshLocal("deref");
      out_ << "  " << r << " = load " << typeStr(et) << ", " << typeStr(et) << "* " << pv << "\n";
      return {r, et};
    }
    std::string r = tmp();
    switch (u->op) {
      case UnaryExpr::Op::neg:
        if (fp) out_ << "  " << r << " = fneg double " << b << "\n";
        else out_ << "  " << r << " = sub " << intIrTy(bt) << " 0, " << b << "\n";
        return {r, bt};
      case UnaryExpr::Op::not_:
        out_ << "  " << r << " = xor i1 " << b << ", true\n";
        return {r, BType::bool_};
      case UnaryExpr::Op::bnot:
        out_ << "  " << r << " = xor " << intIrTy(bt) << " " << b << ", -1\n";
        return {r, BType::i64};
      default: return {"0", BType::i64};
    }
  }
  if (auto b = dynamic_cast<BinaryExpr*>(e)) {
    if (b->op == BinaryExpr::Op::land || b->op == BinaryExpr::Op::lor) {
      std::string rhsBB = freshLabel("sc_rhs");
      std::string doneBB = freshLabel("sc_done");
      std::string trueBB = freshLabel("sc_true");
      std::string r = tmp();
      std::string falseSrc;
      if (b->op == BinaryExpr::Op::land) {
        auto [l, lt] = genExpr(b->lhs.get());
        falseSrc = curBlock_;
        branch(l, rhsBB, doneBB);
        beginBlock(rhsBB);
        auto [rr, rt] = genExpr(b->rhs.get());
        std::string rhsSrc = curBlock_;
        jump(doneBB);
        beginBlock(doneBB);
        out_ << "  " << r << " = phi i1 [ false, %" << falseSrc << " ], [ " << rr << ", %" << rhsSrc << " ]\n";
      } else {
        auto [l, lt] = genExpr(b->lhs.get());
        branch(l, trueBB, rhsBB);
        beginBlock(rhsBB);
        auto [rr, rt] = genExpr(b->rhs.get());
        std::string rhsSrc = curBlock_;
        jump(doneBB);
        beginBlock(trueBB);
        std::string trueSrc = trueBB;
        jump(doneBB);
        beginBlock(doneBB);
        out_ << "  " << r << " = phi i1 [ " << rr << ", %" << rhsSrc << " ], [ true, %" << trueSrc << " ]\n";
      }
      return {r, BType::bool_};
    }


    if (b->methodOverload) {
      std::vector<ExprPtr> args;
      args.push_back(std::unique_ptr<Expr>(b->rhs.release()));
      bool neg = (b->op == BinaryExpr::Op::ne);
      return emitOverloadCall(b->overloadStruct, b->overloadMethod,
                              b->overloadRecvType, b->recvByRef, b->lhs.get(),
                              args, neg);
    }
    auto [l, lt] = genExpr(b->lhs.get());
    auto [r, rt] = genExpr(b->rhs.get());
    bool fp = isFloat(lt);
    bool isCmp = (b->op >= BinaryExpr::Op::eq && b->op <= BinaryExpr::Op::ge);


    if (lt.tag == BType::Tag::ptr && (b->op == BinaryExpr::Op::add || b->op == BinaryExpr::Op::sub) && isInt(rt)) {
      std::string idx = r;
      if (b->op == BinaryExpr::Op::sub) {
        std::string n = freshLocal("ni");
        out_ << "  " << n << " = sub i64 0, " << idx << "\n";
        idx = n;
      }
      std::string res = freshLocal("gep");
      out_ << "  " << res << " = getelementptr inbounds " << typeStr(pointee(lt)) << ", "
           << typeStr(lt) << " " << l << ", i64 " << idx << "\n";
      return {res, lt};
    }
    if (rt.tag == BType::Tag::ptr && b->op == BinaryExpr::Op::add && isInt(lt)) {
      std::string res = freshLocal("gep");
      out_ << "  " << res << " = getelementptr inbounds " << typeStr(pointee(rt)) << ", "
           << typeStr(rt) << " " << r << ", i64 " << l << "\n";
      return {res, rt};
    }

    if (lt.tag == BType::Tag::ptr && rt.tag == BType::Tag::ptr && (b->op == BinaryExpr::Op::eq || b->op == BinaryExpr::Op::ne)) {
      std::string res = freshLocal("pc");
      out_ << "  " << res << " = icmp " << (b->op == BinaryExpr::Op::eq ? "eq" : "ne")
           << " " << typeStr(lt) << " " << l << ", " << r << "\n";
      return {res, BType::bool_};
    }


    if (lt == BType::str && rt == BType::str && b->op == BinaryExpr::Op::add) {
      std::string sb = freshLocal("sb"); out_ << "  " << sb << " = call i8* @ox_sb_new()\n";
      out_ << "  call void @ox_sb_puts(i8* " << sb << ", i8* " << l << ")\n";
      out_ << "  call void @ox_sb_puts(i8* " << sb << ", i8* " << r << ")\n";
      std::string res = freshLocal("cat");
      out_ << "  " << res << " = call i8* @ox_sb_finish(i8* " << sb << ")\n";
      return {res, BType::str};
    }


    if (b->op == BinaryExpr::Op::add &&
        ((lt == BType::str && rt == BType::char_) ||
         (lt == BType::char_ && rt == BType::str))) {
      std::string ls = l, rs = r;
      if (lt == BType::char_) {
        std::string c = genCoerce(l, BType::char_, BType::i64);
        ls = freshLocal("c2s");
        out_ << "  " << ls << " = call i8* @ox_char_str(i64 " << c << ")\n";
      }
      if (rt == BType::char_) {
        std::string c = genCoerce(r, BType::char_, BType::i64);
        rs = freshLocal("c2s");
        out_ << "  " << rs << " = call i8* @ox_char_str(i64 " << c << ")\n";
      }
      std::string sb = freshLocal("sb"); out_ << "  " << sb << " = call i8* @ox_sb_new()\n";
      out_ << "  call void @ox_sb_puts(i8* " << sb << ", i8* " << ls << ")\n";
      out_ << "  call void @ox_sb_puts(i8* " << sb << ", i8* " << rs << ")\n";
      std::string res = freshLocal("cat");
      out_ << "  " << res << " = call i8* @ox_sb_finish(i8* " << sb << ")\n";
      return {res, BType::str};
    }

    if (lt == BType::str && rt == BType::str && (b->op == BinaryExpr::Op::eq || b->op == BinaryExpr::Op::ne)) {
      std::string d = freshLocal("scmp");
      out_ << "  " << d << " = call i64 @ox_strcmp(i8* " << l << ", i8* " << r << ")\n";
      std::string res = freshLocal("ceq");
      out_ << "  " << res << " = icmp eq i64 " << d << ", 0\n";
      if (b->op == BinaryExpr::Op::ne) {
        std::string nres = freshLocal("cne");
        out_ << "  " << nres << " = xor i1 " << res << ", true\n";
        res = nres;
      }
      return {res, BType::bool_};
    }


    if (lt == BType::str && rt == BType::str &&
        (b->op == BinaryExpr::Op::lt || b->op == BinaryExpr::Op::le ||
         b->op == BinaryExpr::Op::gt || b->op == BinaryExpr::Op::ge)) {
      std::string d = freshLocal("scmp");
      out_ << "  " << d << " = call i64 @ox_strcmp(i8* " << l << ", i8* " << r << ")\n";
      const char* pred = nullptr;
      switch (b->op) {
        case BinaryExpr::Op::lt: pred = "slt"; break;
        case BinaryExpr::Op::le: pred = "sle"; break;
        case BinaryExpr::Op::gt: pred = "sgt"; break;
        case BinaryExpr::Op::ge: pred = "sge"; break;
        default: pred = "slt"; break;
      }
      std::string res = freshLocal("ord");
      out_ << "  " << res << " = icmp " << pred << " i64 " << d << ", 0\n";
      return {res, BType::bool_};
    }

    // Matrix multiply via BinaryExpr::mul with isMatMul flag (set by Sema when
    // both operands are 2-D arrays).  Both sides are already opaque i8* matrix
    // handles from their MatrixLit / MatMulExpr / array binding genExpr arms;
    // delegate to the runtime @ox_mat_mul and return a fresh handle.
    if (b->isMatMul && b->op == BinaryExpr::Op::mul) {
      usedMatMul_ = true;
      std::string mr = freshLocal("mm");
      out_ << "  " << mr << " = call i8* @ox_mat_mul(i8* " << l << ", i8* " << r << ")\n";
      return {mr, makePtr(BType::i8)};
    }

    std::string lval = genCoerce(l, lt, lt);
    std::string rval = genCoerce(r, rt, lt);
    std::string res = tmp();
    std::string instruction;
    bool isIntOp = isInt(lt);


    std::string ity = isIntOp ? intIrTy(lt) : typeStr(lt);
    bool signedOp = isSignedInt(lt);
    switch (b->op) {
      case BinaryExpr::Op::add: instruction = fp ? ("fadd " + ity) : ("add " + ity); break;
      case BinaryExpr::Op::sub: instruction = fp ? ("fsub " + ity) : ("sub " + ity); break;
      case BinaryExpr::Op::mul: instruction = fp ? ("fmul " + ity) : ("mul " + ity); break;
      case BinaryExpr::Op::div: instruction = fp ? ("fdiv " + ity) : (std::string(signedOp ? "sdiv " : "udiv ") + ity); break;
      case BinaryExpr::Op::mod: instruction = fp ? ("frem " + ity) : (std::string(signedOp ? "srem " : "urem ") + ity); break;
      case BinaryExpr::Op::band: instruction = "and " + ity; break;
      case BinaryExpr::Op::bor: instruction = "or " + ity; break;
      case BinaryExpr::Op::bxor: instruction = "xor " + ity; break;
      case BinaryExpr::Op::shl: instruction = "shl " + ity; break;
      case BinaryExpr::Op::shr: instruction = std::string(signedOp ? "ashr " : "lshr ") + ity; break;
      case BinaryExpr::Op::eq: instruction = fp ? ("fcmp oeq " + ity) : ("icmp eq " + ity); break;
      case BinaryExpr::Op::ne: instruction = fp ? ("fcmp one " + ity) : ("icmp ne " + ity); break;
      case BinaryExpr::Op::lt: instruction = fp ? ("fcmp olt " + ity) : (std::string("icmp ") + (signedOp ? "slt " : "ult ") + ity); break;
      case BinaryExpr::Op::gt: instruction = fp ? ("fcmp ogt " + ity) : (std::string("icmp ") + (signedOp ? "sgt " : "ugt ") + ity); break;
      case BinaryExpr::Op::le: instruction = fp ? ("fcmp ole " + ity) : (std::string("icmp ") + (signedOp ? "sle " : "ule ") + ity); break;
      case BinaryExpr::Op::ge: instruction = fp ? ("fcmp oge " + ity) : (std::string("icmp ") + (signedOp ? "sge " : "uge ") + ity); break;
      default: instruction = fp ? ("fadd " + ity) : ("add " + ity); break;
    }
    out_ << "  " << res << " = " << instruction << " " << lval << ", " << rval << "\n";
    return {res, isCmp ? BType::bool_ : lt};
  }
  if (auto t = dynamic_cast<TernaryExpr*>(e)) {


    BType rt = t->resultTy;
    auto [cv, ct] = genExpr(t->cond.get());
    std::string thenBB = freshLabel("tern_then");
    std::string elseBB = freshLabel("tern_else");
    std::string doneBB = freshLabel("tern_done");
    branch(cv, thenBB, elseBB);
    beginBlock(thenBB);
    auto [tv, tt] = genExpr(t->thenE.get());
    std::string tc = genCoerce(tv, tt, rt);
    std::string thenSrc = curBlock_;
    jump(doneBB);
    beginBlock(elseBB);
    auto [ev, et] = genExpr(t->elseE.get());
    std::string ec = genCoerce(ev, et, rt);
    std::string elseSrc = curBlock_;
    jump(doneBB);
    beginBlock(doneBB);
    std::string r = tmp();
    out_ << "  " << r << " = phi " << typeStr(rt) << " [ " << tc << ", %" << thenSrc
         << " ], [ " << ec << ", %" << elseSrc << " ]\n";
    return {r, rt};
  }
  if (auto d = dynamic_cast<IncDecExpr*>(e)) {


    ExprPtr lv;
    if (d->kind == AssignTarget::Kind::var) {
      auto vr = std::make_unique<VarRef>();
      vr->name = d->name; vr->line = d->line; vr->col = d->col;
      lv = std::move(vr);
    } else if (d->kind == AssignTarget::Kind::field) {
      auto fl = std::make_unique<Field>();
      fl->field = d->field; fl->line = d->line; fl->col = d->col;
      fl->base = cloneExpr(d->base.get());
      lv = std::move(fl);
    } else if (d->kind == AssignTarget::Kind::index) {
      auto ix = std::make_unique<Index>();
      ix->line = d->line; ix->col = d->col;
      ix->base = cloneExpr(d->base.get());
      ix->index = cloneExpr(d->index.get());
      lv = std::move(ix);
    } else {
      auto u = std::make_unique<UnaryExpr>();
      u->op = UnaryExpr::Op::deref; u->line = d->line; u->col = d->col;
      u->base = cloneExpr(d->base.get());
      lv = std::move(u);
    }
    auto [addr, st] = genAddr(lv.get());


    if (d->kind == AssignTarget::Kind::index && d->valueTy.tag != BType::Tag::void_ &&
        (addr.empty() || st != d->valueTy)) {

      auto [bv, bvt] = genExpr(d->base.get());
      auto [idxv, idxt] = genExpr(d->index.get());
      BType et = d->valueTy;
      std::string sx = elemSuffix(et);
      if (!sx.empty()) {
        usedVec_.insert(sx);
        std::string oldv = freshLocal("igg");
        out_ << "  " << oldv << " = call " << typeStr(et) << " @ox_vec_get_"
             << sx << "(i8* " << bv << ", i64 " << idxv << ")\n";
        std::string one = freshLocal("one");
        out_ << "  " << one << " = add " << typeStr(et) << " " << oldv << ", "
             << (d->isInc ? "1" : "-1") << "\n";
        out_ << "  call void @ox_vec_set_" << sx << "(i8* " << bv << ", i64 "
             << idxv << ", " << typeStr(et) << " " << one << ")\n";
        return {d->isPost ? oldv : one, et};
      }


    }
    if (addr.empty()) return {"", d->valueTy};
    BType vt = d->valueTy;
    std::string oldv = freshLocal("inc");
    out_ << "  " << oldv << " = load " << typeStr(vt) << ", " << typeStr(vt) << "* "
         << addr << "\n";


    std::string newv, storev;
    if (vt.tag == BType::Tag::ptr) {
      newv = freshLocal("incp");
      BType pt = pointee(vt);
      out_ << "  " << newv << " = getelementptr inbounds " << typeStr(pt) << ", "
           << typeStr(vt) << " " << oldv << ", i64 " << (d->isInc ? "1" : "-1") << "\n";
      storev = newv;
    } else if (vt == BType::bool_) {

      storev = freshLocal("incb");
      const char* op = d->isInc ? "or" : "and";
      std::string imm = d->isInc ? "true" : "false";
      out_ << "  " << storev << " = " << op << " i1 " << oldv << ", " << imm << "\n";
      newv = storev;
    } else {
      bool fp = (vt == BType::f64 || vt == BType::f32);

      newv = freshLocal("incn");
      const char* op = d->isInc ? (fp ? "fadd" : "add") : (fp ? "fsub" : "sub");
      std::string imm = fp ? "1.0" : "1";
      out_ << "  " << newv << " = " << op << " " << typeStr(vt) << " " << oldv << ", " << imm << "\n";
      storev = newv;
    }
    out_ << "  store " << typeStr(vt) << " " << storev << ", " << typeStr(vt) << "* "
         << addr << "\n";
    return {d->isPost ? oldv : newv, vt};
  }
  if (auto a = dynamic_cast<AssignTarget*>(e)) {


    if (a->methodOverload) {
      ExprPtr recv;
      a->line = a->line;
      if (a->kind == AssignTarget::Kind::var) {
        auto vr = std::make_unique<VarRef>();
        vr->name = a->name; vr->line = a->line; vr->col = a->col;
        recv = std::move(vr);
      } else if (a->kind == AssignTarget::Kind::field) {
        auto fl = std::make_unique<Field>();
        fl->field = a->field; fl->line = a->line; fl->col = a->col;
        fl->base = std::unique_ptr<Expr>(a->base.release());
        recv = std::move(fl);
      } else if (a->kind == AssignTarget::Kind::index) {
        auto ix = std::make_unique<Index>();
        ix->line = a->line; ix->col = a->col;
        ix->base = std::unique_ptr<Expr>(a->base.release());
        ix->index = std::unique_ptr<Expr>(a->index.release());
        recv = std::move(ix);
      } else if (a->kind == AssignTarget::Kind::deref) {
        auto u = std::make_unique<UnaryExpr>();
        u->op = UnaryExpr::Op::deref; u->line = a->line; u->col = a->col;
        u->base = std::unique_ptr<Expr>(a->base.release());
        recv = std::move(u);
      }
      std::vector<ExprPtr> args;
      args.push_back(std::unique_ptr<Expr>(a->value.release()));
      return emitOverloadCall(a->overloadStruct, a->overloadMethod,
                              a->overloadRecvType,true, recv.get(),
                              args, false);
    }
    std::string addr;
    BType st = BType::void_;
    if (a->kind == AssignTarget::Kind::var) {
      auto [s, t] = findVar(a->name);
      addr = s; st = t;
    } else if (a->kind == AssignTarget::Kind::index) {
      auto [baddr, bt] = genAddr(a->base.get());

      if (!baddr.empty() && bt.tag == BType::Tag::ptr &&
          pointee(bt).tag == BType::Tag::array) {
        std::string lp = freshLocal("adri");
        out_ << "  " << lp << " = load " << typeStr(bt) << ", " << typeStr(bt) << "* " << baddr << "\n";
        BType at = pointee(bt);
        auto [idx, it2] = genExpr(a->index.get());
        boundsCheck(idx, at.count);
        std::string ep = freshLocal("ep");
        out_ << "  " << ep << " = getelementptr inbounds " << typeStr(at) << ", "
             << typeStr(at) << "* " << lp << ", i64 0, i64 " << idx << "\n";
        addr = ep; st = arrayElem(at);
      } else if (!baddr.empty() && bt.tag == BType::Tag::array) {
        auto [idx, it2] = genExpr(a->index.get());
        boundsCheck(idx, bt.count);
        std::string ep = freshLocal("ep");
        out_ << "  " << ep << " = getelementptr inbounds " << typeStr(bt) << ", "
             << typeStr(bt) << "* " << baddr << ", i64 0, i64 " << idx << "\n";
        addr = ep; st = arrayElem(bt);
      } else {

        auto [bv, bvt] = genExpr(a->base.get());
        auto [idx, it2] = genExpr(a->index.get());
        if (bvt.tag == BType::Tag::dynarray) {
          BType et = dynArrayElem(bvt);
          std::string sx = elemSuffix(et);
          dynSetPending_ = true;
          dynSetBlob_ = sx.empty();
          dynSetHandle_ = bv; dynSetIdx_ = idx; dynSetSx_ = sx; dynSetEt_ = et;
          st = et;
          if (!sx.empty()) usedVec_.insert(sx); else usedVec_blob_ = true;
        } else if (bvt.tag == BType::Tag::map_ || bvt.tag == BType::Tag::hmap_) {
          // `m[k] = v`: there is no in-memory slot (entries live in the runtime
          // table), so defer the @ox_map_set/@ox_hmap_set until the RHS `v` is
          // evaluated below. Spill the key now; spill the value when it lands.
          BType keyT = mapKeyType(bvt), valT = mapValType(bvt);
          if (bvt.tag == BType::Tag::hmap_) usedHMap_ = true; else usedMap_ = true;
          collectStruct(keyT); collectStruct(valT);
          mapSetPending_ = true;
          mapIsH_ = (bvt.tag == BType::Tag::hmap_);
          mapHandle_ = bv;
          keyScratch_ = spillScratch(genCoerce(idx, it2, keyT), keyT);
          mapValT_ = valT;
          st = valT;
        }
      }
    } else if (a->kind == AssignTarget::Kind::field) {
      auto [baddr, bt] = genAddr(a->base.get());


      if (!baddr.empty() && bt.tag == BType::Tag::ptr &&
          pointee(bt).tag == BType::Tag::struct_) {
        std::string lp = freshLocal("adrfw");
        out_ << "  " << lp << " = load " << typeStr(bt) << ", " << typeStr(bt) << "* " << baddr << "\n";
        bt = pointee(bt);
        baddr = lp;
      }
      if (!baddr.empty() && bt.tag == BType::Tag::struct_) {
        StructDef* d = findStruct(bt.structName);
        int32_t fi = d ? structFieldIndex(d, a->field) : -1;
        if (fi >= 0) {
          std::string fp = freshLocal("fp");
          out_ << "  " << fp << " = getelementptr inbounds " << typeStr(bt) << ", "
               << typeStr(bt) << "* " << baddr << ", i64 0, i32 " << fi << "\n";
          addr = fp; st = d->fields[fi].type;
        }
      }
    } else if (a->kind == AssignTarget::Kind::deref) {


      auto [pv, pvt] = genExpr(a->base.get());
      if (pvt.tag == BType::Tag::ptr) { addr = pv; st = pointee(pvt); }
    }
    auto [v, vt] = genExpr(a->value.get());

    if (dynSetPending_) {
      dynSetPending_ = false;
      if (dynSetBlob_) {


        std::string cv = genCoerce(v, vt, dynSetEt_);
        std::string tmp = freshLocal("vbset");
        out_ << "  " << tmp << " = alloca " << typeStr(dynSetEt_) << "\n";
        out_ << "  store " << typeStr(dynSetEt_) << " " << cv << ", "
             << typeStr(dynSetEt_) << "* " << tmp << "\n";
        int32_t esz = fieldByteWidth(dynSetEt_);
        out_ << "  call void @ox_vec_blob_set(i8* " << dynSetHandle_
             << ", i64 " << dynSetIdx_ << ", i64 " << (esz > 0 ? esz : 8)
             << ", i8* " << tmp << ")\n";
        return {cv, dynSetEt_};
      }
      if (a->isCompound) {
        auto oldv = freshLocal("vget");
        out_ << "  " << oldv << " = call " << typeStr(dynSetEt_) << " @ox_vec_get_"
             << dynSetSx_ << "(i8* " << dynSetHandle_ << ", i64 " << dynSetIdx_ << ")\n";
        bool fp = (dynSetEt_ == BType::f64);
        const char* op = nullptr;
        switch (a->compound) {
          case BinaryExpr::Op::add: op = fp ? "fadd double" : "add i64"; break;
          case BinaryExpr::Op::sub: op = fp ? "fsub double" : "sub i64"; break;
          case BinaryExpr::Op::mul: op = fp ? "fmul double" : "mul i64"; break;
          case BinaryExpr::Op::div: op = fp ? "fdiv double" : "sdiv i64"; break;
          case BinaryExpr::Op::mod: op = fp ? "frem double" : "srem i64"; break;
          default: op = "add i64"; break;
        }
        std::string res = freshLocal("op");
        out_ << "  " << res << " = " << op << " " << oldv << ", " << v << "\n";
        out_ << "  call void @ox_vec_set_" << dynSetSx_ << "(i8* " << dynSetHandle_
             << ", i64 " << dynSetIdx_ << ", " << typeStr(dynSetEt_) << " " << res << ")\n";
        return {res, dynSetEt_};
      }
      out_ << "  call void @ox_vec_set_" << dynSetSx_ << "(i8* " << dynSetHandle_
           << ", i64 " << dynSetIdx_ << ", " << typeStr(dynSetEt_) << " " << v << ")\n";
      return {v, dynSetEt_};
    }
    if (mapSetPending_) {
      mapSetPending_ = false;
      const char* rt_get = mapIsH_ ? "ox_hmap_get" : "ox_map_get";
      const char* rt_set = mapIsH_ ? "ox_hmap_set" : "ox_map_set";
      std::string cur;
      if (a->isCompound) {
        // `m[k] += v` et al: read the current value, combine with the RHS,
        // store back. There is no addressable slot, so do the load via the
        // runtime get-into-scratch.
        std::string vslot = freshLocal("mcs");
        out_ << "  " << vslot << " = alloca " << typeStr(mapValT_) << "\n";
        std::string vptr = freshLocal("mcsp");
        out_ << "  " << vptr << " = bitcast " << typeStr(mapValT_) << "* " << vslot
             << " to i8*\n";
        out_ << "  call i64 @" << rt_get << "(i8* " << mapHandle_ << ", i8* "
             << keyScratch_ << ", i8* " << vptr << ")\n";
        cur = loadScratch(vptr, mapValT_);
      }
      std::string storev = genCoerce(v, vt, mapValT_);
      if (a->isCompound) {
        const char* op = nullptr;
        bool fp = (mapValT_ == BType::f64 || mapValT_ == BType::f32);
        switch (a->compound) {
          case BinaryExpr::Op::add: op = fp ? "fadd" : "add"; break;
          case BinaryExpr::Op::sub: op = fp ? "fsub" : "sub"; break;
          case BinaryExpr::Op::mul: op = fp ? "fmul" : "mul"; break;
          case BinaryExpr::Op::div: op = fp ? "fdiv" : (isSignedInt(mapValT_) ? "sdiv" : "udiv"); break;
          case BinaryExpr::Op::mod: op = fp ? "frem" : (isSignedInt(mapValT_) ? "srem" : "urem"); break;
          default: op = fp ? "fadd" : "add"; break;
        }
        std::string res = freshLocal("mcr");
        out_ << "  " << res << " = " << op << " " << typeStr(mapValT_) << " " << cur
             << ", " << storev << "\n";
        storev = res;
      }
      std::string vpScratch = spillScratch(storev, mapValT_);
      out_ << "  call void @" << rt_set << "(i8* " << mapHandle_ << ", i8* "
           << keyScratch_ << ", i8* " << vpScratch << ")\n";
      return {storev, mapValT_};
    }
    if (addr.empty()) return {v, st};
    if (a->isCompound) {


      if (st == BType::str && a->compound == BinaryExpr::Op::add) {
        std::string cur = freshLocal("ld");
        out_ << "  " << cur << " = load " << typeStr(st) << ", " << typeStr(st) << "* " << addr << "\n";
        std::string sb = freshLocal("sb"); out_ << "  " << sb << " = call i8* @ox_sb_new()\n";
        out_ << "  call void @ox_sb_puts(i8* " << sb << ", i8* " << cur << ")\n";
        out_ << "  call void @ox_sb_puts(i8* " << sb << ", i8* " << v << ")\n";
        std::string res = freshLocal("cat");
        out_ << "  " << res << " = call i8* @ox_sb_finish(i8* " << sb << ")\n";
        out_ << "  store " << typeStr(st) << " " << res << ", " << typeStr(st) << "* " << addr << "\n";
        return {res, st};
      }
      std::string cur = freshLocal("ld");
      out_ << "  " << cur << " = load " << typeStr(st) << ", " << typeStr(st) << "* " << addr << "\n";
      bool fp = (st == BType::f64);
      const char* op = nullptr;
      switch (a->compound) {
        case BinaryExpr::Op::add: op = fp ? "fadd double" : "add i64"; break;
        case BinaryExpr::Op::sub: op = fp ? "fsub double" : "sub i64"; break;
        case BinaryExpr::Op::mul: op = fp ? "fmul double" : "mul i64"; break;
        case BinaryExpr::Op::div: op = fp ? "fdiv double" : "sdiv i64"; break;
        case BinaryExpr::Op::mod: op = fp ? "frem double" : "srem i64"; break;
        case BinaryExpr::Op::band: op = "and i64"; break;
        case BinaryExpr::Op::bor: op = "or i64"; break;
        case BinaryExpr::Op::bxor: op = "xor i64"; break;
        case BinaryExpr::Op::shl: op = "shl i64"; break;
        case BinaryExpr::Op::shr: op = "ashr i64"; break;
        default: op = "add i64"; break;
      }
      std::string res = freshLocal("op");
      out_ << "  " << res << " = " << op << " " << cur << ", " << v << "\n";
      out_ << "  store " << typeStr(st) << " " << res << ", " << typeStr(st) << "* " << addr << "\n";
      return {res, st};
    }
    // Overwrite-drop: for a plain (non-compound, non-vec-set) assignment INTO a
    // location holding a drop-having struct, run the old value's destructor BEFORE
    // the store  -  else the previous owning value leaks (the classic C++ assignment
    // pitfall). The RHS was already materialized above (genExpr(value)), so even a
    // self-referencing RHS like `a = a.clone()` is safe: the new value sits in a
    // temp by the time we drop the old slot.
    if (!addr.empty() && st.tag == BType::Tag::struct_ && !a->isCompound)
      (void)emitDropFor(addr, st);
    out_ << "  store " << typeStr(st) << " " << v << ", " << typeStr(st) << "* " << addr << "\n";
    // Move discipline on assignment: `b = a` where a is a whole hasDrop struct
    // value rooted at a local MOVES a into b  -  a's slot is now dead and may not
    // be dropped again at scope exit (only b drops the value). This mirrors the
    // let-init move path. Only when the RHS value is itself the drop-struct (a
    // whole-aggregate move); assigning a field like `b.x = a.x` does not move a.
    if (!a->isCompound && st.tag == BType::Tag::struct_ && vt.tag == BType::Tag::struct_ &&
        isMoveOnlyStruct(st.structName) && vt == st) {
      std::string root = genMoveRootVar(a->value.get());
      if (!root.empty() && root != a->name) markMovedOut(root);
    }
    return {v, st};
  }
  if (auto ix = dynamic_cast<Index*>(e)) {


    if (ix->methodOverload) {
      std::vector<ExprPtr> args;
      args.push_back(std::unique_ptr<Expr>(ix->index.release()));
      return emitOverloadCall(ix->overloadStruct, ix->overloadMethod,
                              ix->overloadRecvType, ix->recvByRef, ix->base.get(),
                              args, false);
    }

    // ── Matrix-handle 2D indexing intercept ────────────────────────────
    // When a matrix variable M (declared with type ptr(void_) by the
    // LetStmt matrix-slot path) is indexed as M[r][c], the AST is
    //   Index( Index( VarRef(M), r ), c )
    // The normal path below calls genExpr(ix->base) first, which would
    // recurse into the inner Index, hit the old ptr(void_) branch, and
    // return f64  -  making the outer Index see a non-pointer and fail.
    // We must intercept BEFORE the genExpr(ix->base) call on L1990.
    //
    // Pattern: ix->base is Index*, ix->base->base is VarRef*, and
    // findVar(name) yields type ptr(void_).
    if (auto outerBase = dynamic_cast<Index*>(ix->base.get())) {
      if (auto vr = dynamic_cast<VarRef*>(outerBase->base.get())) {
        auto [store, vrt] = findVar(vr->name);
        if (vrt.tag == BType::Tag::ptr && pointee(vrt) == BType::i8) {
          // Confirmed M[r][c] on a matrix binding.
          usedMat_ = true;
          // Reload the handle ourselves  -  genExpr(VarRef) gives the loaded i8*.
          auto [mhv, mhvt] = genExpr(vr);  // {handle_i8star, ptr(void_)}
          auto [rv, rt2] = genExpr(outerBase->index.get());   // row expr
          auto [cv, ct2] = genExpr(ix->index.get());           // col expr
          std::string rvI = genCoerce(rv, rt2, BType::i64);
          std::string cvI = genCoerce(cv, ct2, BType::i64);
          std::string r = freshLocal("mget");
          out_ << "  " << r << " = call double @ox_mat_get(i8* " << mhv
               << ", i64 " << rvI << ", i64 " << cvI << ")\n";
          return {r, BType::f64};
        }
      }
    }
    // ── End matrix 2D intercept ─────────────────────────────────────────
    auto [bv, bvt] = genExpr(ix->base.get());
    if (bvt.tag == BType::Tag::dynarray) {
      BType et = dynArrayElem(bvt);
      std::string sx = elemSuffix(et);
      if (!sx.empty()) {

        usedVec_.insert(sx);
        auto [iv, it2] = genExpr(ix->index.get());
        std::string slot = vecSlotType(sx);
        std::string raw = freshLocal("vget");
        out_ << "  " << raw << " = call " << slot << " @ox_vec_get_" << sx
             << "(i8* " << bv << ", i64 " << iv << ")\n";
        std::string r = genCoerce(raw, vecSlotBType(sx), et);
        return {r, et};
      }


      usedVec_blob_ = true;
      auto [iv, it2] = genExpr(ix->index.get());
      int32_t esz = fieldByteWidth(et);
      std::string dst = freshLocal("vbg");
      out_ << "  " << dst << " = alloca " << typeStr(et) << "\n";
      out_ << "  call void @ox_vec_blob_get(i8* " << bv << ", i64 " << iv
           << ", i64 " << (esz > 0 ? esz : 8) << ", i8* " << dst << ")\n";


      std::string r = freshLocal("vbl");
      out_ << "  " << r << " = load " << typeStr(et) << ", " << typeStr(et) << "* " << dst << "\n";
      return {r, et};
    }
    // Matrix-handle 2D indexing: when genExpr(base) returns {i8*, ptr(void_)},
    // we have a matrix handle from MatrixLit/SolveExpr/MatMulExpr/VarRef of a
    // matrix binding.  If ix->base is itself an Index (M[r][c] pattern), we
    // need to extract both row and col and call @ox_mat_get.  Otherwise single
    // indexing on a matrix handle means row→element which isn't directly
    // meaningful, but we handle the common M[r][c] case.
    if (bvt.tag == BType::Tag::ptr && pointee(bvt) == BType::i8) {
      // Try to recognize nested Index(Index(matrix, row), col).
      if (auto innerIx = dynamic_cast<Index*>(ix->base.get())) {
        // Get the matrix handle from the innermost base
        auto [mhv, mhvt] = genExpr(innerIx->base.get());
        if (mhvt.tag == BType::Tag::ptr && pointee(mhvt) == BType::i8) {
          usedMat_ = true;
          auto [rv, rt2] = genExpr(innerIx->index.get());   // row
          auto [cv, ct2] = genExpr(ix->index.get());         // col
          std::string rvI = genCoerce(rv, rt2, BType::i64);
          std::string cvI = genCoerce(cv, ct2, BType::i64);
          std::string r = freshLocal("mget");
          out_ << "  " << r << " = call double @ox_mat_get(i8* " << mhv
               << ", i64 " << rvI << ", i64 " << cvI << ")\n";
          return {r, BType::f64};
        }
      }
      // Single-level index on a matrix handle  -  treat index as flattened
      // (row*ncols + col) and we don't know ncols at codegen time, so fall
      // through to genAddr which will also fail.  Better: just call with
      // (index, 0) as a best-effort column-0 access.
      usedMat_ = true;
      auto [cv, ct2] = genExpr(ix->index.get());
      std::string cvI = genCoerce(cv, ct2, BType::i64);
      std::string r = freshLocal("mget");
      out_ << "  " << r << " = call double @ox_mat_get(i8* " << bv
           << ", i64 " << cvI << ", i64 0)\n";
      return {r, BType::f64};
    }
    if (bvt == BType::str) {
      auto [ivRaw, it2] = genExpr(ix->index.get());
      std::string iv = genCoerce(ivRaw, it2, BType::i64);
      strBoundsCheck(bv, iv);

      std::string ce = freshLocal("sc");
      out_ << "  " << ce << " = getelementptr inbounds i8, i8* " << bv << ", i64 " << iv << "\n";
      std::string r = freshLocal("sl");
      out_ << "  " << r << " = load i8, i8* " << ce << "\n";
      return {r, BType::char_};
    }
    if (bvt.tag == BType::Tag::map_ || bvt.tag == BType::Tag::hmap_) {
      // `m[k]` READ: no addressable slot (entry lives in the runtime table), so
      // load via @ox_map_get/@ox_hmap_get into a scratch slot. A missing key
      // reads as the zero value (ox_*_get zero-fills on miss), matching map_get.
      bool hm = (bvt.tag == BType::Tag::hmap_);
      if (hm) usedHMap_ = true; else usedMap_ = true;
      BType keyT = mapKeyType(bvt), valT = mapValType(bvt);
      collectStruct(keyT); collectStruct(valT);
      auto [iv, it2] = genExpr(ix->index.get());
      std::string kp = spillScratch(genCoerce(iv, it2, keyT), keyT);
      std::string vslot = freshLocal("igv");
      out_ << "  " << vslot << " = alloca " << typeStr(valT) << "\n";
      out_ << "  store " << typeStr(valT) << " " << zeroVal(valT) << ", " << typeStr(valT)
           << "* " << vslot << "\n";
      std::string vp = freshLocal("igp");
      out_ << "  " << vp << " = bitcast " << typeStr(valT) << "* " << vslot << " to i8*\n";
      out_ << "  call i64 @" << (hm ? "ox_hmap_get" : "ox_map_get") << "(i8* " << bv
           << ", i8* " << kp << ", i8* " << vp << ")\n";
      std::string rv = loadScratch(vp, valT);
      return {rv, valT};
    }
    auto [addr, bt] = genAddr(ix);
    if (addr.empty()) return {"0", BType::i64};
    std::string r = freshLocal("idx");
    out_ << "  " << r << " = load " << typeStr(bt) << ", " << typeStr(bt) << "* " << addr << "\n";
    return {r, bt};
  }
  if (auto fl = dynamic_cast<Field*>(e)) {
    auto [addr, bt] = genAddr(fl);
    if (addr.empty()) return {"0", BType::i64};
    std::string r = freshLocal("fld");
    out_ << "  " << r << " = load " << typeStr(bt) << ", " << typeStr(bt) << "* " << addr << "\n";
    return {r, bt};
  }
  if (auto al = dynamic_cast<ArrayLit*>(e)) {
    // 2D nested ArrayLit: [[1,0],[0,1]]  -  route through the matrix runtime
    // (ox_mat_new / ox_mat_set) just like MatrixLit.  When the first element
    // is itself an ArrayLit we build an opaque i8* handle so the value composes
    // with matmul, solve, printValue, and `let m = [[..]]` bindings.
    if (!al->elems.empty() && dynamic_cast<ArrayLit*>(al->elems[0].get())) {
      usedMat_ = true;
      size_t rows = al->elems.size();
      auto* row0 = dynamic_cast<ArrayLit*>(al->elems[0].get());
      size_t cols = row0 ? row0->elems.size() : 0;
      std::string h = freshLocal("mat");
      out_ << "  " << h << " = call i8* @ox_mat_new(i64 " << (long long)rows
           << ", i64 " << (long long)cols << ")\n";
      for (size_t r = 0; r < rows; r++) {
        auto* rowAL = dynamic_cast<ArrayLit*>(al->elems[r].get());
        if (!rowAL) continue;
        for (size_t c = 0; c < rowAL->elems.size() && c < cols; c++) {
          auto [v, vt] = genExpr(rowAL->elems[c].get());
          std::string cv = genCoerce(v, vt, BType::f64);
          out_ << "  call void @ox_mat_set(i8* " << h << ", i64 " << (long long)r
               << ", i64 " << (long long)c << ", double " << cv << ")\n";
        }
      }
      return {h, makePtr(BType::i8)};
    }
    // 1D ArrayLit  -  stack alloca, by value
    BType elemT = BType::i64;
    if (!al->elems.empty()) elemT = genExpr(al->elems[0].get()).second;
    BType arrT = makeArrayType(elemT, (int32_t)al->elems.size());
    std::string a = freshLocal("arr");
    out_ << "  " << a << " = alloca " << typeStr(arrT) << "\n";
    for (size_t i = 0; i < al->elems.size(); i++) {
      auto [v, vt] = genExpr(al->elems[i].get());
      std::string cv = genCoerce(v, vt, elemT);
      std::string ep = freshLocal("ae");
      out_ << "  " << ep << " = getelementptr inbounds " << typeStr(arrT) << ", "
           << typeStr(arrT) << "* " << a << ", i64 0, i64 " << i << "\n";
      out_ << "  store " << typeStr(elemT) << " " << cv << ", " << typeStr(elemT) << "* " << ep << "\n";
    }
    std::string r = freshLocal("arrval");
    out_ << "  " << r << " = load " << typeStr(arrT) << ", " << typeStr(arrT) << "* " << a << "\n";
    return {r, arrT};
  }
  if (auto sl = dynamic_cast<StructLit*>(e)) {
    BType st; st.tag = BType::Tag::struct_; st.structName = sl->name;
    std::string a = freshLocal("st");
    out_ << "  " << a << " = alloca " << typeStr(st) << "\n";
    StructDef* d = findStruct(sl->name);
    for (size_t i = 0; i < sl->values.size() && i < sl->fieldNames.size(); i++) {
      auto [v, vt] = genExpr(sl->values[i].get());
      if (!d) continue;
      int32_t fi = structFieldIndex(d, sl->fieldNames[i]);
      if (fi < 0) continue;
      BType ft = d->fields[fi].type;
      std::string cv = genCoerce(v, vt, ft);
      std::string fp = freshLocal("sf");
      out_ << "  " << fp << " = getelementptr inbounds " << typeStr(st) << ", "
           << typeStr(st) << "* " << a << ", i64 0, i32 " << fi << "\n";
      out_ << "  store " << typeStr(ft) << " " << cv << ", " << typeStr(ft) << "* " << fp << "\n";
    }
    // For a polymorphic struct, install the vtable pointer into the synthetic
    // __oxvt slot at the root's offset 0 (reachable here at field index 0 of
    // the merged d->fields). The user never names this field; Sema rejects
    // literal attempts to set it. Runs in EVERY construction path: a literal,
    // and the impl `new` associated fn (which returns a StructLit). A root
    // polymorphic only by up-propagation has an empty [0 x i8*] vtable and a
    // 0-element global  -  its ptr is still stored (never slot-indexed, no harm).
    if (d && isPolymorphic(sl->name)) {
      int vfi = vtableFieldIndex(d);
      std::string fp = freshLocal("vtfp");
      out_ << "  " << fp << " = getelementptr inbounds " << typeStr(st) << ", "
           << typeStr(st) << "* " << a << ", i64 0, i32 " << vfi << "\n";
      out_ << "  store i8* bitcast ([" << d->vtableSlots.size()
           << " x i8*]* @__oxvt_" << sl->name << " to i8*), i8* " << fp << "\n";
    }
    std::string r = freshLocal("stval");
    out_ << "  " << r << " = load " << typeStr(st) << ", " << typeStr(st) << "* " << a << "\n";
    return {r, st};
  }
  if (auto dn = dynamic_cast<DynNew*>(e)) {
    std::string sx = elemSuffix(dn->elemType);
    std::string r = freshLocal("vnew");
    if (!sx.empty()) {

      usedVec_.insert(sx);
      out_ << "  " << r << " = call i8* @ox_vec_new_" << sx << "()\n";
    } else {


      usedVec_blob_ = true;
      int32_t esz = fieldByteWidth(dn->elemType);
      out_ << "  " << r << " = call i8* @ox_vec_blob_new(i64 " << (esz > 0 ? esz : 8) << ")\n";
    }
    return {r, makeDynArray(dn->elemType)};
  }
  if (auto mn = dynamic_cast<MapNew*>(e)) {


    BType kt = mn->keyType, vt = mn->valType;
    long long kw = fieldByteWidth(kt), vw = fieldByteWidth(vt), kk = keyCategory(kt);
    if (kw <= 0) kw = 8;
    if (vw <= 0) vw = 8;
    usedMap_ = true;
    collectStruct(kt); collectStruct(vt);
    std::string r = freshLocal("mnew");
    out_ << "  " << r << " = call i8* @ox_map_new(i64 " << kw << ", i64 " << vw
         << ", i64 " << kk << ")\n";
    return {r, makeMapType(kt, vt)};
  }
  if (auto sn = dynamic_cast<SetNew*>(e)) {
    BType et = sn->elemType;
    long long kw = fieldByteWidth(et), kk = keyCategory(et);
    if (kw <= 0) kw = 8;
    usedSet_ = true; collectStruct(et);
    std::string r = freshLocal("snew");
    out_ << "  " << r << " = call i8* @ox_set_new(i64 " << kw << ", i64 " << kk << ")\n";
    return {r, makeSetType(et)};
  }
  if (auto hmn = dynamic_cast<HMapNew*>(e)) {
    BType kt = hmn->keyType, vt = hmn->valType;
    long long kw = fieldByteWidth(kt), vw = fieldByteWidth(vt), kk = keyCategory(kt);
    if (kw <= 0) kw = 8;
    if (vw <= 0) vw = 8;
    usedHMap_ = true; collectStruct(kt); collectStruct(vt);
    std::string r = freshLocal("hmnew");
    out_ << "  " << r << " = call i8* @ox_hmap_new(i64 " << kw << ", i64 " << vw
         << ", i64 " << kk << ")\n";
    return {r, makeHMapType(kt, vt)};
  }
  if (auto hsn = dynamic_cast<HSetNew*>(e)) {
    BType et = hsn->elemType;
    long long kw = fieldByteWidth(et), kk = keyCategory(et);
    if (kw <= 0) kw = 8;
    usedHSet_ = true; collectStruct(et);
    std::string r = freshLocal("hsnew");
    out_ << "  " << r << " = call i8* @ox_hset_new(i64 " << kw << ", i64 " << kk << ")\n";
    return {r, makeHSetType(et)};
  }
  // `Channel<T>::new()`  -  construct a buffered channel handle. Register the
  // element-type suffix in usedChan_ (or usedChan_blob_ when the element has
  // no scalar suffix) so the matching `@ox_chan_*` declares are emitted, and
  // call `@ox_chan_new_<sx>(0)` (buffer capacity 0 = unbuffered/synchronous;
  // a future `Channel<T>::new(cap)` would pass `cap`). The channel value is an
  // opaque i8* handle; its Oxide type is `Channel<T>` (makeChannelType).
  if (auto cn = dynamic_cast<ChannelNew*>(e)) {
    BType et = cn->elemType;
    collectStruct(et);
    std::string sx = elemSuffix(et);
    std::string r = freshLocal("chnew");
    if (!sx.empty()) {
      usedChan_.insert(sx);
      out_ << "  " << r << " = call i8* @ox_chan_new_" << sx << "(i64 0)\n";
    } else {
      usedChan_blob_ = true;
      long long esz = fieldByteWidth(et); if (esz <= 0) esz = 8;
      out_ << "  " << r << " = call i8* @ox_chan_new_blob(i64 0, i64 " << esz << ")\n";
    }
    return {r, makeChannelType(et)};
  }
  // `chan <- val`  -  send `val` into `chan`. Evaluate the channel handle to an
  // i8*, evaluate the value, coerce it to the channel's element IR type, and
  // call `@ox_chan_send_<sx>(handle, value)`. Result is void (no value). The
  // channel's element type is recovered from the channel handle's Sema type
  // (`ht`); Sema already verified the value's type matches that element type.
  if (auto cs = dynamic_cast<ChannelSend*>(e)) {
    auto [hv, ht] = genExpr(cs->chan.get());
    auto [vv, vt] = genExpr(cs->val.get());
    BType et = (ht.tag == BType::Tag::channel_) ? channelElemType(ht) : vt;
    collectStruct(et);
    std::string sx = elemSuffix(et);
    if (!sx.empty()) {
      usedChan_.insert(sx);
      std::string slotT = (sx == "i64") ? "i64" : (sx == "f64") ? "double" :
                          (sx == "i1") ? "i1" : (sx == "i8") ? "i8" : "i8*";
      std::string cv = genCoerce(vv, vt, et);
      out_ << "  call void @ox_chan_send_" << sx << "(i8* " << hv << ", "
           << slotT << " " << cv << ")\n";
    } else {
      usedChan_blob_ = true;
      long long esz = fieldByteWidth(et); if (esz <= 0) esz = 8;
      // Spill the value to a stack slot and pass its address (blob semantics).
      std::string slot = freshLocal("chsend");
      out_ << "  " << slot << " = alloca " << typeStr(et) << "\n";
      std::string cv = genCoerce(vv, vt, et);
      out_ << "  store " << typeStr(et) << " " << cv << ", " << typeStr(et)
           << "* " << slot << "\n";
      out_ << "  call void @ox_chan_send_blob(i8* " << hv << ", i8* " << slot
           << ", i64 " << esz << ")\n";
    }
    return {"", BType::void_};
  }
  // `<- chan`  -  receive one value from `chan`. Call `@ox_chan_recv_<sx>(handle)`
  // and return the typed register. Sema cached the element type on the node
  // (`cr->elemType`) so we use that directly (it matches the channel's element
  // type, verified at Sema time). The recv runtime blocks until a value is
  // available, then returns it in a typed register.
  if (auto cr = dynamic_cast<ChannelRecv*>(e)) {
    auto [hv, ht] = genExpr(cr->chan.get());
    BType et = cr->elemType;
    (void)ht;
    collectStruct(et);
    std::string sx = elemSuffix(et);
    if (!sx.empty()) {
      usedChan_.insert(sx);
      std::string slotT = (sx == "i64") ? "i64" : (sx == "f64") ? "double" :
                          (sx == "i1") ? "i1" : (sx == "i8") ? "i8" : "i8*";
      std::string r = freshLocal("chrecv");
      out_ << "  " << r << " = call " << slotT << " @ox_chan_recv_" << sx
           << "(i8* " << hv << ")\n";
      return {r, et};
    } else {
      usedChan_blob_ = true;
      long long esz = fieldByteWidth(et); if (esz <= 0) esz = 8;
      std::string slot = freshLocal("chrecv");
      out_ << "  " << slot << " = alloca " << typeStr(et) << "\n";
      out_ << "  call void @ox_chan_recv_blob(i8* " << hv << ", i8* " << slot
           << ", i64 " << esz << ")\n";
      std::string r = freshLocal("chrecvld");
      out_ << "  " << r << " = load " << typeStr(et) << ", " << typeStr(et)
           << "* " << slot << "\n";
      return {r, et};
    }
  }
  // `spawn <body>`  -  create a thread. Full thread creation would extract the
  // body into a top-level `void(i8*)` wrapper with captured state (a non-
  // trivial lowering). For an honest, non-crashing v1: evaluate the body's
  // side effects INLINE in the current thread (so `spawn print(x);` still
  // happens), then create a thread handle via `@ox_thread_create(null, null)`
  // and return it. The thread runs no body, so the handle may be joined (or
  // ignored). This keeps the surface syntax compilable and runnable while the
  // real concurrency lowering is built out; Sema type-checks the body as a
  // closure so the program is well-formed even though the thread is a stub.
  if (auto sp = dynamic_cast<SpawnExpr*>(e)) {
    usedSpawn_ = true;
    if (sp->body) (void)genExpr(sp->body.get());   // eager side effects, inline
    std::string r = freshLocal("spawn");
    out_ << "  " << r << " = call i8* @ox_thread_create(i8* null, i8* null)\n";
    return {r, BType::i64};   // thread handle (opaque pointer-width)
  }
  if (auto c = dynamic_cast<Call*>(e)) {


    if (c->fnPtr && c->calleeExpr) {
      // Capture-closure call: callee is a `let` bound to a __oxclosure_* struct.
      // Dispatch as `closure.fnptr(closure.cap0, ..., callargs...)`. The lowered
      // lambda takes leading capture params then the real params.
      if (auto v = dynamic_cast<VarRef*>(c->calleeExpr.get())) {
        BType vt = BType::void_;
        for (auto rit = scopes_.rbegin(); rit != scopes_.rend(); ++rit) {
          auto f = rit->find(v->name);
          if (f != rit->end()) { vt = f->second.second; break; }
        }
        if (vt.tag != BType::Tag::struct_ && sema_.globals.count(v->name))
          vt = sema_.globals.at(v->name).type;
        if (vt.tag == BType::Tag::struct_ &&
            vt.structName.rfind("__oxclosure_", 0) == 0) {
          StructDef* d = findStruct(vt.structName);
          // Need an address for the closure to read fields; genAddr of the let.
          auto [caddr, cat] = genAddr(v);
          if (!d || caddr.empty()) return {"0", BType::void_};
          // field 0: fn ptr; load it.
          std::string ffp = freshLocal("cfp");
          out_ << "  " << ffp << " = getelementptr inbounds " << typeStr(vt) << ", "
               << typeStr(vt) << "* " << caddr << ", i64 0, i32 0\n";
          std::string fnt = freshLocal("cfn");
          BType fnTy = d->fields[0].type;
          out_ << "  " << fnt << " = load " << typeStr(fnTy) << ", " << typeStr(fnTy)
               << "* " << ffp << "\n";
          BType retT = fnRet(fnTy);
          std::string retIr = (retT == BType::void_) ? std::string("void") : typeStr(retT);
          // The fn field is stored as i8* (typeStr(fn_)); bitcast it to the
          // concrete `ret (params)*` before the indirect call, exactly like the
          // plain fn-ptr call path, so LLVM sees a real function-pointer call.
          const auto& fps = fnParams(fnTy);
          std::ostringstream fnty;
          fnty << retIr << " (";
          for (size_t i = 0; i < fps.size(); i++) { if (i) fnty << ", "; fnty << typeStr(fps[i]); }
          fnty << ")*";
          std::string fcast = freshLocal("cfcall");
          out_ << "  " << fcast << " = bitcast " << typeStr(fnTy) << " " << fnt
               << " to " << fnty.str() << "\n";
          fnt = fcast;
          // capture field values first (fields 1.. ncols), then call args.
          std::ostringstream args;
          size_t nargs = d->fields.size() - 1 + c->args.size();
          size_t idx = 0;
          for (size_t ci = 1; ci < d->fields.size(); ci++) {
            BType ft = d->fields[ci].type;
            std::string cp = freshLocal("cc");
            out_ << "  " << cp << " = getelementptr inbounds " << typeStr(vt) << ", "
                 << typeStr(vt) << "* " << caddr << ", i64 0, i32 " << ci << "\n";
            std::string cv = freshLocal("cl");
            out_ << "  " << cv << " = load " << typeStr(ft) << ", " << typeStr(ft)
                 << "* " << cp << "\n";
            if (idx) args << ", ";
            args << typeStr(ft) << " " << cv;
            idx++;
          }
          const auto& ps = fnParams(fnTy);   // full params (caps + reals)
          for (size_t k = 0; k < c->args.size(); k++) {
            BType pt = (k + d->fields.size() - 1 < ps.size())
                         ? ps[k + d->fields.size() - 1] : BType::i64;
            std::string cv;
            if (pt.tag == BType::Tag::ptr && isLvalueExpr(c->args[k].get())) {
              auto [addr, bt] = genAddr(c->args[k].get());
              bool decay = (pointee(pt) == BType::u8 &&
                            (bt.tag == BType::Tag::struct_ ||
                             bt.tag == BType::Tag::array ||
                             bt.tag == BType::Tag::ptr));
              if (!addr.empty() && (argPtrAccepts(bt, pt) || decay)) {
                cv = genCoerce(addr, bt, pt);
                if (idx) args << ", ";
                args << typeStr(pt) << " " << cv; idx++;
                continue;
              }
            }
            auto [v2, vt2] = genExpr(c->args[k].get());
            cv = genCoerce(v2, vt2, pt);
            if (idx) args << ", ";
            args << typeStr(pt) << " " << cv; idx++;
          }
          (void)nargs;
          if (retT == BType::void_) {
            out_ << "  call void " << fnt << "(" << args.str() << ")\n";
            return {"", BType::void_};
          }
          std::string r = freshLocal("cc");
          out_ << "  " << r << " = call " << retIr << " " << fnt
               << "(" << args.str() << ")\n";
          return {r, retT};
        }
      }

      // Closure callee that is NOT a bare `VarRef` binding  -  e.g. an
      // immediately-invoked capturing lambda `(fn[&](){...})()` or a closure
      // pulled out of a container `arr[i]()`. The callee value is a struct, so
      // spill it to a fresh alloca and dispatch through field 0 exactly like the
      // VarRef path above. (A non-capturing lambda / fn-ptr callee falls through
      // to the inttoptr tail below.)
      BType cfty = c->calleeFnType;
      if (cfty.tag == BType::Tag::struct_ &&
          cfty.structName.rfind("__oxclosure_", 0) == 0) {
        StructDef* d = findStruct(cfty.structName);
        if (d && !d->fields.empty()) {
          auto [sv, svt] = genExpr(c->calleeExpr.get());
          std::string a = freshLocal("clostmp");
          out_ << "  " << a << " = alloca " << typeStr(cfty) << "\n";
          out_ << "  store " << typeStr(cfty) << " " << sv << ", "
               << typeStr(cfty) << "* " << a << "\n";
          BType fnTy = d->fields[0].type;
          const auto& fps = fnParams(fnTy);
          size_t ncols = d->fields.size() - 1;
          BType retT = fnRet(fnTy);
          std::string retIr = (retT == BType::void_) ? "void" : typeStr(retT);
          // field 0: fn ptr; load + bitcast to concrete `ret (params)*`.
          std::string ffp = freshLocal("cfp");
          out_ << "  " << ffp << " = getelementptr inbounds " << typeStr(cfty) << ", "
               << typeStr(cfty) << "* " << a << ", i64 0, i32 0\n";
          std::string fnt = freshLocal("cfn");
          out_ << "  " << fnt << " = load " << typeStr(fnTy) << ", " << typeStr(fnTy)
               << "* " << ffp << "\n";
          std::ostringstream fnty;
          fnty << retIr << " (";
          for (size_t i = 0; i < fps.size(); i++) { if (i) fnty << ", "; fnty << typeStr(fps[i]); }
          fnty << ")*";
          std::string fcast = freshLocal("cfcall");
          out_ << "  " << fcast << " = bitcast " << typeStr(fnTy) << " " << fnt
               << " to " << fnty.str() << "\n";
          fnt = fcast;
          // args: capture fields (1..ncols) inline, then the call's own args.
          std::ostringstream args;
          size_t idx = 0;
          for (size_t ci = 1; ci < d->fields.size(); ci++) {
            BType ft = d->fields[ci].type;
            std::string cp = freshLocal("cc");
            out_ << "  " << cp << " = getelementptr inbounds " << typeStr(cfty) << ", "
                 << typeStr(cfty) << "* " << a << ", i64 0, i32 " << ci << "\n";
            std::string cv = freshLocal("cl");
            out_ << "  " << cv << " = load " << typeStr(ft) << ", " << typeStr(ft)
                 << "* " << cp << "\n";
            if (idx) args << ", ";
            args << typeStr(ft) << " " << cv; idx++;
          }
          for (size_t k = 0; k < c->args.size(); k++) {
            BType pt = (k + ncols < fps.size()) ? fps[k + ncols] : BType::i64;
            std::string cv;
            if (pt.tag == BType::Tag::ptr && isLvalueExpr(c->args[k].get())) {
              auto [addr, bt] = genAddr(c->args[k].get());
              bool decay = (pointee(pt) == BType::u8 &&
                            (bt.tag == BType::Tag::struct_ ||
                             bt.tag == BType::Tag::array ||
                             bt.tag == BType::Tag::ptr));
              if (!addr.empty() && (argPtrAccepts(bt, pt) || decay)) {
                cv = genCoerce(addr, bt, pt);
                if (idx) args << ", ";
                args << typeStr(pt) << " " << cv; idx++;
                continue;
              }
            }
            auto [v2, vt2] = genExpr(c->args[k].get());
            cv = genCoerce(v2, vt2, pt);
            if (idx) args << ", ";
            args << typeStr(pt) << " " << cv; idx++;
          }
          if (retT == BType::void_) {
            out_ << "  call void " << fnt << "(" << args.str() << ")\n";
            return {"", BType::void_};
          }
          std::string r = freshLocal("ci");
          out_ << "  " << r << " = call " << retIr << " " << fnt
               << "(" << args.str() << ")\n";
          return {r, retT};
        }
      }

      auto it = sema_.funcs.end();
      BType fnType = c->calleeFnType;


      if (fnType == BType::void_) {
        if (auto v = dynamic_cast<VarRef*>(c->calleeExpr.get())) {
          BType vt = BType::void_;

          for (auto rit = scopes_.rbegin(); rit != scopes_.rend(); ++rit) {
            auto f = rit->find(v->name);
            if (f != rit->end()) { vt = f->second.second; break; }
          }
          if (vt.tag != BType::Tag::fn_ && sema_.globals.count(v->name))
            vt = sema_.globals.at(v->name).type;
          if (vt.tag == BType::Tag::fn_) fnType = vt;
        }
      }
      BType retT = fnRet(fnType);
      BType retEff = (retT == BType::void_) ? BType::i64 : retT;
      std::string retIr = (retT == BType::void_) ? "void" : typeStr(retT);

      auto [fv, fvt] = genExpr(c->calleeExpr.get());
      std::string ptr = fv;
      if (fvt.tag != BType::Tag::ptr && fvt.tag != BType::Tag::fn_) {

        std::string p = freshLocal("fnp");
        out_ << "  " << p << " = inttoptr i64 " << fv << " to i8*\n";
        ptr = p;
      }

      const auto& ps = fnParams(fnType);
      std::ostringstream fnty;
      fnty << retIr << " (";
      for (size_t i = 0; i < ps.size(); i++) { if (i) fnty << ", "; fnty << typeStr(ps[i]); }
      fnty << ")*";

      std::string cp = freshLocal("fntype");
      out_ << "  " << cp << " = bitcast i8* " << ptr << " to " << fnty.str() << "\n";

      std::ostringstream args;
      for (size_t k = 0; k < c->args.size() && k < ps.size(); k++) {
        BType pt = ps[k];
        if (pt.tag == BType::Tag::ptr && isLvalueExpr(c->args[k].get())) {
          auto [addr, bt] = genAddr(c->args[k].get());
          // exact pointee auto-addressof, OR void* decay: a `&u8` param accepts
          // any addressable value (struct/array/scALAR) via a bitcast (genCoerce).
          bool decay = (pointee(pt) == BType::u8 &&
                        (bt.tag == BType::Tag::struct_ ||
                         bt.tag == BType::Tag::array ||
                         bt.tag == BType::Tag::ptr));
          if (!addr.empty() && (argPtrAccepts(bt, pt) || decay)) {
            std::string cv = genCoerce(addr, bt, pt);
            if (k) args << ", ";
            args << typeStr(pt) << " " << cv;
            continue;
          }
        }
        auto [v, vt] = genExpr(c->args[k].get());
        std::string cv = genCoerce(v, vt, pt);
        if (k) args << ", ";
        args << typeStr(pt) << " " << cv;
      }
      if (retT == BType::void_) {
        out_ << "  call void " << cp << "(" << args.str() << ")\n";
        return {"", BType::void_};
      }
      std::string r = tmp();
      out_ << "  " << r << " = call " << retIr << " " << cp
           << "(" << args.str() << ")\n";
      return {r, retT};
    }
    if (c->isPrint) {
      std::string sp = strConst(" ");
      for (size_t i = 0; i < c->args.size(); i++) {
        if (i) out_ << "  call i32 @ox_puts(i8* " << sp << ")\n";
        auto [v, vt] = genExpr(c->args[i].get());
        printValue(v, vt);
      }
      out_ << "  call i32 @ox_newline()\n";
      return {"", BType::void_};
    }

    if (!c->isPrint && !sema_.funcs.count(c->callee)) {
      auto r = lowerBuiltin(c);
      if (r.first != "|||no|||") return {r.first, r.second};
    }
    auto it = sema_.funcs.find(c->callee);
    if (it == sema_.funcs.end()) return {"", BType::void_};
    std::ostringstream args;
    for (size_t k = 0; k < c->args.size() && k < it->second.paramTypes.size(); k++) {
      BType pt = it->second.paramTypes[k];
      // A move-only struct passed by value is consumed: the callee owns it now.
      // Mark the arg's root local moved-out so the caller doesn't also drop it.
      maybeMarkArgMoved(c->args[k].get(), pt);

      if (pt.tag == BType::Tag::ptr && isLvalueExpr(c->args[k].get())) {

        auto [addr, bt] = genAddr(c->args[k].get());
        // exact pointee auto-addressof, OR void* decay (:genCoerce bitcasts).
        bool decay = (pointee(pt) == BType::u8 &&
                      (bt.tag == BType::Tag::struct_ ||
                       bt.tag == BType::Tag::array ||
                       bt.tag == BType::Tag::ptr));
        if (!addr.empty() && (argPtrAccepts(bt, pt) || decay)) {
          std::string cv = genCoerce(addr, bt, pt);
          if (k) args << ", ";
          args << typeStr(pt) << " " << cv;
          continue;
        }
      }
      auto [v, vt] = genExpr(c->args[k].get());
      std::string cv = genCoerce(v, vt, pt);
      if (k) args << ", ";
      args << typeStr(pt) << " " << cv;
    }
    if (it->second.retType == BType::void_) {
      out_ << "  call void @" << c->callee << "(" << args.str() << ")\n";
      return {"", BType::void_};
    }
    std::string r = tmp();
    out_ << "  " << r << " = call " << typeStr(it->second.retType) << " @" << c->callee
         << "(" << args.str() << ")\n";
    return {r, it->second.retType};
  }
  // Compile-time macro invocation (`expand name(args...)`). Sema has already
  // expanded the macro and stashed the substituted, type-checked body tree on
  // `mc->expanded`; the normal path is to codegen that tree directly (it is the
  // very tree Sema verified, so the IR matches the types). If, exceptionally,
  // `expanded` is null, we fall back to the spec-mandated IRGen self-contained
  // look-up + substitute + codegen path: consult sema_'s macroRegistry, clone
  // the body, substitute the $param markers for the args, and codegen that.
  // The fallback is parity with Sema::expandMacro; the primary path is the
  // Sema-verified tree. `resultTy` carries the expanded type for the caller.
  if (auto mc = dynamic_cast<MacroCall*>(e)) {
    if (mc->expanded) {
      // Emit any expanded `let` statements (from the macro's bodyStmts) so
      // the bound locals get their allocas/stores before we evaluate the
      // trailing result expression.
      for (auto& s : mc->expandedStmts) genStmt(s.get());
      return genExpr(mc->expanded.get());
    }
    const MacroDecl* m = sema_.findMacro(mc->macroName);
    if (!m || !m->bodyExpr) return {"", BType::void_};
    if (mc->args.size() != m->paramNames.size()) return {"", BType::void_};
    ExprPtr body = cloneExpr(m->bodyExpr.get());
    if (!body) return {"", BType::void_};
    // Substitute $param markers with the caller args (clones). Mirrors
    // Sema::substMacroParams; duplicated here so IRGen's expand path is
    // self-contained per the spec even when Sema's stash is missing.
    std::function<void(ExprPtr&)> walk = [&](ExprPtr& slot) {
      if (!slot) return;
      if (auto v = dynamic_cast<VarRef*>(slot.get())) {
        for (size_t k = 0; k < m->paramNames.size(); k++) {
          if (v->name == "$" + m->paramNames[k]) {
            slot = cloneExpr(mc->args[k].get());
            if (slot) { slot->line = v->line; slot->col = v->col; }
            return;
          }
        }
        return;
      }
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
      if (auto m2 = dynamic_cast<MethodCall*>(slot.get())) {
        walk(m2->receiver);
        for (auto& a : m2->args) walk(a);
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
        for (auto& a : mc2->args) walk(a);
        return;
      }
    };
    walk(body);
    return genExpr(body.get());
  }
  if (auto mc = dynamic_cast<MethodCall*>(e)) {
    return emitMethodCall(mc->recvType.structName, mc->callee, mc->recvType,
                          mc->receiverByRef, mc->receiver.get(), mc->args);
  }
  if (auto ac = dynamic_cast<AssocCall*>(e)) {

    std::string sn = ac->typeName;
    if (isAliasName(sn)) sn = resolveAlias(sn).second.structName;
    auto sit = sema_.methods.find(sn);
    if (sit == sema_.methods.end() || !sit->second.count(ac->callee))
      return {"0", BType::void_};
    const MethodInfo& mi = sit->second.at(ac->callee);
    std::ostringstream args;
    bool first = true;
    for (size_t k = 0; k < ac->args.size() && k < mi.paramTypes.size(); k++) {
      BType pt = mi.paramTypes[k];
      std::string argStr;
      if (pt.tag == BType::Tag::ptr && isLvalueExpr(ac->args[k].get())) {
        auto [addr, bt] = genAddr(ac->args[k].get());
        bool decay = (pointee(pt) == BType::u8 &&
                      (bt.tag == BType::Tag::struct_ ||
                       bt.tag == BType::Tag::array ||
                       bt.tag == BType::Tag::ptr));
        if (!addr.empty() && (argPtrAccepts(bt, pt) || decay)) argStr = genCoerce(addr, bt, pt);
        else { auto [v, vt] = genExpr(ac->args[k].get()); argStr = genCoerce(v, vt, pt); }
      } else {
        auto [v, vt] = genExpr(ac->args[k].get());
        argStr = genCoerce(v, vt, pt);
      }
      if (!first) args << ", ";
      first = false;
      args << typeStr(pt) << " " << argStr;
    }
    if (mi.retType == BType::void_) {
      out_ << "  call void @" << mi.mangled << "(" << args.str() << ")\n";
      return {"", BType::void_};
    }
    std::string r = tmp();
    out_ << "  " << r << " = call " << typeStr(mi.retType) << " @" << mi.mangled
         << "(" << args.str() << ")\n";
    return {r, mi.retType};
  }
  if (auto lam = dynamic_cast<LambdaLit*>(e)) {
    genLambda(lam);

    if (lam->closureStructName.empty()) {
      // Non-capturing: the value is a plain function pointer, lowered to the
      // fn value representation (i8*) via a bitcast of the defined symbol. An
      // indirect call `f(args)` then bitcasts this i8* back to the concrete
      // `ret (params)*` at the call site, exactly like C++'s bare-fn-ptr
      // conversion of a non-capturing lambda. [a] copies; [&a] takes a's
      // address; both flow through the closure-struct branch below.
      std::string p = freshLocal("lamfp");
      out_ << "  " << p << " = bitcast " << typeStr(fnRet(lam->fnType)) << " (";
      const auto& ps = fnParams(lam->fnType);
      for (size_t i = 0; i < ps.size(); i++) { if (i) out_ << ", "; out_ << typeStr(ps[i]); }
      out_ << ")* @" << lam->loweredName << " to i8*\n";
      return {p, lam->fnType};
    }

    // Capturing: build a closure struct { fn: <FnType>, cap0: T0, ... }.
    BType ct; ct.tag = BType::Tag::struct_; ct.structName = lam->closureStructName;
    StructDef* d = findStruct(lam->closureStructName);
    // Ensure the closure struct (and every type it transitively references,
    // including pointer element types of by-ref captures) gets a `%struct.NAME
    // = type {...}` body emitted  -  otherwise the alloca below hits an opaque
    // (unsized) type and clang rejects it.
    collectStruct(ct);
    if (d) { for (auto& f : d->fields) collectStruct(f.type); }
    std::string a = freshLocal("clos");
    out_ << "  " << a << " = alloca " << typeStr(ct) << "\n";
    if (d) {
      // field 0: the function pointer.
      std::string fp = freshLocal("cfn");
      out_ << "  " << fp << " = getelementptr inbounds " << typeStr(ct) << ", "
           << typeStr(ct) << "* " << a << ", i64 0, i32 0\n";
      out_ << "  store " << typeStr(lam->fnType) << " @" << lam->loweredName
           << ", " << typeStr(lam->fnType) << "* " << fp << "\n";
      // capture fields: by-ref passes the local's address; by-value the value.
      for (size_t i = 0; i < lam->captures.size() && d && i + 1 < d->fields.size(); i++) {
        const auto& cap = lam->captures[i];
        BType ft = d->fields[i + 1].type;   // +1 for the `fn` field at index 0
        std::string cv;
        if (cap.byRef) {
          // &local: genAddr returns {addressStorage, pointeeType T}. The closure
          // field is *T (ptr), so the storage (LLVM-typed typeStr(T)*) already has
          // exactly the field's IR type  -  pass it through unchanged. (genCoerce
          // would wrongly inttoptr a pointee-type-typed value to a pointer.)
          auto vref = std::make_unique<VarRef>();
          vref->name = cap.name; vref->line = lam->line; vref->col = lam->col;
          auto [addr, at] = genAddr(vref.get());
          cv = addr;
          (void)at;
        } else {
          auto vref = std::make_unique<VarRef>();
          vref->name = cap.name; vref->line = lam->line; vref->col = lam->col;
          auto [vval, vt] = genExpr(vref.get());
          cv = genCoerce(vval, vt, ft);
        }
        std::string gp = freshLocal("ccap");
        out_ << "  " << gp << " = getelementptr inbounds " << typeStr(ct) << ", "
             << typeStr(ct) << "* " << a << ", i64 0, i32 " << (i + 1) << "\n";
        out_ << "  store " << typeStr(ft) << " " << cv << ", " << typeStr(ft)
             << "* " << gp << "\n";
      }
    }
    std::string r = freshLocal("closval");
    out_ << "  " << r << " = load " << typeStr(ct) << ", " << typeStr(ct) << "* "
         << a << "\n";
    return {r, ct};
  }
  // A RangeLit reaching genExpr means it wasn't consumed as a `for x in <range>`
  // iterable  -  Sema rejects that earlier, so this is unreachable in valid code.
  // Emit a benign i64 zero rather than silently producing garbage, so an
  // (invalid) program still lowers instead of crashing IRGen.
  if (auto rng = dynamic_cast<RangeLit*>(e)) {
    (void)rng;
    return {"0", BType::i64};
  }
  // --- Contract spec expressions ---
  // `old(x)`: a pre-state reference. Only meaningful inside an ensures gate,
  // where the enclosing snapshot machinery made an entry-time copy of x into
  // oldSnapshots_. Outside ensures, Sema already rejected it; if it slips
  // through we lower the sub-expression directly (no snapshot).
  if (auto o = dynamic_cast<OldExpr*>(e)) {
    // In an ensures gate, `old(<bareName>)` reads the entry snapshot taken for
    // that name. We snapshot only whole-variable old(bareName); a field/index
    // old(x.f) falls back to reading the live value (documented limitation).
    if (inEnsuresGate_) {
      if (auto vr = dynamic_cast<VarRef*>(o->sub.get())) {
        auto it = oldSnapshots_.find(vr->name);
        if (it != oldSnapshots_.end()) {
          // Read the entry-time snapshot. Use the snapshot's own recorded type
          // rather than findVar(): the exit-block ensures gate may run after
          // the param/local scopes are popped, so the live-var lookup could
          // return {empty, void_} and load `void` from an i64 slot.
          const std::string& slot = it->second.first;
          BType st = it->second.second;
          std::string r = tmp();
          out_ << "  " << r << " = load " << typeStr(st) << ", " << typeStr(st)
               << "* " << slot << "\n";
          return {r, st};
        }
      }
    }
    return genExpr(o->sub.get());
  }
  // ox:proof `forall/exists i in a..b implies P`: lower to a runtime loop that computes
  // the boolean (all-hold for forall, any-hold for exists). Reuses the quantifier
  // gate helper, which builds the loop and returns an i1 result; we widen to i64
  // so it composes with the rest of genExpr's i64-bool convention.
  if (auto q = dynamic_cast<QuantExpr*>(e)) {
    std::string res = genQuantifierGate(q, CT_ASSERT, e->line);
    if (res.empty()) return {"0", BType::bool_};
    return {res, BType::bool_};
  }

  // --- Advanced math operators (power / matrix / matmul / solve / integrate) ---
  // Each lowers to a call into a runtime C helper (see Driver.cpp runtimeC()).
  // The corresponding `declare` is only emitted when the matching usedX_ flag is
  // set (in emitHeaderAndRuntime), so an untouched operator adds nothing to the
  // IR / link line. Sema gives every operator a numeric result type stored on
  // the AST node; we honour it so printValue coerces correctly.

  // `base ** exponent` / `base ^ exponent` / `x²`.
  if (auto pe = dynamic_cast<PowerExpr*>(e)) {
    auto [bv, bt] = genExpr(pe->base.get());
    BType rt = pe->resultType;
    // Postfix ² (square): exponent is null and the runtime square helper
    // multiplies the value by itself. Coerce once to the result type so i64²
    // stays integral and f64² stays floating.
    if (!pe->exponent) {
      usedSquare_ = true;
      std::string cv = (rt == BType::i64) ? genCoerce(bv, bt, BType::i64)
                                         : genCoerce(bv, bt, BType::f64);
      std::string r = freshLocal("sq");
      if (rt == BType::i64)
        out_ << "  " << r << " = call i64 @ox_square_i64(i64 " << cv << ")\n";
      else
        out_ << "  " << r << " = call double @ox_square_f64(double " << cv << ")\n";
      return {r, rt};
    }
    auto [ev, et] = genExpr(pe->exponent.get());
    if (rt == BType::i64) {
      // Integer power: exponent ≥ 0, base and exponent both i64, use the
      // iterative ipow runtime (avoids libc float power on an exact-integer op).
      usedPowI_ = true;
      std::string bb = genCoerce(bv, bt, BType::i64);
      std::string ee = genCoerce(ev, et, BType::i64);
      std::string r = freshLocal("ipow");
      out_ << "  " << r << " = call i64 @ox_ipow(i64 " << bb << ", i64 " << ee << ")\n";
      return {r, BType::i64};
    }
    // Floating power: coerce both operands to f64 and call libc pow() (via the
    // runtime wrapper so the declare dedups cleanly).
    usedPowF_ = true;
    std::string bb = genCoerce(bv, bt, BType::f64);
    std::string ee = genCoerce(ev, et, BType::f64);
    std::string r = freshLocal("fpow");
    out_ << "  " << r << " = call double @ox_pow_f64(double " << bb << ", double " << ee << ")\n";
    return {r, BType::f64};
  }

  // Matrix literal `[ [..], [..] ]`. Spill every element to the contiguous
  // row-major buffer returned by @ox_mat_new(rows, cols) and return an opaque
  // i8* handle. The element type is f64 for the runtime (we widen i64 elements
  // at the store; if Sema proved all elements were integer literals it would
  // have set elemType=i64  -  we still store as f64 for a uniform backing store,
  // since matmul/solve/printf all operate on doubles). The handle is pointer-
  // width (i8*) so callers can carry it through a `let m = [[..]]` binding.
  if (auto ml = dynamic_cast<MatrixLit*>(e)) {
    usedMat_ = true;
    size_t rows = ml->rows.size();
    size_t cols = rows ? ml->rows[0].size() : 0;
    // Allocate the row-major buffer.
    std::string h = freshLocal("mat");
    out_ << "  " << h << " = call i8* @ox_mat_new(i64 " << (long long)rows
         << ", i64 " << (long long)cols << ")\n";
    // Fill it element-by-element, widening to f64 for the backing store.
    for (size_t r = 0; r < rows; r++) {
      for (size_t c = 0; c < ml->rows[r].size() && c < cols; c++) {
        auto [v, vt] = genExpr(ml->rows[r][c].get());
        std::string cv = genCoerce(v, vt, BType::f64);
        out_ << "  call void @ox_mat_set(i8* " << h << ", i64 " << (long long)r
             << ", i64 " << (long long)c << ", double " << cv << ")\n";
      }
    }
    // The matrix value is the i8* handle; the element type is carried by Sema
    // (ml->elemType) only for downstream type-checking, the runtime is uniform.
    // We surface the value as an opaque i8* (i8 pointee) so it composes with
    // `let m = [[..]]`, with printValue, and with matrix-mul/solve callees.
    return {h, makePtr(BType::i8)};
  }

  // `A * B` matrix product. Both operands are matrix handles (Sema enforces
  // conformability); the result is a fresh matrix handle.
  if (auto mm = dynamic_cast<MatMulExpr*>(e)) {
    usedMatMul_ = true;
    auto [lv, lt] = genExpr(mm->lhs.get());
    auto [rv, rt] = genExpr(mm->rhs.get());
    (void)lt; (void)rt;   // operands are opaque i8* handles
    std::string r = freshLocal("mm");
    out_ << "  " << r << " = call i8* @ox_mat_mul(i8* " << lv << ", i8* " << rv << ")\n";
    return {r, makePtr(BType::i8)};
  }

  // `A \ b`  -  linear solve. lhs is the matrix A, rhs is the vector b. The
  // runtime returns a freshly-allocated solution vector (a one-column matrix
  // handle) carrying b's dimensions.
  if (auto se = dynamic_cast<SolveExpr*>(e)) {
    usedSolve_ = true;
    auto [lv, lt] = genExpr(se->lhs.get());
    auto [rv, rt] = genExpr(se->rhs.get());
    (void)lt; (void)rt;   // both operands are opaque i8* handles
    std::string r = freshLocal("solve");
    out_ << "  " << r << " = call i8* @ox_mat_solve(i8* " << lv << ", i8* " << rv << ")\n";
    return {r, makePtr(BType::i8)};
  }

  // Numeric integration: `integrate f from lo..hi with N`. The body is either
  // a Call to a 1-arg f64->f64 function, in which case we resolve its address
  // to an i8* (bitcast to `double (double)*` inside the runtime), or a bare
  // VarRef naming such a function. We fall back to a trapezoidal-rule runtime
  // call @ox_integrate_trapz(fnptr, lo, hi, N). Bounds and N are evaluated
  // inline and coerced to f64 / i64 respectively.
  if (auto ie = dynamic_cast<IntegrateExpr*>(e)) {
    usedIntegrate_ = true;
    auto [loV, loT] = genExpr(ie->lo.get());
    auto [hiV, hiT] = genExpr(ie->hi.get());
    std::string lo = genCoerce(loV, loT, BType::f64);
    std::string hi = genCoerce(hiV, hiT, BType::f64);
    // Resolve the integrand to a bare i8* function pointer. If `body` is a Call
    // to a named function, the callee address is `@name`; if it's a bare
    // VarRef, resolve through Sema's function table exactly like the VarRef
    // path in genExpr; otherwise (a lambda or fn-ptr value) codegen the value
    // and bitcast to i8*.
    std::string fnPtrI8;
    if (auto c = dynamic_cast<Call*>(ie->body.get())) {
      // The named function's address is @name; bitcast to i8* for the runtime.
      std::string sym = "@" + c->callee;
      std::string fp = freshLocal("intfn");
      out_ << "  " << fp << " = bitcast double (double)* " << sym << " to i8*\n";
      fnPtrI8 = fp;
    } else if (auto v = dynamic_cast<VarRef*>(ie->body.get())) {
      auto fit = sema_.funcs.find(v->name);
      if (fit != sema_.funcs.end()) {
        std::string fp = freshLocal("intfn");
        out_ << "  " << fp << " = bitcast double (double)* @" << v->name
             << " to i8*\n";
        fnPtrI8 = fp;
      } else {
        // ox:why Otherwise treat the VarRef as a value (handle/int) and bitcast.
        auto [fv, fvt] = genExpr(v);
        std::string fp = freshLocal("intfn");
        if (fvt.tag == BType::Tag::ptr || fvt.tag == BType::Tag::fn_) {
          out_ << "  " << fp << " = bitcast " << typeStr(fvt) << " " << fv
               << " to i8*\n";
        } else {
          std::string p = freshLocal("intfnp");
          out_ << "  " << p << " = inttoptr i64 " << fv << " to i8*\n";
          fp = p;
        }
        fnPtrI8 = fp;
      }
    } else if (ie->body) {
      auto [fv, fvt] = genExpr(ie->body.get());
      std::string fp = freshLocal("intfn");
      if (fvt.tag == BType::Tag::ptr || fvt.tag == BType::Tag::fn_) {
        out_ << "  " << fp << " = bitcast " << typeStr(fvt) << " " << fv
             << " to i8*\n";
      } else {
        std::string p = freshLocal("intfnp");
        out_ << "  " << p << " = inttoptr i64 " << fv << " to i8*\n";
        fp = p;
      }
      fnPtrI8 = fp;
    } else {
      // No integrand  -  emit a no-op 0.0 via the runtime (sema would reject first).
      std::string fp = freshLocal("intfn");
      out_ << "  " << fp << " = inttoptr i64 0 to i8*\n";
      fnPtrI8 = fp;
    }
    std::string r = freshLocal("integ");
    out_ << "  " << r << " = call double @ox_integrate_trapz(i8* " << fnPtrI8
         << ", double " << lo << ", double " << hi << ", i64 " << ie->samples << ")\n";
    return {r, BType::f64};
  }

  // MathSymExpr: a known Unicode math constant.  Sema (checkExpr) has already
  // validated `ms->text` against a whitelist and emitted a compile error for
  // unknown symbols  -  by the time IRGen runs, only `pi`/`π` should be possible.
  // We route those to `ox_pi()`.  We deliberately do NOT silently map any other
  // glyph to `ox_e()` anymore: that is the bug where τ/Σ/∞ would have silently
  // evaluated to Euler's number.  `e`/`ℯ` are not currently produced as
  // MathSymExpr nodes by the lexer/parser (there is no `kw_e`; `e` lexes as an
  // `ident`), so we defensively fall back to a plain 0.0 constant rather than
  // `ox_e()` to avoid reintroducing the silent-fallback trap. `ox_pi()` is
  // declared in the C preamble (Driver.cpp) and picked up via `usedExt_`.
  if (auto ms = dynamic_cast<MathSymExpr*>(e)) {
    std::string fn;
    if (ms->text == "pi" || ms->text == "\u03c0") {
      fn = "ox_pi";
    } else {
      // Should be unreachable after Sema's whitelist check  -  emit a literal
      // 0.0 rather than silently evaluating an unknown glyph to e.
      std::string r = freshLocal("mconst");
      out_ << "  " << r << " = fadd double 0.000000e+00, 0.000000e+00\n";
      return {r, BType::f64};
    }
    usedExt_.insert(fn);
    std::string r = freshLocal("mconst");
    out_ << "  " << r << " = call double @" << fn << "()\n";
    return {r, BType::f64};
  }
  // SuperscriptExpr: xⁿ with a general (multi-code-point) superscript run.
  // The Parser creates SuperscriptExpr when the superscript is too complex
  // for the ²/³ fast-path (e.g. x⁻¹, xⁿ).  Lowering mirrors PowerExpr:
  // call ox_ipow for integers, ox_pow_f64 for floats.
  if (auto ss = dynamic_cast<SuperscriptExpr*>(e)) {
    auto [bv, bt] = genExpr(ss->base.get());
    auto [ev, et] = genExpr(ss->exponent.get());
    if (ss->resultType == BType::f64 || bt == BType::f64 ||
        bt == BType::f32) {
      usedPowF_ = true;
      std::string b = genCoerce(bv, bt, BType::f64);
      std::string x = genCoerce(ev, et, BType::f64);
      std::string r = freshLocal("pow");
      out_ << "  " << r << " = call double @ox_pow_f64(double " << b
           << ", double " << x << ")\n";
      return {r, BType::f64};
    } else {
      usedPowI_ = true;
      std::string b = genCoerce(bv, bt, BType::i64);
      std::string x = genCoerce(ev, et, BType::i64);
      std::string r = freshLocal("pow");
      out_ << "  " << r << " = call i64 @ox_ipow(i64 " << b
           << ", i64 " << x << ")\n";
      return {r, BType::i64};
    }
  }

  return {"0", BType::i64};
}


std::pair<std::string, BType> IRGen::emitMethodCall(const std::string& structName,
                                                    const std::string& methodName,
                                                    const BType& recvType, bool recvByRef,
                                                    Expr* receiver,
                                                    const std::vector<ExprPtr>& args) {
  // Resolve through Sema's base-chain helper: a method not on the static
  // struct type may be inherited from a base. `mi.implStruct` is the struct
  // that actually defines the method; the receiver must be lowered as that
  // struct's type (%struct.ImplBase*), which (single inheritance, base-first
  // layout) is a bitcast of the derived object's storage pointer.
  const MethodInfo* mip = sema_.resolveMethod(structName, methodName);
  if (!mip) return {"0", BType::void_};
  const MethodInfo& mi = *mip;
  BType st; st.tag = BType::Tag::struct_; st.structName = structName;
  collectStruct(st);
  // The method's "self" struct  -  the impl that defines it (could be a base).
  BType selfT; selfT.tag = BType::Tag::struct_; selfT.structName = mi.implStruct;
  if (!mi.implStruct.empty() && mi.implStruct != structName) collectStruct(selfT);

  std::ostringstream argStr;
  bool first = true;

  std::string recvVal;
  std::string recvAddr;   // static-type (%struct.<structName>*) view of &self, set
                          // in the recvByRef branch; used by virtual dispatch below.
  if (recvByRef) {

    auto [addr, bt] = genAddr(receiver);
    if (!addr.empty() && bt.tag == BType::Tag::struct_ && bt.structName == structName) {
      recvVal = addr;
    } else if (!addr.empty() && bt.tag == BType::Tag::ptr &&
               pointee(bt).tag == BType::Tag::struct_ &&
               pointee(bt).structName == structName) {
      // The receiver is a *local pointer variable* (`let p = &obj; p.m()`):
      // `addr` is the address of the pointer SLOT (%struct.T**), not the pointed-
      // to object. Load the pointer first  -  the loaded %struct.T* is &self.
      std::string ld = freshLocal("mrecvld");
      out_ << "  " << ld << " = load " << typeStr(bt) << ", " << typeStr(bt) << "* "
           << addr << "\n";
      recvVal = ld;
    } else {


      auto [v, vt2] = genExpr(receiver);
      std::string a = freshLocal("mrecv");
      out_ << "  " << a << " = alloca " << typeStr(st) << "\n";
      std::string av = genCoerce(v, vt2, st);
      out_ << "  store " << typeStr(st) << " " << av << ", " << typeStr(st) << "* " << a << "\n";
      recvVal = a;
    }
    // For an inherited method, the self param is &Base  -  bitcast the derived
    // storage pointer to %struct.Base* (single inheritance: base at offset 0,
    // so it's a plain bitcast, which genCoerce on pointers already performs).
    // Capture the STATIC-type view (%struct.<structName>*) before the bitcast:
    // a virtual dispatch loads the vtable ptr from field index 0 of the STATIC
    // type (whose layout starts with the inherited __oxvt slot), so it needs
    // the static-type address, not the implStruct-bitcast self-arg address.
    recvAddr = recvVal;   // %struct.<structName>* (static view)
    if (!mi.implStruct.empty() && mi.implStruct != structName) {
      BType ptrT = makePtr(selfT);
      recvVal = genCoerce(recvVal, makePtr(st), ptrT);
    }
    BType selfPtrT = makePtr(st);
    if (!mi.implStruct.empty() && mi.implStruct != structName) selfPtrT = makePtr(selfT);
    argStr << typeStr(selfPtrT) << " " << recvVal;
    first = false;
  } else {

    // A by-value `self` receiver MOVES the receiver when the struct has a
    // destructor (no implicit copy for hasDrop types  -  see isMoveOnlyStruct).
    // The callee now owns that value and will drop its `self` slot at its own
    // scope exit; mark the caller's root local moved-out so the caller does not
    // ALSO drop it (no double-free). A plain (no-Drop) struct is a cheap copy.
    if (isMoveOnlyStruct(structName))
      markMovedOut(genMoveRootVar(receiver));
    auto [val, vt] = genExpr(receiver);
    // For an inherited method, the by-value self is the Base sub-object (slice
    // at offset 0). Load it from a %struct.Base* view of the derived object's
    // storage (bitcast, single inheritance: base at offset 0).
    if (!mi.implStruct.empty() && mi.implStruct != structName) {
      auto [src, sbt] = genAddr(receiver);
      if (!src.empty()) {
        // genAddr reports sbt as the pointee struct type; the IR pointer type
        // is makePtr(sbt). Bitcast to %struct.Base* and load the base sub-object.
        BType srcPtrT = (sbt.tag == BType::Tag::ptr) ? sbt : makePtr(sbt);
        std::string bcp = genCoerce(src, srcPtrT, makePtr(selfT));
        std::string ld = freshLocal("mrecvld");
        out_ << "  " << ld << " = load " << typeStr(selfT) << ", "
             << typeStr(selfT) << "* " << bcp << "\n";
        recvVal = ld;
      } else {
        recvVal = genCoerce(val, vt, selfT);
      }
      argStr << typeStr(selfT) << " " << recvVal;
    } else {
      recvVal = genCoerce(val, vt, st);
      argStr << typeStr(st) << " " << recvVal;
    }
    first = false;
  }


  for (size_t k = 0; k < args.size() && k < mi.paramTypes.size(); k++) {
    BType pt = mi.paramTypes[k];
    std::string s;
    if (pt.tag == BType::Tag::ptr && isLvalueExpr(args[k].get())) {
      auto [addr, bt] = genAddr(args[k].get());
      bool decay = (pointee(pt) == BType::u8 &&
                    (bt.tag == BType::Tag::struct_ ||
                     bt.tag == BType::Tag::array ||
                     bt.tag == BType::Tag::ptr));
      if (!addr.empty() && (argPtrAccepts(bt, pt) || decay)) s = genCoerce(addr, bt, pt);
      else { auto [v, vt] = genExpr(args[k].get()); s = genCoerce(v, vt, pt); }
    } else {
      auto [v, vt] = genExpr(args[k].get());
      s = genCoerce(v, vt, pt);
    }
    if (!first) argStr << ", ";
    first = false;
    argStr << typeStr(pt) << " " << s;
  }

  // Virtual dispatch: an `isVirtual` method (declared `virtual fn` or
  // `override fn`) is called indirectly through the per-object vtable rather
  // than the direct @mangled symbol, so a base-ref holding a derived instance
  // resolves to the derived override. recvByRef is guaranteed true for all
  // virtuals (Sema rejects by-value-self virtuals). Steps: load the __oxvt
  // vtable-ptr from field index 0 of the STATIC-type receiver view; treat it
  // as [N x i8*]* (N = the static struct's vtableSlots size  -  the slot layout
  // is identical up the chain, so the static type's slot count matches the
  // instance's vtable); GEP to mi.vtableSlot; load the i8* fn ptr; bitcast it
  // to the concrete fn type (ret + self-implStruct-ptr + params) and call.
  if (mi.isVirtual && mi.vtableSlot >= 0 && recvByRef && !recvAddr.empty()) {
    StructDef* sd = findStruct(structName);
    size_t N = sd ? sd->vtableSlots.size() : 0;
    // Load the __oxvt slot (field index 0) of the static-type receiver.
    std::string vtp = freshLocal("vtgep");
    out_ << "  " << vtp << " = getelementptr inbounds " << typeStr(st) << ", "
         << typeStr(st) << "* " << recvAddr << ", i64 0, i32 0\n";
    std::string vtptr = freshLocal("vtld");
    out_ << "  " << vtptr << " = load i8*, i8** " << vtp << "\n";
    // The instance's vtable is an [N x i8*]; index into slot mi.vtableSlot.
    std::string vtarray = "vtarr";
    if (N > 0) {
      std::string vtacast = freshLocal("vtac");
      out_ << "  " << vtacast << " = bitcast i8* " << vtptr << " to ["
           << N << " x i8*]*\n";
      std::string slp = freshLocal("vtslot");
      out_ << "  " << slp << " = getelementptr inbounds [" << N
           << " x i8*], [" << N << " x i8*]* " << vtacast << ", i64 0, i64 "
           << mi.vtableSlot << "\n";
      vtarray = freshLocal("vtfnld");
      out_ << "  " << vtarray << " = load i8*, i8** " << slp << "\n";
    } else {
      vtarray = vtptr;   // degenerate [0 x ...] (should not be reached: an
                         // empty-vtable struct has no dispatchable virtual).
    }
    // Concrete fn type: ret (selfPtrT, <params>)*  -  matches genMethod's
    // define emission (the self param is %struct.<implStruct>* for &self).
    BType callSelfT; callSelfT.tag = BType::Tag::struct_;
    callSelfT.structName = mi.implStruct.empty() ? structName : mi.implStruct;
    BType callSelfPtrT = makePtr(callSelfT);
    std::string fnTy = typeStr(mi.retType) + " (" + typeStr(callSelfPtrT);
    for (size_t k = 0; k < mi.paramTypes.size(); k++)
      fnTy += ", " + typeStr(mi.paramTypes[k]);
    fnTy += ")*";
    std::string fncast = freshLocal("vtfncast");
    out_ << "  " << fncast << " = bitcast i8* " << vtarray << " to " << fnTy
         << "\n";
    // Indirect call through %fncast. The function-pointer type (`fnTy`) is
    // established by the bitcast above, so the call site only needs the
    // return type and the callee value  -  re-emitting `fnTy` here would
    // produce the malformed `call <retty> <fnTy> %fncast(...)` that clang
    // rejects with "expected value token" (the <retty> and <fnTy> collide).
    // argStr already carries the typed `self` argument plus any explicit params.
    if (mi.retType == BType::void_) {
      out_ << "  call void " << fncast << "(" << argStr.str() << ")\n";
      return {"", BType::void_};
    }
    std::string r = tmp();
    out_ << "  " << r << " = call " << typeStr(mi.retType) << " " << fncast
         << "(" << argStr.str() << ")\n";
    return {r, mi.retType};
  }

  if (mi.retType == BType::void_) {
    out_ << "  call void @" << mi.mangled << "(" << argStr.str() << ")\n";
    return {"", BType::void_};
  }
  std::string r = tmp();
  out_ << "  " << r << " = call " << typeStr(mi.retType) << " @" << mi.mangled
       << "(" << argStr.str() << ")\n";
  return {r, mi.retType};
}


std::pair<std::string, BType> IRGen::emitOverloadCall(const std::string& structName,
                                                      const std::string& methodName,
                                                      const BType& recvType, bool recvByRef,
                                                      Expr* receiver,
                                                      const std::vector<ExprPtr>& args,
                                                      bool negateResult) {


  bool byRef = recvByRef;
  auto sit = sema_.methods.find(structName);
  if (sit != sema_.methods.end() && sit->second.count(methodName))
    byRef = sit->second.at(methodName).selfByRef;
  auto [val, rt] = emitMethodCall(structName, methodName, recvType, byRef,
                                  receiver, args);
  if (negateResult) {

    std::string neg = freshLocal("olneg");
    out_ << "  " << neg << " = xor i1 " << val << ", true\n";
    return {neg, BType::bool_};
  }
  return {val, rt};
}

void IRGen::printValue(const std::string& val, const BType& t, const std::string& prefix) {
  (void)prefix;
  if (t == BType::void_) return;
  if (t == BType::char_) {
    std::string ext = freshLocal("cext");
    out_ << "  " << ext << " = zext i8 " << val << " to i64\n";
    out_ << "  call i32 @ox_putc(i64 " << ext << ")\n";
    return;
  }
  if (t.tag == BType::Tag::enum_) {


    EnumDef* ed = findEnum(t.structName);
    if (ed && !ed->variants.empty()) {

      std::string v = genCoerce(val, t, BType::i64);
      std::string doneBB = freshLabel("en_done");
      for (size_t i = 0; i < ed->variants.size(); i++) {
        std::string nv = freshLocal("enchk");
        out_ << "  " << nv << " = icmp eq i64 " << v << ", " << i << "\n";
        std::string yesBB = freshLabel("en_yes");
        std::string noBB = freshLabel("en_no");
        branch(nv, yesBB, noBB);
        beginBlock(yesBB);
        std::string nm = strConst(ed->variants[i]);
        out_ << "  call i32 @ox_puts(i8* " << nm << ")\n";
        jump(doneBB);
        beginBlock(noBB);
      }

      out_ << "  call i32 @ox_puti(i64 " << v << ")\n";
      jump(doneBB);
      beginBlock(doneBB);
      return;
    }

    std::string cv = genCoerce(val, t, BType::i64);
    out_ << "  call i32 @ox_puti(i64 " << cv << ")\n";
    return;
  }
  if (isInt(t)) {

    std::string cv = genCoerce(val, t, BType::i64);
    out_ << "  call i32 @ox_puti(i64 " << cv << ")\n";
    return;
  }


  if (t == BType::f64) { out_ << "  call i32 @ox_putf(double " << val << ")\n"; return; }
  if (t == BType::f32) {
    std::string dv = genCoerce(val, BType::f32, BType::f64);
    out_ << "  call i32 @ox_putf(double " << dv << ")\n";
    return;
  }
  if (t == BType::bool_) {
    std::string ext = freshLocal("bext");
    out_ << "  " << ext << " = zext i1 " << val << " to i64\n";
    out_ << "  call i32 @ox_puti(i64 " << ext << ")\n";
    return;
  }
  if (t.tag == BType::Tag::ptr && pointee(t) == BType::i8 && val != "null") {
    // Matrix handle (i8* from MatrixLit/ArrayLit2D/MatMul/Solve/LetStmt).
    // @ox_mat_print already declared at runtime decl; prints rows×cols contents.
    out_ << "  call void @ox_mat_print(i8* " << val << ")\n";
    return;
  }
  if (t.tag == BType::Tag::ptr) {

    std::string cv = genCoerce(val, t, BType::i64);
    out_ << "  call i32 @ox_puti(i64 " << cv << ")\n";
    return;
  }
  if (t == BType::str) { out_ << "  call i32 @ox_puts(i8* " << val << ")\n"; return; }
  if (t.tag == BType::Tag::array) {

    std::string a = freshLocal("pa");
    out_ << "  " << a << " = alloca " << typeStr(t) << "\n";
    out_ << "  store " << typeStr(t) << " " << val << ", " << typeStr(t) << "* " << a << "\n";
    std::string lb = strConst("[");
    out_ << "  call i32 @ox_puts(i8* " << lb << ")\n";
    const BType& et = arrayElem(t);
    for (int32_t i = 0; i < t.count; i++) {
      std::string sep;
      if (i) { sep = strConst(", "); out_ << "  call i32 @ox_puts(i8* " << sep << ")\n"; }
      std::string ep = freshLocal("pe");
      out_ << "  " << ep << " = getelementptr inbounds " << typeStr(t) << ", "
           << typeStr(t) << "* " << a << ", i64 0, i64 " << i << "\n";
      std::string ev = freshLocal("ev");
      out_ << "  " << ev << " = load " << typeStr(et) << ", " << typeStr(et) << "* " << ep << "\n";
      printValue(ev, et);
    }
    std::string rb = strConst("]");
    out_ << "  call i32 @ox_puts(i8* " << rb << ")\n";
    return;
  }
  if (t.tag == BType::Tag::struct_) {
    StructDef* d = findStruct(t.structName);
    std::string showName = (d && !d->genericOf.empty()) ? d->genericOf : t.structName;
    std::string hdr = strConst(showName + "{");
    out_ << "  call i32 @ox_puts(i8* " << hdr << ")\n";
    std::string a = freshLocal("ps");
    out_ << "  " << a << " = alloca " << typeStr(t) << "\n";
    out_ << "  store " << typeStr(t) << " " << val << ", " << typeStr(t) << "* " << a << "\n";
    if (d) {
      for (size_t i = 0; i < d->fields.size(); i++) {
        std::string sep;
        if (i) { sep = strConst(", "); out_ << "  call i32 @ox_puts(i8* " << sep << ")\n"; }
        std::string fn = strConst(d->fields[i].name + ": ");
        out_ << "  call i32 @ox_puts(i8* " << fn << ")\n";
        const BType& ft = d->fields[i].type;
        std::string fp = freshLocal("pf");
        out_ << "  " << fp << " = getelementptr inbounds " << typeStr(t) << ", "
             << typeStr(t) << "* " << a << ", i64 0, i32 " << i << "\n";
        std::string fv = freshLocal("fv");
        out_ << "  " << fv << " = load " << typeStr(ft) << ", " << typeStr(ft) << "* " << fp << "\n";
        printValue(fv, ft);
      }
    }
    std::string rb = strConst("}");
    out_ << "  call i32 @ox_puts(i8* " << rb << ")\n";
    return;
  }
  if (t.tag == BType::Tag::dynarray) {
    BType et = dynArrayElem(t);
    std::string sx = elemSuffix(et);
    if (!sx.empty()) {
      usedVec_.insert(sx);
      out_ << "  call void @ox_vec_print_" << sx << "(i8* " << val << ")\n";
      return;
    }


    usedVec_blob_ = true;
    int32_t esz = fieldByteWidth(et);
    std::string len = freshLocal("vpl");
    out_ << "  " << len << " = call i64 @ox_vec_len(i8* " << val << ")\n";
    std::string iSlot = freshLocal("vpi");
    out_ << "  " << iSlot << " = alloca i64\n";
    out_ << "  store i64 0, i64* " << iSlot << "\n";
    std::string lb = strConst("[");
    out_ << "  call i32 @ox_puts(i8* " << lb << ")\n";
    std::string condBB = freshLabel("vpc");
    std::string bodyBB = freshLabel("vpb");
    std::string stepBB = freshLabel("vps");
    std::string endBB = freshLabel("vpe");
    jump(condBB);
    beginBlock(condBB);
    {
      std::string i = freshLocal("vpil");
      out_ << "  " << i << " = load i64, i64* " << iSlot << "\n";
      std::string cmp = freshLocal("vpc2");
      out_ << "  " << cmp << " = icmp slt i64 " << i << ", " << len << "\n";
      branch(cmp, bodyBB, endBB);
    }
    beginBlock(bodyBB);
    {
      std::string i = freshLocal("vpib");
      out_ << "  " << i << " = load i64, i64* " << iSlot << "\n";

      std::string skipBB = freshLabel("vpskp");
      std::string sepsz = freshLocal("vpne");
      out_ << "  " << sepsz << " = icmp eq i64 " << i << ", 0\n";
      std::string bodyCont = freshLabel("vpbd");
      branch(sepsz, skipBB, bodyCont);
      beginBlock(bodyCont);
      {
        std::string sepStr = strConst(", ");
        out_ << "  call i32 @ox_puts(i8* " << sepStr << ")\n";
      }
      jump(skipBB);
      beginBlock(skipBB);

      std::string ep = freshLocal("vpp");
      out_ << "  " << ep << " = call i8* @ox_vec_blob_ptr(i8* " << val
           << ", i64 " << i << ", i64 " << (esz > 0 ? esz : 8) << ")\n";
      std::string ev = freshLocal("vpv");
      if (et.tag == BType::Tag::dynarray || et.tag == BType::Tag::ptr) {
        out_ << "  " << ev << " = load " << typeStr(et) << ", i8* " << ep << "\n";
      } else {
        std::string tp = freshLocal("vptc");
        out_ << "  " << tp << " = bitcast i8* " << ep << " to " << typeStr(et) << "*\n";
        out_ << "  " << ev << " = load " << typeStr(et) << ", " << typeStr(et) << "* " << tp << "\n";
      }
      printValue(ev, et);
    }
    jump(stepBB);
    beginBlock(stepBB);
    {
      std::string i = freshLocal("vpis");
      out_ << "  " << i << " = load i64, i64* " << iSlot << "\n";
      std::string ni = freshLocal("vpin");
      out_ << "  " << ni << " = add i64 " << i << ", 1\n";
      out_ << "  store i64 " << ni << ", i64* " << iSlot << "\n";
    }
    jump(condBB);
    beginBlock(endBB);
    std::string rb = strConst("]");
    out_ << "  call i32 @ox_puts(i8* " << rb << ")\n";
    return;
  }
  if (t.tag == BType::Tag::map_ || t.tag == BType::Tag::hmap_) {


    bool hm = (t.tag == BType::Tag::hmap_);
    if (hm) usedHMap_ = true; else usedMap_ = true;
    const char* rt_new_len   = hm ? "ox_hmap_len"     : "ox_map_len";
    const char* rt_key_ptr   = hm ? "ox_hmap_key_ptr" : "ox_map_key_ptr";
    const char* rt_get       = hm ? "ox_hmap_get"     : "ox_map_get";
    BType keyT = mapKeyType(t), valT = mapValType(t);
    collectStruct(keyT); collectStruct(valT);
    std::string len = freshLocal("mpl");
    out_ << "  " << len << " = call i64 @" << rt_new_len << "(i8* " << val << ")\n";
    std::string iSlot = freshLocal("mpi");
    out_ << "  " << iSlot << " = alloca i64\n";
    out_ << "  store i64 0, i64* " << iSlot << "\n";
    std::string lb = strConst("{");
    out_ << "  call i32 @ox_puts(i8* " << lb << ")\n";
    std::string condBB = freshLabel("mpc"), bodyBB = freshLabel("mpb"),
                stepBB = freshLabel("mps"), endBB = freshLabel("mpe");
    jump(condBB); beginBlock(condBB);
    {
      std::string i = freshLocal("mpcl");
      out_ << "  " << i << " = load i64, i64* " << iSlot << "\n";
      std::string cmp = freshLocal("mpcc");
      out_ << "  " << cmp << " = icmp slt i64 " << i << ", " << len << "\n";
      branch(cmp, bodyBB, endBB);
    }
    beginBlock(bodyBB);
    {
      std::string i = freshLocal("mpbl");
      out_ << "  " << i << " = load i64, i64* " << iSlot << "\n";

      std::string skipBB = freshLabel("mpsk");
      std::string sepsz = freshLocal("mpne");
      out_ << "  " << sepsz << " = icmp eq i64 " << i << ", 0\n";
      std::string cont = freshLabel("mpbd");
      branch(sepsz, skipBB, cont); beginBlock(cont);
      { std::string sep = strConst(", "); out_ << "  call i32 @ox_puts(i8* " << sep << ")\n"; }
      jump(skipBB); beginBlock(skipBB);

      std::string kp = freshLocal("mpkp");
      out_ << "  " << kp << " = call i8* @" << rt_key_ptr << "(i8* " << val << ", i64 " << i << ")\n";
      std::string kv = loadScratch(kp, keyT);
      printValue(kv, keyT);
      std::string colon = strConst(": ");
      out_ << "  call i32 @ox_puts(i8* " << colon << ")\n";

      std::string vslot = freshLocal("mpvs");
      out_ << "  " << vslot << " = alloca " << typeStr(valT) << "\n";
      out_ << "  store " << typeStr(valT) << " " << zeroVal(valT) << ", " << typeStr(valT)
           << "* " << vslot << "\n";
      std::string vptr = freshLocal("mpvp");
      out_ << "  " << vptr << " = bitcast " << typeStr(valT) << "* " << vslot << " to i8*\n";
      out_ << "  call i64 @" << rt_get << "(i8* " << val << ", i8* " << kp << ", i8* " << vptr << ")\n";
      std::string vv = loadScratch(vptr, valT);
      printValue(vv, valT);
    }
    jump(stepBB); beginBlock(stepBB);
    {
      std::string i = freshLocal("mpsl");
      out_ << "  " << i << " = load i64, i64* " << iSlot << "\n";
      std::string ni = freshLocal("mpn");
      out_ << "  " << ni << " = add i64 " << i << ", 1\n";
      out_ << "  store i64 " << ni << ", i64* " << iSlot << "\n";
    }
    jump(condBB);
    beginBlock(endBB);
    std::string rb = strConst("}");
    out_ << "  call i32 @ox_puts(i8* " << rb << ")\n";
    return;
  }
  if (t.tag == BType::Tag::set_ || t.tag == BType::Tag::hset_) {

    bool hs = (t.tag == BType::Tag::hset_);
    if (hs) usedHSet_ = true; else usedSet_ = true;
    const char* rt_len = hs ? "ox_hset_len" : "ox_set_len";
    const char* rt_ptr = hs ? "ox_hset_ptr" : "ox_set_ptr";
    BType et = setElemType(t);
    collectStruct(et);
    std::string len = freshLocal("spl");
    out_ << "  " << len << " = call i64 @" << rt_len << "(i8* " << val << ")\n";
    std::string iSlot = freshLocal("spi");
    out_ << "  " << iSlot << " = alloca i64\n";
    out_ << "  store i64 0, i64* " << iSlot << "\n";
    std::string lb = strConst("{");
    out_ << "  call i32 @ox_puts(i8* " << lb << ")\n";
    std::string condBB = freshLabel("spc"), bodyBB = freshLabel("spb"),
                stepBB = freshLabel("sps"), endBB = freshLabel("spe");
    jump(condBB); beginBlock(condBB);
    {
      std::string i = freshLocal("spcl");
      out_ << "  " << i << " = load i64, i64* " << iSlot << "\n";
      std::string cmp = freshLocal("spcc");
      out_ << "  " << cmp << " = icmp slt i64 " << i << ", " << len << "\n";
      branch(cmp, bodyBB, endBB);
    }
    beginBlock(bodyBB);
    {
      std::string i = freshLocal("spbl");
      out_ << "  " << i << " = load i64, i64* " << iSlot << "\n";
      std::string skipBB = freshLabel("spsk");
      std::string sepsz = freshLocal("spne");
      out_ << "  " << sepsz << " = icmp eq i64 " << i << ", 0\n";
      std::string cont = freshLabel("spbd");
      branch(sepsz, skipBB, cont); beginBlock(cont);
      { std::string sep = strConst(", "); out_ << "  call i32 @ox_puts(i8* " << sep << ")\n"; }
      jump(skipBB); beginBlock(skipBB);
      std::string ep = freshLocal("spep");
      out_ << "  " << ep << " = call i8* @" << rt_ptr << "(i8* " << val << ", i64 " << i << ")\n";
      std::string ev = loadScratch(ep, et);
      printValue(ev, et);
    }
    jump(stepBB); beginBlock(stepBB);
    {
      std::string i = freshLocal("spsl");
      out_ << "  " << i << " = load i64, i64* " << iSlot << "\n";
      std::string ni = freshLocal("spn");
      out_ << "  " << ni << " = add i64 " << i << ", 1\n";
      out_ << "  store i64 " << ni << ", i64* " << iSlot << "\n";
    }
    jump(condBB);
    beginBlock(endBB);
    std::string rb = strConst("}");
    out_ << "  call i32 @ox_puts(i8* " << rb << ")\n";
    return;
  }
}

std::string IRGen::strConst(const std::string& s) {
  std::string g = freshGlobal("str");
  std::string data = esc(s);
  size_t n = s.size() + 1;
  globals_ << g << " = private constant [" << n << " x i8] c\"" << data << "\\00\"\n";
  std::string r = freshLocal("sp");
  out_ << "  " << r << " = getelementptr inbounds [" << n << " x i8], [" << n << " x i8]* "
       << g << ", i64 0, i64 0\n";
  return r;
}

std::string IRGen::spillScratch(const std::string& v, const BType& t) {
  std::string slot = freshLocal("scr");
  out_ << "  " << slot << " = alloca " << typeStr(t) << "\n";
  out_ << "  store " << typeStr(t) << " " << v << ", " << typeStr(t) << "* " << slot << "\n";

  std::string p = freshLocal("scrp");
  out_ << "  " << p << " = bitcast " << typeStr(t) << "* " << slot << " to i8*\n";
  return p;
}

std::string IRGen::loadScratch(const std::string& ptr, const BType& t) {
  if (t.tag == BType::Tag::dynarray || t.tag == BType::Tag::ptr ||
      t.tag == BType::Tag::str) {

    std::string r = freshLocal("scl");
    out_ << "  " << r << " = load " << typeStr(t) << ", i8* " << ptr << "\n";
    return r;
  }
  std::string tp = freshLocal("sct");
  out_ << "  " << tp << " = bitcast i8* " << ptr << " to " << typeStr(t) << "*\n";
  std::string r = freshLocal("scl");
  out_ << "  " << r << " = load " << typeStr(t) << ", " << typeStr(t) << "* " << tp << "\n";
  return r;
}


void IRGen::ensureIntrinsic(const std::string& decl, const std::string& name) {
  if (declaredIntrinsics_.count(name)) return;
  declaredIntrinsics_.insert(name);
  globals_ << decl << "\n";
}

std::pair<std::string, BType> IRGen::lowerBuiltin(Call* c) {
  auto emitArgVal = [&](size_t i) -> std::pair<std::string, BType> {
    if (i >= c->args.size()) return {"", BType::void_};
    return genExpr(c->args[i].get());
  };
  const std::string& n = c->callee;

  // `hmap`/`hset` expose the same call surface as the ordered `map`/`set`
  // (Oxide accepts both `map_get(m,...)` and `hmap_get(m,...)` spellings).
  // The runtime is chosen by the operand's type tag inside each `map_*`/`set_*`
  // block below, so the explicit `hmap_*`/`hset_*` names are matched alongside
  // the map/set names there and route identically.


  if (n == "mmio_load") {
    auto [p, pt] = emitArgVal(0);
    BType elem = (pt.tag == BType::Tag::ptr) ? pointee(pt) : BType::i64;
    std::string r = freshLocal("mmio");
    out_ << "  " << r << " = load volatile " << typeStr(elem) << ", "
         << typeStr(makePtr(elem)) << " " << p << "\n";
    return {r, elem};
  }

  if (n == "mmio_store") {
    auto [p, pt] = emitArgVal(0);
    auto [v, vt] = emitArgVal(1);
    BType elem = (pt.tag == BType::Tag::ptr) ? pointee(pt) : vt;
    std::string cv = genCoerce(v, vt, elem);
    out_ << "  store volatile " << typeStr(elem) << " " << cv << ", "
         << typeStr(makePtr(elem)) << " " << p << "\n";
    return {"", BType::void_};
  }

  if (n == "memset") {
    auto [dp, dpt] = emitArgVal(0);
    auto [fv, fvt] = emitArgVal(1);
    auto [cv, cvt] = emitArgVal(2);
    ensureIntrinsic("declare void @llvm.memset.p0i8.i64(i8*, i8, i64, i1)",
                    "@llvm.memset.p0i8.i64");
    std::string d = freshLocal("msetp");
    out_ << "  " << d << " = bitcast " << typeStr(dpt) << " " << dp << " to i8*\n";
    std::string fill = genCoerce(fv, fvt, BType::u8);
    std::string cnt = genCoerce(cv, cvt, BType::i64);
    out_ << "  call void @llvm.memset.p0i8.i64(i8* " << d << ", i8 " << fill
         << ", i64 " << cnt << ", i1 false)\n";
    return {"", BType::void_};
  }

  if (n == "memcpy") {
    auto [dp, dpt] = emitArgVal(0);
    auto [sp, spt] = emitArgVal(1);
    auto [cv, cvt] = emitArgVal(2);
    ensureIntrinsic("declare void @llvm.memcpy.p0i8.p0i8.i64(i8*, i8*, i64, i1)",
                    "@llvm.memcpy.p0i8.p0i8.i64");
    std::string d = freshLocal("mcypd"), s = freshLocal("mcyps");
    out_ << "  " << d << " = bitcast " << typeStr(dpt) << " " << dp << " to i8*\n";
    out_ << "  " << s << " = bitcast " << typeStr(spt) << " " << sp << " to i8*\n";
    std::string cnt = genCoerce(cv, cvt, BType::i64);
    out_ << "  call void @llvm.memcpy.p0i8.p0i8.i64(i8* " << d << ", i8* " << s
         << ", i64 " << cnt << ", i1 false)\n";
    return {"", BType::void_};
  }


  if (n == "str_ptr") {
    auto [sv, svt] = emitArgVal(0);


    std::string p = genCoerce(sv, svt, makePtr(BType::u8));
    return {p, makePtr(BType::u8)};
  }

  if (n == "abs") {
    auto [v, vt] = emitArgVal(0);

    if (vt == BType::f64) {
      std::string r = freshLocal("abs");
      out_ << "  " << r << " = call double @llvm.fabs.f64(double " << v << ")\n";
      return {r, BType::f64};
    }

    if (isInt(vt) && isSignedInt(vt)) {
      std::string wide = genCoerce(v, vt, BType::i64);
      std::string a = freshLocal("abs");
      out_ << "  " << a << " = call i64 @ox_abs_i64(i64 " << wide << ")\n";


      std::string r = genCoerce(a, BType::i64, vt);
      return {r, vt};
    }


    if (isInt(vt)) {
      std::string r = freshLocal("abs");
      std::string ity = intIrTy(vt);
      out_ << "  " << r << " = add " << ity << " 0, " << v << "\n";
      return {r, vt};
    }
    return {"0", BType::i64};
  }
  if (n == "sqrt") {
    auto [v, vt] = emitArgVal(0);
    if (vt == BType::f64) {
      std::string r = freshLocal("sqrt");
      out_ << "  " << r << " = call double @ox_sqrt(double " << v << ")\n";
      return {r, BType::f64};
    }
    return {"0.0", BType::f64};
  }
  if (n == "imin") {
    auto [a, at] = emitArgVal(0); auto [b, bt] = emitArgVal(1);
    std::string r = freshLocal("imin");
    out_ << "  " << r << " = call i64 @ox_imin(i64 " << a << ", i64 " << b << ")\n";
    return {r, BType::i64};
  }
  if (n == "imax") {
    auto [a, at] = emitArgVal(0); auto [b, bt] = emitArgVal(1);
    std::string r = freshLocal("imax");
    out_ << "  " << r << " = call i64 @ox_imax(i64 " << a << ", i64 " << b << ")\n";
    return {r, BType::i64};
  }
  if (n == "fmin") {
    auto [a, at] = emitArgVal(0); auto [b, bt] = emitArgVal(1);
    std::string r = freshLocal("fmin");
    out_ << "  " << r << " = call double @ox_fmin2(double " << a << ", double " << b << ")\n";
    return {r, BType::f64};
  }
  if (n == "fmax") {
    auto [a, at] = emitArgVal(0); auto [b, bt] = emitArgVal(1);
    std::string r = freshLocal("fmax");
    out_ << "  " << r << " = call double @ox_fmax2(double " << a << ", double " << b << ")\n";
    return {r, BType::f64};
  }
  if (n == "itos") {
    auto [v, vt] = emitArgVal(0);
    std::string r = freshLocal("itos");
    out_ << "  " << r << " = call i8* @ox_itos(i64 " << v << ")\n";
    return {r, BType::str};
  }
  if (n == "stoi") {
    auto [v, vt] = emitArgVal(0);
    std::string r = freshLocal("stoi");
    out_ << "  " << r << " = call i64 @ox_stoi(i8* " << v << ")\n";
    return {r, BType::i64};
  }
  if (n == "stod") {
    auto [v, vt] = emitArgVal(0);
    std::string r = freshLocal("stod");
    out_ << "  " << r << " = call double @ox_stod(i8* " << v << ")\n";
    return {r, BType::f64};
  }
  if (n == "ftos") {
    auto [v, vt] = emitArgVal(0);
    std::string r = freshLocal("ftos");
    out_ << "  " << r << " = call i8* @ox_ftos(double " << v << ")\n";
    return {r, BType::str};
  }
  if (n == "char_to_str") {
    auto [v, vt] = emitArgVal(0);

    std::string c = genCoerce(v, vt, BType::i64);
    std::string r = freshLocal("cts");
    out_ << "  " << r << " = call i8* @ox_char_str(i64 " << c << ")\n";
    return {r, BType::str};
  }
  if (n == "substr") {
    auto [s, st] = emitArgVal(0);
    auto [a, at] = emitArgVal(1);
    auto [b, bt] = emitArgVal(2);
    std::string ai = genCoerce(a, at, BType::i64);
    std::string bi = genCoerce(b, bt, BType::i64);
    std::string r = freshLocal("substr");
    out_ << "  " << r << " = call i8* @ox_substr(i8* " << s << ", i64 " << ai
         << ", i64 " << bi << ")\n";
    return {r, BType::str};
  }
  if (n == "index_of") {
    auto [s, st] = emitArgVal(0);
    auto [c, ct] = emitArgVal(1);
    std::string ci = genCoerce(c, ct, BType::i64);
    std::string r = freshLocal("indexof");
    out_ << "  " << r << " = call i64 @ox_strchr(i8* " << s << ", i64 " << ci << ")\n";
    return {r, BType::i64};
  }
  if (n == "read_line") {
    std::string r = freshLocal("rl");
    out_ << "  " << r << " = call i8* @ox_read_line()\n";
    return {r, BType::str};
  }
  if (n == "read_file") {
    auto [v, vt] = emitArgVal(0);
    std::string r = freshLocal("rf");
    out_ << "  " << r << " = call i8* @ox_read_file(i8* " << v << ")\n";
    return {r, BType::str};
  }
  if (n == "file_open") {
    auto [p, pt] = emitArgVal(0); auto [m, mt] = emitArgVal(1);
    std::string r = freshLocal("fopen");
    out_ << "  " << r << " = call i64 @ox_file_open(i8* " << p << ", i8* " << m << ")\n";
    return {r, BType::i64};
  }
  if (n == "file_close") {
    auto [h, ht] = emitArgVal(0);
    std::string r = freshLocal("fclose");
    out_ << "  " << r << " = call i64 @ox_file_close(i64 " << h << ")\n";
    return {r, BType::i64};
  }
  if (n == "file_read") {
    auto [h, ht] = emitArgVal(0);
    std::string r = freshLocal("fread");
    out_ << "  " << r << " = call i8* @ox_file_read(i64 " << h << ")\n";
    return {r, BType::str};
  }
  if (n == "file_write") {
    auto [h, ht] = emitArgVal(0); auto [s, st] = emitArgVal(1);
    std::string r = freshLocal("fwrite");
    out_ << "  " << r << " = call i64 @ox_file_write(i64 " << h << ", i8* " << s << ")\n";
    return {r, BType::i64};
  }
  if (n == "file_exists") {
    auto [p, pt] = emitArgVal(0);
    std::string r = freshLocal("fexists");
    out_ << "  " << r << " = call i1 @ox_file_exists(i8* " << p << ")\n";
    return {r, BType::bool_};
  }

  if (n == "map_len" || n == "hmap_len") {
    auto [h, ht] = emitArgVal(0);
    if (ht.tag == BType::Tag::hmap_) usedHMap_ = true; else usedMap_ = true;
    std::string r = freshLocal("mlen");
    out_ << "  " << r << " = call i64 @" << (ht.tag == BType::Tag::hmap_ ? "ox_hmap_len" : "ox_map_len")
         << "(i8* " << h << ")\n";
    return {r, BType::i64};
  }
  if (n == "map_contains" || n == "hmap_contains") {
    auto [h, ht] = emitArgVal(0); auto [k, kt] = emitArgVal(1);
    if (ht.tag == BType::Tag::hmap_) usedHMap_ = true; else usedMap_ = true;
    collectStruct(mapKeyType(ht));
    BType keyT = mapKeyType(ht);
    std::string kp = spillScratch(genCoerce(k, kt, keyT), keyT);
    std::string raw = freshLocal("mcont");
    out_ << "  " << raw << " = call i64 @" << (ht.tag == BType::Tag::hmap_ ? "ox_hmap_contains" : "ox_map_contains")
         << "(i8* " << h << ", i8* " << kp << ")\n";
    std::string r = freshLocal("mcontb");
    out_ << "  " << r << " = icmp ne i64 " << raw << ", 0\n";
    return {r, BType::bool_};
  }
  if (n == "map_set" || n == "hmap_set") {
    auto [h, ht] = emitArgVal(0); auto [k, kt] = emitArgVal(1); auto [vp, vpt] = emitArgVal(2);
    if (ht.tag == BType::Tag::hmap_) usedHMap_ = true; else usedMap_ = true;
    collectStruct(mapKeyType(ht)); collectStruct(mapValType(ht));
    BType keyT = mapKeyType(ht), valT = mapValType(ht);
    std::string kp = spillScratch(genCoerce(k, kt, keyT), keyT);
    std::string vpScratch = spillScratch(genCoerce(vp, vpt, valT), valT);
    out_ << "  call void @" << (ht.tag == BType::Tag::hmap_ ? "ox_hmap_set" : "ox_map_set")
         << "(i8* " << h << ", i8* " << kp << ", i8* " << vpScratch << ")\n";
    return {"", BType::void_};
  }
  if (n == "map_get" || n == "hmap_get") {
    auto [h, ht] = emitArgVal(0); auto [k, kt] = emitArgVal(1);
    if (ht.tag == BType::Tag::hmap_) usedHMap_ = true; else usedMap_ = true;
    collectStruct(mapKeyType(ht)); collectStruct(mapValType(ht));
    BType keyT = mapKeyType(ht), valT = mapValType(ht);
    std::string kp = spillScratch(genCoerce(k, kt, keyT), keyT);

    std::string vslot = freshLocal("mgvslot");
    out_ << "  " << vslot << " = alloca " << typeStr(valT) << "\n";
    out_ << "  store " << typeStr(valT) << " " << zeroVal(valT) << ", " << typeStr(valT)
         << "* " << vslot << "\n";
    std::string vptr = freshLocal("mgvp");
    out_ << "  " << vptr << " = bitcast " << typeStr(valT) << "* " << vslot << " to i8*\n";
    out_ << "  call i64 @" << (ht.tag == BType::Tag::hmap_ ? "ox_hmap_get" : "ox_map_get")
         << "(i8* " << h << ", i8* " << kp << ", i8* " << vptr << ")\n";
    std::string r = loadScratch(vptr, valT);
    return {r, valT};
  }
  if (n == "map_keys" || n == "hmap_keys") {


    auto [h, ht] = emitArgVal(0);
    const char* rt_len = (ht.tag == BType::Tag::hmap_) ? "ox_hmap_len" : "ox_map_len";
    const char* rt_kp  = (ht.tag == BType::Tag::hmap_) ? "ox_hmap_key_ptr" : "ox_map_key_ptr";
    if (ht.tag == BType::Tag::hmap_) usedHMap_ = true; else usedMap_ = true;
    collectStruct(mapKeyType(ht));
    BType keyT = mapKeyType(ht);
    std::string sx = elemSuffix(keyT);
    BType vecT = makeDynArray(keyT);
    std::string out;
    if (!sx.empty()) {
      usedVec_.insert(sx);
      out = freshLocal("mkvec");
      out_ << "  " << out << " = call i8* @ox_vec_new_" << sx << "()\n";
    } else {
      usedVec_blob_ = true;
      out = freshLocal("mkvec");
      int32_t esz = fieldByteWidth(keyT);
      out_ << "  " << out << " = call i8* @ox_vec_blob_new(i64 "
           << (esz > 0 ? esz : 8) << ")\n";
    }


    std::string len = freshLocal("mklen");
    out_ << "  " << len << " = call i64 @" << rt_len << "(i8* " << h << ")\n";
    std::string iSlot = freshLocal("mki");
    out_ << "  " << iSlot << " = alloca i64\n";
    out_ << "  store i64 0, i64* " << iSlot << "\n";
    std::string condBB = freshLabel("mkc"), bodyBB = freshLabel("mkb"),
                stepBB = freshLabel("mks"), endBB = freshLabel("mke");
    jump(condBB); beginBlock(condBB);
    {
      std::string i = freshLocal("mkcl");
      out_ << "  " << i << " = load i64, i64* " << iSlot << "\n";
      std::string cmp = freshLocal("mkcc");
      out_ << "  " << cmp << " = icmp slt i64 " << i << ", " << len << "\n";
      branch(cmp, bodyBB, endBB);
    }
    beginBlock(bodyBB);
    {
      std::string i = freshLocal("mkbl");
      out_ << "  " << i << " = load i64, i64* " << iSlot << "\n";
      std::string kp = freshLocal("mkkp");
      int32_t kw = fieldByteWidth(keyT);
      out_ << "  " << kp << " = call i8* @" << rt_kp << "(i8* " << h << ", i64 " << i
           << ")\n";
      if (!sx.empty()) {

        std::string tp = freshLocal("mkt");
        if (keyT == BType::str || keyT.tag == BType::Tag::ptr) {
          std::string kv = freshLocal("mkkv");
          out_ << "  " << kv << " = load " << vecSlotType(sx) << ", i8* " << kp << "\n";
          out_ << "  call void @ox_vec_push_" << sx << "(i8* " << out << ", "
               << vecSlotType(sx) << " " << kv << ")\n";
        } else {
          (void)tp; (void)kw;
          out_ << "  " << tp << " = bitcast i8* " << kp << " to " << typeStr(keyT) << "*\n";
          std::string kv = freshLocal("mkkv");
          out_ << "  " << kv << " = load " << typeStr(keyT) << ", " << typeStr(keyT)
               << "* " << tp << "\n";
          std::string cv = genCoerce(kv, keyT, vecSlotBType(sx));
          out_ << "  call void @ox_vec_push_" << sx << "(i8* " << out << ", "
               << vecSlotType(sx) << " " << cv << ")\n";
        }
      } else {

        int32_t esz = fieldByteWidth(keyT);
        std::string tmp = freshLocal("mkel");
        out_ << "  " << tmp << " = alloca " << typeStr(keyT) << "\n";
        out_ << "  call void @llvm.memcpy.p0i8.p0i8.i64(i8* " << tmp << ", i8* " << kp
             << ", i64 " << (esz > 0 ? esz : 8) << ", i1 0)\n";
        out_ << "  call void @ox_vec_blob_push(i8* " << out << ", i64 "
             << (esz > 0 ? esz : 8) << ", i8* " << tmp << ")\n";
      }
    }
    jump(stepBB); beginBlock(stepBB);
    {
      std::string i = freshLocal("mksl");
      out_ << "  " << i << " = load i64, i64* " << iSlot << "\n";
      std::string ni = freshLocal("mkn");
      out_ << "  " << ni << " = add i64 " << i << ", 1\n";
      out_ << "  store i64 " << ni << ", i64* " << iSlot << "\n";
    }
    jump(condBB); beginBlock(endBB);
    return {out, vecT};
  }

  if (n == "set_len" || n == "hset_len") {
    auto [h, ht] = emitArgVal(0);
    if (ht.tag == BType::Tag::hset_) usedHSet_ = true; else usedSet_ = true;
    std::string r = freshLocal("setlen");
    out_ << "  " << r << " = call i64 @" << (ht.tag == BType::Tag::hset_ ? "ox_hset_len" : "ox_set_len")
         << "(i8* " << h << ")\n";
    return {r, BType::i64};
  }
  if (n == "set_contains" || n == "hset_contains") {
    auto [h, ht] = emitArgVal(0); auto [k, kt] = emitArgVal(1);
    if (ht.tag == BType::Tag::hset_) usedHSet_ = true; else usedSet_ = true;
    collectStruct(setElemType(ht));
    BType et = setElemType(ht);
    std::string kp = spillScratch(genCoerce(k, kt, et), et);
    std::string raw = freshLocal("setcont");
    out_ << "  " << raw << " = call i64 @" << (ht.tag == BType::Tag::hset_ ? "ox_hset_contains" : "ox_set_contains")
         << "(i8* " << h << ", i8* " << kp << ")\n";
    std::string r = freshLocal("setcontb");
    out_ << "  " << r << " = icmp ne i64 " << raw << ", 0\n";
    return {r, BType::bool_};
  }
  if (n == "set_insert" || n == "set_remove" || n == "hset_insert" || n == "hset_remove") {
    auto [h, ht] = emitArgVal(0); auto [k, kt] = emitArgVal(1);
    bool hs = (ht.tag == BType::Tag::hset_);
    if (hs) usedHSet_ = true; else usedSet_ = true;
    collectStruct(setElemType(ht));
    BType et = setElemType(ht);
    std::string kp = spillScratch(genCoerce(k, kt, et), et);
    bool inserting = (n == "set_insert" || n == "hset_insert");
    const char* op = inserting ? (hs ? "hset_insert" : "set_insert")
                               : (hs ? "hset_remove" : "set_remove");
    out_ << "  call void @ox_" << op << "(i8* " << h << ", i8* " << kp << ")\n";
    return {"", BType::void_};
  }
  if (n == "set_to_vec" || n == "hset_to_vec") {


    auto [h, ht] = emitArgVal(0);
    bool hs = (ht.tag == BType::Tag::hset_);
    if (hs) usedHSet_ = true; else usedSet_ = true;
    const char* rt_len = hs ? "ox_hset_len" : "ox_set_len";
    const char* rt_ptr = hs ? "ox_hset_ptr" : "ox_set_ptr";
    collectStruct(setElemType(ht));
    BType et = setElemType(ht);
    std::string sx = elemSuffix(et);
    BType vecT = makeDynArray(et);
    std::string out;
    if (!sx.empty()) {
      usedVec_.insert(sx);
      out = freshLocal("setvec");
      out_ << "  " << out << " = call i8* @ox_vec_new_" << sx << "()\n";
    } else {
      usedVec_blob_ = true;
      out = freshLocal("setvec");
      int32_t esz = fieldByteWidth(et);
      out_ << "  " << out << " = call i8* @ox_vec_blob_new(i64 "
           << (esz > 0 ? esz : 8) << ")\n";
    }
    std::string len = freshLocal("setlen");
    out_ << "  " << len << " = call i64 @" << rt_len << "(i8* " << h << ")\n";
    std::string iSlot = freshLocal("seti");
    out_ << "  " << iSlot << " = alloca i64\n";
    out_ << "  store i64 0, i64* " << iSlot << "\n";
    std::string condBB = freshLabel("setc"), bodyBB = freshLabel("setb"),
                stepBB = freshLabel("sets"), endBB = freshLabel("sete");
    jump(condBB); beginBlock(condBB);
    {
      std::string i = freshLocal("setcl");
      out_ << "  " << i << " = load i64, i64* " << iSlot << "\n";
      std::string cmp = freshLocal("setcc");
      out_ << "  " << cmp << " = icmp slt i64 " << i << ", " << len << "\n";
      branch(cmp, bodyBB, endBB);
    }
    beginBlock(bodyBB);
    {
      std::string i = freshLocal("setbl");
      out_ << "  " << i << " = load i64, i64* " << iSlot << "\n";
      std::string ep = freshLocal("setkp");
      out_ << "  " << ep << " = call i8* @" << rt_ptr << "(i8* " << h << ", i64 " << i << ")\n";
      if (!sx.empty()) {
        if (et == BType::str || et.tag == BType::Tag::ptr) {
          std::string ev = freshLocal("setv");
          out_ << "  " << ev << " = load " << vecSlotType(sx) << ", i8* " << ep << "\n";
          out_ << "  call void @ox_vec_push_" << sx << "(i8* " << out << ", "
               << vecSlotType(sx) << " " << ev << ")\n";
        } else {
          std::string tp = freshLocal("sett");
          out_ << "  " << tp << " = bitcast i8* " << ep << " to " << typeStr(et) << "*\n";
          std::string ev = freshLocal("setv");
          out_ << "  " << ev << " = load " << typeStr(et) << ", " << typeStr(et)
               << "* " << tp << "\n";
          std::string cv = genCoerce(ev, et, vecSlotBType(sx));
          out_ << "  call void @ox_vec_push_" << sx << "(i8* " << out << ", "
               << vecSlotType(sx) << " " << cv << ")\n";
        }
      } else {
        int32_t esz = fieldByteWidth(et);
        std::string tmp = freshLocal("setel");
        out_ << "  " << tmp << " = alloca " << typeStr(et) << "\n";
        out_ << "  call void @llvm.memcpy.p0i8.p0i8.i64(i8* " << tmp << ", i8* " << ep
             << ", i64 " << (esz > 0 ? esz : 8) << ", i1 0)\n";
        out_ << "  call void @ox_vec_blob_push(i8* " << out << ", i64 "
             << (esz > 0 ? esz : 8) << ", i8* " << tmp << ")\n";
      }
    }
    jump(stepBB); beginBlock(stepBB);
    {
      std::string i = freshLocal("setsl");
      out_ << "  " << i << " = load i64, i64* " << iSlot << "\n";
      std::string ni = freshLocal("setn");
      out_ << "  " << ni << " = add i64 " << i << ", 1\n";
      out_ << "  store i64 " << ni << ", i64* " << iSlot << "\n";
    }
    jump(condBB); beginBlock(endBB);
    return {out, vecT};
  }
  if (n == "len") {
    auto [v, vt] = emitArgVal(0);
    if (vt.tag == BType::Tag::dynarray) {
      std::string r = freshLocal("vlen");
      out_ << "  " << r << " = call i64 @ox_vec_len(i8* " << v << ")\n";
      return {r, BType::i64};
    }
    if (vt.tag == BType::Tag::array) {
      return {std::to_string(vt.count), BType::i64};
    }
    if (vt.tag == BType::Tag::map_) {
      usedMap_ = true;
      std::string r = freshLocal("mlen");
      out_ << "  " << r << " = call i64 @ox_map_len(i8* " << v << ")\n";
      return {r, BType::i64};
    }
    if (vt.tag == BType::Tag::hmap_) {
      usedHMap_ = true;
      std::string r = freshLocal("hmlen");
      out_ << "  " << r << " = call i64 @ox_hmap_len(i8* " << v << ")\n";
      return {r, BType::i64};
    }
    if (vt.tag == BType::Tag::set_) {
      usedSet_ = true;
      std::string r = freshLocal("setlen");
      out_ << "  " << r << " = call i64 @ox_set_len(i8* " << v << ")\n";
      return {r, BType::i64};
    }
    if (vt.tag == BType::Tag::hset_) {
      usedHSet_ = true;
      std::string r = freshLocal("hslen");
      out_ << "  " << r << " = call i64 @ox_hset_len(i8* " << v << ")\n";
      return {r, BType::i64};
    }
    if (vt == BType::str) {
      std::string r = freshLocal("slen");
      out_ << "  " << r << " = call i64 @ox_strlen(i8* " << v << ")\n";
      return {r, BType::i64};
    }
    return {"0", BType::i64};
  }
  if (n == "push") {
    auto [hv, hvt] = emitArgVal(0);
    auto [xv, xvt] = emitArgVal(1);
    if (hvt.tag == BType::Tag::dynarray) {
      BType et = dynArrayElem(hvt);
      std::string sx = elemSuffix(et);
      if (!sx.empty()) {
        usedVec_.insert(sx);
        std::string cv = genCoerce(xv, xvt, vecSlotBType(sx));
        out_ << "  call void @ox_vec_push_" << sx << "(i8* " << hv << ", "
             << vecSlotType(sx) << " " << cv << ")\n";
      } else {

        std::string cv = genCoerce(xv, xvt, et);
        std::string tmp = freshLocal("vbp");
        out_ << "  " << tmp << " = alloca " << typeStr(et) << "\n";
        out_ << "  store " << typeStr(et) << " " << cv << ", " << typeStr(et) << "* " << tmp << "\n";
        int32_t esz = fieldByteWidth(et);
        usedVec_blob_ = true;
        out_ << "  call void @ox_vec_blob_push(i8* " << hv << ", i64 "
             << (esz > 0 ? esz : 8) << ", i8* " << tmp << ")\n";
      }
    }
    return {"", BType::void_};
  }
  if (n == "sort") {
    auto [hv, hvt] = emitArgVal(0);
    if (hvt.tag == BType::Tag::dynarray) {
      BType et = dynArrayElem(hvt);
      std::string sx = elemSuffix(et);
      if (!sx.empty()) {
        usedSort_.insert(sx);
        out_ << "  call void @ox_sort_" << sx << "(i8* " << hv << ")\n";
      } else {


        int32_t esz = fieldByteWidth(et);
        long long kind = 0;
        if (isFloat(et)) kind = 2;
        else if (!isSignedInt(et) && (et.tag == BType::Tag::u8 || et.tag == BType::Tag::u16 ||
                 et.tag == BType::Tag::u32 || et.tag == BType::Tag::u64 ||
                 et.tag == BType::Tag::usize)) kind = 1;
        else if (et.tag == BType::Tag::ptr) kind = 3;
        usedSort_blob_ = true;
        out_ << "  call void @ox_sort_blob(i8* " << hv << ", i64 "
             << (esz > 0 ? esz : 8) << ", i64 " << kind << ")\n";
      }
    }
    return {"", BType::void_};
  }

  // ---- extended stdlib dispatch ----
  // Mark a runtime extern as referenced (so its declare is emitted) and return
  // its LLVM name. Used everywhere below to gate declares on actual use.
  auto mark = [&](const char* rt) -> const char* { usedExt_.insert(rt); return rt; };

  if (n == "pow")   { auto [a,at]=emitArgVal(0); auto [b,bt]=emitArgVal(1);
    mark("ox_pow"); std::string av=genCoerce(a,at,BType::f64), bv=genCoerce(b,bt,BType::f64);
    std::string r=freshLocal("pow"); out_<<"  "<<r<<" = call double @ox_pow(double "<<av<<", double "<<bv<<")\n"; return {r,BType::f64}; }
  if (n == "floor" || n == "ceil" || n == "round" || n == "trunc" ||
      n == "sin" || n == "cos" || n == "tan" || n == "asin" || n == "acos" ||
      n == "atan" || n == "log" || n == "log2" || n == "log10" ||
      n == "exp" || n == "exp2") {
    // map a friendly name to the runtime suffix (everything after "ox_").
    auto [v, vt] = emitArgVal(0);
    std::string cv = genCoerce(v, vt, BType::f64);
    std::string suffix = n;
    mark((std::string("ox_") + suffix).c_str());
    std::string r = freshLocal("m");
    out_ << "  " << r << " = call double @ox_" << suffix << "(double " << cv << ")\n";
    return {r, BType::f64};
  }
  if (n == "atan2") { auto [a,at]=emitArgVal(0); auto [b,bt]=emitArgVal(1);
    mark("ox_atan2"); std::string av=genCoerce(a,at,BType::f64), bv=genCoerce(b,bt,BType::f64);
    std::string r=freshLocal("atan2"); out_<<"  "<<r<<" = call double @ox_atan2(double "<<av<<", double "<<bv<<")\n"; return {r,BType::f64}; }
  if (n == "hypot") { auto [a,at]=emitArgVal(0); auto [b,bt]=emitArgVal(1);
    mark("ox_hypot"); std::string av=genCoerce(a,at,BType::f64), bv=genCoerce(b,bt,BType::f64);
    std::string r=freshLocal("hyp"); out_<<"  "<<r<<" = call double @ox_hypot(double "<<av<<", double "<<bv<<")\n"; return {r,BType::f64}; }
  if (n == "fmod")  { auto [a,at]=emitArgVal(0); auto [b,bt]=emitArgVal(1);
    mark("ox_fmod"); std::string av=genCoerce(a,at,BType::f64), bv=genCoerce(b,bt,BType::f64);
    std::string r=freshLocal("fmod"); out_<<"  "<<r<<" = call double @ox_fmod(double "<<av<<", double "<<bv<<")\n"; return {r,BType::f64}; }
  if (n == "lround") { auto [v,vt]=emitArgVal(0);
    mark("ox_lround"); std::string cv=genCoerce(v,vt,BType::f64);
    std::string r=freshLocal("lr"); out_<<"  "<<r<<" = call i64 @ox_lround(double "<<cv<<")\n"; return {r,BType::i64}; }
  if (n == "gcd")   { auto [a,at]=emitArgVal(0); auto [b,bt]=emitArgVal(1);
    mark("ox_gcd"); std::string av=genCoerce(a,at,BType::i64), bv=genCoerce(b,bt,BType::i64);
    std::string r=freshLocal("gcd"); out_<<"  "<<r<<" = call double @ox_gcd(i64 "<<av<<", i64 "<<bv<<")\n"; return {r,BType::f64}; }
  if (n == "isnan" || n == "isinf" || n == "finite") {
    auto [v,vt]=emitArgVal(0); std::string cv=genCoerce(v,vt,BType::f64);
    const char* m = (n=="isnan")?"ox_isnan":(n=="isinf")?"ox_isinf":"ox_finite";
    mark(m); std::string suf = (n=="isnan")?"isnan":(n=="isinf")?"isinf":"finite";
    std::string r=freshLocal("m"); out_<<"  "<<r<<" = call i64 @ox_"<<suf<<"(double "<<cv<<")\n"; return {r,BType::i64}; }
  if (n == "deg2rad" || n == "rad2deg") {
    auto [v,vt]=emitArgVal(0); std::string cv=genCoerce(v,vt,BType::f64);
    mark(n=="deg2rad"?"ox_deg2rad":"ox_rad2deg");
    std::string suf = n=="deg2rad"?"deg2rad":"rad2deg";
    std::string r=freshLocal("m"); out_<<"  "<<r<<" = call double @ox_"<<suf<<"(double "<<cv<<")\n"; return {r,BType::f64}; }
  if (n == "pi" || n == "e") {
    mark(n=="pi"?"ox_pi":"ox_e"); std::string suf = n=="pi"?"pi":"e";
    std::string r=freshLocal("c"); out_<<"  "<<r<<" = call double @ox_"<<suf<<"()\n"; return {r,BType::f64}; }
  if (n == "clampf") { auto [v,vt]=emitArgVal(0); auto [lo,lot]=emitArgVal(1); auto [hi,hit]=emitArgVal(2);
    mark("ox_clampf"); std::string cv=genCoerce(v,vt,BType::f64), cl=genCoerce(lo,lot,BType::f64), ch=genCoerce(hi,hit,BType::f64);
    std::string r=freshLocal("cl"); out_<<"  "<<r<<" = call double @ox_clampf(double "<<cv<<", double "<<cl<<", double "<<ch<<")\n"; return {r,BType::f64}; }
  if (n == "clampi" || n == "clamp") { auto [v,vt]=emitArgVal(0); auto [lo,lot]=emitArgVal(1); auto [hi,hit]=emitArgVal(2);
    mark("ox_clampi"); std::string cv=genCoerce(v,vt,BType::i64), cl=genCoerce(lo,lot,BType::i64), ch=genCoerce(hi,hit,BType::i64);
    std::string r=freshLocal("cl"); out_<<"  "<<r<<" = call i64 @ox_clampi(i64 "<<cv<<", i64 "<<cl<<", i64 "<<ch<<")\n"; return {r,BType::i64}; }

  // ---- strings ----
  if (n == "lower" || n == "upper" || n == "reverse" || n == "trim") {
    auto [v, vt] = emitArgVal(0);
    std::string cv = genCoerce(v, vt, BType::str);
    const char* m = (n=="lower")?"ox_tolower":(n=="upper")?"ox_toupper":(n=="reverse")?"ox_str_reverse":"ox_trim";
    const char* suf = (n=="lower")?"tolower":(n=="upper")?"toupper":(n=="reverse")?"str_reverse":"trim";
    mark(m);
    std::string r = freshLocal("s");
    out_ << "  " << r << " = call i8* @ox_" << suf << "(i8* " << cv << ")\n";
    return {r, BType::str};
  }
  if (n == "repeat") { auto [v,vt]=emitArgVal(0); auto [k,kt]=emitArgVal(1);
    mark("ox_str_repeat"); std::string cv=genCoerce(v,vt,BType::str), ck=genCoerce(k,kt,BType::i64);
    std::string r=freshLocal("s"); out_<<"  "<<r<<" = call i8* @ox_str_repeat(i8* "<<cv<<", i64 "<<ck<<")\n"; return {r,BType::str}; }
  if (n == "starts_with" || n == "ends_with" || n == "contains_str" || n == "find") {
    auto [a,at]=emitArgVal(0); auto [b,bt]=emitArgVal(1);
    std::string av=genCoerce(a,at,BType::str), bv=genCoerce(b,bt,BType::str);
    const char* m = (n=="starts_with")?"ox_starts_with":(n=="ends_with")?"ox_ends_with":(n=="contains_str")?"ox_str_contains":"ox_find";
    const char* suf = (n=="starts_with")?"starts_with":(n=="ends_with")?"ends_with":(n=="contains_str")?"str_contains":"find";
    mark(m);
    std::string r=freshLocal("s");
    if (n=="starts_with"||n=="ends_with"||n=="contains_str")
      out_<<"  "<<r<<" = call i64 @ox_"<<suf<<"(i8* "<<av<<", i8* "<<bv<<")\n";
    else
      out_<<"  "<<r<<" = call i64 @ox_"<<suf<<"(i8* "<<av<<", i8* "<<bv<<")\n";
    // contains_str/find are i64/-1; starts/ends -> coerce to bool via comparison
    if (n=="contains_str"||n=="starts_with"||n=="ends_with") {
      std::string b1=freshLocal("c"); out_<<"  "<<b1<<" = icmp ne i64 "<<r<<", 0\n"; return {b1,BType::bool_}; }
    return {r,BType::i64};
  }
  if (n == "replace") { auto [s,st]=emitArgVal(0); auto [from,ft]=emitArgVal(1); auto [to,tt]=emitArgVal(2);
    mark("ox_replace");
    std::string sv=genCoerce(s,st,BType::str), fv=genCoerce(from,ft,BType::str), tv=genCoerce(to,tt,BType::str);
    std::string r=freshLocal("s"); out_<<"  "<<r<<" = call i8* @ox_replace(i8* "<<sv<<", i8* "<<fv<<", i8* "<<tv<<")\n"; return {r,BType::str}; }
  if (n == "split") { auto [s,st]=emitArgVal(0); auto [sep,sept]=emitArgVal(1);
    mark("ox_split");
    std::string sv=genCoerce(s,st,BType::str), sepv=genCoerce(sep,sept,BType::str);
    std::string r=freshLocal("sp"); out_<<"  "<<r<<" = call i8* @ox_split(i8* "<<sv<<", i8* "<<sepv<<")\n";
    // result is a vec[str] handle
    BType vt = makeDynArray(BType::str);
    return {r, vt}; }
  if (n == "join") { auto [h,ht]=emitArgVal(0); auto [sep,sept]=emitArgVal(1);
    mark("ox_str_join");
    std::string hv = h; (void)ht;     // vec handle is already i8*
    std::string sepv=genCoerce(sep,sept,BType::str);
    std::string r=freshLocal("j"); out_<<"  "<<r<<" = call i8* @ox_str_join(i8* "<<hv<<", i8* "<<sepv<<")\n"; return {r,BType::str}; }
  if (n == "itoa_base") { auto [v,vt]=emitArgVal(0); auto [b,bt]=emitArgVal(1);
    mark("ox_itoa_base"); std::string cv=genCoerce(v,vt,BType::i64), cb=genCoerce(b,bt,BType::i64);
    std::string r=freshLocal("s"); out_<<"  "<<r<<" = call i8* @ox_itoa_base(i64 "<<cv<<", i64 "<<cb<<")\n"; return {r,BType::str}; }
  if (n == "stoi_base") { auto [s,st]=emitArgVal(0); auto [b,bt]=emitArgVal(1);
    mark("ox_stoi_base"); usedVec_blob_ = true; // pulls memcpy declare for the out-slot
    std::string sv=genCoerce(s,st,BType::str), cb=genCoerce(b,bt,BType::i64);
    std::string outslot=freshLocal("so"); out_<<"  "<<outslot<<" = alloca i64\n";
    std::string r=freshLocal("ok");
    out_<<"  "<<r<<" = call i64 @ox_stoi_base(i8* "<<sv<<", i64 "<<cb<<", i8* "<<outslot<<")\n";
    // returns a tuple-ish: we expose the parsed int on success, else 0, and the bool via a second local.
    // For simplicity return the parsed int; callers who need the flag use the two-arg `stoi` form.
    std::string pv=freshLocal("pv"); out_<<"  "<<pv<<" = load i64, i64* "<<outslot<<"\n";
    return {pv,BType::i64}; }

  // ---- vec helpers ----
  if (n == "pop") {                       // vec.pop() -> bool (did pop)
    auto [hv,hvt]=emitArgVal(0);
    if (hvt.tag==BType::Tag::dynarray) {
      BType et=dynArrayElem(hvt);
      std::string sx=elemSuffix(et);
      if (!sx.empty()) {
        usedVec_.insert(sx);
        std::string m=std::string("ox_vec_pop_")+sx; usedExt_.insert(m);
        std::string r=freshLocal("pop"); out_<<"  "<<r<<" = call i64 @ox_vec_pop_"<<sx<<"(i8* "<<hv<<")\n"; return {r,BType::bool_};
      } else {
        mark("ox_vec_pop_blob");
        std::string r=freshLocal("pop"); out_<<"  call void @ox_vec_pop_blob(i8* "<<hv<<")\n";
        return {"1", BType::bool_};
      }
    }
    return {"",BType::void_};
  }
  if (n == "first" || n == "last") {
    auto [hv,hvt]=emitArgVal(0);
    if (hvt.tag==BType::Tag::dynarray) {
      BType et=dynArrayElem(hvt);
      std::string sx=elemSuffix(et);
      const char* which = (n=="first")?"first":"last";
      std::string m=std::string("ox_vec_")+which+"_"+sx; usedExt_.insert(m);
      std::string slot = (sx=="i64")?"i64":(sx=="f64")?"double":(sx=="i1")?"i1":(sx=="i8")?"i8":"i8*";
      std::string r=freshLocal(which);
      out_<<"  "<<r<<" = call "<<slot<<" @ox_vec_"<<which<<"_"<<sx<<"(i8* "<<hv<<")\n";
      BType rt = (sx=="i64")?BType::i64:(sx=="f64")?BType::f64:(sx=="i1")?BType::bool_:(sx=="i8")?BType::char_:BType::str;
      return {r, rt};
    }
    return {"0",BType::i64};
  }
  if (n == "pop_last") { // alias commonly wanted: drop last, return nothing
    auto [hv,hvt]=emitArgVal(0); (void)hvt;
    mark("ox_vec_pop_blob");
    out_<<"  call void @ox_vec_pop_blob(i8* "<<hv<<")\n"; return {"",BType::void_};
  }
  if (n == "clear") {
    auto [hv,hvt]=emitArgVal(0);
    if (hvt.tag==BType::Tag::map_) { mark("ox_map_clear"); out_<<"  call void @ox_map_clear(i8* "<<hv<<")\n"; return {"",BType::void_}; }
    if (hvt.tag==BType::Tag::hmap_) { mark("ox_hmap_clear"); usedHMap_=true; out_<<"  call void @ox_hmap_clear(i8* "<<hv<<")\n"; return {"",BType::void_}; }
    if (hvt.tag==BType::Tag::set_) { mark("ox_set_clear"); out_<<"  call void @ox_set_clear(i8* "<<hv<<")\n"; return {"",BType::void_}; }
    if (hvt.tag==BType::Tag::hset_) { mark("ox_hset_clear"); usedHSet_=true; out_<<"  call void @ox_hset_clear(i8* "<<hv<<")\n"; return {"",BType::void_}; }
    mark("ox_vec_clear"); out_<<"  call void @ox_vec_clear(i8* "<<hv<<")\n"; return {"",BType::void_};
  }
  if (n == "remove_at") {
    auto [hv,hvt]=emitArgVal(0); auto [iv,it]=emitArgVal(1);
    BType et = (hvt.tag==BType::Tag::dynarray)?dynArrayElem(hvt):BType::i64;
    int32_t esz = fieldByteWidth(et);
    mark("ox_vec_remove_at");
    out_<<"  call void @ox_vec_remove_at(i8* "<<hv<<", i64 "<<iv<<", i64 "<<(esz>0?esz:8)<<")\n"; return {"",BType::void_};
  }
  if (n == "insert_at") {
    auto [hv,hvt]=emitArgVal(0); auto [iv,it]=emitArgVal(1); auto [xv,xt]=emitArgVal(2);
    BType et = (hvt.tag==BType::Tag::dynarray)?dynArrayElem(hvt):BType::i64;
    int32_t esz = fieldByteWidth(et);
    std::string tmp=freshLocal("ins"); out_<<"  "<<tmp<<" = alloca "<<typeStr(et)<<"\n";
    std::string cv=genCoerce(xv,xt,et);
    out_<<"  store "<<typeStr(et)<<" "<<cv<<", "<<typeStr(et)<<"* "<<tmp<<"\n";
    mark("ox_vec_insert_at");
    out_<<"  call void @ox_vec_insert_at(i8* "<<hv<<", i64 "<<iv<<", i64 "<<(esz>0?esz:8)<<", i8* "<<tmp<<")\n"; return {"",BType::void_};
  }
  if (n == "reverse") {
    auto [hv,hvt]=emitArgVal(0);
    BType et = (hvt.tag==BType::Tag::dynarray)?dynArrayElem(hvt):BType::i64;
    int32_t esz = fieldByteWidth(et);
    mark("ox_vec_reverse");
    out_<<"  call void @ox_vec_reverse(i8* "<<hv<<", i64 "<<(esz>0?esz:8)<<")\n"; return {"",BType::void_};
  }
  if (n == "contains_elem") {   // distinct from string contains_str; hmm naming
    auto [hv,hvt]=emitArgVal(0); auto [xv,xt]=emitArgVal(1);
    BType et = (hvt.tag==BType::Tag::dynarray)?dynArrayElem(hvt):BType::i64;
    int32_t esz = fieldByteWidth(et);
    long long kind = 0;
    if (isFloat(et)) kind = 2;
    else if (!isSignedInt(et) && (et.tag==BType::Tag::u8||et.tag==BType::Tag::u16||et.tag==BType::Tag::u32||
            et.tag==BType::Tag::u64||et.tag==BType::Tag::usize)) kind = 1;
    else if (et.tag==BType::Tag::ptr) kind = 3;
    std::string tmp=freshLocal("cek"); out_<<"  "<<tmp<<" = alloca "<<typeStr(et)<<"\n";
    std::string cv=genCoerce(xv,xt,et);
    out_<<"  store "<<typeStr(et)<<" "<<cv<<", "<<typeStr(et)<<"* "<<tmp<<"\n";
    mark("ox_vec_contains");
    std::string r=freshLocal("c"); out_<<"  "<<r<<" = call i64 @ox_vec_contains(i8* "<<hv<<", i64 "<<(esz>0?esz:8)<<", i8* "<<tmp<<", i64 "<<kind<<")\n";
    std::string b1=freshLocal("c1"); out_<<"  "<<b1<<" = icmp ne i64 "<<r<<", 0\n"; return {b1,BType::bool_};
  }
  if (n == "index_of_elem") {
    auto [hv,hvt]=emitArgVal(0); auto [xv,xt]=emitArgVal(1);
    BType et = (hvt.tag==BType::Tag::dynarray)?dynArrayElem(hvt):BType::i64;
    int32_t esz = fieldByteWidth(et);
    long long kind = 0;
    if (isFloat(et)) kind = 2;
    else if (!isSignedInt(et) && (et.tag==BType::Tag::u8||et.tag==BType::Tag::u16||et.tag==BType::Tag::u32||
            et.tag==BType::Tag::u64||et.tag==BType::Tag::usize)) kind = 1;
    else if (et.tag==BType::Tag::ptr) kind = 3;
    std::string tmp=freshLocal("iek"); out_<<"  "<<tmp<<" = alloca "<<typeStr(et)<<"\n";
    std::string cv=genCoerce(xv,xt,et);
    out_<<"  store "<<typeStr(et)<<" "<<cv<<", "<<typeStr(et)<<"* "<<tmp<<"\n";
    mark("ox_vec_index_of");
    std::string r=freshLocal("i"); out_<<"  "<<r<<" = call i64 @ox_vec_index_of(i8* "<<hv<<", i64 "<<(esz>0?esz:8)<<", i8* "<<tmp<<", i64 "<<kind<<")\n";
    return {r,BType::i64};
  }
  if (n == "extend") {
    auto [dv,dvt]=emitArgVal(0); auto [sv,svt]=emitArgVal(1);
    BType et = (dvt.tag==BType::Tag::dynarray)?dynArrayElem(dvt):BType::i64;
    int32_t esz = fieldByteWidth(et);
    mark("ox_vec_extend");
    out_<<"  call void @ox_vec_extend(i8* "<<dv<<", i8* "<<sv<<", i64 "<<(esz>0?esz:8)<<")\n"; return {"",BType::void_};
  }
  if (n == "sum") {
    auto [hv,hvt]=emitArgVal(0);
    BType et = (hvt.tag==BType::Tag::dynarray)?dynArrayElem(hvt):BType::i64;
    if (et==BType::i64) { mark("ox_vec_sum_i64"); std::string r=freshLocal("s"); out_<<"  "<<r<<" = call i64 @ox_vec_sum_i64(i8* "<<hv<<")\n"; return {r,BType::i64}; }
    if (et==BType::f64) { mark("ox_vec_sum_f64"); std::string r=freshLocal("s"); out_<<"  "<<r<<" = call double @ox_vec_sum_f64(i8* "<<hv<<")\n"; return {r,BType::f64}; }
    return {"0",BType::i64};
  }
  if (n == "vmin") {
    auto [hv,hvt]=emitArgVal(0);
    BType et = (hvt.tag==BType::Tag::dynarray)?dynArrayElem(hvt):BType::i64;
    if (et==BType::i64) { mark("ox_vec_min_i64"); std::string r=freshLocal("s"); out_<<"  "<<r<<" = call i64 @ox_vec_min_i64(i8* "<<hv<<")\n"; return {r,BType::i64}; }
    if (et==BType::f64) { mark("ox_vec_min_f64"); std::string r=freshLocal("s"); out_<<"  "<<r<<" = call double @ox_vec_min_f64(i8* "<<hv<<")\n"; return {r,BType::f64}; }
    return {"0",BType::i64};
  }
  if (n == "vmax") {
    auto [hv,hvt]=emitArgVal(0);
    BType et = (hvt.tag==BType::Tag::dynarray)?dynArrayElem(hvt):BType::i64;
    if (et==BType::i64) { mark("ox_vec_max_i64"); std::string r=freshLocal("s"); out_<<"  "<<r<<" = call i64 @ox_vec_max_i64(i8* "<<hv<<")\n"; return {r,BType::i64}; }
    if (et==BType::f64) { mark("ox_vec_max_f64"); std::string r=freshLocal("s"); out_<<"  "<<r<<" = call double @ox_vec_max_f64(i8* "<<hv<<")\n"; return {r,BType::f64}; }
    return {"0",BType::i64};
  }
  // ---- map / set helpers ----
  if (n == "map_delete" || n == "hmap_delete") { auto [h,ht]=emitArgVal(0); auto [k,kt]=emitArgVal(1);
    bool hm = (ht.tag==BType::Tag::hmap_);
    mark(hm?"ox_hmap_delete":"ox_map_delete"); if(hm) usedHMap_=true; else usedMap_=true;
    BType keyT = mapKeyType(ht);
    std::string kp = spillScratch(genCoerce(k, kt, keyT), keyT);
    out_<<"  call void @ox_"<<(hm?"hmap_delete":"map_delete")<<"(i8* "<<h<<", i8* "<<kp<<")\n"; return {"",BType::void_}; }
  // ---- time + random ----
  if (n == "seed")         { auto [v,vt]=emitArgVal(0); mark("ox_seed"); out_<<"  call void @ox_seed(i64 "<<genCoerce(v,vt,BType::i64)<<")\n"; return {"",BType::void_}; }
  if (n == "rand")         { mark("ox_rand"); std::string r=freshLocal("r"); out_<<"  "<<r<<" = call i64 @ox_rand()\n"; return {r,BType::i64}; }
  if (n == "rand_range")   { auto [a,at]=emitArgVal(0); auto [b,bt]=emitArgVal(1); mark("ox_rand_range");
    std::string r=freshLocal("r"); out_<<"  "<<r<<" = call i64 @ox_rand_range(i64 "<<genCoerce(a,at,BType::i64)<<", i64 "<<genCoerce(b,bt,BType::i64)<<")\n"; return {r,BType::i64}; }
  if (n == "time_ns")      { mark("ox_time_ns"); std::string r=freshLocal("t"); out_<<"  "<<r<<" = call i64 @ox_time_ns()\n"; return {r,BType::i64}; }
  if (n == "clock_ms")     { mark("ox_clock_ms"); std::string r=freshLocal("t"); out_<<"  "<<r<<" = call i64 @ox_clock_ms()\n"; return {r,BType::i64}; }
  if (n == "time_epoch")   { mark("ox_time_epoch"); std::string r=freshLocal("t"); out_<<"  "<<r<<" = call i64 @ox_time_epoch()\n"; return {r,BType::i64}; }

  return {"|||no|||", BType::void_};
}

void IRGen::beginBlock(const std::string& name) {
  out_ << name << ":\n";
  curBlock_ = name;
  terminated_ = false;
}

void IRGen::branch(const std::string& cond, const std::string& t, const std::string& f) {
  if (terminated_) return;
  out_ << "  br i1 " << cond << ", label %" << t << ", label %" << f << "\n";
  terminated_ = true;
}

void IRGen::jump(const std::string& t) {
  if (terminated_) return;
  out_ << "  br label %" << t << "\n";
  terminated_ = true;
}

void IRGen::ensureTerminated() {
  if (!terminated_) { out_ << "  unreachable\n"; terminated_ = true; }
}

void IRGen::genBlock(const std::vector<StmtPtr>& stmts) {
  for (auto& s : stmts) {
    if (terminated_) return;
    genStmt(s.get());
  }
}

void IRGen::genStmt(Stmt* s) {
  if (auto es = dynamic_cast<ExprStmt*>(s)) {
    genExpr(es->expr.get());
    return;
  }
  if (auto ls = dynamic_cast<LetStmt*>(s)) {
    // ox:proof TODO IRGen skip  -  a `ghost let` (GhostLetStmt, an `isGhost` LetStmt
    // subclass) does NOT allocate/run; it's a spec-only binding visible only
    // in the SMT contract domain. We early-return BEFORE the alloca/store so
    // the runtime stack slot is never materialised. The Ghost encoder later
    // names the binding for SMT clauses that reference it. (T2)
    if (dynamic_cast<GhostLetStmt*>(s) != nullptr) {
      // ox:why Register the name with a nullable alloca placeholder so that any later
      // accidental runtime reference (which Sema should have rejected, but be
      // defensive) doesn't crash IRGen's lookup; mark it as a ghost slot.
      declareVar(ls->name, /*alloca=*/"ghost", ls->type);
      return;
    }
    std::string a = "%var_" + ls->name + "_" + std::to_string(labelSeq_++);
    // Matrix-handle binding: when the initializer evaluates to a matrix
    // handle (MatrixLit, SolveExpr, MatMulExpr, or BinaryExpr w/ isMatMul),
    // genExpr returns {i8*, ptr(void_)}  -  NOT a stack array.  In that case
    // alloca an i8* slot and store the handle directly, and declare the
    // variable with ptr(void_) type so VarRef loads the handle back.
    bool matrixSlot = false;
    if (ls->init) {
      // Peek: if the declared type is a 2D array and init is one of the
      // matrix-producing expr kinds, we need the special slot.
      if (ls->type.tag == BType::Tag::array &&
          arrayElem(ls->type).tag == BType::Tag::array) {
        auto mt = dynamic_cast<MatrixLit*>(ls->init.get());
        auto st = dynamic_cast<SolveExpr*>(ls->init.get());
        auto mmt = dynamic_cast<MatMulExpr*>(ls->init.get());
        auto be = dynamic_cast<BinaryExpr*>(ls->init.get());
        auto al = dynamic_cast<ArrayLit*>(ls->init.get());
        // 2D nested ArrayLit: [[1,0],[0,1]]  -  first element is itself an ArrayLit
        if (al && !al->elems.empty() && dynamic_cast<ArrayLit*>(al->elems[0].get())) matrixSlot = true;
        if ((be && be->isMatMul) || mt || st || mmt) matrixSlot = true;
      }
    }
    std::string movedRoot;
    if (matrixSlot) {
      out_ << "  " << a << " = alloca i8*\n";
      auto [v, vt] = genExpr(ls->init.get());
      out_ << "  store i8* " << v << ", i8** " << a << "\n";
      declareVar(ls->name, a, makePtr(BType::i8));
      return;
    }
    out_ << "  " << a << " = alloca " << typeStr(ls->type) << "\n";
    if (ls->init) {
      auto [v, vt] = genExpr(ls->init.get());
      std::string cv = genCoerce(v, vt, ls->type);
      out_ << "  store " << typeStr(ls->type) << " " << cv << ", " << typeStr(ls->type) << "* " << a << "\n";
      // `let b = a` where a is a move-only struct: this is a MOVE. The source
      // local `a` is consumed (its slot now holds dead bytes the new owner has
      // copied out); stop dropping `a` at its scope exit to avoid a double-drop
      // (the new binding `b` owns and drops the value).
      if (ls->type.tag == BType::Tag::struct_ &&
          isMoveOnlyStruct(ls->type.structName)) {
        movedRoot = genMoveRootVar(ls->init.get());
        if (!movedRoot.empty() && movedRoot != ls->name) markMovedOut(movedRoot);
      }
    }
    declareVar(ls->name, a, ls->type);
    return;
  }
  if (auto rs = dynamic_cast<ReturnStmt*>(s)) {
    std::string movedRoot;
    if (rs->value) {
      // Evaluate the return value into the return slot FIRST (it may read the
      // very locals about to be dropped  -  including the moved root itself),
      // then run destructors for every enclosing scope (skip the moved root:
      // ownership transferred to the caller). Only a whole-aggregate move of a
      // hasDrop struct (return value's type IS the struct) transfers ownership
      //  -  returning a *sub-component* (a field `b.tag` or an element) merely
      // READS the aggregate, so the aggregate must still be dropped. Suppressing
      // it in that case would LEAK the destructor (return b.tag dropped nothing).
      auto [v, vt] = genExpr(rs->value.get());
      bool wholeMove = vt.tag == BType::Tag::struct_ && isMoveOnlyStruct(vt.structName);
      if (wholeMove) {
        movedRoot = genMoveRootVar(rs->value.get());
        if (!movedRoot.empty()) suppressDrop_.insert(movedRoot);
      }
      // In ensures exit-block mode, store the coerced return value into the per-
      // function result slot (so the exit block's ensures gates and final ret
      // see it), unwind destructors, and branch to the exit block instead of an
      // inline ret. The ensures gates run in the exit block ONCE before the ret.
      if (!curExitBlock_.empty()) {
        std::string cv = genCoerce(v, vt, curFnRet_);
        out_ << "  store " << typeStr(curFnRet_) << " " << cv << ", " << typeStr(curFnRet_)
             << "* " << curResultSlot_ << "\n";
        emitScopeUnwind(0);   // drop all scopes, innermost..function scope
        suppressDrop_.clear();
        jump(curExitBlock_);
      } else {
        emitScopeUnwind(0);   // drop all scopes, innermost..function scope
        suppressDrop_.clear();
        std::string cv = genCoerce(v, vt, curFnRet_);
        out_ << "  ret " << typeStr(curFnRet_) << " " << cv << "\n";
      }
    } else {
      emitScopeUnwind(0);
      if (!curExitBlock_.empty()) jump(curExitBlock_);
      else out_ << "  ret void\n";
    }
    terminated_ = true;
    return;
  }
  if (auto is = dynamic_cast<IfStmt*>(s)) {
    auto [c, ct] = genExpr(is->cond.get());

    (void)ct;
    std::string thenBB = freshLabel("then");
    std::string elseBB = freshLabel("else");
    std::string mergeBB = freshLabel("merge");

    branch(c, thenBB, elseBB);
    beginBlock(thenBB);
    pushScope(); genBlock(is->then); popScopeWithDrops();
    jump(mergeBB);
    beginBlock(elseBB);
    if (!is->else_.empty()) {
      pushScope(); genBlock(is->else_); popScopeWithDrops();
      jump(mergeBB);
    } else {
      jump(mergeBB);
    }
    beginBlock(mergeBB);
    return;
  }
  if (auto ws = dynamic_cast<WhileStmt*>(s)) {
    std::string condBB = freshLabel("while_cond");
    std::string bodyBB = freshLabel("while_body");
    std::string endBB = freshLabel("while_end");
    jump(condBB);
    beginBlock(condBB);
    auto [c, ct] = genExpr(ws->cond.get());
    branch(c, bodyBB, endBB);
    beginBlock(bodyBB);
    loops_.push_back({condBB, endBB, scopeDropVars_.size()});
    pushScope();
    // Loop invariant gates: checked at the loop head (each iteration, before the
    // body), so an invariant that fails traps here. They run in the loop body
    // scope so the invariants can reference any locals bound for the body.
    for (auto& inv : ws->invariants)
      genContractGate(inv.get(), CT_INVARIANT, inv ? inv->line : s->line);
    genBlock(ws->body);
    popScopeWithDrops();
    loops_.pop_back();
    jump(condBB);
    beginBlock(endBB);
    return;
  }
  if (auto fs = dynamic_cast<ForStmt*>(s)) {
    if (fs->isForeach) {
      // Map iteration `for k in map` / `for k, v in map`. Walk entries by index
      // via the runtime: key pointer from @ox_map_key_ptr, and for the two-var
      // form the value via @ox_map_get into a scratch slot. (arr/vec/str/range
      // handled below.) Mirrors the per-entry load used by `print` / `map_keys`.
      if (fs->isMapIter) {
        BType keyT = fs->elemType;
        BType valT = fs->elemType2;   // void_ in the single-var form
        bool hm = (fs->iterType.tag == BType::Tag::hmap_);
        if (hm) usedHMap_ = true; else usedMap_ = true;
        const char* rt_len  = hm ? "ox_hmap_len"     : "ox_map_len";
        const char* rt_kp   = hm ? "ox_hmap_key_ptr" : "ox_map_key_ptr";
        const char* rt_get  = hm ? "ox_hmap_get"     : "ox_map_get";
        collectStruct(keyT);
        if (!fs->varName2.empty()) collectStruct(valT);
        auto [mapVal, mapT] = genExpr(fs->iter.get());
        (void)mapT;   // a map lowers to an opaque i8* handle

        pushScope();

        // Loop variables: k bound to each key, v (if present) to its value.
        std::string keySlot = "%var_" + fs->varName + "_" + std::to_string(labelSeq_++);
        out_ << "  " << keySlot << " = alloca " << typeStr(keyT) << "\n";
        out_ << "  store " << typeStr(keyT) << " " << zeroVal(keyT) << ", " << typeStr(keyT)
             << "* " << keySlot << "\n";
        declareVar(fs->varName, keySlot, keyT);

        std::string valSlot;
        if (!fs->varName2.empty()) {
          valSlot = "%var_" + fs->varName2 + "_" + std::to_string(labelSeq_++);
          out_ << "  " << valSlot << " = alloca " << typeStr(valT) << "\n";
          out_ << "  store " << typeStr(valT) << " " << zeroVal(valT) << ", " << typeStr(valT)
               << "* " << valSlot << "\n";
          declareVar(fs->varName2, valSlot, valT);
        }

        // Hidden index counter; len read each cond (cheap runtime call) so map
        // insertions during iteration are bounded by the live count.
        std::string idxSlot = "%fm_" + fs->varName + "_" + std::to_string(labelSeq_++);
        out_ << "  " << idxSlot << " = alloca i64\n";
        out_ << "  store i64 0, i64* " << idxSlot << "\n";

        std::string condBB = freshLabel("fmc");
        std::string bodyBB = freshLabel("fmb");
        std::string stepBB = freshLabel("fms");
        std::string endBB = freshLabel("fme");
        jump(condBB);
        beginBlock(condBB);
        {
          std::string i = freshLocal("fmci");
          out_ << "  " << i << " = load i64, i64* " << idxSlot << "\n";
          std::string lenV = freshLocal("fmcl");
          out_ << "  " << lenV << " = call i64 @" << rt_len << "(i8* " << mapVal << ")\n";
          std::string cmp = freshLocal("fmcc");
          out_ << "  " << cmp << " = icmp slt i64 " << i << ", " << lenV << "\n";
          branch(cmp, bodyBB, endBB);
        }
        beginBlock(bodyBB);
        loops_.push_back({stepBB, endBB, scopeDropVars_.size()});
        {
          std::string i = freshLocal("fmbi");
          out_ << "  " << i << " = load i64, i64* " << idxSlot << "\n";
          std::string kp = freshLocal("fmkp");
          out_ << "  " << kp << " = call i8* @" << rt_kp << "(i8* " << mapVal
               << ", i64 " << i << ")\n";
          // load key into the loop variable slot
          std::string kv = loadScratch(kp, keyT);
          out_ << "  store " << typeStr(keyT) << " " << kv << ", " << typeStr(keyT)
               << "* " << keySlot << "\n";
          // load value via @ox_map_get(key ptr) into v's slot
          if (!fs->varName2.empty()) {
            std::string vptr = freshLocal("fmvp");
            out_ << "  " << vptr << " = bitcast " << typeStr(valT) << "* " << valSlot
                 << " to i8*\n";
            out_ << "  call i64 @" << rt_get << "(i8* " << mapVal << ", i8* " << kp
                 << ", i8* " << vptr << ")\n";
          }
        }
        pushScope();
        for (auto& inv : fs->invariants)
          genContractGate(inv.get(), CT_INVARIANT, inv ? inv->line : s->line);
        genBlock(fs->body);
        popScopeWithDrops();
        loops_.pop_back();
        jump(stepBB);
        beginBlock(stepBB);
        {
          std::string i = freshLocal("fmsi");
          out_ << "  " << i << " = load i64, i64* " << idxSlot << "\n";
          std::string ni = freshLocal("fmsn");
          out_ << "  " << ni << " = add i64 " << i << ", 1\n";
          out_ << "  store i64 " << ni << ", i64* " << idxSlot << "\n";
        }
        jump(condBB);
        beginBlock(endBB);
        popScopeWithDrops();
        return;
      }
      // Integer range iterable `for x in lo..hi` / `lo..=hi`. Iterate by an
      // integer counter (no array spill, no bounds call): element value = lo+idx,
      // continue while lo+idx < hi (exclusive) or <= hi (inclusive). lo/hi are
      // evaluated ONCE before the loop in declaration order, matching the
      // array/vec semantics where the bound is fixed up-front.
      if (auto rng = dynamic_cast<RangeLit*>(fs->iter.get())) {
        auto [loV, loT] = genExpr(rng->lo.get());
        auto [hiV, hiT] = genExpr(rng->hi.get());
        std::string lo = genCoerce(loV, loT, BType::i64);
        std::string hi = genCoerce(hiV, hiT, BType::i64);

        pushScope();

        // Loop variable = the element value (lo + idx), updated each step.
        std::string eltSlot = "%var_" + fs->varName + "_" + std::to_string(labelSeq_++);
        out_ << "  " << eltSlot << " = alloca i64\n";
        declareVar(fs->varName, eltSlot, BType::i64);

        // ox:why idx is a hidden counter; seed lo into the loop variable up front so the
        // cond block can branch before the body stores a fresh element.
        std::string idxSlot = "%fr_" + fs->varName + "_" + std::to_string(labelSeq_++);
        out_ << "  " << idxSlot << " = alloca i64\n";
        out_ << "  store i64 0, i64* " << idxSlot << "\n";

        std::string condBB = freshLabel("fr_cond");
        std::string bodyBB = freshLabel("fr_body");
        std::string stepBB = freshLabel("fr_step");
        std::string endBB = freshLabel("fr_end");
        jump(condBB);
        beginBlock(condBB);
        {
          std::string i = freshLocal("fri");
          out_ << "  " << i << " = load i64, i64* " << idxSlot << "\n";
          std::string cur = freshLocal("frv");
          out_ << "  " << cur << " = add i64 " << lo << ", " << i << "\n";   // element value
          std::string cmp = freshLocal("frc");
          if (rng->inclusive)
            out_ << "  " << cmp << " = icmp sle i64 " << cur << ", " << hi << "\n";
          else
            out_ << "  " << cmp << " = icmp slt i64 " << cur << ", " << hi << "\n";
          // ox:why Stash the element into the loop var BEFORE branching so the body and
          // the fall-through state agree (the body reads the var, not recomputed).
          out_ << "  store i64 " << cur << ", i64* " << eltSlot << "\n";
          branch(cmp, bodyBB, endBB);
        }
        beginBlock(bodyBB);
        loops_.push_back({stepBB, endBB, scopeDropVars_.size()});
        pushScope();
        for (auto& inv : fs->invariants)
          genContractGate(inv.get(), CT_INVARIANT, inv ? inv->line : s->line);
        genBlock(fs->body);
        popScopeWithDrops();
        loops_.pop_back();
        jump(stepBB);
        beginBlock(stepBB);
        {
          std::string i = freshLocal("fri2");
          out_ << "  " << i << " = load i64, i64* " << idxSlot << "\n";
          std::string ni = freshLocal("frn");
          out_ << "  " << ni << " = add i64 " << i << ", 1\n";
          out_ << "  store i64 " << ni << ", i64* " << idxSlot << "\n";
        }
        jump(condBB);
        beginBlock(endBB);
        popScopeWithDrops();
        return;
      }




      BType et = fs->elemType;


      auto [iterV, iterT] = genExpr(fs->iter.get());

      pushScope();

      std::string idxSlot = "%fi_" + fs->varName + "_" + std::to_string(labelSeq_++);
      out_ << "  " << idxSlot << " = alloca i64\n";
      out_ << "  store i64 0, i64* " << idxSlot << "\n";

      std::string eltSlot = "%var_" + fs->varName + "_" + std::to_string(labelSeq_++);
      out_ << "  " << eltSlot << " = alloca " << typeStr(et) << "\n";
      declareVar(fs->varName, eltSlot, et);


      std::string basePtr;
      std::string vecHandle = iterV;
      bool isVec = (iterT.tag == BType::Tag::dynarray);
      bool isStr = (iterT == BType::str);
      int32_t arrLen = 0;
      if (isStr) {
        basePtr = iterV;
      } else if (!isVec) {

        arrLen = iterT.count;
        std::string spill = "%fa_" + fs->varName + "_" + std::to_string(labelSeq_++);
        out_ << "  " << spill << " = alloca " << typeStr(iterT) << "\n";
        out_ << "  store " << typeStr(iterT) << " " << iterV << ", " << typeStr(iterT) << "* " << spill << "\n";
        basePtr = spill;
      } else {
        BType ve = dynArrayElem(iterT);
        std::string sx = elemSuffix(ve);
        if (!sx.empty()) usedVec_.insert(sx); else usedVec_blob_ = true;
      }

      std::string condBB = freshLabel("fe_cond");
      std::string bodyBB = freshLabel("fe_body");
      std::string stepBB = freshLabel("fe_step");
      std::string endBB = freshLabel("fe_end");
      jump(condBB);
      beginBlock(condBB);
      {
        std::string i = freshLocal("fi");
        out_ << "  " << i << " = load i64, i64* " << idxSlot << "\n";
        std::string lenV;
        if (isVec) {
          lenV = freshLocal("fvl");
          out_ << "  " << lenV << " = call i64 @ox_vec_len(i8* " << vecHandle << ")\n";
        } else if (isStr) {
          lenV = freshLocal("fsl");
          out_ << "  " << lenV << " = call i64 @ox_strlen(i8* " << basePtr << ")\n";
        } else {
          lenV = std::to_string(arrLen);
        }
        std::string cmp = freshLocal("fc");
        out_ << "  " << cmp << " = icmp slt i64 " << i << ", " << lenV << "\n";
        branch(cmp, bodyBB, endBB);
      }
      beginBlock(bodyBB);
      loops_.push_back({stepBB, endBB, scopeDropVars_.size()});

      {
        std::string i = freshLocal("fi2");
        out_ << "  " << i << " = load i64, i64* " << idxSlot << "\n";
        std::string ev;
        if (isVec) {
          BType ve = dynArrayElem(iterT);
          std::string sx = elemSuffix(ve);
          if (!sx.empty()) {
            std::string slot = vecSlotType(sx);
            std::string raw = freshLocal("fg");
            out_ << "  " << raw << " = call " << slot << " @ox_vec_get_" << sx
                 << "(i8* " << vecHandle << ", i64 " << i << ")\n";
            ev = genCoerce(raw, vecSlotBType(sx), et);
          } else {

            int32_t esz = fieldByteWidth(et);
            std::string ep = freshLocal("fpb");
            out_ << "  " << ep << " = call i8* @ox_vec_blob_ptr(i8* " << vecHandle
                 << ", i64 " << i << ", i64 " << (esz > 0 ? esz : 8) << ")\n";
            if (et.tag == BType::Tag::array || et.tag == BType::Tag::struct_) {

              out_ << "  call void @llvm.memcpy.p0i8.p0i8.i64(i8* " << eltSlot
                   << ", i8* " << ep << ", i64 " << (esz > 0 ? esz : 8)
                   << ", i1 false)\n";
              ev = "";
            } else {
              std::string ev2 = freshLocal("fbl");
              if (et.tag == BType::Tag::dynarray || et.tag == BType::Tag::ptr)
                out_ << "  " << ev2 << " = load " << typeStr(et) << ", i8* " << ep << "\n";
              else {
                std::string tp = freshLocal("fbtc");
                out_ << "  " << tp << " = bitcast i8* " << ep << " to " << typeStr(et) << "*\n";
                out_ << "  " << ev2 << " = load " << typeStr(et) << ", " << typeStr(et) << "* " << tp << "\n";
              }
              ev = ev2;
            }
          }
        } else if (isStr) {

          std::string ep = freshLocal("fep");
          out_ << "  " << ep << " = getelementptr inbounds i8, i8* " << basePtr << ", i64 " << i << "\n";
          ev = freshLocal("fl");
          out_ << "  " << ev << " = load i8, i8* " << ep << "\n";
        } else {
          std::string ep = freshLocal("fep");
          out_ << "  " << ep << " = getelementptr inbounds " << typeStr(iterT) << ", "
               << typeStr(iterT) << "* " << basePtr << ", i64 0, i64 " << i << "\n";
          ev = freshLocal("fl");
          out_ << "  " << ev << " = load " << typeStr(et) << ", " << typeStr(et) << "* " << ep << "\n";
        }
        if (!ev.empty())
          out_ << "  store " << typeStr(et) << " " << ev << ", " << typeStr(et) << "* " << eltSlot << "\n";
      }
      pushScope();
      for (auto& inv : fs->invariants)
        genContractGate(inv.get(), CT_INVARIANT, inv ? inv->line : s->line);
      genBlock(fs->body);
      popScopeWithDrops();
      loops_.pop_back();
      jump(stepBB);
      beginBlock(stepBB);
      {
        std::string i = freshLocal("fi3");
        out_ << "  " << i << " = load i64, i64* " << idxSlot << "\n";
        std::string ni = freshLocal("fin");
        out_ << "  " << ni << " = add i64 " << i << ", 1\n";
        out_ << "  store i64 " << ni << ", i64* " << idxSlot << "\n";
      }
      jump(condBB);
      beginBlock(endBB);
      popScopeWithDrops();
      return;
    }

    std::string condBB = freshLabel("for_cond");
    std::string bodyBB = freshLabel("for_body");
    std::string stepBB = freshLabel("for_step");
    std::string endBB = freshLabel("for_end");


    pushScope();
    std::string loopVar = "%var_" + fs->varName + "_" + std::to_string(labelSeq_++);
    out_ << "  " << loopVar << " = alloca i64\n";
    auto [startV, startT] = genExpr(fs->start.get());
    out_ << "  store i64 " << startV << ", i64* " << loopVar << "\n";
    declareVar(fs->varName, loopVar, BType::i64);

    jump(condBB);
    beginBlock(condBB);

    if (fs->end) {
      auto [condV, condT] = genExpr(fs->end.get());
      branch(condV, bodyBB, endBB);
    } else {
      branch("true", bodyBB, endBB);
    }
    beginBlock(bodyBB);
    loops_.push_back({stepBB, endBB, scopeDropVars_.size()});
    pushScope();
    for (auto& inv : fs->invariants)
      genContractGate(inv.get(), CT_INVARIANT, inv ? inv->line : s->line);
    genBlock(fs->body);
    popScopeWithDrops();
    loops_.pop_back();
    jump(stepBB);
    beginBlock(stepBB);

    if (fs->step) {
      genExpr(fs->step.get());
    }
    jump(condBB);
    beginBlock(endBB);
    popScopeWithDrops();
    return;
  }
  if (dynamic_cast<BreakStmt*>(s)) {
    if (!loops_.empty()) {
      // Run destructors for every scope from here down to and including the
      // loop's body scope (those locals die with this iteration), then jump.
      emitScopeUnwind(loops_.back().bodyScope);
      jump(loops_.back().brk);
    }
    return;
  }
  if (dynamic_cast<ContinueStmt*>(s)) {
    if (!loops_.empty()) {
      emitScopeUnwind(loops_.back().bodyScope);
      jump(loops_.back().cont);
    }
    return;
  }
  if (auto b = dynamic_cast<Block*>(s)) {
    pushScope(); genBlock(b->stmts); popScopeWithDrops();
    return;
  }
  if (auto sb = dynamic_cast<SyncBlock*>(s)) {
    // `sync { ... }`  -  wrap the body with @ox_sync_begin / @ox_sync_end so
    // concurrent `spawn`ed threads may rendezvous before/from the block. The
    // inner statements run in a fresh runtime scope (their own `let` slots and
    // RAII drops), exactly like a plain Block  -  the only difference is the two
    // runtime calls straddling the body.
    usedSync_ = true;
    out_ << "  call void @ox_sync_begin()\n";
    pushScope(); genBlock(sb->body); popScopeWithDrops();
    out_ << "  call void @ox_sync_end()\n";
    return;
  }
  if (auto a = dynamic_cast<AssertStmt*>(s)) {
    // ox:unsafe A runtime contract checkpoint: trap if the boolean condition is false.
    // A quantifier inside the assert is lowered (by genExpr) into a bounded
    // runtime loop computing the boolean, then the gate traps on false.
    genContractGate(a->cond.get(), CT_ASSERT, s->line);
    // ox:proof `assert <expr> by { <hints> };`  -  the byBody hints are SMT-only proof
    // statements (instantiate / lemma-call / nested assert / calc). They have
    // NO runtime representation, so we do not codegen them here. Each hint's
    // own IRGen arm (InstantiateStmt / ProofStmt / ProofBlockStmt / CalcStmt
    // / ExprStmt-for-lemma-call) returns no-op; skipping them entirely here
    // matches the existing contract for ghost-proof statements.
    return;
  }
  if (auto ds = dynamic_cast<DeferStmt*>(s)) {
    // Don't emit the body now  -  schedule it to run at the enclosing scope's exit
    // (LIFO with the RAII drops), on every exit path. emitScopeDrops generates
    // it inline, in this scope's position, when the scope is destroyed.
    if (ds->body)
      scopeDropVars_.back().push_back({DropEntry::Defer, "", ds->body.get()});
    return;
  }
  if (dynamic_cast<InstantiateStmt*>(s)) {
    // ox:proof fixB  -  `instantiate` pragma is a SMT-only ghost statement that guides Z3
    // quantifier instantiation. It has NO runtime representation: no LLVM IR is
    // emitted, no value is computed, no scope is entered. Sema has already
    // type-checked its quantifier / witness / pattern terms; the only consumer
    // of the pragma is the SMT emitter (Driver.cpp's smtEncodeStmt arm). Just
    // return  -  generating nothing here is the entire codegen contract.
    return;
  }
  if (dynamic_cast<ProofStmt*>(s)) {
    // ox:proof Fix C  -  `proof` pragma is a SMT-only ghost statement that encodes an
    // induction proof for the SMT emitter (Driver.cpp's smtEncodeStmt ProofStmt
    // arm). It has NO runtime representation: no LLVM IR, no value, no scope.
    // Sema has already type-checked its theorem / baseCase / ih / goal; the only
    // consumer is the SMT emitter. Just return  -  generating nothing is the
    // entire codegen contract (same as InstantiateStmt above).
    return;
  }
  if (dynamic_cast<ProofBlockStmt*>(s)) {
    // ox:proof Lemma-functions brace form  -  `proof { <stmts> }`. A SMT-only ghost block
    // (the SMT encoder assumes lemma ensures for each call + discharge/asserts);
    // it has NO runtime representation (no LLVM IR, no scope). Sema has already
    // type-checked the body; the only consumer is the SMT emitter. Just return.
    return;
  }
  if (dynamic_cast<AssumeStmt*>(s)) {
    // ox:proof `assume <expr>;` (non-trusted) and `trusted assume <expr>;`  -  a SMT-only
    // ghost statement that adds a hypothesis premise for the SMT emitter. It
    // has NO runtime representation: no LLVM IR is emitted, no value is
    // computed, no scope is entered (a hypothesis is an SMT assertion, not a
    // runtime gate  -  unlike `assert`, which traps on a false condition). Sema
    // has already type-checked the condition (boolean); the only consumer of
    // the assume is the SMT emitter (Driver.cpp's smtEncodeStmt assumeStmt
    // arm), which emits `(assert <cond>)` so the fact becomes a premise. A
    // `trusted` assume is additionally recorded in the trust audit. Just
    // return  -  generating nothing here is the entire codegen contract (same as
    // InstantiateStmt / ProofStmt / ProofBlockStmt above).
    return;
  }
  if (dynamic_cast<CalcStmt*>(s)) {
    // calcD  -  `calc { <expr>; <REL> {hints;} <expr>; ... }` equational-reasoning
    // block. A SMT-only ghost statement that encodes a chain of discharge
    // queries (one per step pair) for the SMT emitter (Driver.cpp's
    // smtEncodeStmt calcStmt arm). It has NO runtime representation: no LLVM IR,
    // no value, no scope. Sema has already type-checked each step expression +
    // hint statement; the only consumer is the SMT emitter. Just return  - 
    // generating nothing is the entire codegen contract (same as InstantiateStmt
    // / ProofStmt / ProofBlockStmt above).
    return;
  }
}

void IRGen::genFunc(FuncDecl& fn) {
  labelSeq_ = 0;
  terminated_ = false;
  curFnRet_ = fn.retType;

  // Reset per-function contract state. `curExitBlock_` is set non-empty only
  // when this function has at least one `ensures` clause; then returns rewrite
  // to store+br-exit and the exit block runs the ensures gates before the ret.
  curExitBlock_.clear();
  curResultSlot_.clear();
  oldSnapshots_.clear();
  inEnsuresGate_ = false;
  bool hasEnsures = !fn.ensures_.empty();
  bool hasRequires = !fn.requires_.empty();

  std::ostringstream sig;
  // `export fn` emits `define dso_local` so C can link against the symbol
  // (a plain `define` is internal-only; `dso_local` forces local relocation
  // so the symbol remains visible to the linker). Non-export fns stay `define`.
  sig << (fn.isExport ? "define dso_local " : "define ")
      << typeStr(fn.retType) << " @" << fn.name << "(";
  for (size_t i = 0; i < fn.params.size(); i++) {
    if (i) sig << ", ";
    sig << typeStr(fn.params[i].type) << " %arg_" << fn.params[i].name;
  }
  sig << ") {\n";
  out_ << sig.str();

  scopes_.clear();
  scopeDropVars_.clear();
  pushScope();

  std::string entry = freshLabel("entry");
  out_ << entry << ":\n";
  curBlock_ = entry;


  for (auto& kv : sema_.globals)
    declareVar(kv.first, "@" + kv.first, kv.second.type);

  for (auto& p : fn.params) {
    std::string a = "%var_" + p.name + "_" + std::to_string(labelSeq_++);
    out_ << "  " << a << " = alloca " << typeStr(p.type) << "\n";
    out_ << "  store " << typeStr(p.type) << " %arg_" << p.name << ", " << typeStr(p.type) << "* " << a << "\n";
    declareVar(p.name, a, p.type);
  }

  // If this function has `ensures`, set up the exit-block machinery:
  //  - allocate a result slot of the return type (initialised to zeroval) that
  //    every `return e` stores its coerced value into before branching to exit;
  //  - snapshot every `old(<bareName>)` mention: an entry-time alloca loaded
  //    from the live var, so ensures-time `old(x)` reads the pre-state.
  if (hasEnsures && fn.retType != BType::void_) {
    curResultSlot_ = "%ox_result_" + std::to_string(labelSeq_++);
    out_ << "  " << curResultSlot_ << " = alloca " << typeStr(fn.retType) << "\n";
    out_ << "  store " << typeStr(fn.retType) << " " << zeroVal(fn.retType)
         << ", " << typeStr(fn.retType) << "* " << curResultSlot_ << "\n";
  }
  // Collect the set of bare names referenced via `old(x)` across all ensures
  // clauses and snapshot each at entry (after its alloca above).
  if (hasEnsures) {
    std::set<std::string> oldNames;
    std::function<void(Expr*)> walk = [&](Expr* e) {
      if (!e) return;
      if (auto o = dynamic_cast<OldExpr*>(e)) {
        if (auto vr = dynamic_cast<VarRef*>(o->sub.get()))
          oldNames.insert(vr->name);
        walk(o->sub.get());
        return;
      }
      if (auto b = dynamic_cast<BinaryExpr*>(e)) { walk(b->lhs.get()); walk(b->rhs.get()); return; }
      if (auto u = dynamic_cast<UnaryExpr*>(e)) { walk(u->base.get()); return; }
      if (auto t = dynamic_cast<TernaryExpr*>(e)) { walk(t->cond.get()); walk(t->thenE.get()); walk(t->elseE.get()); return; }
      if (auto ix = dynamic_cast<Index*>(e)) { walk(ix->base.get()); walk(ix->index.get()); return; }
      if (auto f = dynamic_cast<Field*>(e)) { walk(f->base.get()); return; }
      if (auto c = dynamic_cast<Call*>(e)) { for (auto& a : c->args) walk(a.get()); walk(c->calleeExpr.get()); return; }
      if (auto mc = dynamic_cast<MethodCall*>(e)) { walk(mc->receiver.get()); for (auto& a : mc->args) walk(a.get()); return; }
      if (auto ac = dynamic_cast<AssocCall*>(e)) { for (auto& a : ac->args) walk(a.get()); return; }
      // A MacroCall's `old()` references live in the Sema-expanded tree, so
      // descend into `expanded` (the substituted body) to find them.
      if (auto mx = dynamic_cast<MacroCall*>(e)) { walk(mx->expanded.get()); for (auto& a : mx->args) walk(a.get()); return; }
      if (auto ce = dynamic_cast<CastExpr*>(e)) { walk(ce->e.get()); return; }
      if (auto qt = dynamic_cast<QuantExpr*>(e)) { walk(qt->lo.get()); walk(qt->hi.get()); walk(qt->body.get()); return; }
    };
    for (auto& e : fn.ensures_) walk(e.get());
    for (const auto& name : oldNames) {
      auto [store, t] = findVar(name);
      if (store.empty()) continue;   // unresolvable name -> Sema already flagged
      std::string snap = "%ox_old_" + name + "_" + std::to_string(labelSeq_++);
      out_ << "  " << snap << " = alloca " << typeStr(t) << "\n";
      std::string lv = tmp();
      out_ << "  " << lv << " = load " << typeStr(t) << ", " << typeStr(t) << "* " << store << "\n";
      out_ << "  store " << typeStr(t) << " " << lv << ", " << typeStr(t) << "* " << snap << "\n";
      oldSnapshots_[name] = {snap, t};
    }
    curExitBlock_ = freshLabel("ox_exit");
  }

  // `requires` gates run at function entry, after the param allocas (so they may
  // reference the params). A false requires traps immediately.
  for (auto& r : fn.requires_)
    genContractGate(r.get(), CT_REQUIRES, r ? r->line : fn.line);

  genBlock(fn.body);
  // Decide WHERE scope-0 ends. Normally scope-0 (the function's param scope) is
  // dropped+pop- ped AFTER the body so the fall-through path runs its dtors.
  // BUT when this fn has `ensures`, the exit block below (beginBlock curExitBlock_;
  // genContractGate per ensures) still needs scope-0's param/old-snapshot bindings
  // so genExpr(gateExpr) resolves VarRefs to the LIVE param allocas. If scope-0
  // is popped here, findVar() returns {} for the params and genExpr returns the
  // "0"-placeholder fallback  -  so an ensures gate like `result == x` is lowered
  // at runtime as `result == 0`, aborting spuriously for every positive example
  // whose static return-site WP goal is provably `unsat`.
  //
  // Fix: split emitScopeDrops (which only writes dtor IR inline  -  does not touch
  // scopes_) from popScope (which removes scope-0 from the stack):
	//   - non-ensures fns: the original popScopeWithDrops() (drops + pops) at the
  //     fall-through point.
	//   - ensures fns: emit scope-0 dtors inline here if the body fell off the end
  //     (no `return`: terminated_ is false, dtors not yet run); if the body hit a
  //     `return`, the return path's `emitScopeUnwind(0)` already emitted the
  //     dtors  -  so DON'T re-emit (terminated_ guard). Defer the popScope() itself
  //     to AFTER the exit block (below, after inEnsuresGate_=false).
  if (!hasEnsures) {
    popScopeWithDrops();   // drop + pop scope-0 now (no exit block follows)
  } else if (!terminated_) {
    // Body fell off the end without `return`; emit scope-0 dtors inline now at
    // the fall-through point (RAII order), then jump to the exit block below.
    // scopes_ is intentionally retained: the exit block still needs scope-0.
    if (!scopeDropVars_.empty()) emitScopeDrops(scopeDropVars_.size() - 1);
  }

  if (!hasEnsures) {
    if (!terminated_) {
      if (fn.retType == BType::void_) out_ << "  ret void\n";
      else if (fn.retType == BType::f64) out_ << "  ret double 0.0\n";
      else if (fn.retType == BType::bool_) out_ << "  ret i1 0\n";
      else if (fn.retType == BType::str) out_ << "  ret i8* null\n";
      else if (fn.retType.tag == BType::Tag::array ||
               fn.retType.tag == BType::Tag::struct_)
        out_ << "  ret " << typeStr(fn.retType) << " zeroinitializer\n";
      else out_ << "  ret i64 0\n";
      terminated_ = true;
    }
  } else {
    // Ensures exit-block mode: branch the fall-through path (no `return`) to the
    // exit block, then emit the exit block  -  run every ensures gate there (with
    // inEnsuresGate_ so `old(x)`/`result` resolve to the snapshots/slot), and
    // finally issue the single `ret` from the result slot.
    if (!terminated_) jump(curExitBlock_);
    beginBlock(curExitBlock_);
    inEnsuresGate_ = true;
    for (auto& e : fn.ensures_)
      genContractGate(e.get(), CT_ENSURES, e ? e->line : fn.line);
    inEnsuresGate_ = false;
    if (fn.retType == BType::void_) out_ << "  ret void\n";
    else {
      std::string rv = tmp();
      out_ << "  " << rv << " = load " << typeStr(fn.retType) << ", " << typeStr(fn.retType)
           << "* " << curResultSlot_ << "\n";
      out_ << "  ret " << typeStr(fn.retType) << " " << rv << "\n";
    }
    terminated_ = true;
  }
  // ox:why If this fn had `ensures`, scope-0 was deliberately left on scopes_ so the
  // exit-block ensures gates could resolve param VarRefs to live allocas (we
  // emitted the fall-through dtors inline above instead of popping). Now that
  // the exit block has been fully emitted, safe to pop scope-0  -  keeping a
  // drained scope on the stack would leak into the next genFunc (scopes_.clear
  // at next genFunc top makes the leak benign, but explicit balance is clearer).
  if (hasEnsures) popScope();
  out_ << "}\n\n";
}


void IRGen::genLambda(const LambdaLit* lam) {
  if (emittedLambdas_.count(lam->loweredName)) return;
  emittedLambdas_.insert(lam->loweredName);

  // ox:why genLambda runs in the middle of codegen for the *enclosing* function, so we
  // must save ALL generator control state and restore it on exit, exactly as
  // Sema::checkLambda saves/restores scopes_. The previous version swapped out_
  // but cleared scopes_ and never restored it, leaving the enclosing genFunc's
  // scope stack empty  -  the next declareVar() then called scopes_.back() on an
  // empty vector and crashed (use-after-free read). Swap is only safe if every
  // piece of transient state is saved and restored.
  std::ostringstream swapped;
  out_.swap(swapped);
  auto savedScopes = scopes_;
  auto savedDropVars = scopeDropVars_;
  std::string savedBlock = curBlock_;
  std::string savedFnName = curFnName_;
  int savedLabelSeq = labelSeq_;
  bool savedTerminated = terminated_;
  BType savedFnRet = curFnRet_;

  labelSeq_ = 0;
  terminated_ = false;
  curFnName_ = lam->loweredName;
  curFnRet_ = lam->retType;
  out_ << "define " << typeStr(lam->retType) << " @" << lam->loweredName << "(";
  // Leading capture parameters (by-ref are pointers, by-value are the value).
  bool first = true;
  for (size_t i = 0; i < lam->captures.size(); i++) {
    if (lam->captures[i].name.empty()) continue;
    if (!first) out_ << ", ";
    first = false;
    out_ << typeStr(lam->captureTypes[i]) << " %arg_cap_" << i;
  }
  for (size_t i = 0; i < lam->params.size(); i++) {
    if (!first) out_ << ", ";
    first = false;
    out_ << typeStr(lam->params[i].type) << " %arg_" << lam->params[i].name;
  }
  out_ << ") {\n";
  scopes_.clear();
  scopeDropVars_.clear();
  pushScope();
  std::string entry = freshLabel("entry");
  out_ << entry << ":\n";
  curBlock_ = entry;
  for (auto& kv : sema_.globals)
    declareVar(kv.first, "@" + kv.first, kv.second.type);
  // Bind captures inside the body. By-ref: bind the name straight to the passed
  // pointer storage (pointee type) so reads load through it and writes store
  // through it  -  transparent, like C++ [&, name]. By-value: alloca + store a
  // private copy the body may mutate without touching the caller's local.
  for (size_t i = 0; i < lam->captures.size(); i++) {
    if (lam->captures[i].name.empty()) continue;
    const std::string& cname = lam->captures[i].name;
    BType pt = lam->captureTypes[i];
    if (lam->captures[i].byRef) {
      declareVar(cname, "%arg_cap_" + std::to_string(i), pointee(pt));
    } else {
      std::string a = "%var_" + cname + "_" + std::to_string(labelSeq_++);
      out_ << "  " << a << " = alloca " << typeStr(pt) << "\n";
      out_ << "  store " << typeStr(pt) << " %arg_cap_" << i << ", " << typeStr(pt)
           << "* " << a << "\n";
      declareVar(cname, a, pt);
    }
  }
  for (auto& p : lam->params) {
    std::string a = "%var_" + p.name + "_" + std::to_string(labelSeq_++);
    out_ << "  " << a << " = alloca " << typeStr(p.type) << "\n";
    out_ << "  store " << typeStr(p.type) << " %arg_" << p.name << ", " << typeStr(p.type) << "* " << a << "\n";
    declareVar(p.name, a, p.type);
  }
  genBlock(lam->body);
  popScopeWithDrops();
  if (!terminated_) {
    if (lam->retType == BType::void_) out_ << "  ret void\n";
    else if (lam->retType == BType::f64) out_ << "  ret double 0.0\n";
    else if (lam->retType == BType::bool_) out_ << "  ret i1 0\n";
    else if (lam->retType == BType::str) out_ << "  ret i8* null\n";
    else if (lam->retType.tag == BType::Tag::array ||
             lam->retType.tag == BType::Tag::struct_)
      out_ << "  ret " << typeStr(lam->retType) << " zeroinitializer\n";
    else out_ << "  ret i64 0\n";
    terminated_ = true;
  }
  out_ << "}\n\n";
  lambdas_ << out_.str();
  out_.swap(swapped);
  scopes_ = std::move(savedScopes);
  scopeDropVars_ = std::move(savedDropVars);
  curBlock_ = std::move(savedBlock);
  curFnName_ = std::move(savedFnName);
  labelSeq_ = savedLabelSeq;
  terminated_ = savedTerminated;
  curFnRet_ = std::move(savedFnRet);
}

void IRGen::genMethod(const std::string& structName, FuncDecl& fn) {
  labelSeq_ = 0;
  terminated_ = false;
  curFnRet_ = fn.retType;
  std::string mangled = mangleMethod(structName, fn.name);
  BType st; st.tag = BType::Tag::struct_; st.structName = structName;
  collectStruct(st);

  std::ostringstream sig;
  sig << "define " << typeStr(fn.retType) << " @" << mangled << "(";

  bool first = true;
  if (fn.hasSelf) {
    sig << typeStr(fn.selfByRef ? makePtr(st) : st) << " %arg_self";
    first = false;
  }
  for (auto& p : fn.params) {
    if (!first) sig << ", ";
    first = false;
    sig << typeStr(p.type) << " %arg_" << p.name;
  }
  sig << ") {\n";
  out_ << sig.str();

  scopes_.clear();
  scopeDropVars_.clear();
  pushScope();
  std::string entry = freshLabel("entry");
  out_ << entry << ":\n";
  curBlock_ = entry;

  for (auto& kv : sema_.globals)
    declareVar(kv.first, "@" + kv.first, kv.second.type);


  if (fn.hasSelf) {
    if (fn.selfByRef) {
      // A borrowed receiver (&self/&mut self) is NOT owned by the method  -  its
      // lifetime is the caller's. Removing it from this scope's drop-list prevents
      // the method body's scope-exit drop from recursing (a `drop` method would
      // otherwise drop `self`, calling itself forever).
      declareVar("self", "%arg_self", st);
      excludeDrop("self");
    } else {
      // By-value `self` IS consumed by the method; ownership transferred from
      // the caller, so it IS dropped at method exit (correct  -  use-after-call is
      // a Sema move error in the caller).
      std::string a = "%var_self_" + std::to_string(labelSeq_++);
      out_ << "  " << a << " = alloca " << typeStr(st) << "\n";
      out_ << "  store " << typeStr(st) << " %arg_self, " << typeStr(st) << "* " << a << "\n";
      declareVar("self", a, st);
    }
  }

  for (auto& p : fn.params) {
    std::string a = "%var_" + p.name + "_" + std::to_string(labelSeq_++);
    out_ << "  " << a << " = alloca " << typeStr(p.type) << "\n";
    out_ << "  store " << typeStr(p.type) << " %arg_" << p.name << ", " << typeStr(p.type) << "* " << a << "\n";
    declareVar(p.name, a, p.type);
  }

  genBlock(fn.body);
  popScopeWithDrops();   // drop scope-0 locals when falling off the function end

  if (!terminated_) {
    if (fn.retType == BType::void_) out_ << "  ret void\n";
    else if (fn.retType == BType::f64) out_ << "  ret double 0.0\n";
    else if (fn.retType == BType::bool_) out_ << "  ret i1 0\n";
    else if (fn.retType == BType::str) out_ << "  ret i8* null\n";
    else if (fn.retType.tag == BType::Tag::array ||
             fn.retType.tag == BType::Tag::struct_)
      out_ << "  ret " << typeStr(fn.retType) << " zeroinitializer\n";
    else out_ << "  ret i64 0\n";
    terminated_ = true;
  }
  out_ << "}\n\n";
}

bool IRGen::isPolymorphic(const std::string& sn) const {
  for (const StructDef* d = findStruct(sn); d; d = d->base)
    if (d->hasVirtuals) return true;
  return false;
}

int IRGen::vtableFieldIndex(const StructDef* d) {
  // The __oxvt slot is at field index 0 of the baseless root and inherited by
  // derived structs via base-splice (Sema's recomputeVtableLayout puts it at
  // index 0 of the root's merged fields, which becomes index 0 of every
  // derived's merged fields too). So the vtable ptr is always at field index 0
  // for a polymorphic struct.
  (void)d;
  return 0;
}

// Emit one vtable global per polymorphic struct. The vtable is an array
// [N x i8*], N = vtableSlots.size(), each slot initialized to the bitcast of
// the MOST-DERIVED implementation's mangled symbol (resolveMethod(struct,
// slotName) finds the override or the inherited base impl). For a struct with
// no virtuals of its own (a base that is polymorphic only because a derived
// declared virtuals), N = 0 → an empty [0 x i8*] constant (never slot-indexed).
void IRGen::emitVtables() {
  for (StructDef* d : allStructDefs()) {
    if (!d->hasVirtuals) continue;
    if (d->name.rfind("__oxg_", 0) == 0) continue;   // generic instance: no own vtable
    if (emittedVtables_.count(d->name)) continue;
    emittedVtables_.insert(d->name);
    const std::string g = vtableGlobalSym(d->name);
    // Ensure the struct + the implStruct of each slot's owning method each get
    // a %struct.T header line (collectStruct keys structDefs_ which the header
    // loop emits). Bases are reachable as fields but polymorphic implStructs of
    // an inherited virtual may not otherwise be collected.
    BType st; st.tag = BType::Tag::struct_; st.structName = d->name;
    collectStruct(st);
    size_t n = d->vtableSlots.size();
    if (n == 0) {
      // Empty vtable (a polymorphic-by-propagation root that declares no
      // virtuals of its own). The address is still stored into derived
      // instances' __oxvt slot if such a root is ever constructed standalone,
      // but it is never slot-indexed (no virtuals to dispatch).
      out_ << "@__oxvt_" << d->name
           << " = private constant [0 x i8*] zeroinitializer\n";
      continue;
    }
    out_ << "@__oxvt_" << d->name << " = private constant [" << n
         << " x i8*] [ ";
    for (size_t i = 0; i < n; i++) {
      if (i) out_ << ", ";
      const std::string& slotName = d->vtableSlots[i];
      const MethodInfo* mi = sema_.resolveMethod(d->name, slotName);
      if (!mi || mi->mangled.empty()) {
        // Unresolved slot (should not happen  -  vtableSlots is built from
        // declared/overridden virtuals and resolveMethod walks the base chain).
        out_ << "i8* null";
        continue;
      }
      if (!mi->implStruct.empty()) {
        BType ist; ist.tag = BType::Tag::struct_; ist.structName = mi->implStruct;
        collectStruct(ist);
      }
      // The function's full signature: ret (self-param-type, <paramTypes>)*.
      // genMethod emits `define ret @mangled(%struct.<implStruct>* %arg_self,
      // <params>)` for a &self method; an override lives in the most-derived
      // impl (implStruct = that impl's owner), an inherited non-overridden
      // virtual in a base impl (implStruct = base). The self param type is
      // always %struct.<mi.implStruct>* for a virtual (all virtuals are &self).
      BType selfT; selfT.tag = BType::Tag::struct_;
      selfT.structName = mi->implStruct.empty() ? d->name : mi->implStruct;
      BType selfPtrT = makePtr(selfT);
      std::string fnTy = typeStr(mi->retType) + " (" + typeStr(selfPtrT);
      for (size_t k = 0; k < mi->paramTypes.size(); k++)
        fnTy += ", " + typeStr(mi->paramTypes[k]);
      fnTy += ")*";
      out_ << "i8* bitcast (" << fnTy << " @" << mi->mangled << " to i8*)";
    }
    out_ << " ]\n";
  }
}

void IRGen::emitTrapTable(Program& prog) {
  // ox:unsafe fix2  -  bare-metal trap vector table (VMCS host RIP / IDT-style dispatch).
  // Only emit in freestanding/bare-metal mode: a hosted program that cites a
  // trap handler (e.g. fix2_trap_test.ox's `main` unit-testing the handler) is
  // a normal test harness and must NOT get a trap table dumped into its data
  // segment. `--freestanding` is the existing bare-metal gate (see main.cpp);
  // sema_.freestanding is set from opt.freestanding in Driver before IRGen runs.
  if (!sema_.freestanding) return;

  // ox:unsafe Collect trap handlers that actually have compiled bodies. Mirror the genFunc
  // loop's gating exactly: skip extern prototypes (`trap name(...);`  -  signature
  // only, body supplied elsewhere) and generic handlers (Sema registers them but
  // defers body type-checking, so no codegen). A handler with an empty body is
  // also skipped (defensive  -  parseTrapHandler only sets isTrapHandler, and a
  // body-less Form-1 prototype has isExtern=true, but be robust either way).
  std::vector<FuncDecl*> handlers;
  for (auto& th : prog.trapHandlers) {
    if (!th) continue;
    if (th->isExtern) continue;
    if (th->isGeneric) continue;
    if (!th->isTrapHandler) continue;
    if (th->body.empty()) continue;
    handlers.push_back(th.get());
  }
  if (handlers.empty()) return;

  // ox:unsafe Emit the trap vector table as a private constant array of opaque function
  // pointers in the `.trap_table` section. Each entry is bitcast to i8* so the
  // table is signature-agnostic (the VMX startup code loads the raw address
  // into the VMCS host-RIP field / IDT entry, regardless of handler arity).
  // Forward references to @<handler_name> are fine  -  LLVM resolves them against
  // the `define`s emitted later in the bodies stream (same as vtables).
  //
  // A companion count symbol is emitted so startup code can bound the table
  // without __stop/start-section symbols (handy for freestanding, no libc).
  size_t n = handlers.size();
  out_ << "; fix2  -  bare-metal trap vector table (.trap_table section)\n";
  out_ << "@.ox_trap_table = private constant [" << n << " x i8*] [ ";
  for (size_t i = 0; i < n; i++) {
    if (i) out_ << ", ";
    FuncDecl* fn = handlers[i];
    // Build the handler's full signature type for the bitcast, mirroring
    // emitVtables: "<retType> (<paramType0>, <paramType1>, ...)*". These are the
    // same types genFunc wrote in the `define` line, so the cast type-checks.
    std::string fnTy = typeStr(fn->retType) + " (";
    for (size_t k = 0; k < fn->params.size(); k++) {
      if (k) fnTy += ", ";
      fnTy += typeStr(fn->params[k].type);
    }
    fnTy += ")*";
    out_ << "i8* bitcast (" << fnTy << " @" << fn->name << " to i8*)";
  }
  out_ << " ], section \".trap_table\"\n";
  // Entry count for the startup loader (read as a normal i64 global). Placed in
  // the same section so the whole table  -  count + entries  -  is contiguous and
  // the startup code can reference a single section base.
  out_ << "@.ox_trap_table_count = private constant i64 " << n
       << ", section \".trap_table\"\n";
  out_ << "\n";
}

void IRGen::generate(Program& prog) {
  g_tmp = 0;


  for (auto& fn : prog.funcs)
    if (!fn->isExtern) userDefinedFns_.insert(fn->name);   // ghost fns included: harmless (never emitted, never called at runtime)
  for (auto& mf : sema_.monomorphFns) userDefinedFns_.insert(mf->name);
  // ox:why Generic-struct method clones  -  register the mangled method symbol so the
  // extern-decl / undefined-symbol path treats them as user-defined (same
  // reason as free fns above). The clone lives in sema_.monomorphMethods as
  // {mangledStruct, fn}; the symbol genMethod emits is mangleMethod(struct, name).
  for (auto& cm : sema_.monomorphMethods)
    userDefinedFns_.insert(mangleMethod(cm.structName, cm.fn->name));
  // ox:unsafe fix2  -  register trap handler symbol names as user-defined so
  // emitExternDecls / the call path won't treat a user-authored handler as
  // a missing extern (e.g. when emitting `declare` stubs). A body-less
  // `trap name(...);` prototype (isExtern=true) is skipped  -  its signature is
  // in `funcs` and the user is expected to provide the body elsewhere.
  for (auto& th : prog.trapHandlers)
    if (th && !th->isExtern) userDefinedFns_.insert(th->name);


  for (auto& sd : prog.structs) {
    StructDef* d = findStruct(sd->name);
    if (d) structDefs_[sd->name] = d;
  }

  for (auto& fn : prog.funcs) {
    collectStruct(fn->retType);
    for (auto& p : fn->params) collectStruct(p.type);
  }

  for (auto& im : prog.impls) {
    BType st; st.tag = BType::Tag::struct_; st.structName = im->structName;
    collectStruct(st);
    for (auto& m : im->methods) {
      collectStruct(m->retType);
      for (auto& p : m->params) collectStruct(p.type);
    }
  }


  for (StructDef* d : allStructDefs()) {
    if (d->name.rfind("__oxg_", 0) == 0) {
      BType t; t.tag = BType::Tag::struct_; t.structName = d->name;
      collectStruct(t);
    }
  }


  std::ostringstream bodies;
  bodies.swap(out_);
  for (auto& fn : prog.funcs) {
    if (fn->isExtern) continue;
    if (fn->isGeneric) continue;
    if (fn->isGhost) continue;   // TODO IRGen skip  -  `ghost fn` does not codegen (T2)
    genFunc(*fn);
  }


  for (auto& mf : sema_.monomorphFns) {
    collectStruct(mf->retType);
    for (auto& p : mf->params) collectStruct(p.type);
    genFunc(*mf);
  }


  // ox:unsafe fix2  -  codegen trap handler bodies. A `trap [handler] name(...) { body }`
  // is a FuncDecl (reuses the whole param/contract/body pipeline via Parser's
  // parseTrapHandler) whose signature Sema already registered in `funcs` so
  // call sites (e.g. `main` unit-testing a handler) resolve the symbol. IRGen
  // never iterated `prog.trapHandlers` before this loop, so the call site hit
  // an undefined-symbol error at link/clang-assemble time. A body-less
  // `trap name(...);` prototype has `isExtern=true` (set by the parser) and is
  // skipped here, mirroring the free-fn loop above. Generic trap handlers are
  // out of scope for v1 (Sema registers them but defers body type-checking)
  //  -  skip the same as free fns.
  for (auto& th : prog.trapHandlers) {
    if (!th) continue;
    if (th->isExtern) continue;
    if (th->isGeneric) continue;
    genFunc(*th);
  }

  for (auto& im : prog.impls) {
    if (!findStruct(im->structName)) continue;
    for (auto& m : im->methods) {
      if (m->isGhost) continue;   // T2  -  `ghost fn` does not codegen
      genMethod(im->structName, *m);
    }
  }
  // Generic-struct method clones  -  emit each substituted method body under its
  // mangled instantiation struct name. Mirrors the prog.impls loop above but
  // draws from sema_.monomorphMethods (populated lazily by
  // Sema::instantiateGenericStruct as each instantiation is registered). The
  // struct + clone signatures are collected implicitly by genMethod via
  // collectStruct. Skip ghosts (consistent with the prog.impls loop).
  for (auto& cm : sema_.monomorphMethods) {
    if (cm.fn->isGhost) continue;
    genMethod(cm.structName, *cm.fn);
  }
  bodies.swap(out_);


  out_.str("");
  out_.clear();
  out_ << "; oxide generated ir\n";


  if (!targetTriple_.empty())
    out_ << "target triple = \"" << targetTriple_ << "\"\n";
  emitHeaderAndRuntime();
  emitVtables();
  emitTrapTable(prog);   // fix2  -  bare-metal trap vector table (VMCS host RIP)
  emitGlobalsAndExterns();
  out_ << globals_.str();
  out_ << "\n";

  out_ << lambdas_.str();
  out_ << bodies.str();
}
