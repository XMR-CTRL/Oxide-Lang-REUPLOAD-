; oxide-generated SMT-LIB (contracts)
; Encoding: requires/ensures/invariant/assert as boolean terms;
;           discharge query per clause is (assert (not <term>)) then (check-sat).
;           unsat  => clause holds for all inputs (static discharge OK).
;           sat/unknown => clause could not be discharged (or uses an
;                        uninterpreted placeholder, flagged above).
; Types: Bool / Int / Real; bools are ints widened for arithmetic.

(set-logic ALL)
(set-option :timeout 5)
(set-info :status unknown)

; ============================================================
; function main
; ============================================================

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
(declare-const main_ph0 Int)
; note: replaced an unsupported subform (unknown name 'ept') with the uninterpreted constant main_ph0
(declare-const main_ph1 Int)
; note: replaced an unsupported subform (unsupported expr) with the uninterpreted constant main_ph1

(exit)
