#include "Driver.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif


std::string runtimeC();

static void usage(const char* argv0) {
  std::printf(
    "oxide compiler\n"
    "usage: %s <command> [options] <file.ox>\n"
    "commands:\n"
    "  run     jit compile and run the program\n"
    "  emit    print LLVM IR to stdout\n"
    "  build   emit an object file (.o)\n"
    "  exe     compile to a native executable\n"
    "  check   lex, parse, and type-check without generating LLVM IR\n"
    "  verify  discharge every contract (requires/ensures/invariant/assert) with\n"
    "          an SMT solver; non-zero exit if any clause is sat or unknown\n"
    "  bindgen parse a C header (.h) and emit Oxide extern fn declarations; shells\n"
    "          out to clang for the AST. Use: oxide bindgen foo.h [-o foo.ox]\n"
    "          Prints to stdout, or writes to -o PATH. Requires clang on PATH.\n"
    "  rt      print (or -o PATH write) the bundled C runtime for separate linking\n"
    "options:\n"
    "  -o PATH          output path (build/exe/rt)\n"
    "  -O0              disable optimization\n"
    "  --target TRIPLE  emit a target triple into the IR (default: host, omitted)\n"
    "  --freestanding   skip the C runtime + the @main->@oxide_main wrapper (--no-rt)\n"
    "  --entry NAME     the program entry symbol (freestanding), default `main`\n"
    "  --link NAME      link a native library (adds `-l NAME`); repeatable. e.g.\n"
    "                   --link user32 --link kernel32 to call Win32 from `extern fn`\n"
    "  -Wl,FLAG         pass a raw flag straight to the linker; repeatable\n"
    "  --emit-smt PATH  also write the program's contracts (requires/ensures/\n"
    "                   invariant/assert) as SMT-LIB to PATH (.smt2), with the\n"
    "                   negated query per clause for Z3/Why3 static discharge\n"
    "  --solver NAME     solver used by `verify` (default z3; cvc5 and why3 ok)\n"
    "  --solver-timeout SECONDS  timeout passed to the solver; 0 = no timeout\n"
    "  --verify-only     with `verify`: skip LLVM IR lowering entirely (no clang\n"
    "                    invocation) and only discharge contracts through the SMT\n"
    "                    solver. For iterating contracts this is sub-second on\n"
    "                    small files. The default path still lowers IR (so a code\n"
    "                    path that typechecks but breaks IRGen can't hide here).\n"
    "  --audit-axioms    with `verify`: after the report, list every trusted (non-\n"
    "                    machine-verified) axiom used  -  fully-qualified name,\n"
    "                    file/line, and source citation (e.g. an Intel SDM\n"
    "                    reference). Makes unchecked trust decisions visible.\n"
    "  --audit-trust    with `verify`: after the report, print the FULL trust\n"
    "                    audit  -  every trusted assumption the proof depended on\n"
    "                    but did NOT discharge. Combines `trusted assume ...`\n"
    "                    statements and `trusted axiom ...` declarations (also\n"
    "                    surfaced individually by --audit-axioms), so a reviewer\n"
    "                    sees the whole trust boundary in one place.\n",
    argv0);
}

int main(int argc, char** argv) {
  if (argc < 2) { usage(argv[0]); return 1; }

  Options opt;
  std::string cmd = argv[1];
  if (cmd == "run") opt.action = Action::run;
  else if (cmd == "emit") opt.action = Action::emit;
  else if (cmd == "build") opt.action = Action::build;
  else if (cmd == "exe") opt.action = Action::exe;
  else if (cmd == "check") opt.action = Action::check;
  else if (cmd == "verify") opt.action = Action::verify;
  else if (cmd == "bindgen") opt.action = Action::bindgen;


  else if (cmd == "rt") {
    std::string rt = runtimeC();
    bool writeFile = false;
    std::string outPath;
    for (int i = 2; i < argc; i++) { std::string a = argv[i]; if (a == "-o" && i + 1 < argc) { outPath = argv[++i]; writeFile = true; } }
    if (writeFile) {
      std::ofstream f(outPath, std::ios::binary);
      if (!f) { std::printf("error: cannot open '%s' for write\n", outPath.c_str()); return 1; }
      f << rt;
      return (bool)f ? 0 : 1;
    }
    std::fputs(rt.c_str(), stdout);
    return 0;
  }
  else if (cmd == "-h" || cmd == "--help" || cmd == "help") { usage(argv[0]); return 0; }
  else {
    std::printf("unknown command '%s'\n", cmd.c_str());
    usage(argv[0]);
    return 1;
  }

  for (int i = 2; i < argc; i++) {
    std::string a = argv[i];
    if (a == "-o") {
      if (i + 1 < argc) { opt.output = argv[++i]; }
      else { std::printf("error: -o requires a path\n"); return 1; }
    } else if (a == "-O0") {
      opt.optimize = false;
    } else if (a == "--target") {
      if (i + 1 < argc) { opt.targetTriple = argv[++i]; }
      else { std::printf("error: --target requires a triple\n"); return 1; }
    } else if (a == "--freestanding" || a == "--no-rt") {
      opt.freestanding = true;
    } else if (a == "--entry") {
      if (i + 1 < argc) { opt.entry = argv[++i]; }
      else { std::printf("error: --entry requires a name\n"); return 1; }
    } else if (a == "--link" || a == "-l") {
      if (i + 1 < argc) { opt.linkLibs.push_back(argv[++i]); }
      else { std::printf("error: --link requires a library name\n"); return 1; }
    } else if (a.rfind("-Wl,", 0) == 0) {


      opt.linkFlags.push_back(a);
    } else if (a == "--emit-smt") {
      if (i + 1 < argc) { opt.smtOut = argv[++i]; }
      else { std::printf("error: --emit-smt requires a path\n"); return 1; }
    } else if (a == "--solver") {
      if (i + 1 < argc) { opt.solver = argv[++i]; }
      else { std::printf("error: --solver requires a name\n"); return 1; }
    } else if (a == "--solver-timeout") {
      if (i + 1 < argc) {
        char* end = nullptr;
        long t = std::strtol(argv[++i], &end, 10);
        if (end == argv[i] || t < 0) { std::printf("error: --solver-timeout needs a non-negative integer\n"); return 1; }
        opt.solverTimeout = (int)t;
      }
      else { std::printf("error: --solver-timeout requires a value\n"); return 1; }
    } else if (a == "--verify-only") {
      opt.verifyOnly = true;
    } else if (a == "--audit-axioms") {
      // ox:proof Namespace/audit: with `verify`, print every trusted axiom (name, source,
      // file:line) after the verify report. Citizens can audit the unchecked
      // trust assumptions (vs. the machine-proven contracts above it).
      opt.auditAxioms = true;
    } else if (a == "--audit-trust") {
      // Trust audit: with `verify`, print the FULL trust boundary  -  every
      // trusted assumption (trusted `assume` statements + trusted axioms) the
      // proof depended on but did NOT discharge. Broader companion to
      // --audit-axioms (which lists trusted axioms only).
      opt.auditTrust = true;
    } else {
      opt.input = a;
    }
  }

  // ox:proof For the `verify` action we need an SMT file on disk before doVerify runs.
  // If the user didn't pass --emit-smt, point smtOut at a temp file ourselves.
  bool verifyTempSmt = false;
  if (opt.action == Action::verify && opt.smtOut.empty()) {
    const char* t = std::getenv("TEMP");
    if (!t) t = std::getenv("TMP");
    if (!t) t = ".";
#ifdef _WIN32
    const long long pid = (long long)_getpid();
#else
    const long long pid = (long long)getpid();
#endif
    opt.smtOut = std::string(t) + "\\oxide_verify_" + std::to_string(pid) + ".smt2";
    verifyTempSmt = true;
  }

  if (opt.input.empty()) {
    std::printf("error: no input file given\n");
    usage(argv[0]);
    return 1;
  }

  // ox:note `bindgen` parses a C header, not Oxide source. It shells out to clang for
  // the AST and never touches the Oxide lexer/parser, so short-circuit before
  // Driver::run would try to read/lex the header as an Oxide program.
  // `driver` is declared at outer scope so the post-bindgen branches below
  // (which use `driver.run`, `driver.errs`, `driver.printErrors`) resolve.
  Driver driver;
  if (opt.action == Action::bindgen) {
    return driver.doBindgen(opt) ? 0 : 1;
  }
  if (!driver.run(opt)) {
    // Route EVERY compile-path failure to STDERR (per standard CLI convention:
    // errors don't belong on stdout next to program data / emitted IR). When a
    // diagnostic exists we print it via printErrors; the bare fall-through is
    // kept ONLY for the defensive case where Driver::run returned false without
    // recording a CompileError. That case should be vanishingly rare after the
    // doRun error-reporting fix (previously `return rr == 0` in doRun returned
    // false with an empty `errs` on ANY nonzero program exit  -  that swallowed
    // the real cause and was the root of hmap.ox/hset.ox printing only the bare
    // `error: compilation failed`). Surface a pointer to the unfixed route so
    // a regression here can be located instead of silent.
    if (driver.errs.empty()) {
      std::fprintf(stderr,
        "error: compilation failed (no diagnostic recorded; "
        "this is itself a bug  -  Driver::run returned false without pushing a "
        "CompileError. Likely action=%d path)\n",
        (int)opt.action);
      std::fflush(stderr);
    } else {
      driver.printErrors(opt.input);
    }
    return 1;
  }
  // For `verify` we printed the report from inside Driver::run; clean up the
  // temp .smt2 we allocated on Windows TEMP unless the user asked to keep it
  // with an explicit --emit-smt (in which case verifyTempSmt is false).
  if (verifyTempSmt) std::remove(opt.smtOut.c_str());
  return 0;
}
