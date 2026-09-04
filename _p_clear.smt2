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
; function map_page_mut
; ============================================================
(declare-const p_map_page_mut_idx Int)
(declare-const p_map_page_mut_gpa Int)
(declare-const p_map_page_mut_ept (Array Int Int))
(declare-const map_page_mut_result Int)

; ---- requires (assumed, not discharged) ----
; map_page_mut_requires_0 (source line 0)
(define-fun map_page_mut_requires_0 () Bool (and (<= 0 p_map_page_mut_idx) (< p_map_page_mut_idx 4)))

; map_page_mut_requires_1 (source line 18)
(define-fun map_page_mut_requires_1 () Bool (= (bvand ((_ int2bv 64) p_map_page_mut_gpa) (_ bv4095 64)) (_ bv0 64)))

; ---- ensures (signature-level, fallback) ----
; map_page_mut_ensures_0 (source line 19)
(define-fun map_page_mut_ensures_0 () Bool (= map_page_mut_result p_map_page_mut_gpa))

; --- discharge (map_page_mut_ensures_0) ---
(push)
(assert map_page_mut_requires_0)
(assert map_page_mut_requires_1)
(assert (not map_page_mut_ensures_0))
(check-sat)
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; map_page_mut_ensures_ret_0_0 (return-site ensures, source line 19)
(define-fun map_page_mut_ensures_ret_0_0 () Bool (= p_map_page_mut_gpa p_map_page_mut_gpa))

; --- discharge (map_page_mut_ensures_ret_0_0) ---
(push)
(assert map_page_mut_requires_0)
(assert map_page_mut_requires_1)
(assert (not map_page_mut_ensures_ret_0_0))
(check-sat)
(pop)


; ============================================================
; function clear_page
; ============================================================
(declare-const p_clear_page_idx Int)
(declare-const p_clear_page_gpa Int)
(declare-const p_clear_page_ept (Array Int Int))
(declare-const clear_page_result Int)

; ---- requires (assumed, not discharged) ----
; clear_page_requires_0 (source line 0)
(define-fun clear_page_requires_0 () Bool (and (<= 0 p_clear_page_idx) (< p_clear_page_idx 4)))

; ---- ensures (signature-level, fallback) ----
; clear_page_ensures_0 (source line 28)
(define-fun clear_page_ensures_0 () Bool (= clear_page_result 0))

; --- discharge (clear_page_ensures_0) ---
(push)
(assert clear_page_requires_0)
(assert (not clear_page_ensures_0))
(check-sat)
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; clear_page_ensures_ret_0_0 (return-site ensures, source line 28)
(define-fun clear_page_ensures_ret_0_0 () Bool (= 0 0))

; --- discharge (clear_page_ensures_ret_0_0) ---
(push)
(assert clear_page_requires_0)
(assert (not clear_page_ensures_ret_0_0))
(check-sat)
(pop)


; ============================================================
; function dispatch_mut
; ============================================================
(declare-const p_dispatch_mut_idx Int)
(declare-const p_dispatch_mut_gpa Int)
(declare-const p_dispatch_mut_ept (Array Int Int))
(declare-const dispatch_mut_result Int)

; ---- requires (assumed, not discharged) ----
; dispatch_mut_requires_0 (source line 0)
(define-fun dispatch_mut_requires_0 () Bool (and (<= 0 p_dispatch_mut_idx) (< p_dispatch_mut_idx 4)))

; dispatch_mut_requires_1 (source line 37)
(define-fun dispatch_mut_requires_1 () Bool (= (bvand ((_ int2bv 64) p_dispatch_mut_gpa) (_ bv4095 64)) (_ bv0 64)))

; ---- ensures (signature-level, fallback) ----
; dispatch_mut_ensures_0 (source line 38)
(define-fun dispatch_mut_ensures_0 () Bool (>= dispatch_mut_result 0))

; --- discharge (dispatch_mut_ensures_0) ---
(push)
(assert dispatch_mut_requires_0)
(assert dispatch_mut_requires_1)
(assert (not dispatch_mut_ensures_0))
(check-sat)
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
(declare-const dispatch_mut_call_result_0 Int)
; dispatch_mut_call_requires_1_0 (call requires, source line 0)
(define-fun dispatch_mut_call_requires_1_0 () Bool (and (<= 0 p_dispatch_mut_idx) (< p_dispatch_mut_idx 4)))

; --- discharge (dispatch_mut_call_requires_1_0) ---
(push)
(assert dispatch_mut_requires_0)
(assert dispatch_mut_requires_1)
(assert (= p_dispatch_mut_idx 0))
(assert (not dispatch_mut_call_requires_1_0))
(check-sat)
(pop)

; dispatch_mut_call_requires_2_1 (call requires, source line 18)
(define-fun dispatch_mut_call_requires_2_1 () Bool (= (bvand ((_ int2bv 64) p_dispatch_mut_gpa) (_ bv4095 64)) (_ bv0 64)))

; --- discharge (dispatch_mut_call_requires_2_1) ---
(push)
(assert dispatch_mut_requires_0)
(assert dispatch_mut_requires_1)
(assert (= p_dispatch_mut_idx 0))
(assert (not dispatch_mut_call_requires_2_1))
(check-sat)
(pop)

(define-fun dispatch_mut_inline_3_req0 () Bool (and (<= 0 p_dispatch_mut_idx) (< p_dispatch_mut_idx 4)))
(define-fun dispatch_mut_inline_3_req1 () Bool (= (bvand ((_ int2bv 64) p_dispatch_mut_gpa) (_ bv4095 64)) (_ bv0 64)))
; dispatch_mut_ensures_ret_4_0 (return-site ensures, source line 38)
(define-fun dispatch_mut_ensures_ret_4_0 () Bool (>= p_dispatch_mut_gpa 0))

; --- discharge (dispatch_mut_ensures_ret_4_0) ---
(push)
(assert dispatch_mut_requires_0)
(assert dispatch_mut_requires_1)
(assert (= p_dispatch_mut_idx 0))
(assert (not dispatch_mut_ensures_ret_4_0))
(check-sat)
(pop)

(declare-const dispatch_mut_call_result_5 Int)
; dispatch_mut_call_requires_6_0 (call requires, source line 0)
(define-fun dispatch_mut_call_requires_6_0 () Bool (and (<= 0 p_dispatch_mut_idx) (< p_dispatch_mut_idx 4)))

; --- discharge (dispatch_mut_call_requires_6_0) ---
(push)
(assert dispatch_mut_requires_0)
(assert dispatch_mut_requires_1)
(assert (not dispatch_mut_call_requires_6_0))
(check-sat)
(pop)

(define-fun dispatch_mut_inline_7_req0 () Bool (and (<= 0 p_dispatch_mut_idx) (< p_dispatch_mut_idx 4)))
; dispatch_mut_ensures_ret_8_0 (return-site ensures, source line 38)
(define-fun dispatch_mut_ensures_ret_8_0 () Bool (>= 0 0))

; --- discharge (dispatch_mut_ensures_ret_8_0) ---
(push)
(assert dispatch_mut_requires_0)
(assert dispatch_mut_requires_1)
(assert (not dispatch_mut_ensures_ret_8_0))
(check-sat)
(pop)


; ============================================================
; function main
; ============================================================

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
(declare-const main_ph0 Int)
; note: replaced an unsupported subform (unsupported expr) with the uninterpreted constant main_ph0
(declare-const main_call_result_0 Int)
; main_call_requires_1_0 (call requires, source line 0)
(define-fun main_call_requires_1_0 () Bool (and (<= 0 0) (< 0 4)))

; --- discharge (main_call_requires_1_0) ---
(push)
(assert (not main_call_requires_1_0))
(check-sat)
(pop)

; main_call_requires_2_1 (call requires, source line 37)
(define-fun main_call_requires_2_1 () Bool (= (bvand ((_ int2bv 64) 8192) (_ bv4095 64)) (_ bv0 64)))

; --- discharge (main_call_requires_2_1) ---
(push)
(assert (not main_call_requires_2_1))
(check-sat)
(pop)

(define-fun main_inline_3_req0 () Bool (and (<= 0 0) (< 0 4)))
(define-fun main_inline_3_req1 () Bool (= (bvand ((_ int2bv 64) 8192) (_ bv4095 64)) (_ bv0 64)))
(declare-const main_call_result_4 Int)
; main_call_requires_5_0 (call requires, source line 0)
(define-fun main_call_requires_5_0 () Bool (and (<= 0 0) (< 0 4)))

; --- discharge (main_call_requires_5_0) ---
(push)
(assert main_inline_3_req0)
(assert main_inline_3_req1)
(assert (= 0 0))
(assert (not main_call_requires_5_0))
(check-sat)
(pop)

; main_call_requires_6_1 (call requires, source line 18)
(define-fun main_call_requires_6_1 () Bool (= (bvand ((_ int2bv 64) 8192) (_ bv4095 64)) (_ bv0 64)))

; --- discharge (main_call_requires_6_1) ---
(push)
(assert main_inline_3_req0)
(assert main_inline_3_req1)
(assert (= 0 0))
(assert (not main_call_requires_6_1))
(check-sat)
(pop)

(define-fun main_inline_7_req0 () Bool (and (<= 0 0) (< 0 4)))
(define-fun main_inline_7_req1 () Bool (= (bvand ((_ int2bv 64) 8192) (_ bv4095 64)) (_ bv0 64)))
(declare-const main_call_result_8 Int)
; main_call_requires_9_0 (call requires, source line 0)
(define-fun main_call_requires_9_0 () Bool (and (<= 0 0) (< 0 4)))

; --- discharge (main_call_requires_9_0) ---
(push)
(assert main_inline_3_req0)
(assert main_inline_3_req1)
(assert (not main_call_requires_9_0))
(check-sat)
(pop)

(define-fun main_inline_10_req0 () Bool (and (<= 0 0) (< 0 4)))


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
; spec fn page_aligned (source line 4)
(define-fun sf_page_aligned ((p_sf_page_aligned_gpa Int)) Bool (= (bvand ((_ int2bv 64) p_sf_page_aligned_gpa) (_ bv4095 64)) (_ bv0 64)))

; spec fn ept_aligned_at (source line 12)
(declare-fun sf_ept_aligned_at (Int Int (Array Int Int)) Bool)
; note: spec fn ept_aligned_at body references calls/array/field — declared uninterpreted


; ============================================================

; Missing-#6 — preserves (per-handler invariant-preservation)
; ============================================================

; preserves map_page_mut <= ept_aligned_at  (source line 49)
(declare-const preserves_map_page_mut_ept_aligned_at_arg_0 Int)
(declare-const preserves_map_page_mut_ept_aligned_at_arg_1 Int)
(declare-const preserves_map_page_mut_ept_aligned_at_arg_2 (Array Int Int))
(declare-const preserves_map_page_mut_ept_aligned_at_call_result_0 Int)
; preserves_map_page_mut_ept_aligned_at_call_requires_1_0 (call requires, source line 0)
(define-fun preserves_map_page_mut_ept_aligned_at_call_requires_1_0 () Bool (and (<= 0 preserves_map_page_mut_ept_aligned_at_arg_0) (< preserves_map_page_mut_ept_aligned_at_arg_0 4)))

; --- discharge (preserves_map_page_mut_ept_aligned_at_call_requires_1_0) ---
(push)
(assert (not preserves_map_page_mut_ept_aligned_at_call_requires_1_0))
(check-sat)
(pop)

; preserves_map_page_mut_ept_aligned_at_call_requires_2_1 (call requires, source line 18)
(define-fun preserves_map_page_mut_ept_aligned_at_call_requires_2_1 () Bool (= (bvand ((_ int2bv 64) preserves_map_page_mut_ept_aligned_at_arg_1) (_ bv4095 64)) (_ bv0 64)))

; --- discharge (preserves_map_page_mut_ept_aligned_at_call_requires_2_1) ---
(push)
(assert (not preserves_map_page_mut_ept_aligned_at_call_requires_2_1))
(check-sat)
(pop)

(define-fun preserves_map_page_mut_ept_aligned_at_inline_3_req0 () Bool (and (<= 0 preserves_map_page_mut_ept_aligned_at_arg_0) (< preserves_map_page_mut_ept_aligned_at_arg_0 4)))
(define-fun preserves_map_page_mut_ept_aligned_at_inline_3_req1 () Bool (= (bvand ((_ int2bv 64) preserves_map_page_mut_ept_aligned_at_arg_1) (_ bv4095 64)) (_ bv0 64)))
(declare-const old_preserves_map_page_mut_ept_aligned_at_arg_2 (Array Int Int))
(assert (= old_preserves_map_page_mut_ept_aligned_at_arg_2 preserves_map_page_mut_ept_aligned_at_arg_2))
; preservation obligation: forall args, reqHandler ==> I(args, result)
;   reqHandler = conjunction of handler's requires clauses
;   I(args, result) = invariant spec fn body, with `result` bound to
;     the #2 WP mini-walker's inlined body terminal term
;     (NOT a fresh const + assumed ensures — soundness-critical)
(push)
(assert (not (forall ((preserves_map_page_mut_ept_aligned_at_arg_0 Int) (preserves_map_page_mut_ept_aligned_at_arg_1 Int) (preserves_map_page_mut_ept_aligned_at_arg_2 (Array Int Int))) (=> (and (and (<= 0 preserves_map_page_mut_ept_aligned_at_arg_0) (< preserves_map_page_mut_ept_aligned_at_arg_0 4)) (= (bvand ((_ int2bv 64) preserves_map_page_mut_ept_aligned_at_arg_1) (_ bv4095 64)) (_ bv0 64))) (= (bvand ((_ int2bv 64) (select (store preserves_map_page_mut_ept_aligned_at_arg_2 preserves_map_page_mut_ept_aligned_at_arg_0 preserves_map_page_mut_ept_aligned_at_arg_1) preserves_map_page_mut_ept_aligned_at_arg_0)) (_ bv4095 64)) (_ bv0 64))))))
(check-sat)
(pop)
; note: preserves discharge — unsat => handler preserves invariant


; preserves clear_page <= ept_aligned_at  (source line 50)
(declare-const preserves_clear_page_ept_aligned_at_arg_0 Int)
(declare-const preserves_clear_page_ept_aligned_at_arg_1 Int)
(declare-const preserves_clear_page_ept_aligned_at_arg_2 (Array Int Int))
(declare-const preserves_clear_page_ept_aligned_at_call_result_0 Int)
; preserves_clear_page_ept_aligned_at_call_requires_1_0 (call requires, source line 0)
(define-fun preserves_clear_page_ept_aligned_at_call_requires_1_0 () Bool (and (<= 0 preserves_clear_page_ept_aligned_at_arg_0) (< preserves_clear_page_ept_aligned_at_arg_0 4)))

; --- discharge (preserves_clear_page_ept_aligned_at_call_requires_1_0) ---
(push)
(assert (not preserves_clear_page_ept_aligned_at_call_requires_1_0))
(check-sat)
(pop)

(define-fun preserves_clear_page_ept_aligned_at_inline_2_req0 () Bool (and (<= 0 preserves_clear_page_ept_aligned_at_arg_0) (< preserves_clear_page_ept_aligned_at_arg_0 4)))
(declare-const old_preserves_clear_page_ept_aligned_at_arg_2 (Array Int Int))
(assert (= old_preserves_clear_page_ept_aligned_at_arg_2 preserves_clear_page_ept_aligned_at_arg_2))
; preservation obligation: forall args, reqHandler ==> I(args, result)
;   reqHandler = conjunction of handler's requires clauses
;   I(args, result) = invariant spec fn body, with `result` bound to
;     the #2 WP mini-walker's inlined body terminal term
;     (NOT a fresh const + assumed ensures — soundness-critical)
(push)
(assert (not (forall ((preserves_clear_page_ept_aligned_at_arg_0 Int) (preserves_clear_page_ept_aligned_at_arg_1 Int) (preserves_clear_page_ept_aligned_at_arg_2 (Array Int Int))) (=> (and (<= 0 preserves_clear_page_ept_aligned_at_arg_0) (< preserves_clear_page_ept_aligned_at_arg_0 4)) (= (bvand ((_ int2bv 64) (select (store preserves_clear_page_ept_aligned_at_arg_2 preserves_clear_page_ept_aligned_at_arg_0 0) preserves_clear_page_ept_aligned_at_arg_0)) (_ bv4095 64)) (_ bv0 64))))))
(check-sat)
(pop)
; note: preserves discharge — unsat => handler preserves invariant

(exit)
