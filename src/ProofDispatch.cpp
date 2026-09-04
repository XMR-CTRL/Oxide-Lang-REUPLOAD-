// ProofDispatch.cpp  -  implementation of Feature 3 (Multi-Prover Dispatch) and
// Feature 6 (Proof Certificates) for the Oxide verifier. See ProofDispatch.h
// for the architecture and the responsibilities of each function.
//
// This translation unit is pure C++17 (no SMT/AST dependencies) so it links
// into the oxide.exe produced by `clang++ -std=c++17 -O0 -Isrc -o
// build/oxide.exe src/*.cpp` and into the CMake build without touching
// Driver.cpp's SMT internals.
#include "ProofDispatch.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <cctype>
#include <algorithm>

#ifdef _WIN32
  #define popen  _popen
  #define pclose _pclose
#endif

namespace proofdispatch {


namespace {

// Trim ASCII whitespace from both ends of `s`. Used on status words and on
// raw solver output lines. Implemented with explicit char classes so we never
// rely on a particular locale's isspace table.
std::string trimBoth(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r')) ++a;
  if (a == b) return std::string();
  --b;
  while (b > a && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r')) --b;
  return s.substr(a, b - a + 1);
}

// ox:proof Remove trailing '.' that SMT-LIB solvers sometimes append (e.g. "unsat.").
// We trim after whitespace so a stray period only counts as a suffix, never as
// leading text.
std::string trimStatusWord(const std::string& s) {
  std::string t = trimBoth(s);
  if (!t.empty() && t.back() == '.') t.pop_back();
  return t;
}

} // namespace

// ox:proof Probe whether a named solver binary is on PATH. Returns the name on success
// (so the caller can use it verbatim as the command) or "" if missing. Mirrors
// Driver::findTool's WHERE/command -v probe so behaviour is consistent.
std::string probeSolver(const std::string& name) {
  std::string probe;
#ifdef _WIN32
  probe = std::string("where ") + name + " >nul 2>nul";
#else
  probe = std::string("command -v ") + name + " >/dev/null 2>&1";
#endif
  FILE* p = popen(probe.c_str(), "r");
  if (!p) return std::string();
  int rc = pclose(p);
  return rc == 0 ? name : std::string();
}

// ox:proof Feature 3  -  running one solver + parallel dispatch + merge.

SolverResult runOneSolver(const std::string& solverName,
                           const std::string& smtPath,
                           const std::string& solverBin,
                           int timeoutSec) {
  SolverResult r;
  r.solver = solverName;
  if (solverBin.empty()) {
    r.available = false;
    r.status = "unknown";
    r.raw = "(solver not on PATH)";
    return r;
  }
  // ox:proof Per-solver invocation build. Same quoting discipline as the existing
  // single-solver path in Driver::doVerify: bare binary name, only the
  // (possibly-spaced) .smt2 path quoted.
  std::string cmd;
  if (solverName == "why3") {
    cmd = solverBin + " prove";
    if (timeoutSec > 0)
      cmd += " -t " + std::to_string(timeoutSec);
    cmd += " \"" + smtPath + "\"";
  } else if (solverName == "cvc5") {
    cmd = solverBin;
    if (timeoutSec > 0)
      cmd += " --tlimit=" + std::to_string(timeoutSec * 1000);
    cmd += " \"" + smtPath + "\"";
  } else if (solverName == "altergo" || solverName == "alt-ergo") {
    // Alt-Ergo: -t <seconds> timeout flag.
    cmd = solverBin;
    if (timeoutSec > 0)
      cmd += " -t " + std::to_string(timeoutSec);
    cmd += " \"" + smtPath + "\"";
  } else if (solverName == "z3") {
    // ox:proof Z3's -T timeout is measured in seconds and applies to the whole input.
    cmd = solverBin;
    if (timeoutSec > 0)
      cmd += " -T:" + std::to_string(timeoutSec);
    cmd += " \"" + smtPath + "\"";
  } else {
    // ox:proof Unknown solver: retain the conventional '<bin> <file>' interface. A
    // caller that needs a hard timeout must use a supported adapter above.
    cmd = solverBin + " \"" + smtPath + "\"";
  }
  cmd += " 2>&1";

  FILE* p = popen(cmd.c_str(), "r");
  if (!p) {
    r.status = "unknown";
    r.raw = "(popen failed)";
    return r;
  }
  // Read all stdout+stderr.
  std::string out;
  {
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0)
      out.append(buf, n);
  }
  pclose(p);

  // ox:proof Walk output lines; pick the first status word. SMT-LIB solvers print
  // "unsat"/"sat"/"unknown"; Alt-Ergo prints "Valid"/"Invalid"/"Unknown".
  std::string status = "unknown";
  bool haveStatus = false;
  {
    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
      std::string w = trimStatusWord(line);
      if (w.empty()) continue;
      if (w == "unsat")   { status = "unsat";   haveStatus = true; break; }
      if (w == "sat")    { status = "sat";     haveStatus = true; break; }
      if (w == "unknown") { status = "unknown"; haveStatus = true; break; }
      // Alt-Ergo conventions.
      if (w == "Valid")   { status = "unsat";   haveStatus = true; break; }
      if (w == "Invalid") { status = "sat";     haveStatus = true; break; }
      if (w == "Unknown" || w == "I don't know" || w == "Timeout") {
        status = "unknown"; haveStatus = true; break;
      }
    }
  }
  r.status = status;
  // Record the first non-empty output line as `raw` (diagnostics).
  {
    std::istringstream iss(out);
    std::string line;
    if (std::getline(iss, line)) r.raw = trimBoth(line);
    if (r.raw.empty()) r.raw = haveStatus ? status : "(no status line)";
  }
  (void)timeoutSec;  // already folded into cmd for solvers that honour it
  return r;
}

std::vector<SolverResult> dispatchToSolvers(
    const std::string& smtPath,
    const std::vector<std::string>& solvers,
    int timeoutSec) {
  std::vector<SolverResult> results(solvers.size());
  if (solvers.empty()) return results;

  // Resolve binaries up front (missing solvers short-circuit without a thread).
  std::vector<std::string> bins(solvers.size());
  for (size_t i = 0; i < solvers.size(); i++)
    bins[i] = probeSolver(solvers[i]);

  // ox:proof One thread per solver; each fills its own slot in `results`. Every thread is
  // joined before return. The old implementation detached workers at the host
  // deadline while they still captured `results` and `done` by reference; a
  // late solver could then write into destroyed stack storage. Supported solver
  // adapters receive their own native timeout flag in runOneSolver, so joining
  // remains bounded without sacrificing object lifetime safety.
  std::vector<std::thread> th;
  th.reserve(solvers.size());
  for (size_t i = 0; i < solvers.size(); i++) {
    std::string nm = solvers[i];
    std::string bin = bins[i];
    th.emplace_back([&, i, nm, bin]() {
      results[i] = runOneSolver(nm, smtPath, bin, timeoutSec);
    });
  }
  for (auto& t : th) {
    if (t.joinable()) t.join();
  }

  // ox:proof Ensure every slot has a solver name even if it was never filled.
  for (size_t i = 0; i < solvers.size(); i++) {
    if (results[i].solver.empty()) {
      results[i].solver = solvers[i];
      results[i].status = "unknown";
      results[i].available = bins[i] != "";
      results[i].raw = bins[i].empty() ? "(solver not on PATH)" : "(no result)";
    }
  }
  return results;
}

MergedResult mergeResults(const std::vector<SolverResult>& rs) {
  MergedResult m;
  m.solvers = rs;
  bool haveUnsat = false, haveSat = false;
  for (const auto& r : rs) {
    if (r.status == "unsat") haveUnsat = true;
    else if (r.status == "sat") haveSat = true;
  }
  m.disagreement = haveUnsat && haveSat;
  if (haveUnsat && haveSat)      m.status = "BUG";
  else if (haveUnsat)           m.status = "unsat";
  else if (haveSat)            m.status = "sat";
  else                          m.status = "unknown";
  return m;
}

// Feature 6  -  JSON + per-goal .smt2 splitting + proof certificate.

std::string jsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char ch : s) {
    switch (ch) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if ((unsigned char)ch < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x",
                        (unsigned)(unsigned char)ch);
          out += buf;
        } else {
          out += ch;
        }
    }
  }
  return out;
}

std::vector<SolverGoal> splitPerGoalSmt(const std::string& smtPath,
                                        const std::string& outDir,
                                        std::string& firstTactic) {
  firstTactic.clear();
  std::vector<SolverGoal> goals;
  std::ifstream f(smtPath, std::ios::binary);
  if (!f) return goals;
  std::stringstream ss; ss << f.rdbuf();
  std::string txt = ss.str();

  // Normalise CRLF -> LF for uniform line splitting.
  {
    std::string norm;
    norm.reserve(txt.size());
    for (size_t i = 0; i < txt.size(); i++) {
      if (txt[i] == '\r' && i + 1 < txt.size() && txt[i + 1] == '\n') {
        norm += '\n'; ++i;
      } else if (txt[i] == '\r') {
        norm += '\n';
      } else {
        norm += txt[i];
      }
    }
    txt.swap(norm);
  }

  // ox:proof The "header" is everything before the first "; --- discharge (" line:
  // (set-logic ALL), memory axioms, (declare-const)s, and every (define-fun).
  // A discharge block runs from that header line through the first "(pop)".
  std::string header;
  std::vector<std::pair<std::string, std::string>> blocks; // (label, block text)
  std::string firstTacticLine;
  {
    std::istringstream iss(txt);
    std::string line;
    bool inHeader = true;
    bool inBlock = false;
    std::string curLabel;
    std::string curBlock;
    while (std::getline(iss, line)) {
      if (inHeader) {
        if (line.rfind("; --- discharge (", 0) == 0) {
          inHeader = false;
          inBlock = true;
          // ox:proof "; --- discharge (<label>) ---" -> extract <label>.
          size_t a = line.find('(');
          size_t b = line.find(')', a == std::string::npos ? 0 : a);
          if (a != std::string::npos && b != std::string::npos && b > a)
            curLabel = line.substr(a + 1, b - a - 1);
          curBlock = line + "\n";
        } else {
          header += line + "\n";
        }
        continue;
      }
      if (inBlock) {
        curBlock += line + "\n";
        // Capture the FIRST tactic of the FIRST block as the representative
        // tactic for the proof certificate (all blocks share the cascade in
        // the current emitter).
        if (firstTacticLine.empty() && line.find("(check-sat-using ") != std::string::npos) {
          firstTacticLine = line;
          size_t s = line.find("(check-sat-using ");
          if (s != std::string::npos) {
            size_t e = line.find(')', s);
            if (e != std::string::npos)
              firstTactic = line.substr(s + 1, e - s); // "check-sat-using ...)"
            else
              firstTactic = line.substr(s);
          }
        }
        if (line.find("(pop)") != std::string::npos) {
          blocks.push_back({curLabel, curBlock});
          curLabel.clear();
          curBlock.clear();
          inBlock = false;
          inHeader = true; // resume header-mode scanning until the next block
        }
      }
    }
  }

  // Write each block as its own per-goal file.
  int idx = 0;
  for (auto& b : blocks) {
    if (b.first.empty()) continue;
    std::string safe = b.first;
    for (char& c : safe)
      if (!std::isalnum((unsigned char)c) && c != '_' && c != '-') c = '_';
    std::string path = outDir + "/_goal_" + std::to_string(idx) + "_" + safe + ".smt2";
    std::ofstream of(path, std::ios::binary);
    if (!of) continue;
    of << header << b.second << "(exit)\n";
    of.close();
    SolverGoal g;
    g.label = b.first;
    g.smt_file = path;
    g.tactic = firstTactic;
    goals.push_back(std::move(g));
    ++idx;
  }
  return goals;
}

std::string proofLogDir()  { return "build/_proof"; }
std::string proofLogPath(const std::string& dir) { return dir + "/proof_log.json"; }

bool writeProofLog(const std::vector<ProofGoal>& goals,
                   const std::string& dir,
                   const std::string& sourceFile) {
  // ox:why Best-effort directory creation. Use std::system so we need no <sys/stat.h>
  // (which differs on Windows). Errors here are non-fatal: the open below
  // will simply fail and we return false.
#ifdef _WIN32
  std::system((std::string("if not exist \"") + dir + "\" mkdir \"" + dir + "\" >nul 2>&1").c_str());
#else
  std::system((std::string("mkdir -p \"") + dir + "\" >/dev/null 2>&1").c_str());
#endif
  std::string path = proofLogPath(dir);
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  f << "{\n";
  f << "  \"version\": 1,\n";
  f << "  \"source\": \"" << jsonEscape(sourceFile) << "\",\n";
  // Seconds since epoch  -  portable, no <ctime> formatting needed.
  auto now = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  f << "  \"timestamp\": " << now << ",\n";
  f << "  \"goals\": [\n";
  for (size_t i = 0; i < goals.size(); i++) {
    const ProofGoal& g = goals[i];
    f << "    {\n";
    f << "      \"label\": \"" << jsonEscape(g.label) << "\",\n";
    f << "      \"tactic\": \"" << jsonEscape(g.tactic) << "\",\n";
    f << "      \"solver\": \"" << jsonEscape(g.solver) << "\",\n";
    f << "      \"status\": \"" << jsonEscape(g.status) << "\",\n";
    f << "      \"smt_file\": \"" << jsonEscape(g.smt_file) << "\"";
    if (g.disagreement) f << ",\n      \"disagreement\": true";
    f << "\n    }";
    if (i + 1 < goals.size()) f << ",";
    f << "\n";
  }
  f << "  ]\n";
  f << "}\n";
  return (bool)f;
}

// `oxide check`  -  parse + replay a proof log.

namespace {

// Minimal JSON value extractor for our flat schema. Finds `"key": <value>`
// where value is a quoted string; returns the unescaped string. Returns "" and
// sets found=false if the key is absent. Good enough for our needs: we never
// have nested objects inside a goal's leaf fields, only simple quoted strings
// (and "disagreement": true is handled explicitly by the caller).
std::string jsonStringField(const std::string& obj, const std::string& key,
                            bool& found) {
  found = false;
  std::string pat = "\"" + key + "\":";
  size_t k = obj.find(pat);
  if (k == std::string::npos) return std::string();
  size_t i = k + pat.size();
  while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t')) ++i;
  if (i >= obj.size() || obj[i] != '"') return std::string();
  ++i;
  std::string out;
  while (i < obj.size() && obj[i] != '"') {
    if (obj[i] == '\\' && i + 1 < obj.size()) {
      char nx = obj[i + 1];
      switch (nx) {
        case '"':  out += '"';  break;
        case '\\': out += '\\'; break;
        case 'n':  out += '\n'; break;
        case 'r':  out += '\r'; break;
        case 't':  out += '\t'; break;
        default:   out += nx;   break;
      }
      i += 2;
    } else {
      out += obj[i++];
    }
  }
  found = true;
  return out;
}

bool jsonBoolField(const std::string& obj, const std::string& key, bool def) {
  std::string pat = "\"" + key + "\":";
  size_t k = obj.find(pat);
  if (k == std::string::npos) return def;
  size_t i = k + pat.size();
  while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t')) ++i;
  if (obj.compare(i, 4, "true") == 0) return true;
  if (obj.compare(i, 5, "false") == 0) return false;
  return def;
}

} // namespace

bool parseProofLog(const std::string& path,
                   std::vector<LoggedGoal>& out,
                   std::string& errOut) {
  out.clear();
  errOut.clear();
  std::ifstream f(path, std::ios::binary);
  if (!f) { errOut = "cannot open proof log '" + path + "'"; return false; }
  std::stringstream ss; ss << f.rdbuf();
  std::string txt = ss.str();

  // Locate the "goals": [ ... ] array and split its top-level objects. Our
  // schema is flat (no nested {} inside a goal), so brace-depth-1 objects are
  // exactly the goals.
  size_t arr = txt.find("\"goals\"");
  if (arr == std::string::npos) {
    errOut = "proof log has no \"goals\" array";
    return false;
  }
  size_t lb = txt.find('[', arr);
  size_t rb = txt.find(']', lb == std::string::npos ? 0 : lb);
  if (lb == std::string::npos || rb == std::string::npos || rb < lb) {
    errOut = "proof log \"goals\" array is malformed";
    return false;
  }
  std::string body = txt.substr(lb + 1, rb - lb - 1);
  // Walk brace-delimited objects.
  size_t i = 0;
  while (i < body.size()) {
    size_t ob = body.find('{', i);
    if (ob == std::string::npos) break;
    // Find the matching close brace (depth-1; our flattish schema has no
    // nested objects, but be robust against a stray brace in a string... we
    // don't bother  -  JSON with a literal brace inside a string is not produced
    // by our writer).
    size_t cb = body.find('}', ob);
    if (cb == std::string::npos) break;
    std::string obj = body.substr(ob, cb - ob + 1);
    bool ok = false;
    LoggedGoal g;
    g.label    = jsonStringField(obj, "label",    ok); (void)ok;
    bool okt; g.tactic  = jsonStringField(obj, "tactic",  okt);
    bool oks; g.solver  = jsonStringField(obj, "solver",  oks);
    bool okst; g.status = jsonStringField(obj, "status", okst);
    bool okf; g.smt_file= jsonStringField(obj, "smt_file", okf);
    (void)okt; (void)oks; (void)okst; (void)okf;
    out.push_back(std::move(g));
    i = cb + 1;
  }
  return true;
}

std::vector<ReplayOutcome> replayProofLog(const std::vector<LoggedGoal>& goals,
                                          const std::string& replaySolver,
                                          int timeoutSec) {
  std::string bin = replaySolver.empty() ? std::string() : probeSolver(replaySolver);
  std::vector<ReplayOutcome> res;
  res.reserve(goals.size());
  for (const auto& g : goals) {
    ReplayOutcome o;
    o.logged_status = g.status;
    o.replayed.label    = g.label;
    o.replayed.tactic   = g.tactic;
    o.replayed.smt_file = g.smt_file;
    // ox:proof Re-run the goal's .smt2 against the chosen solver.
    SolverResult sr = runOneSolver(replaySolver, g.smt_file, bin, timeoutSec);
    o.replayed.solver = replaySolver;
    o.replayed.status = sr.status;
    // A replay matches if both the logged and replayed statuses agree. A
    // logged "BUG" (solver disagreement) never "matches" a single-solver
    // replay  -  we surface that as a mismatch so the user notices the original
    // multi-prover conflict.
    o.matches = (o.replayed.status == o.logged_status);
    res.push_back(std::move(o));
  }
  return res;
}

} // namespace proofdispatch
