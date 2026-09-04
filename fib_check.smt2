; oxide-generated SMT-LIB (contracts)
; Encoding: requires/ensures/invariant/assert as boolean terms;
;           discharge query per clause is (assert (not <term>)) then (check-sat).
;           unsat  => clause holds for all inputs (static discharge OK).
;           sat/unknown => clause could not be discharged (or uses an
;                        uninterpreted placeholder, flagged above).
; Types: Bool / Int / Real; bools are ints widened for arithmetic.

(set-logic ALL)
(set-info :status unknown)

; ============================================================
; function fib
; ============================================================
(declare-const p_fib_n Int)
(declare-const fib_result Int)

; ---- requires (assumed, not discharged) ----
; fib_requires_0 (source line 10)
(define-fun fib_requires_0 () Bool (>= p_fib_n 0))

; ---- ensures ----
; fib_ensures_0 (source line 11)
(define-fun fib_ensures_0 () Bool (>= fib_result 0))

; --- discharge (fib_ensures_0) ---
(push)
(assert fib_requires_0)
(assert (not fib_ensures_0))
(check-sat)
(pop)

; ---- body contracts (invariants/asserts) ----
; fib_invariant_d0_0 (while, source line 0)
(declare-const fib_ph0 Int)
; note: replaced an unsupported subform (unknown name 'i') with the uninterpreted constant fib_ph0
(declare-const fib_ph1 Int)
; note: replaced an unsupported subform (unknown name 'i') with the uninterpreted constant fib_ph1
(define-fun fib_invariant_d0_0 () Bool (and (<= 1 fib_ph0) (<= fib_ph1 p_fib_n)))

; --- discharge (fib_invariant_d0_0) ---
(push)
(assert fib_requires_0)
(assert (not fib_invariant_d0_0))
(check-sat)
(pop)


; ============================================================
; function main
; ============================================================

; ---- body contracts (invariants/asserts) ----

(exit)
