// ProofDispatch.h  -  Feature 3 (Multi-Prover Dispatch) + Feature 6 (Proof
// Certificates) for the Oxide verifier.
//
// This is a self-contained translation unit (see ProofDispatch.cpp) so the
// multi-prover and proof-log machinery can be added to `Driver::doVerify`
// without touching the SMT/AST internals of Driver.cpp. Driver.cpp calls
// `dispatchVerifyGoals` after the single-solver pass (or instead of it, under
// `--multi-prover`), and `writeProofCertificate` at the very end. `replayProofLog`
// backs the standalone `oxide check` action.
//
// Design notes:
//   * Solvers are forked in PARALLEL via popen, one owned std::thread per solver.
//     Supported solver adapters receive native timeout flags and every worker is
//     joined before dispatch returns. A solver not on PATH reports
//     `available=false`, status "unknown"  -  it never crashes the dispatch.
//   * `mergeResults`: any unsat => unsat; sat AND unsat => "BUG" (disagreement
//     flag set); only sat => sat; otherwise unknown.
//   * Proof certificate = JSON log at build/_proof/proof_log.json with one entry
//     per discharged check-sat obligation: label, tactic, solver, status, smt_file.
//   * `oxide check` replays every goal's smt_file against a (possibly different)
//     solver and reports whether each result matches the logged status.
#pragma once

#include <string>
#include <vector>

namespace proofdispatch {

// ox:proof Feature 3  -  per-solver result + merge.

// ox:proof One solver's verdict on one goal.
struct SolverResult {
  std::string solver;     // "z3", "cvc5", "altergo", ...
  std::string status;      // "unsat" | "sat" | "unknown"
  bool available = true;   // false => solver binary not on PATH
  std::string raw;         // trimmed first output line, for diagnostics
};

// Merged verdict across solvers for one goal.
struct MergedResult {
  std::string status = "unknown";  // "unsat" | "sat" | "unknown" | "BUG"
  bool disagreement = false;       // sat and unsat both appeared across solvers
  std::vector<SolverResult> solvers;  // the per-solver breakdown that produced this
};

// ox:proof One goal ready to dispatch: a label + the .smt2 file holding its discharge.
struct SolverGoal {
  std::string label;
  std::string smt_file;
  std::string tactic;   // first check-sat-using tactic text (for the certificate)
};

// ox:proof Run a single solver against `smtPath` (used internally + by `oxide check`).
SolverResult runOneSolver(const std::string& solverName,
                          const std::string& smtPath,
                          const std::string& solverBin,
                          int timeoutSec);

// Dispatch one goal to all `solvers` in parallel, with a wall-clock deadline
// (timeoutSec seconds; 0 => no deadline). Solvers not on PATH contribute
// `available=false, status="unknown"` and never crash the dispatch.
std::vector<SolverResult> dispatchToSolvers(
    const std::string& smtPath,
    const std::vector<std::string>& solvers,
    int timeoutSec);

// ox:proof Fold per-solver results into a single verdict.
//   any unsat                -> "unsat"
//   sat AND unsat both       -> "BUG" (disagreement flag set)
//   only sat                 -> "sat"
//   otherwise                -> "unknown"
MergedResult mergeResults(const std::vector<SolverResult>& rs);

// ox:proof Probe whether a named solver is on PATH (returns the name, or "" if missing).
std::string probeSolver(const std::string& name);

// Feature 6  -  proof certificate (JSON) + per-goal .smt2 splitting.

// One entry in the proof certificate log.
struct ProofGoal {
  std::string label;     // clause's SMT label, e.g. "fib_ensures_ret_0_0"
  std::string tactic;    // e.g. "check-sat-using (then simplify smt)"
  std::string solver;    // "z3" or "z3+cvc5+altergo" (multi-prover: joined names)
  std::string status;    // "unsat" | "sat" | "unknown" | "BUG"
  std::string smt_file;  // path to the per-goal .smt2 this row was checked on
  bool disagreement = false;  // Feature 3: sat+unsat conflict across solvers
};

// Split a combined .smt2 file (as emitted by ox_smt::emitSmt) into per-goal
// files under `outDir`, one discharge block each. Returns the (label, path)
// list and (via the second out-param) the tactic seen on the FIRST discharge
// block (all blocks share the same cascade in the current emitter, so this is
// a faithful single tactic to record per goal). Writes `_goal_<n>_<label>.smt2`.
//
// Each per-goal file = (set-logic/axioms/declares/define-funs header) + one
// (push)...(check-sat-using...)...(pop) discharge block + (exit). Header
// reuse is sound: declare-const/define-fun are global in SMT-LIB, and each
// block is push/pop-isolated so assertions never leak across goals.
//
// Returns an empty vector if the combined file has no discharge blocks.
std::vector<SolverGoal> splitPerGoalSmt(const std::string& smtPath,
                                        const std::string& outDir,
                                        std::string& firstTactic);

// Default proof certificate directory + log path.
std::string proofLogDir();
std::string proofLogPath(const std::string& dir);

// Write the proof certificate JSON log. Returns false on write failure.
bool writeProofLog(const std::vector<ProofGoal>& goals,
                   const std::string& dir,
                   const std::string& sourceFile);

// Emit a JSON string with control-char / quote / backslash escapes.
std::string jsonEscape(const std::string& s);

// ox:proof `oxide check`  -  replay a proof log against a (possibly different) solver.

// Parsed entry from proof_log.json.
struct LoggedGoal {
  std::string label;
  std::string tactic;
  std::string solver;     // solver recorded at verify time
  std::string status;     // status recorded at verify time
  std::string smt_file;
};

// Parse proof_log.json. Returns false on read/parse failure (errOut filled).
bool parseProofLog(const std::string& path,
                   std::vector<LoggedGoal>& out,
                   std::string& errOut);

// Re-run every logged goal's smt_file against `replaySolver` (timeout N secs)
// and return one ProofGoal per goal with the replayed `solver`/`status`. The
// `logged_*` members of the returned vector are the ORIGINAL logged values so
// the caller can report matches/mismatches.
struct ReplayOutcome {
  ProofGoal replayed;
  std::string logged_status;
  bool matches = false;   // replayed.status == logged_status
};
std::vector<ReplayOutcome> replayProofLog(const std::vector<LoggedGoal>& goals,
                                          const std::string& replaySolver,
                                          int timeoutSec);

} // namespace proofdispatch
