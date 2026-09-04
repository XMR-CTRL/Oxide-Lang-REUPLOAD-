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
; function nested_write_2d
; ============================================================
(declare-const p_nested_write_2d_arr (Array Int (Array Int Int)))
(declare-const p_nested_write_2d_i Int)
(declare-const p_nested_write_2d_j Int)
(declare-const p_nested_write_2d_x Int)
(declare-const nested_write_2d_result Int)

; ---- requires (assumed, not discharged) ----
; nested_write_2d_requires_0 (source line 0)
(define-fun nested_write_2d_requires_0 () Bool (and (<= 0 p_nested_write_2d_i) (< p_nested_write_2d_i 2)))

; nested_write_2d_requires_1 (source line 0)
(define-fun nested_write_2d_requires_1 () Bool (and (<= 0 p_nested_write_2d_j) (< p_nested_write_2d_j 2)))

; ---- ensures (signature-level, fallback) ----
; nested_write_2d_ensures_0 (source line 15)
(define-fun nested_write_2d_ensures_0 () Bool (= (select (select p_nested_write_2d_arr p_nested_write_2d_i) p_nested_write_2d_j) p_nested_write_2d_x))

; --- discharge (nested_write_2d_ensures_0) ---
(push)
(assert nested_write_2d_requires_0)
(assert nested_write_2d_requires_1)
(assert (not nested_write_2d_ensures_0))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; nested_write_2d_ensures_ret_0_0 (return-site ensures, source line 15)
(define-fun nested_write_2d_ensures_ret_0_0 () Bool (= (select (select (store p_nested_write_2d_arr p_nested_write_2d_i (store (select p_nested_write_2d_arr p_nested_write_2d_i) p_nested_write_2d_j p_nested_write_2d_x)) p_nested_write_2d_i) p_nested_write_2d_j) p_nested_write_2d_x))

; --- discharge (nested_write_2d_ensures_ret_0_0) ---
(push)
(assert nested_write_2d_requires_0)
(assert nested_write_2d_requires_1)
(assert (not nested_write_2d_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function nested_write_3d
; ============================================================
(declare-const p_nested_write_3d_arr (Array Int (Array Int (Array Int Int))))
(declare-const p_nested_write_3d_i Int)
(declare-const p_nested_write_3d_j Int)
(declare-const p_nested_write_3d_k Int)
(declare-const p_nested_write_3d_x Int)
(declare-const nested_write_3d_result Int)

; ---- requires (assumed, not discharged) ----
; nested_write_3d_requires_0 (source line 0)
(define-fun nested_write_3d_requires_0 () Bool (and (<= 0 p_nested_write_3d_i) (< p_nested_write_3d_i 2)))

; nested_write_3d_requires_1 (source line 0)
(define-fun nested_write_3d_requires_1 () Bool (and (<= 0 p_nested_write_3d_j) (< p_nested_write_3d_j 2)))

; nested_write_3d_requires_2 (source line 0)
(define-fun nested_write_3d_requires_2 () Bool (and (<= 0 p_nested_write_3d_k) (< p_nested_write_3d_k 2)))

; ---- ensures (signature-level, fallback) ----
; nested_write_3d_ensures_0 (source line 25)
(define-fun nested_write_3d_ensures_0 () Bool (= (select (select (select p_nested_write_3d_arr p_nested_write_3d_i) p_nested_write_3d_j) p_nested_write_3d_k) p_nested_write_3d_x))

; --- discharge (nested_write_3d_ensures_0) ---
(push)
(assert nested_write_3d_requires_0)
(assert nested_write_3d_requires_1)
(assert nested_write_3d_requires_2)
(assert (not nested_write_3d_ensures_0))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; nested_write_3d_ensures_ret_0_0 (return-site ensures, source line 25)
(define-fun nested_write_3d_ensures_ret_0_0 () Bool (= (select (select (select (store p_nested_write_3d_arr p_nested_write_3d_i (store (select p_nested_write_3d_arr p_nested_write_3d_i) p_nested_write_3d_j (store (select (select p_nested_write_3d_arr p_nested_write_3d_i) p_nested_write_3d_j) p_nested_write_3d_k p_nested_write_3d_x))) p_nested_write_3d_i) p_nested_write_3d_j) p_nested_write_3d_k) p_nested_write_3d_x))

; --- discharge (nested_write_3d_ensures_ret_0_0) ---
(push)
(assert nested_write_3d_requires_0)
(assert nested_write_3d_requires_1)
(assert nested_write_3d_requires_2)
(assert (not nested_write_3d_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function main
; ============================================================

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
(declare-const main_ph0 Int)
; note: replaced an unsupported subform (unsupported expr) with the uninterpreted constant main_ph0
(declare-const main_call_result_0 Int)
; main_call_requires_1_0 (call requires, source line 0)
(define-fun main_call_requires_1_0 () Bool (and (<= 0 1) (< 1 2)))

; --- discharge (main_call_requires_1_0) ---
(push)
(assert (not main_call_requires_1_0))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_2_1 (call requires, source line 0)
(define-fun main_call_requires_2_1 () Bool (and (<= 0 1) (< 1 2)))

; --- discharge (main_call_requires_2_1) ---
(push)
(assert (not main_call_requires_2_1))
(check-sat-using (then simplify smt))
(pop)

(define-fun main_inline_3_req0 () Bool (and (<= 0 1) (< 1 2)))
(define-fun main_inline_3_req1 () Bool (and (<= 0 1) (< 1 2)))
(declare-const main_ph1 Int)
; note: replaced an unsupported subform (unsupported expr) with the uninterpreted constant main_ph1
(declare-const main_call_result_4 Int)
; main_call_requires_5_0 (call requires, source line 0)
(define-fun main_call_requires_5_0 () Bool (and (<= 0 0) (< 0 2)))

; --- discharge (main_call_requires_5_0) ---
(push)
(assert (not main_call_requires_5_0))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_6_1 (call requires, source line 0)
(define-fun main_call_requires_6_1 () Bool (and (<= 0 0) (< 0 2)))

; --- discharge (main_call_requires_6_1) ---
(push)
(assert (not main_call_requires_6_1))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_7_2 (call requires, source line 0)
(define-fun main_call_requires_7_2 () Bool (and (<= 0 0) (< 0 2)))

; --- discharge (main_call_requires_7_2) ---
(push)
(assert (not main_call_requires_7_2))
(check-sat-using (then simplify smt))
(pop)

(define-fun main_inline_8_req0 () Bool (and (<= 0 0) (< 0 2)))
(define-fun main_inline_8_req1 () Bool (and (<= 0 0) (< 0 2)))
(define-fun main_inline_8_req2 () Bool (and (<= 0 0) (< 0 2)))


; ############################################################
; # Ghost encoder section (T1/T2/T3 + Missing-#6) — appended by src/Ghost.cpp
; # These constructs carry no runtime state; they exist so the SMT
; # prover can reason about the ABSTRACT layer (spec fns, refines),
; # ghost state (ghost let/fn), modular frame conditions
; # (regions + modifies), AND per-handler invariant preservation
; # (preserves, Missing-#6). No-op discharge is `sat` (honest); `unsat`
; # on a refinement/frame/preservation query means it holds for all args.
; ############################################################

; ============================================================
; T1 — spec fns (abstract layer; refines targets live here)
; ============================================================
; spec fn slot2_ok (source line 31)
(declare-fun sf_slot2_ok (Int Int Int (Array Int (Array Int Int))) Bool)
; note: spec fn slot2_ok body references calls/array/field — declared uninterpreted


; ============================================================
; Missing-#6 — preserves (per-handler invariant-preservation)
; ============================================================

; preserves nested_write_2d <= slot2_ok  (source line 37)
(declare-const preserves_nested_write_2d_slot2_ok_arg_0 (Array Int (Array Int Int)))
(declare-const preserves_nested_write_2d_slot2_ok_arg_1 Int)
(declare-const preserves_nested_write_2d_slot2_ok_arg_2 Int)
(declare-const preserves_nested_write_2d_slot2_ok_arg_3 Int)
(declare-const preserves_nested_write_2d_slot2_ok_call_result_0 Int)
; preserves_nested_write_2d_slot2_ok_call_requires_1_0 (call requires, source line 0)
(define-fun preserves_nested_write_2d_slot2_ok_call_requires_1_0 () Bool (and (<= 0 preserves_nested_write_2d_slot2_ok_arg_1) (< preserves_nested_write_2d_slot2_ok_arg_1 2)))

; --- discharge (preserves_nested_write_2d_slot2_ok_call_requires_1_0) ---
(push)
(assert (not preserves_nested_write_2d_slot2_ok_call_requires_1_0))
(check-sat-using (then simplify smt))
(pop)

; preserves_nested_write_2d_slot2_ok_call_requires_2_1 (call requires, source line 0)
(define-fun preserves_nested_write_2d_slot2_ok_call_requires_2_1 () Bool (and (<= 0 preserves_nested_write_2d_slot2_ok_arg_2) (< preserves_nested_write_2d_slot2_ok_arg_2 2)))

; --- discharge (preserves_nested_write_2d_slot2_ok_call_requires_2_1) ---
(push)
(assert (not preserves_nested_write_2d_slot2_ok_call_requires_2_1))
(check-sat-using (then simplify smt))
(pop)

(define-fun preserves_nested_write_2d_slot2_ok_inline_3_req0 () Bool (and (<= 0 preserves_nested_write_2d_slot2_ok_arg_1) (< preserves_nested_write_2d_slot2_ok_arg_1 2)))
(define-fun preserves_nested_write_2d_slot2_ok_inline_3_req1 () Bool (and (<= 0 preserves_nested_write_2d_slot2_ok_arg_2) (< preserves_nested_write_2d_slot2_ok_arg_2 2)))
(declare-const old_preserves_nested_write_2d_slot2_ok_arg_0 (Array Int (Array Int Int)))
(assert (= old_preserves_nested_write_2d_slot2_ok_arg_0 preserves_nested_write_2d_slot2_ok_arg_0))
; preservation obligation: forall args, reqHandler ==> I(args, result)
;   reqHandler = conjunction of handler's requires clauses
;   I(args, result) = invariant spec fn body, with `result` bound to
;     the #2 WP mini-walker's inlined body terminal term
;     (NOT a fresh const + assumed ensures — soundness-critical)
(push)
(assert (not (forall ((preserves_nested_write_2d_slot2_ok_arg_0 (Array Int (Array Int Int))) (preserves_nested_write_2d_slot2_ok_arg_1 Int) (preserves_nested_write_2d_slot2_ok_arg_2 Int) (preserves_nested_write_2d_slot2_ok_arg_3 Int)) (=> (and (and (<= 0 (store preserves_nested_write_2d_slot2_ok_arg_0 preserves_nested_write_2d_slot2_ok_arg_1 (store (select preserves_nested_write_2d_slot2_ok_arg_0 preserves_nested_write_2d_slot2_ok_arg_1) preserves_nested_write_2d_slot2_ok_arg_2 preserves_nested_write_2d_slot2_ok_arg_3))) (< (store preserves_nested_write_2d_slot2_ok_arg_0 preserves_nested_write_2d_slot2_ok_arg_1 (store (select preserves_nested_write_2d_slot2_ok_arg_0 preserves_nested_write_2d_slot2_ok_arg_1) preserves_nested_write_2d_slot2_ok_arg_2 preserves_nested_write_2d_slot2_ok_arg_3)) 2)) (and (<= 0 preserves_nested_write_2d_slot2_ok_arg_2) (< preserves_nested_write_2d_slot2_ok_arg_2 2))) (= (select (select preserves_nested_write_2d_slot2_ok_arg_3 (store preserves_nested_write_2d_slot2_ok_arg_0 preserves_nested_write_2d_slot2_ok_arg_1 (store (select preserves_nested_write_2d_slot2_ok_arg_0 preserves_nested_write_2d_slot2_ok_arg_1) preserves_nested_write_2d_slot2_ok_arg_2 preserves_nested_write_2d_slot2_ok_arg_3))) preserves_nested_write_2d_slot2_ok_arg_2) preserves_nested_write_2d_slot2_ok_arg_3)))))
(check-sat-using (then simplify smt))
(pop)
; note: preserves discharge — unsat => handler preserves invariant

(exit)
