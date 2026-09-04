# Focused smoke for the advanced-math test suite (examples/math_*.ox).
# Mirrors _ex_smoke2.sh: distinguishes compiler-bug failures from the
# "main returned N" case (a clean exit signaling the test harness chose
# non-zero to flag a runtime expectation), and prints the tail of each
# failure so the offending parse/codegen/contract error is visible.
#
# Run after a fresh `cmake --build build` so the binary reflects the
# latest source. Intended use: track the math-feature converge — every
# math_*.ox should move to PASS as the Parser/Sema/IRGen subagents land
# the advanced-math wiring.
cd '/c/Users/mosky/Documents/Oxide-lang backup' || exit 1
if [ ! -x ./build/oxide.exe ]; then
  echo "MISSING build/oxide.exe — run _build.bat first" >&2
  exit 2
fi
PASS=0; FAIL=0; TIMEOUT=0; MAIN_RET_NONZERO=0
for f in $(find examples -maxdepth 1 -name "math_*.ox" | sort); do
  out=$(timeout 20 ./build/oxide.exe run "$f" 2>&1)
  rc=$?
  if [ $rc -eq 0 ]; then
    PASS=$((PASS+1))
  elif [ $rc -eq 124 ]; then
    TIMEOUT=$((TIMEOUT+1))
    echo "TIMEOUT: $f"
  elif echo "$out" | grep -qE 'error\[run\]: program exited with code'; then
    MAIN_RET_NONZERO=$((MAIN_RET_NONZERO+1))
    code=$(echo "$out" | grep -oE 'exited with code -?[0-9]+' | head -1)
    printf 'MAIN_RET_NONZERO %s %s\n' "$f" "$code"
  else
    FAIL=$((FAIL+1))
    echo "FAIL($rc): $f"
    echo "$out" | tail -4 | sed 's/^/    /'
  fi
done
echo "=== MATH SMOKE SUMMARY ==="
echo "PASS=$PASS FAIL=$FAIL TIMEOUT=$TIMEOUT MAIN_RET_NONZERO=$MAIN_RET_NONZERO"
echo "(FAIL = parse/codegen/contract/link error — the math feature is not yet"
echo " wired through; MAIN_RET_NONZERO = the program ran but main returned non-zero.)"
