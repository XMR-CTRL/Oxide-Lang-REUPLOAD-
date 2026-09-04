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
; function map_page
; ============================================================
(declare-const p_map_page_idx Int)
(declare-const p_map_page_gpa Int)
(declare-const map_page_result Int)

; ---- requires (assumed, not discharged) ----
; map_page_requires_0 (source line 0)
(define-fun map_page_requires_0 () Bool (and (<= 0 p_map_page_idx) (< p_map_page_idx 4)))

; map_page_requires_1 (source line 42)
(define-fun map_page_requires_1 () Bool (= (bvand ((_ int2bv 64) p_map_page_gpa) (_ bv4095 64)) (_ bv0 64)))

; ---- ensures (signature-level, fallback) ----
; map_page_ensures_0 (source line 43)
(define-fun map_page_ensures_0 () Bool (= map_page_result p_map_page_idx))

; --- discharge (map_page_ensures_0) ---
(push)
(assert map_page_requires_0)
(assert map_page_requires_1)
(assert (not map_page_ensures_0))
(check-sat)
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; map_page_ensures_ret_0_0 (return-site ensures, source line 43)
(define-fun map_page_ensures_ret_0_0 () Bool (= p_map_page_idx p_map_page_idx))

; --- discharge (map_page_ensures_ret_0_0) ---
(push)
(assert map_page_requires_0)
(assert map_page_requires_1)
(assert (not map_page_ensures_ret_0_0))
(check-sat)
(pop)


; ============================================================
; function handle_exit
; ============================================================
(declare-const p_handle_exit_exit_idx Int)
(declare-const p_handle_exit_gpa Int)
(declare-const handle_exit_result Int)

; ---- requires (assumed, not discharged) ----
; handle_exit_requires_0 (source line 0)
(define-fun handle_exit_requires_0 () Bool (and (<= 0 p_handle_exit_exit_idx) (< p_handle_exit_exit_idx 4)))

; handle_exit_requires_1 (source line 57)
(define-fun handle_exit_requires_1 () Bool (= (bvand ((_ int2bv 64) p_handle_exit_gpa) (_ bv4095 64)) (_ bv0 64)))

; ---- ensures (signature-level, fallback) ----
; handle_exit_ensures_0 (source line 58)
(define-fun handle_exit_ensures_0 () Bool (= handle_exit_result p_handle_exit_exit_idx))

; --- discharge (handle_exit_ensures_0) ---
(push)
(assert handle_exit_requires_0)
(assert handle_exit_requires_1)
(assert (not handle_exit_ensures_0))
(check-sat)
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
(declare-const handle_exit_call_result_0 Int)
; handle_exit_call_requires_1_0 (call requires, source line 0)
(define-fun handle_exit_call_requires_1_0 () Bool (and (<= 0 p_handle_exit_exit_idx) (< p_handle_exit_exit_idx 4)))

; --- discharge (handle_exit_call_requires_1_0) ---
(push)
(assert handle_exit_requires_0)
(assert handle_exit_requires_1)
(assert (not handle_exit_call_requires_1_0))
(check-sat)
(pop)

; handle_exit_call_requires_2_1 (call requires, source line 42)
(define-fun handle_exit_call_requires_2_1 () Bool (= (bvand ((_ int2bv 64) p_handle_exit_gpa) (_ bv4095 64)) (_ bv0 64)))

; --- discharge (handle_exit_call_requires_2_1) ---
(push)
(assert handle_exit_requires_0)
(assert handle_exit_requires_1)
(assert (not handle_exit_call_requires_2_1))
(check-sat)
(pop)

; handle_exit_ensures_ret_3_0 (return-site ensures, source line 58)
(define-fun handle_exit_ensures_ret_3_0 () Bool (= p_handle_exit_exit_idx p_handle_exit_exit_idx))

; --- discharge (handle_exit_ensures_ret_3_0) ---
(push)
(assert handle_exit_requires_0)
(assert handle_exit_requires_1)
(assert (not handle_exit_ensures_ret_3_0))
(check-sat)
(pop)


; ============================================================
; function emulate_mmio
; ============================================================
(declare-const p_emulate_mmio_exit_idx Int)
(declare-const p_emulate_mmio_gpa Int)
(declare-const emulate_mmio_result Int)

; ---- requires (assumed, not discharged) ----
; emulate_mmio_requires_0 (source line 0)
(define-fun emulate_mmio_requires_0 () Bool (and (<= 0 p_emulate_mmio_exit_idx) (< p_emulate_mmio_exit_idx 4)))

; emulate_mmio_requires_1 (source line 71)
(define-fun emulate_mmio_requires_1 () Bool (= (bvand ((_ int2bv 64) p_emulate_mmio_gpa) (_ bv4095 64)) (_ bv0 64)))

; ---- ensures (signature-level, fallback) ----
; emulate_mmio_ensures_0 (source line 72)
(define-fun emulate_mmio_ensures_0 () Bool (>= emulate_mmio_result 0))

; --- discharge (emulate_mmio_ensures_0) ---
(push)
(assert emulate_mmio_requires_0)
(assert emulate_mmio_requires_1)
(assert (not emulate_mmio_ensures_0))
(check-sat)
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; emulate_mmio_ensures_ret_0_0 (return-site ensures, source line 72)
(define-fun emulate_mmio_ensures_ret_0_0 () Bool (>= 1 0))

; --- discharge (emulate_mmio_ensures_ret_0_0) ---
(push)
(assert emulate_mmio_requires_0)
(assert emulate_mmio_requires_1)
(assert (= p_emulate_mmio_exit_idx 0))
(assert (not emulate_mmio_ensures_ret_0_0))
(check-sat)
(pop)

; emulate_mmio_ensures_ret_1_0 (return-site ensures, source line 72)
(define-fun emulate_mmio_ensures_ret_1_0 () Bool (>= p_emulate_mmio_exit_idx 0))

; --- discharge (emulate_mmio_ensures_ret_1_0) ---
(push)
(assert emulate_mmio_requires_0)
(assert emulate_mmio_requires_1)
(assert (not emulate_mmio_ensures_ret_1_0))
(check-sat)
(pop)


; ============================================================
; function main
; ============================================================

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
(declare-const main_ph0 Int)
; note: replaced an unsupported subform (unsupported expr) with the uninterpreted constant main_ph0
(declare-const main_ph1 Int)
; note: replaced an unsupported subform (unsupported expr) with the uninterpreted constant main_ph1
(declare-const main_call_result_0 Int)
; main_call_requires_1_0 (call requires, source line 0)
(define-fun main_call_requires_1_0 () Bool (and (<= 0 1) (< 1 4)))

; --- discharge (main_call_requires_1_0) ---
(push)
(assert (not main_call_requires_1_0))
(check-sat)
(pop)

; main_call_requires_2_1 (call requires, source line 57)
(define-fun main_call_requires_2_1 () Bool (= (bvand ((_ int2bv 64) 8192) (_ bv4095 64)) (_ bv0 64)))

; --- discharge (main_call_requires_2_1) ---
(push)
(assert (not main_call_requires_2_1))
(check-sat)
(pop)

(define-fun main_inline_3_req0 () Bool (and (<= 0 1) (< 1 4)))
(define-fun main_inline_3_req1 () Bool (= (bvand ((_ int2bv 64) 8192) (_ bv4095 64)) (_ bv0 64)))
(declare-const main_call_result_4 Int)
; main_call_requires_5_0 (call requires, source line 0)
(define-fun main_call_requires_5_0 () Bool (and (<= 0 1) (< 1 4)))

; --- discharge (main_call_requires_5_0) ---
(push)
(assert main_inline_3_req0)
(assert main_inline_3_req1)
(assert (not main_call_requires_5_0))
(check-sat)
(pop)

; main_call_requires_6_1 (call requires, source line 42)
(define-fun main_call_requires_6_1 () Bool (= (bvand ((_ int2bv 64) 8192) (_ bv4095 64)) (_ bv0 64)))

; --- discharge (main_call_requires_6_1) ---
(push)
(assert main_inline_3_req0)
(assert main_inline_3_req1)
(assert (not main_call_requires_6_1))
(check-sat)
(pop)



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
; spec fn ept_memtype_wb (source line 23)
(define-fun sf_ept_memtype_wb ((p_sf_ept_memtype_wb_entry Int)) Bool (= (bvand ((_ int2bv 64) p_sf_ept_memtype_wb_entry) (_ bv56 64)) (_ bv24 64)))

; spec fn ept_present (source line 24)
(define-fun sf_ept_present ((p_sf_ept_present_entry Int)) Bool (= (bvand ((_ int2bv 64) p_sf_ept_present_entry) (_ bv1 64)) (_ bv1 64)))

; spec fn page_aligned (source line 25)
(define-fun sf_page_aligned ((p_sf_page_aligned_gpa Int)) Bool (= (bvand ((_ int2bv 64) p_sf_page_aligned_gpa) (_ bv4095 64)) (_ bv0 64)))

; spec fn dispatch_ok (source line 33)
(define-fun sf_dispatch_ok ((p_sf_dispatch_ok_idx Int) (p_sf_dispatch_ok_gpa Int)) Bool (and (= (bvand ((_ int2bv 64) p_sf_dispatch_ok_gpa) (_ bv4095 64)) (_ bv0 64)) (>= p_sf_dispatch_ok_idx 0)))


; ============================================================
; T1 — refines (concrete-implies-abstract discharge queries)
; ============================================================

; refines handle_exit <= dispatch_ok  (source line 89)
(declare-const refines_handle_exit_dispatch_ok_arg_0 Int)
(declare-const refines_handle_exit_dispatch_ok_arg_1 Int)
(declare-const refines_handle_exit_dispatch_ok_arg_result Int)
; refinement obligation: forall args, reqConc ==> (ensConc ==> specPost)
;   specPost = spec_body (bool-spec direct postcondition)
(push)
(assert (not (forall ((refines_handle_exit_dispatch_ok_arg_0 Int) (refines_handle_exit_dispatch_ok_arg_1 Int)) (=> (and (and (<= 0 refines_handle_exit_dispatch_ok_arg_0) (< refines_handle_exit_dispatch_ok_arg_0 4)) (= (bvand ((_ int2bv 64) refines_handle_exit_dispatch_ok_arg_1) (_ bv4095 64)) (_ bv0 64))) (=> (= refines_handle_exit_dispatch_ok_arg_result refines_handle_exit_dispatch_ok_arg_0) (and (= (bvand ((_ int2bv 64) refines_handle_exit_dispatch_ok_arg_1) (_ bv4095 64)) (_ bv0 64)) (>= refines_handle_exit_dispatch_ok_arg_0 0)))))))
(check-sat)
(pop)
; note: refines discharge — unsat => abstract refinement holds


; ============================================================
; Missing-#6 — preserves (per-handler invariant-preservation)
; ============================================================

; preserves map_page <= dispatch_ok  (source line 90)
(declare-const preserves_map_page_dispatch_ok_arg_0 Int)
(declare-const preserves_map_page_dispatch_ok_arg_1 Int)
(declare-const preserves_map_page_dispatch_ok_call_result_0 Int)
; preserves_map_page_dispatch_ok_call_requires_1_0 (call requires, source line 0)
(define-fun preserves_map_page_dispatch_ok_call_requires_1_0 () Bool (and (<= 0 preserves_map_page_dispatch_ok_arg_0) (< preserves_map_page_dispatch_ok_arg_0 4)))

; --- discharge (preserves_map_page_dispatch_ok_call_requires_1_0) ---
(push)
(assert (not preserves_map_page_dispatch_ok_call_requires_1_0))
(check-sat)
(pop)

; preserves_map_page_dispatch_ok_call_requires_2_1 (call requires, source line 42)
(define-fun preserves_map_page_dispatch_ok_call_requires_2_1 () Bool (= (bvand ((_ int2bv 64) preserves_map_page_dispatch_ok_arg_1) (_ bv4095 64)) (_ bv0 64)))

; --- discharge (preserves_map_page_dispatch_ok_call_requires_2_1) ---
(push)
(assert (not preserves_map_page_dispatch_ok_call_requires_2_1))
(check-sat)
(pop)

; preservation obligation: forall args, reqHandler ==> I(args, result)
;   reqHandler = conjunction of handler's requires clauses
;   I(args, result) = invariant spec fn body, with `result` bound to
;     the #2 WP mini-walker's inlined body terminal term
;     (NOT a fresh const + assumed ensures — soundness-critical)
(push)
(assert (not (forall ((preserves_map_page_dispatch_ok_arg_0 Int) (preserves_map_page_dispatch_ok_arg_1 Int)) (=> (and (and (<= 0 preserves_map_page_dispatch_ok_arg_0) (< preserves_map_page_dispatch_ok_arg_0 4)) (= (bvand ((_ int2bv 64) preserves_map_page_dispatch_ok_arg_1) (_ bv4095 64)) (_ bv0 64))) (and (= (bvand ((_ int2bv 64) preserves_map_page_dispatch_ok_arg_1) (_ bv4095 64)) (_ bv0 64)) (>= preserves_map_page_dispatch_ok_arg_0 0))))))
(check-sat)
(pop)
; note: preserves discharge — unsat => handler preserves invariant


; preserves handle_exit <= dispatch_ok  (source line 91)
(declare-const preserves_handle_exit_dispatch_ok_arg_0 Int)
(declare-const preserves_handle_exit_dispatch_ok_arg_1 Int)
(declare-const preserves_handle_exit_dispatch_ok_call_result_0 Int)
; preserves_handle_exit_dispatch_ok_call_requires_1_0 (call requires, source line 0)
(define-fun preserves_handle_exit_dispatch_ok_call_requires_1_0 () Bool (and (<= 0 preserves_handle_exit_dispatch_ok_arg_0) (< preserves_handle_exit_dispatch_ok_arg_0 4)))

; --- discharge (preserves_handle_exit_dispatch_ok_call_requires_1_0) ---
(push)
(assert (not preserves_handle_exit_dispatch_ok_call_requires_1_0))
(check-sat)
(pop)

; preserves_handle_exit_dispatch_ok_call_requires_2_1 (call requires, source line 57)
(define-fun preserves_handle_exit_dispatch_ok_call_requires_2_1 () Bool (= (bvand ((_ int2bv 64) preserves_handle_exit_dispatch_ok_arg_1) (_ bv4095 64)) (_ bv0 64)))

; --- discharge (preserves_handle_exit_dispatch_ok_call_requires_2_1) ---
(push)
(assert (not preserves_handle_exit_dispatch_ok_call_requires_2_1))
(check-sat)
(pop)

(define-fun preserves_handle_exit_dispatch_ok_inline_3_req0 () Bool (and (<= 0 preserves_handle_exit_dispatch_ok_arg_0) (< preserves_handle_exit_dispatch_ok_arg_0 4)))
(define-fun preserves_handle_exit_dispatch_ok_inline_3_req1 () Bool (= (bvand ((_ int2bv 64) preserves_handle_exit_dispatch_ok_arg_1) (_ bv4095 64)) (_ bv0 64)))
(declare-const preserves_handle_exit_dispatch_ok_call_result_4 Int)
; preserves_handle_exit_dispatch_ok_call_requires_5_0 (call requires, source line 0)
(define-fun preserves_handle_exit_dispatch_ok_call_requires_5_0 () Bool (and (<= 0 preserves_handle_exit_dispatch_ok_arg_0) (< preserves_handle_exit_dispatch_ok_arg_0 4)))

; --- discharge (preserves_handle_exit_dispatch_ok_call_requires_5_0) ---
(push)
(assert preserves_handle_exit_dispatch_ok_inline_3_req0)
(assert preserves_handle_exit_dispatch_ok_inline_3_req1)
(assert (not preserves_handle_exit_dispatch_ok_call_requires_5_0))
(check-sat)
(pop)

; preserves_handle_exit_dispatch_ok_call_requires_6_1 (call requires, source line 42)
(define-fun preserves_handle_exit_dispatch_ok_call_requires_6_1 () Bool (= (bvand ((_ int2bv 64) preserves_handle_exit_dispatch_ok_arg_1) (_ bv4095 64)) (_ bv0 64)))

; --- discharge (preserves_handle_exit_dispatch_ok_call_requires_6_1) ---
(push)
(assert preserves_handle_exit_dispatch_ok_inline_3_req0)
(assert preserves_handle_exit_dispatch_ok_inline_3_req1)
(assert (not preserves_handle_exit_dispatch_ok_call_requires_6_1))
(check-sat)
(pop)

; preservation obligation: forall args, reqHandler ==> I(args, result)
;   reqHandler = conjunction of handler's requires clauses
;   I(args, result) = invariant spec fn body, with `result` bound to
;     the #2 WP mini-walker's inlined body terminal term
;     (NOT a fresh const + assumed ensures — soundness-critical)
(push)
(assert (not (forall ((preserves_handle_exit_dispatch_ok_arg_0 Int) (preserves_handle_exit_dispatch_ok_arg_1 Int)) (=> (and (and (<= 0 preserves_handle_exit_dispatch_ok_arg_0) (< preserves_handle_exit_dispatch_ok_arg_0 4)) (= (bvand ((_ int2bv 64) preserves_handle_exit_dispatch_ok_arg_1) (_ bv4095 64)) (_ bv0 64))) (and (= (bvand ((_ int2bv 64) preserves_handle_exit_dispatch_ok_arg_1) (_ bv4095 64)) (_ bv0 64)) (>= preserves_handle_exit_dispatch_ok_arg_0 0))))))
(check-sat)
(pop)
; note: preserves discharge — unsat => handler preserves invariant


; preserves emulate_mmio <= dispatch_ok  (source line 92)
(declare-const preserves_emulate_mmio_dispatch_ok_arg_0 Int)
(declare-const preserves_emulate_mmio_dispatch_ok_arg_1 Int)
(declare-const preserves_emulate_mmio_dispatch_ok_call_result_0 Int)
; preserves_emulate_mmio_dispatch_ok_call_requires_1_0 (call requires, source line 0)
(define-fun preserves_emulate_mmio_dispatch_ok_call_requires_1_0 () Bool (and (<= 0 preserves_emulate_mmio_dispatch_ok_arg_0) (< preserves_emulate_mmio_dispatch_ok_arg_0 4)))

; --- discharge (preserves_emulate_mmio_dispatch_ok_call_requires_1_0) ---
(push)
(assert (not preserves_emulate_mmio_dispatch_ok_call_requires_1_0))
(check-sat)
(pop)

; preserves_emulate_mmio_dispatch_ok_call_requires_2_1 (call requires, source line 71)
(define-fun preserves_emulate_mmio_dispatch_ok_call_requires_2_1 () Bool (= (bvand ((_ int2bv 64) preserves_emulate_mmio_dispatch_ok_arg_1) (_ bv4095 64)) (_ bv0 64)))

; --- discharge (preserves_emulate_mmio_dispatch_ok_call_requires_2_1) ---
(push)
(assert (not preserves_emulate_mmio_dispatch_ok_call_requires_2_1))
(check-sat)
(pop)

(define-fun preserves_emulate_mmio_dispatch_ok_inline_3_req0 () Bool (and (<= 0 preserves_emulate_mmio_dispatch_ok_arg_0) (< preserves_emulate_mmio_dispatch_ok_arg_0 4)))
(define-fun preserves_emulate_mmio_dispatch_ok_inline_3_req1 () Bool (= (bvand ((_ int2bv 64) preserves_emulate_mmio_dispatch_ok_arg_1) (_ bv4095 64)) (_ bv0 64)))
; preservation obligation: forall args, reqHandler ==> I(args, result)
;   reqHandler = conjunction of handler's requires clauses
;   I(args, result) = invariant spec fn body, with `result` bound to
;     the #2 WP mini-walker's inlined body terminal term
;     (NOT a fresh const + assumed ensures — soundness-critical)
(push)
(assert (not (forall ((preserves_emulate_mmio_dispatch_ok_arg_0 Int) (preserves_emulate_mmio_dispatch_ok_arg_1 Int)) (=> (and (and (<= 0 preserves_emulate_mmio_dispatch_ok_arg_0) (< preserves_emulate_mmio_dispatch_ok_arg_0 4)) (= (bvand ((_ int2bv 64) preserves_emulate_mmio_dispatch_ok_arg_1) (_ bv4095 64)) (_ bv0 64))) (and (= (bvand ((_ int2bv 64) preserves_emulate_mmio_dispatch_ok_arg_1) (_ bv4095 64)) (_ bv0 64)) (>= preserves_emulate_mmio_dispatch_ok_arg_0 0))))))
(check-sat)
(pop)
; note: preserves discharge — unsat => handler preserves invariant

(exit)
