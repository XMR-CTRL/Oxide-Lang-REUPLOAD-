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
; function f
; ============================================================
(declare-const p_f_x Int)
(declare-const f_result Int)

; ---- requires (assumed, not discharged) ----
; f_requires_0 (source line 26)
(define-fun f_requires_0 () Bool (>= p_f_x 0))

; ---- ensures (signature-level, fallback) ----
; f_ensures_0 (source line 27)
(define-fun f_ensures_0 () Bool (= f_result p_f_x))

; --- discharge (f_ensures_0) ---
(push)
(assert f_requires_0)
(assert (not f_ensures_0))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; f_ensures_ret_0_0 (return-site ensures, source line 27)
(define-fun f_ensures_ret_0_0 () Bool (= p_f_x p_f_x))

; --- discharge (f_ensures_ret_0_0) ---
(push)
(assert f_requires_0)
(assert (not f_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function e
; ============================================================
(declare-const p_e_x Int)
(declare-const e_result Int)

; ---- requires (assumed, not discharged) ----
; e_requires_0 (source line 33)
(define-fun e_requires_0 () Bool (>= p_e_x 0))

; ---- ensures (signature-level, fallback) ----
; e_ensures_0 (source line 34)
(define-fun e_ensures_0 () Bool (= e_result (+ p_e_x 1)))

; --- discharge (e_ensures_0) ---
(push)
(assert e_requires_0)
(assert (not e_ensures_0))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
(declare-const e_call_result_0 Int)
; e_call_requires_1_0 (call requires, source line 26)
(define-fun e_call_requires_1_0 () Bool (>= (+ p_e_x 1) 0))

; --- discharge (e_call_requires_1_0) ---
(push)
(assert e_requires_0)
(assert (not e_call_requires_1_0))
(check-sat-using (then simplify smt))
(pop)

; e_ensures_ret_2_0 (return-site ensures, source line 34)
(define-fun e_ensures_ret_2_0 () Bool (= (+ p_e_x 1) (+ p_e_x 1)))

; --- discharge (e_ensures_ret_2_0) ---
(push)
(assert e_requires_0)
(assert (not e_ensures_ret_2_0))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function d
; ============================================================
(declare-const p_d_x Int)
(declare-const d_result Int)

; ---- requires (assumed, not discharged) ----
; d_requires_0 (source line 40)
(define-fun d_requires_0 () Bool (>= p_d_x 0))

; ---- ensures (signature-level, fallback) ----
; d_ensures_0 (source line 41)
(define-fun d_ensures_0 () Bool (= d_result (+ p_d_x 2)))

; --- discharge (d_ensures_0) ---
(push)
(assert d_requires_0)
(assert (not d_ensures_0))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
(declare-const d_call_result_0 Int)
; d_call_requires_1_0 (call requires, source line 33)
(define-fun d_call_requires_1_0 () Bool (>= (+ p_d_x 1) 0))

; --- discharge (d_call_requires_1_0) ---
(push)
(assert d_requires_0)
(assert (not d_call_requires_1_0))
(check-sat-using (then simplify smt))
(pop)

(declare-const d_call_result_2 Int)
; d_call_requires_3_0 (call requires, source line 26)
(define-fun d_call_requires_3_0 () Bool (>= (+ (+ p_d_x 1) 1) 0))

; --- discharge (d_call_requires_3_0) ---
(push)
(assert d_requires_0)
(assert (not d_call_requires_3_0))
(check-sat-using (then simplify smt))
(pop)

; d_ensures_ret_4_0 (return-site ensures, source line 41)
(define-fun d_ensures_ret_4_0 () Bool (= (+ (+ p_d_x 1) 1) (+ p_d_x 2)))

; --- discharge (d_ensures_ret_4_0) ---
(push)
(assert d_requires_0)
(assert (not d_ensures_ret_4_0))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function c
; ============================================================
(declare-const p_c_x Int)
(declare-const c_result Int)

; ---- requires (assumed, not discharged) ----
; c_requires_0 (source line 47)
(define-fun c_requires_0 () Bool (>= p_c_x 0))

; ---- ensures (signature-level, fallback) ----
; c_ensures_0 (source line 48)
(define-fun c_ensures_0 () Bool (= c_result (+ p_c_x 3)))

; --- discharge (c_ensures_0) ---
(push)
(assert c_requires_0)
(assert (not c_ensures_0))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
(declare-const c_call_result_0 Int)
; c_call_requires_1_0 (call requires, source line 40)
(define-fun c_call_requires_1_0 () Bool (>= (+ p_c_x 1) 0))

; --- discharge (c_call_requires_1_0) ---
(push)
(assert c_requires_0)
(assert (not c_call_requires_1_0))
(check-sat-using (then simplify smt))
(pop)

(declare-const c_call_result_2 Int)
; c_call_requires_3_0 (call requires, source line 33)
(define-fun c_call_requires_3_0 () Bool (>= (+ (+ p_c_x 1) 1) 0))

; --- discharge (c_call_requires_3_0) ---
(push)
(assert c_requires_0)
(assert (not c_call_requires_3_0))
(check-sat-using (then simplify smt))
(pop)

(declare-const c_call_result_4 Int)
; c_call_requires_5_0 (call requires, source line 26)
(define-fun c_call_requires_5_0 () Bool (>= (+ (+ (+ p_c_x 1) 1) 1) 0))

; --- discharge (c_call_requires_5_0) ---
(push)
(assert c_requires_0)
(assert (not c_call_requires_5_0))
(check-sat-using (then simplify smt))
(pop)

; c_ensures_ret_6_0 (return-site ensures, source line 48)
(define-fun c_ensures_ret_6_0 () Bool (= (+ (+ (+ p_c_x 1) 1) 1) (+ p_c_x 3)))

; --- discharge (c_ensures_ret_6_0) ---
(push)
(assert c_requires_0)
(assert (not c_ensures_ret_6_0))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function b
; ============================================================
(declare-const p_b_x Int)
(declare-const b_result Int)

; ---- requires (assumed, not discharged) ----
; b_requires_0 (source line 54)
(define-fun b_requires_0 () Bool (>= p_b_x 0))

; ---- ensures (signature-level, fallback) ----
; b_ensures_0 (source line 55)
(define-fun b_ensures_0 () Bool (= b_result (+ p_b_x 4)))

; --- discharge (b_ensures_0) ---
(push)
(assert b_requires_0)
(assert (not b_ensures_0))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
(declare-const b_call_result_0 Int)
; b_call_requires_1_0 (call requires, source line 47)
(define-fun b_call_requires_1_0 () Bool (>= (+ p_b_x 1) 0))

; --- discharge (b_call_requires_1_0) ---
(push)
(assert b_requires_0)
(assert (not b_call_requires_1_0))
(check-sat-using (then simplify smt))
(pop)

(declare-const b_call_result_2 Int)
; b_call_requires_3_0 (call requires, source line 40)
(define-fun b_call_requires_3_0 () Bool (>= (+ (+ p_b_x 1) 1) 0))

; --- discharge (b_call_requires_3_0) ---
(push)
(assert b_requires_0)
(assert (not b_call_requires_3_0))
(check-sat-using (then simplify smt))
(pop)

(declare-const b_call_result_4 Int)
; b_call_requires_5_0 (call requires, source line 33)
(define-fun b_call_requires_5_0 () Bool (>= (+ (+ (+ p_b_x 1) 1) 1) 0))

; --- discharge (b_call_requires_5_0) ---
(push)
(assert b_requires_0)
(assert (not b_call_requires_5_0))
(check-sat-using (then simplify smt))
(pop)

(declare-const b_call_result_6 Int)
; b_call_requires_7_0 (call requires, source line 26)
(define-fun b_call_requires_7_0 () Bool (>= (+ (+ (+ (+ p_b_x 1) 1) 1) 1) 0))

; --- discharge (b_call_requires_7_0) ---
(push)
(assert b_requires_0)
(assert (not b_call_requires_7_0))
(check-sat-using (then simplify smt))
(pop)

; b_ensures_ret_8_0 (return-site ensures, source line 55)
(define-fun b_ensures_ret_8_0 () Bool (= (+ (+ (+ (+ p_b_x 1) 1) 1) 1) (+ p_b_x 4)))

; --- discharge (b_ensures_ret_8_0) ---
(push)
(assert b_requires_0)
(assert (not b_ensures_ret_8_0))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function a
; ============================================================
(declare-const p_a_x Int)
(declare-const a_result Int)

; ---- requires (assumed, not discharged) ----
; a_requires_0 (source line 61)
(define-fun a_requires_0 () Bool (>= p_a_x 0))

; ---- ensures (signature-level, fallback) ----
; a_ensures_0 (source line 62)
(define-fun a_ensures_0 () Bool (= a_result (+ p_a_x 5)))

; --- discharge (a_ensures_0) ---
(push)
(assert a_requires_0)
(assert (not a_ensures_0))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
(declare-const a_call_result_0 Int)
; a_call_requires_1_0 (call requires, source line 54)
(define-fun a_call_requires_1_0 () Bool (>= (+ p_a_x 1) 0))

; --- discharge (a_call_requires_1_0) ---
(push)
(assert a_requires_0)
(assert (not a_call_requires_1_0))
(check-sat-using (then simplify smt))
(pop)

(declare-const a_call_result_2 Int)
; a_call_requires_3_0 (call requires, source line 47)
(define-fun a_call_requires_3_0 () Bool (>= (+ (+ p_a_x 1) 1) 0))

; --- discharge (a_call_requires_3_0) ---
(push)
(assert a_requires_0)
(assert (not a_call_requires_3_0))
(check-sat-using (then simplify smt))
(pop)

(declare-const a_call_result_4 Int)
; a_call_requires_5_0 (call requires, source line 40)
(define-fun a_call_requires_5_0 () Bool (>= (+ (+ (+ p_a_x 1) 1) 1) 0))

; --- discharge (a_call_requires_5_0) ---
(push)
(assert a_requires_0)
(assert (not a_call_requires_5_0))
(check-sat-using (then simplify smt))
(pop)

(declare-const a_call_result_6 Int)
; a_call_requires_7_0 (call requires, source line 33)
(define-fun a_call_requires_7_0 () Bool (>= (+ (+ (+ (+ p_a_x 1) 1) 1) 1) 0))

; --- discharge (a_call_requires_7_0) ---
(push)
(assert a_requires_0)
(assert (not a_call_requires_7_0))
(check-sat-using (then simplify smt))
(pop)

(declare-const a_call_result_8 Int)
; note: callee 'f' hit max inline depth (4) — assuming ensures (recursive or max depth) at call site instead of inlining body
(define-fun a_rec_req_9_0 () Bool (>= (+ (+ (+ (+ (+ p_a_x 1) 1) 1) 1) 1) 0))
(assert a_rec_req_9_0)
(define-fun a_rec_ens_10_0 () Bool (= a_call_result_8 (+ (+ (+ (+ (+ p_a_x 1) 1) 1) 1) 1)))
(assert a_rec_ens_10_0)
; a_ensures_ret_11_0 (return-site ensures, source line 62)
(define-fun a_ensures_ret_11_0 () Bool (= a_call_result_8 (+ p_a_x 5)))

; --- discharge (a_ensures_ret_11_0) ---
(push)
(assert a_requires_0)
(assert (not a_ensures_ret_11_0))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function main
; ============================================================

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
(declare-const main_call_result_0 Int)
; main_call_requires_1_0 (call requires, source line 61)
(define-fun main_call_requires_1_0 () Bool (>= 0 0))

; --- discharge (main_call_requires_1_0) ---
(push)
(assert (not main_call_requires_1_0))
(check-sat-using (then simplify smt))
(pop)

(declare-const main_call_result_2 Int)
; main_call_requires_3_0 (call requires, source line 54)
(define-fun main_call_requires_3_0 () Bool (>= (+ 0 1) 0))

; --- discharge (main_call_requires_3_0) ---
(push)
(assert (not main_call_requires_3_0))
(check-sat-using (then simplify smt))
(pop)

(declare-const main_call_result_4 Int)
; main_call_requires_5_0 (call requires, source line 47)
(define-fun main_call_requires_5_0 () Bool (>= (+ (+ 0 1) 1) 0))

; --- discharge (main_call_requires_5_0) ---
(push)
(assert (not main_call_requires_5_0))
(check-sat-using (then simplify smt))
(pop)

(declare-const main_call_result_6 Int)
; main_call_requires_7_0 (call requires, source line 40)
(define-fun main_call_requires_7_0 () Bool (>= (+ (+ (+ 0 1) 1) 1) 0))

; --- discharge (main_call_requires_7_0) ---
(push)
(assert (not main_call_requires_7_0))
(check-sat-using (then simplify smt))
(pop)

(declare-const main_call_result_8 Int)
; note: callee 'e' hit max inline depth (4) — assuming ensures (recursive or max depth) at call site instead of inlining body
(define-fun main_rec_req_9_0 () Bool (>= (+ (+ (+ (+ 0 1) 1) 1) 1) 0))
(assert main_rec_req_9_0)
(define-fun main_rec_ens_10_0 () Bool (= main_call_result_8 (+ (+ (+ (+ (+ 0 1) 1) 1) 1) 1)))
(assert main_rec_ens_10_0)
(declare-const main_ph0 Int)
; note: replaced an unsupported subform (unsupported expr) with the uninterpreted constant main_ph0

(exit)
