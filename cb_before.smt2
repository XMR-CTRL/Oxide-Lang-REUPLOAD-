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
; function sumAll
; ============================================================
(declare-const p_sumAll_a Int)
(declare-const sumAll_result Int)

; ---- requires (assumed, not discharged) ----
; sumAll_requires_0 (source line 10)
(declare-const sumAll_ph1 Int)
; note: replaced an unsupported subform (unsupported expr) with the uninterpreted constant sumAll_ph1
(define-fun sumAll_requires_0 () Bool (forall ((sumAll_q_k_0 Int)) (=> (and (>= sumAll_q_k_0 0) (< sumAll_q_k_0 4)) (>= sumAll_ph1 0))))

; ---- ensures ----
; sumAll_ensures_0 (source line 11)
(define-fun sumAll_ensures_0 () Bool (>= sumAll_result 0))

; --- discharge (sumAll_ensures_0) ---
(push)
(assert sumAll_requires_0)
(assert (not sumAll_ensures_0))
(check-sat)
(pop)

; ---- body contracts (invariants/asserts) ----
; sumAll_invariant_d0_0 (while, source line 0)
(declare-const sumAll_ph2 Int)
; note: replaced an unsupported subform (unknown name 'i') with the uninterpreted constant sumAll_ph2
(declare-const sumAll_ph3 Int)
; note: replaced an unsupported subform (unknown name 'i') with the uninterpreted constant sumAll_ph3
(define-fun sumAll_invariant_d0_0 () Bool (and (<= 0 sumAll_ph2) (<= sumAll_ph3 4)))

; --- discharge (sumAll_invariant_d0_0) ---
(push)
(assert sumAll_requires_0)
(assert (not sumAll_invariant_d0_0))
(check-sat)
(pop)


; ============================================================
; function main
; ============================================================

; ---- body contracts (invariants/asserts) ----

(exit)
