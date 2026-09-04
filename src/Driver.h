#pragma once

#include <string>
#include <vector>
#include <memory>
#include <set>

#include "Lexer.h"

struct CompileError {
  std::string stage;
  std::string msg;
  int line;
  int col = 0;
  std::string hint;
};

// One row of the trust audit printed by `oxide verify --audit-trust`. A trusted
// assumption is a fact the proof depends on but did NOT discharge with the
// solver  -  either a `trusted assume <expr> source "...";` statement (recorded
// in the .smt2 as a `; note: trusted assume at line N` comment) or a
// `trusted axiom ...;` module-level declaration (recorded as a `; axiom <name>
// (source line N)` header preceded by `; note: trusted axiom at line N`).
// `description` is a short human label (the assumed expression's source text or
// the axiom's qualified name); `line`/`file` locate it; `source` is the
// optional `source "..."` citation (e.g. "Intel SDM Vol 3C §24.6"), empty when
// none was cited. Populated by Driver::doVerify rescanning the emitted .smt2.
struct TrustEntry {
  std::string description;
  int line = 0;
  std::string source;
  std::string file;
};

struct Program;   // forward decl  -  defined in AST.h (Driver holds it transiently)

enum class Action { run, emit, build, exe, check, verify, bindgen };

struct Options {
  Action action = Action::run;
  std::string input;
  std::string output;
  bool optimize = true;


  std::string targetTriple;
  bool freestanding = false;
  std::string entry = "main";


  std::vector<std::string> linkLibs;
  std::vector<std::string> linkFlags;

  // ox:proof Optional SMT-LIB emission path: when set, after IR generation the compiler
  // also writes a .smt2 file encoding every contract (requires/ensures/invariant/
  // assert) as SMT, plus the negated query for static discharge by an external
  // solver (Z3/Why3). See `--emit-smt`. Empty => no SMT file.
  std::string smtOut;

  // --- `oxide verify` ---
  // The solver invoked by `oxide verify` (default "z3"; "cvc5" and "why3" also
  // recognised). `solverTimeout` seconds, 0 = no timeout.
  std::string solver = "z3";
  int solverTimeout = 0;
  // ox:note `--verify-only`: when set, skip LLVM IR lowering (no IRGen, no clang) and
  // only emit contracts as SMT + run the solver. Sub-second on small files for
  // fast contract iteration. Default false (the full path still lowers IR, so a
  // program that typechecks but breaks IRGen cannot hide behind verify-only).
  bool verifyOnly = false;
  // Namespace/audit  -  `--audit-axioms`: with `verify`, after the verify report,
  // print every trusted axiom used (fully-qualified name, file/line, source
  // citation) so manual trust decisions are auditable. Default false.
  bool auditAxioms = false;
  // `--audit-trust`: with `verify`, after the verify report, print the FULL
  // trust audit  -  every trusted assumption the proof depended on that was NOT
  // discharged by the solver. This combines trusted `assume` statements (the
  // `trusted assume <expr> source "...";` form) AND trusted axioms (the
  // `trusted axiom ...` form, also surfaced by `--audit-axioms`), so a reviewer
  // sees the whole trust boundary in one place. Default false.
  bool auditTrust = false;
};

class Driver {
public:
  bool run(const Options& opt);

  void printErrors(const std::string& file) const;

  std::vector<CompileError> errs;

  // `doBindgen` is invoked directly from main() for the `bindgen` action
  // (which short-circuits before the full Driver::run pipeline); it must be
  // accessible from outside the class. Other `doX` helpers stay private.
  bool doBindgen(const Options& opt);

private:
  std::string ir_;
  std::vector<std::string> srcLines_;
  std::set<std::string> importVisited_;
  // Transient pointer to the parsed Program, set in run() before dispatching to
  // doX helpers. Used by doVerify() to walk prog->axioms for --audit-axioms.
  // Owned by run()'s local `prog` unique_ptr; valid only during the action call.
  const Program* program_ = nullptr;

  bool doRun(const Options& opt);
  bool doEmit();
  bool doBuild(const Options& opt, const std::string& outPath);
  bool doExe(const Options& opt, const std::string& outPath);
  bool doVerify(const Options& opt);

  std::string findTool(const std::vector<const char*>& names);
  int runCmd(const std::string& cmd);
  static std::string optFlag(bool optimize) { return optimize ? "-O2" : "-O0"; }


  static std::string renameMain(const std::string& ir);


  std::vector<Token> resolveImports(std::vector<Token>& toks, const std::string& dir,
                                    std::vector<CompileError>& lexErrs);
};
