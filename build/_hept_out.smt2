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
; function hept_map
; ============================================================
(declare-const p_hept_map_table (Array Int Int))
(declare-const p_hept_map_gpa Int)
(declare-const p_hept_map_hpa (_ BitVec 64))
(declare-const p_hept_map_perms (_ BitVec 64))
(declare-const hept_map_result (Array Int Int))

; ---- requires (assumed, not discharged) ----
; hept_map_requires_0 (source line 25)
(define-fun hept_map_requires_0 () Bool (<= 0 p_hept_map_gpa))

; hept_map_requires_1 (source line 26)
(define-fun hept_map_requires_1 () Bool (< p_hept_map_gpa (* 16 4096)))

; hept_map_requires_2 (source line 27)
(define-fun hept_map_requires_2 () Bool (= (mod p_hept_map_gpa 4096) 0))

; hept_map_requires_3 (source line 28)
(define-fun hept_map_requires_3 () Bool (<= 0 ((_ bv2int 64) p_hept_map_hpa)))

; hept_map_requires_4 (source line 29)
(define-fun hept_map_requires_4 () Bool (= (mod ((_ bv2int 64) p_hept_map_hpa) 4096) 0))

; hept_map_requires_5 (source line 0)
(define-fun hept_map_requires_5 () Bool (= (mod ((_ bv2int 64) p_hept_map_perms) 2) 1))

; hept_map_requires_6 (source line 0)
(define-fun hept_map_requires_6 () Bool (= (bvand p_hept_map_perms (bvnot (_ bv7 64))) (_ bv0 64)))

; ---- ensures (signature-level, fallback) ----
; hept_map_ensures_0 (source line 32)
(define-fun hept_map_ensures_0 () Bool (= (not (= (select p_hept_map_table (div p_hept_map_gpa 4096)) 0)) true))

; hept_map_ensures_0#p0 (source line 0)
; --- discharge (hept_map_ensures_0#p0) ---
(push)
(assert hept_map_requires_0)
(define-fun hept_map_ensures_0#p0_ds () Bool (= (not (= (select p_hept_map_table (div p_hept_map_gpa 4096)) 0)) true))
(assert (not hept_map_ensures_0#p0_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_map_ensures_0#p1 (source line 0)
; --- discharge (hept_map_ensures_0#p1) ---
(push)
(assert hept_map_requires_1)
(define-fun hept_map_ensures_0#p1_ds () Bool (= (not (= (select p_hept_map_table (div p_hept_map_gpa 4096)) 0)) true))
(assert (not hept_map_ensures_0#p1_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_map_ensures_0#p2 (source line 0)
; --- discharge (hept_map_ensures_0#p2) ---
(push)
(assert hept_map_requires_2)
(define-fun hept_map_ensures_0#p2_ds () Bool (= (not (= (select p_hept_map_table (div p_hept_map_gpa 4096)) 0)) true))
(assert (not hept_map_ensures_0#p2_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_map_ensures_0#p3 (source line 0)
; --- discharge (hept_map_ensures_0#p3) ---
(push)
(assert hept_map_requires_3)
(define-fun hept_map_ensures_0#p3_ds () Bool (= (not (= (select p_hept_map_table (div p_hept_map_gpa 4096)) 0)) true))
(assert (not hept_map_ensures_0#p3_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_map_ensures_0#p4 (source line 0)
; --- discharge (hept_map_ensures_0#p4) ---
(push)
(assert hept_map_requires_4)
(define-fun hept_map_ensures_0#p4_ds () Bool (= (not (= (select p_hept_map_table (div p_hept_map_gpa 4096)) 0)) true))
(assert (not hept_map_ensures_0#p4_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_map_ensures_0#p5 (source line 0)
; --- discharge (hept_map_ensures_0#p5) ---
(push)
(assert hept_map_requires_5)
(define-fun hept_map_ensures_0#p5_ds () Bool (= (not (= (select p_hept_map_table (div p_hept_map_gpa 4096)) 0)) true))
(assert (not hept_map_ensures_0#p5_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_map_ensures_0#p6 (source line 0)
; --- discharge (hept_map_ensures_0#p6) ---
(push)
(assert hept_map_requires_6)
(define-fun hept_map_ensures_0#p6_ds () Bool (= (not (= (select p_hept_map_table (div p_hept_map_gpa 4096)) 0)) true))
(assert (not hept_map_ensures_0#p6_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_map_ensures_0#pfull (source line 0)
; --- discharge (hept_map_ensures_0#pfull) ---
(push)
(assert hept_map_requires_0)
(assert hept_map_requires_1)
(assert hept_map_requires_2)
(assert hept_map_requires_3)
(assert hept_map_requires_4)
(assert hept_map_requires_5)
(assert hept_map_requires_6)
(define-fun hept_map_ensures_0#pfull_ds () Bool (= (not (= (select p_hept_map_table (div p_hept_map_gpa 4096)) 0)) true))
(assert (not hept_map_ensures_0#pfull_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_map_ensures_1 (source line 33)
(define-fun hept_map_ensures_1 () Bool (= ((_ int2bv 64) (select p_hept_map_table (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))

; hept_map_ensures_1#p0 (source line 0)
; --- discharge (hept_map_ensures_1#p0) ---
(push)
(assert hept_map_requires_0)
(define-fun hept_map_ensures_1#p0_ds () Bool (= ((_ int2bv 64) (select p_hept_map_table (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))
(assert (not hept_map_ensures_1#p0_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_map_ensures_1#p1 (source line 0)
; --- discharge (hept_map_ensures_1#p1) ---
(push)
(assert hept_map_requires_1)
(define-fun hept_map_ensures_1#p1_ds () Bool (= ((_ int2bv 64) (select p_hept_map_table (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))
(assert (not hept_map_ensures_1#p1_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_map_ensures_1#p2 (source line 0)
; --- discharge (hept_map_ensures_1#p2) ---
(push)
(assert hept_map_requires_2)
(define-fun hept_map_ensures_1#p2_ds () Bool (= ((_ int2bv 64) (select p_hept_map_table (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))
(assert (not hept_map_ensures_1#p2_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_map_ensures_1#p3 (source line 0)
; --- discharge (hept_map_ensures_1#p3) ---
(push)
(assert hept_map_requires_3)
(define-fun hept_map_ensures_1#p3_ds () Bool (= ((_ int2bv 64) (select p_hept_map_table (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))
(assert (not hept_map_ensures_1#p3_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_map_ensures_1#p4 (source line 0)
; --- discharge (hept_map_ensures_1#p4) ---
(push)
(assert hept_map_requires_4)
(define-fun hept_map_ensures_1#p4_ds () Bool (= ((_ int2bv 64) (select p_hept_map_table (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))
(assert (not hept_map_ensures_1#p4_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_map_ensures_1#p5 (source line 0)
; --- discharge (hept_map_ensures_1#p5) ---
(push)
(assert hept_map_requires_5)
(define-fun hept_map_ensures_1#p5_ds () Bool (= ((_ int2bv 64) (select p_hept_map_table (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))
(assert (not hept_map_ensures_1#p5_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_map_ensures_1#p6 (source line 0)
; --- discharge (hept_map_ensures_1#p6) ---
(push)
(assert hept_map_requires_6)
(define-fun hept_map_ensures_1#p6_ds () Bool (= ((_ int2bv 64) (select p_hept_map_table (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))
(assert (not hept_map_ensures_1#p6_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_map_ensures_1#pfull (source line 0)
; --- discharge (hept_map_ensures_1#pfull) ---
(push)
(assert hept_map_requires_0)
(assert hept_map_requires_1)
(assert hept_map_requires_2)
(assert hept_map_requires_3)
(assert hept_map_requires_4)
(assert hept_map_requires_5)
(assert hept_map_requires_6)
(define-fun hept_map_ensures_1#pfull_ds () Bool (= ((_ int2bv 64) (select p_hept_map_table (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))
(assert (not hept_map_ensures_1#pfull_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_map_ensures_2 (source line 34)
(define-fun hept_map_ensures_2 () Bool (= ((_ int2bv 64) (select p_hept_map_table (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))

; hept_map_ensures_2#p0 (source line 0)
; --- discharge (hept_map_ensures_2#p0) ---
(push)
(assert hept_map_requires_0)
(define-fun hept_map_ensures_2#p0_ds () Bool (= ((_ int2bv 64) (select p_hept_map_table (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))
(assert (not hept_map_ensures_2#p0_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_map_ensures_2#p1 (source line 0)
; --- discharge (hept_map_ensures_2#p1) ---
(push)
(assert hept_map_requires_1)
(define-fun hept_map_ensures_2#p1_ds () Bool (= ((_ int2bv 64) (select p_hept_map_table (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))
(assert (not hept_map_ensures_2#p1_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_map_ensures_2#p2 (source line 0)
; --- discharge (hept_map_ensures_2#p2) ---
(push)
(assert hept_map_requires_2)
(define-fun hept_map_ensures_2#p2_ds () Bool (= ((_ int2bv 64) (select p_hept_map_table (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))
(assert (not hept_map_ensures_2#p2_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_map_ensures_2#p3 (source line 0)
; --- discharge (hept_map_ensures_2#p3) ---
(push)
(assert hept_map_requires_3)
(define-fun hept_map_ensures_2#p3_ds () Bool (= ((_ int2bv 64) (select p_hept_map_table (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))
(assert (not hept_map_ensures_2#p3_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_map_ensures_2#p4 (source line 0)
; --- discharge (hept_map_ensures_2#p4) ---
(push)
(assert hept_map_requires_4)
(define-fun hept_map_ensures_2#p4_ds () Bool (= ((_ int2bv 64) (select p_hept_map_table (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))
(assert (not hept_map_ensures_2#p4_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_map_ensures_2#p5 (source line 0)
; --- discharge (hept_map_ensures_2#p5) ---
(push)
(assert hept_map_requires_5)
(define-fun hept_map_ensures_2#p5_ds () Bool (= ((_ int2bv 64) (select p_hept_map_table (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))
(assert (not hept_map_ensures_2#p5_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_map_ensures_2#p6 (source line 0)
; --- discharge (hept_map_ensures_2#p6) ---
(push)
(assert hept_map_requires_6)
(define-fun hept_map_ensures_2#p6_ds () Bool (= ((_ int2bv 64) (select p_hept_map_table (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))
(assert (not hept_map_ensures_2#p6_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_map_ensures_2#pfull (source line 0)
; --- discharge (hept_map_ensures_2#pfull) ---
(push)
(assert hept_map_requires_0)
(assert hept_map_requires_1)
(assert hept_map_requires_2)
(assert hept_map_requires_3)
(assert hept_map_requires_4)
(assert hept_map_requires_5)
(assert hept_map_requires_6)
(define-fun hept_map_ensures_2#pfull_ds () Bool (= ((_ int2bv 64) (select p_hept_map_table (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))
(assert (not hept_map_ensures_2#pfull_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; hept_map_ensures_ret_0_0 (return-site ensures, source line 32)
(define-fun hept_map_ensures_ret_0_0 () Bool (= (not (= (select (store p_hept_map_table (div p_hept_map_gpa 4096) ((_ bv2int 64) (bvor p_hept_map_hpa p_hept_map_perms))) (div p_hept_map_gpa 4096)) 0)) true))

; --- discharge (hept_map_ensures_ret_0_0) ---
(push)
(assert hept_map_requires_0)
(assert hept_map_requires_1)
(assert hept_map_requires_2)
(assert hept_map_requires_3)
(assert hept_map_requires_4)
(assert hept_map_requires_5)
(assert hept_map_requires_6)
(assert (not hept_map_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(pop)

; hept_map_ensures_ret_1_1 (return-site ensures, source line 33)
(define-fun hept_map_ensures_ret_1_1 () Bool (= ((_ int2bv 64) (select (store p_hept_map_table (div p_hept_map_gpa 4096) ((_ bv2int 64) (bvor p_hept_map_hpa p_hept_map_perms))) (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))

; --- discharge (hept_map_ensures_ret_1_1) ---
(push)
(assert hept_map_requires_0)
(assert hept_map_requires_1)
(assert hept_map_requires_2)
(assert hept_map_requires_3)
(assert hept_map_requires_4)
(assert hept_map_requires_5)
(assert hept_map_requires_6)
(assert (not hept_map_ensures_ret_1_1))
(check-sat-using (then simplify smt))
(pop)

; hept_map_ensures_ret_2_2 (return-site ensures, source line 34)
(define-fun hept_map_ensures_ret_2_2 () Bool (= ((_ int2bv 64) (select (store p_hept_map_table (div p_hept_map_gpa 4096) ((_ bv2int 64) (bvor p_hept_map_hpa p_hept_map_perms))) (div p_hept_map_gpa 4096))) (bvor p_hept_map_hpa p_hept_map_perms)))

; --- discharge (hept_map_ensures_ret_2_2) ---
(push)
(assert hept_map_requires_0)
(assert hept_map_requires_1)
(assert hept_map_requires_2)
(assert hept_map_requires_3)
(assert hept_map_requires_4)
(assert hept_map_requires_5)
(assert hept_map_requires_6)
(assert (not hept_map_ensures_ret_2_2))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function hept_unmap
; ============================================================
(declare-const p_hept_unmap_table (Array Int Int))
(declare-const p_hept_unmap_gpa Int)
(declare-const hept_unmap_result (Array Int Int))

; ---- requires (assumed, not discharged) ----
; hept_unmap_requires_0 (source line 42)
(define-fun hept_unmap_requires_0 () Bool (<= 0 p_hept_unmap_gpa))

; hept_unmap_requires_1 (source line 43)
(define-fun hept_unmap_requires_1 () Bool (< p_hept_unmap_gpa (* 16 4096)))

; hept_unmap_requires_2 (source line 44)
(define-fun hept_unmap_requires_2 () Bool (= (mod p_hept_unmap_gpa 4096) 0))

; ---- ensures (signature-level, fallback) ----
; hept_unmap_ensures_0 (source line 45)
(define-fun hept_unmap_ensures_0 () Bool (= (not (= (select p_hept_unmap_table (div p_hept_unmap_gpa 4096)) 0)) false))

; hept_unmap_ensures_0#p0 (source line 0)
; --- discharge (hept_unmap_ensures_0#p0) ---
(push)
(assert hept_unmap_requires_0)
(define-fun hept_unmap_ensures_0#p0_ds () Bool (= (not (= (select p_hept_unmap_table (div p_hept_unmap_gpa 4096)) 0)) false))
(assert (not hept_unmap_ensures_0#p0_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_unmap_ensures_0#p1 (source line 0)
; --- discharge (hept_unmap_ensures_0#p1) ---
(push)
(assert hept_unmap_requires_1)
(define-fun hept_unmap_ensures_0#p1_ds () Bool (= (not (= (select p_hept_unmap_table (div p_hept_unmap_gpa 4096)) 0)) false))
(assert (not hept_unmap_ensures_0#p1_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_unmap_ensures_0#p2 (source line 0)
; --- discharge (hept_unmap_ensures_0#p2) ---
(push)
(assert hept_unmap_requires_2)
(define-fun hept_unmap_ensures_0#p2_ds () Bool (= (not (= (select p_hept_unmap_table (div p_hept_unmap_gpa 4096)) 0)) false))
(assert (not hept_unmap_ensures_0#p2_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_unmap_ensures_0#pfull (source line 0)
; --- discharge (hept_unmap_ensures_0#pfull) ---
(push)
(assert hept_unmap_requires_0)
(assert hept_unmap_requires_1)
(assert hept_unmap_requires_2)
(define-fun hept_unmap_ensures_0#pfull_ds () Bool (= (not (= (select p_hept_unmap_table (div p_hept_unmap_gpa 4096)) 0)) false))
(assert (not hept_unmap_ensures_0#pfull_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_unmap_ensures_1 (source line 46)
(define-fun hept_unmap_ensures_1 () Bool (= (select p_hept_unmap_table (div p_hept_unmap_gpa 4096)) 0))

; hept_unmap_ensures_1#p0 (source line 0)
; --- discharge (hept_unmap_ensures_1#p0) ---
(push)
(assert hept_unmap_requires_0)
(define-fun hept_unmap_ensures_1#p0_ds () Bool (= (select p_hept_unmap_table (div p_hept_unmap_gpa 4096)) 0))
(assert (not hept_unmap_ensures_1#p0_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_unmap_ensures_1#p1 (source line 0)
; --- discharge (hept_unmap_ensures_1#p1) ---
(push)
(assert hept_unmap_requires_1)
(define-fun hept_unmap_ensures_1#p1_ds () Bool (= (select p_hept_unmap_table (div p_hept_unmap_gpa 4096)) 0))
(assert (not hept_unmap_ensures_1#p1_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_unmap_ensures_1#p2 (source line 0)
; --- discharge (hept_unmap_ensures_1#p2) ---
(push)
(assert hept_unmap_requires_2)
(define-fun hept_unmap_ensures_1#p2_ds () Bool (= (select p_hept_unmap_table (div p_hept_unmap_gpa 4096)) 0))
(assert (not hept_unmap_ensures_1#p2_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_unmap_ensures_1#pfull (source line 0)
; --- discharge (hept_unmap_ensures_1#pfull) ---
(push)
(assert hept_unmap_requires_0)
(assert hept_unmap_requires_1)
(assert hept_unmap_requires_2)
(define-fun hept_unmap_ensures_1#pfull_ds () Bool (= (select p_hept_unmap_table (div p_hept_unmap_gpa 4096)) 0))
(assert (not hept_unmap_ensures_1#pfull_ds))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; hept_unmap_ensures_ret_0_0 (return-site ensures, source line 45)
(define-fun hept_unmap_ensures_ret_0_0 () Bool (= (not (= (select (store p_hept_unmap_table (div p_hept_unmap_gpa 4096) 0) (div p_hept_unmap_gpa 4096)) 0)) false))

; --- discharge (hept_unmap_ensures_ret_0_0) ---
(push)
(assert hept_unmap_requires_0)
(assert hept_unmap_requires_1)
(assert hept_unmap_requires_2)
(assert (not hept_unmap_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(pop)

; hept_unmap_ensures_ret_1_1 (return-site ensures, source line 46)
(define-fun hept_unmap_ensures_ret_1_1 () Bool (= (select (store p_hept_unmap_table (div p_hept_unmap_gpa 4096) 0) (div p_hept_unmap_gpa 4096)) 0))

; --- discharge (hept_unmap_ensures_ret_1_1) ---
(push)
(assert hept_unmap_requires_0)
(assert hept_unmap_requires_1)
(assert hept_unmap_requires_2)
(assert (not hept_unmap_ensures_ret_1_1))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function hept_translate
; ============================================================
(declare-const p_hept_translate_table (Array Int Int))
(declare-const p_hept_translate_gpa Int)
(declare-const hept_translate_result Int)

; ---- requires (assumed, not discharged) ----
; hept_translate_requires_0 (source line 54)
(define-fun hept_translate_requires_0 () Bool (<= 0 p_hept_translate_gpa))

; hept_translate_requires_1 (source line 55)
(define-fun hept_translate_requires_1 () Bool (< p_hept_translate_gpa (* 16 4096)))

; hept_translate_requires_2 (source line 56)
(define-fun hept_translate_requires_2 () Bool (= (mod p_hept_translate_gpa 4096) 0))

; hept_translate_requires_3 (source line 57)
(define-fun hept_translate_requires_3 () Bool (= (not (= (select p_hept_translate_table (div p_hept_translate_gpa 4096)) 0)) true))

; hept_translate_requires_4 (source line 58)
(define-fun hept_translate_requires_4 () Bool (<= 0 (select p_hept_translate_table (div p_hept_translate_gpa 4096))))

; ---- ensures (signature-level, fallback) ----
; hept_translate_ensures_0 (source line 59)
(define-fun hept_translate_ensures_0 () Bool (= hept_translate_result ((_ bv2int 64) (bvshl (bvlshr ((_ int2bv 64) (select p_hept_translate_table (div p_hept_translate_gpa 4096))) ((_ int2bv 64) 12)) ((_ int2bv 64) 12)))))

; hept_translate_ensures_0#p0 (source line 0)
; --- discharge (hept_translate_ensures_0#p0) ---
(push)
(assert hept_translate_requires_0)
(define-fun hept_translate_ensures_0#p0_ds () Bool (= hept_translate_result ((_ bv2int 64) (bvshl (bvlshr ((_ int2bv 64) (select p_hept_translate_table (div p_hept_translate_gpa 4096))) ((_ int2bv 64) 12)) ((_ int2bv 64) 12)))))
(assert (not hept_translate_ensures_0#p0_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_translate_ensures_0#p1 (source line 0)
; --- discharge (hept_translate_ensures_0#p1) ---
(push)
(assert hept_translate_requires_1)
(define-fun hept_translate_ensures_0#p1_ds () Bool (= hept_translate_result ((_ bv2int 64) (bvshl (bvlshr ((_ int2bv 64) (select p_hept_translate_table (div p_hept_translate_gpa 4096))) ((_ int2bv 64) 12)) ((_ int2bv 64) 12)))))
(assert (not hept_translate_ensures_0#p1_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_translate_ensures_0#p2 (source line 0)
; --- discharge (hept_translate_ensures_0#p2) ---
(push)
(assert hept_translate_requires_2)
(define-fun hept_translate_ensures_0#p2_ds () Bool (= hept_translate_result ((_ bv2int 64) (bvshl (bvlshr ((_ int2bv 64) (select p_hept_translate_table (div p_hept_translate_gpa 4096))) ((_ int2bv 64) 12)) ((_ int2bv 64) 12)))))
(assert (not hept_translate_ensures_0#p2_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_translate_ensures_0#p3 (source line 0)
; --- discharge (hept_translate_ensures_0#p3) ---
(push)
(assert hept_translate_requires_3)
(define-fun hept_translate_ensures_0#p3_ds () Bool (= hept_translate_result ((_ bv2int 64) (bvshl (bvlshr ((_ int2bv 64) (select p_hept_translate_table (div p_hept_translate_gpa 4096))) ((_ int2bv 64) 12)) ((_ int2bv 64) 12)))))
(assert (not hept_translate_ensures_0#p3_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_translate_ensures_0#p4 (source line 0)
; --- discharge (hept_translate_ensures_0#p4) ---
(push)
(assert hept_translate_requires_4)
(define-fun hept_translate_ensures_0#p4_ds () Bool (= hept_translate_result ((_ bv2int 64) (bvshl (bvlshr ((_ int2bv 64) (select p_hept_translate_table (div p_hept_translate_gpa 4096))) ((_ int2bv 64) 12)) ((_ int2bv 64) 12)))))
(assert (not hept_translate_ensures_0#p4_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_translate_ensures_0#pfull (source line 0)
; --- discharge (hept_translate_ensures_0#pfull) ---
(push)
(assert hept_translate_requires_0)
(assert hept_translate_requires_1)
(assert hept_translate_requires_2)
(assert hept_translate_requires_3)
(assert hept_translate_requires_4)
(define-fun hept_translate_ensures_0#pfull_ds () Bool (= hept_translate_result ((_ bv2int 64) (bvshl (bvlshr ((_ int2bv 64) (select p_hept_translate_table (div p_hept_translate_gpa 4096))) ((_ int2bv 64) 12)) ((_ int2bv 64) 12)))))
(assert (not hept_translate_ensures_0#pfull_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_translate_ensures_1 (source line 60)
(define-fun hept_translate_ensures_1 () Bool (= (mod hept_translate_result 4096) 0))

; hept_translate_ensures_1#p0 (source line 0)
; --- discharge (hept_translate_ensures_1#p0) ---
(push)
(assert hept_translate_requires_0)
(define-fun hept_translate_ensures_1#p0_ds () Bool (= (mod hept_translate_result 4096) 0))
(assert (not hept_translate_ensures_1#p0_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_translate_ensures_1#p1 (source line 0)
; --- discharge (hept_translate_ensures_1#p1) ---
(push)
(assert hept_translate_requires_1)
(define-fun hept_translate_ensures_1#p1_ds () Bool (= (mod hept_translate_result 4096) 0))
(assert (not hept_translate_ensures_1#p1_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_translate_ensures_1#p2 (source line 0)
; --- discharge (hept_translate_ensures_1#p2) ---
(push)
(assert hept_translate_requires_2)
(define-fun hept_translate_ensures_1#p2_ds () Bool (= (mod hept_translate_result 4096) 0))
(assert (not hept_translate_ensures_1#p2_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_translate_ensures_1#p3 (source line 0)
; --- discharge (hept_translate_ensures_1#p3) ---
(push)
(assert hept_translate_requires_3)
(define-fun hept_translate_ensures_1#p3_ds () Bool (= (mod hept_translate_result 4096) 0))
(assert (not hept_translate_ensures_1#p3_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_translate_ensures_1#p4 (source line 0)
; --- discharge (hept_translate_ensures_1#p4) ---
(push)
(assert hept_translate_requires_4)
(define-fun hept_translate_ensures_1#p4_ds () Bool (= (mod hept_translate_result 4096) 0))
(assert (not hept_translate_ensures_1#p4_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_translate_ensures_1#pfull (source line 0)
; --- discharge (hept_translate_ensures_1#pfull) ---
(push)
(assert hept_translate_requires_0)
(assert hept_translate_requires_1)
(assert hept_translate_requires_2)
(assert hept_translate_requires_3)
(assert hept_translate_requires_4)
(define-fun hept_translate_ensures_1#pfull_ds () Bool (= (mod hept_translate_result 4096) 0))
(assert (not hept_translate_ensures_1#pfull_ds))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; hept_translate_ensures_ret_0_0 (return-site ensures, source line 59)
(define-fun hept_translate_ensures_ret_0_0 () Bool (= ((_ bv2int 64) (bvshl (bvlshr ((_ int2bv 64) (select p_hept_translate_table (div p_hept_translate_gpa 4096))) ((_ int2bv 64) 12)) ((_ int2bv 64) 12))) ((_ bv2int 64) (bvshl (bvlshr ((_ int2bv 64) (select p_hept_translate_table (div p_hept_translate_gpa 4096))) ((_ int2bv 64) 12)) ((_ int2bv 64) 12)))))

; --- discharge (hept_translate_ensures_ret_0_0) ---
(push)
(assert hept_translate_requires_0)
(assert hept_translate_requires_1)
(assert hept_translate_requires_2)
(assert hept_translate_requires_3)
(assert hept_translate_requires_4)
(assert (not hept_translate_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(pop)

; hept_translate_ensures_ret_1_1 (return-site ensures, source line 60)
(define-fun hept_translate_ensures_ret_1_1 () Bool (= (mod ((_ bv2int 64) (bvshl (bvlshr ((_ int2bv 64) (select p_hept_translate_table (div p_hept_translate_gpa 4096))) ((_ int2bv 64) 12)) ((_ int2bv 64) 12))) 4096) 0))

; --- discharge (hept_translate_ensures_ret_1_1) ---
(push)
(assert hept_translate_requires_0)
(assert hept_translate_requires_1)
(assert hept_translate_requires_2)
(assert hept_translate_requires_3)
(assert hept_translate_requires_4)
(assert (not hept_translate_ensures_ret_1_1))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function hept_check_perms
; ============================================================
(declare-const p_hept_check_perms_table (Array Int Int))
(declare-const p_hept_check_perms_gpa Int)
(declare-const p_hept_check_perms_required (_ BitVec 64))
(declare-const hept_check_perms_result Int)

; ---- requires (assumed, not discharged) ----
; hept_check_perms_requires_0 (source line 68)
(define-fun hept_check_perms_requires_0 () Bool (<= 0 p_hept_check_perms_gpa))

; hept_check_perms_requires_1 (source line 69)
(define-fun hept_check_perms_requires_1 () Bool (< p_hept_check_perms_gpa (* 16 4096)))

; hept_check_perms_requires_2 (source line 70)
(define-fun hept_check_perms_requires_2 () Bool (= (mod p_hept_check_perms_gpa 4096) 0))

; hept_check_perms_requires_3 (source line 0)
(define-fun hept_check_perms_requires_3 () Bool (= (bvand p_hept_check_perms_required (bvnot (_ bv7 64))) (_ bv0 64)))

; hept_check_perms_requires_4 (source line 72)
(define-fun hept_check_perms_requires_4 () Bool (= (not (= (select p_hept_check_perms_table (div p_hept_check_perms_gpa 4096)) 0)) true))

; ---- ensures (signature-level, fallback) ----
; hept_check_perms_ensures_0 (source line 0)
(define-fun hept_check_perms_ensures_0 () Bool (or (= hept_check_perms_result 1) (= hept_check_perms_result 0)))

; hept_check_perms_ensures_0#p0 (source line 0)
; --- discharge (hept_check_perms_ensures_0#p0) ---
(push)
(assert hept_check_perms_requires_0)
(define-fun hept_check_perms_ensures_0#p0_ds () Bool (or (= hept_check_perms_result 1) (= hept_check_perms_result 0)))
(assert (not hept_check_perms_ensures_0#p0_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_check_perms_ensures_0#p1 (source line 0)
; --- discharge (hept_check_perms_ensures_0#p1) ---
(push)
(assert hept_check_perms_requires_1)
(define-fun hept_check_perms_ensures_0#p1_ds () Bool (or (= hept_check_perms_result 1) (= hept_check_perms_result 0)))
(assert (not hept_check_perms_ensures_0#p1_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_check_perms_ensures_0#p2 (source line 0)
; --- discharge (hept_check_perms_ensures_0#p2) ---
(push)
(assert hept_check_perms_requires_2)
(define-fun hept_check_perms_ensures_0#p2_ds () Bool (or (= hept_check_perms_result 1) (= hept_check_perms_result 0)))
(assert (not hept_check_perms_ensures_0#p2_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_check_perms_ensures_0#p3 (source line 0)
; --- discharge (hept_check_perms_ensures_0#p3) ---
(push)
(assert hept_check_perms_requires_3)
(define-fun hept_check_perms_ensures_0#p3_ds () Bool (or (= hept_check_perms_result 1) (= hept_check_perms_result 0)))
(assert (not hept_check_perms_ensures_0#p3_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_check_perms_ensures_0#p4 (source line 0)
; --- discharge (hept_check_perms_ensures_0#p4) ---
(push)
(assert hept_check_perms_requires_4)
(define-fun hept_check_perms_ensures_0#p4_ds () Bool (or (= hept_check_perms_result 1) (= hept_check_perms_result 0)))
(assert (not hept_check_perms_ensures_0#p4_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_check_perms_ensures_0#pfull (source line 0)
; --- discharge (hept_check_perms_ensures_0#pfull) ---
(push)
(assert hept_check_perms_requires_0)
(assert hept_check_perms_requires_1)
(assert hept_check_perms_requires_2)
(assert hept_check_perms_requires_3)
(assert hept_check_perms_requires_4)
(define-fun hept_check_perms_ensures_0#pfull_ds () Bool (or (= hept_check_perms_result 1) (= hept_check_perms_result 0)))
(assert (not hept_check_perms_ensures_0#pfull_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_check_perms_ensures_1 (source line 0)
(define-fun hept_check_perms_ensures_1 () Bool (or (not (= hept_check_perms_result 1)) (= (bvand ((_ int2bv 64) (select p_hept_check_perms_table (div p_hept_check_perms_gpa 4096))) p_hept_check_perms_required) p_hept_check_perms_required)))

; hept_check_perms_ensures_1#p0 (source line 0)
; --- discharge (hept_check_perms_ensures_1#p0) ---
(push)
(assert hept_check_perms_requires_0)
(define-fun hept_check_perms_ensures_1#p0_ds () Bool (or (not (= hept_check_perms_result 1)) (= (bvand ((_ int2bv 64) (select p_hept_check_perms_table (div p_hept_check_perms_gpa 4096))) p_hept_check_perms_required) p_hept_check_perms_required)))
(assert (not hept_check_perms_ensures_1#p0_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_check_perms_ensures_1#p1 (source line 0)
; --- discharge (hept_check_perms_ensures_1#p1) ---
(push)
(assert hept_check_perms_requires_1)
(define-fun hept_check_perms_ensures_1#p1_ds () Bool (or (not (= hept_check_perms_result 1)) (= (bvand ((_ int2bv 64) (select p_hept_check_perms_table (div p_hept_check_perms_gpa 4096))) p_hept_check_perms_required) p_hept_check_perms_required)))
(assert (not hept_check_perms_ensures_1#p1_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_check_perms_ensures_1#p2 (source line 0)
; --- discharge (hept_check_perms_ensures_1#p2) ---
(push)
(assert hept_check_perms_requires_2)
(define-fun hept_check_perms_ensures_1#p2_ds () Bool (or (not (= hept_check_perms_result 1)) (= (bvand ((_ int2bv 64) (select p_hept_check_perms_table (div p_hept_check_perms_gpa 4096))) p_hept_check_perms_required) p_hept_check_perms_required)))
(assert (not hept_check_perms_ensures_1#p2_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_check_perms_ensures_1#p3 (source line 0)
; --- discharge (hept_check_perms_ensures_1#p3) ---
(push)
(assert hept_check_perms_requires_3)
(define-fun hept_check_perms_ensures_1#p3_ds () Bool (or (not (= hept_check_perms_result 1)) (= (bvand ((_ int2bv 64) (select p_hept_check_perms_table (div p_hept_check_perms_gpa 4096))) p_hept_check_perms_required) p_hept_check_perms_required)))
(assert (not hept_check_perms_ensures_1#p3_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_check_perms_ensures_1#p4 (source line 0)
; --- discharge (hept_check_perms_ensures_1#p4) ---
(push)
(assert hept_check_perms_requires_4)
(define-fun hept_check_perms_ensures_1#p4_ds () Bool (or (not (= hept_check_perms_result 1)) (= (bvand ((_ int2bv 64) (select p_hept_check_perms_table (div p_hept_check_perms_gpa 4096))) p_hept_check_perms_required) p_hept_check_perms_required)))
(assert (not hept_check_perms_ensures_1#p4_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; hept_check_perms_ensures_1#pfull (source line 0)
; --- discharge (hept_check_perms_ensures_1#pfull) ---
(push)
(assert hept_check_perms_requires_0)
(assert hept_check_perms_requires_1)
(assert hept_check_perms_requires_2)
(assert hept_check_perms_requires_3)
(assert hept_check_perms_requires_4)
(define-fun hept_check_perms_ensures_1#pfull_ds () Bool (or (not (= hept_check_perms_result 1)) (= (bvand ((_ int2bv 64) (select p_hept_check_perms_table (div p_hept_check_perms_gpa 4096))) p_hept_check_perms_required) p_hept_check_perms_required)))
(assert (not hept_check_perms_ensures_1#pfull_ds))
(check-sat-using (then simplify solve-eqs smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; hept_check_perms_ensures_ret_0_0 (return-site ensures, source line 0)
(define-fun hept_check_perms_ensures_ret_0_0 () Bool (or (= 1 1) (= 1 0)))

; --- discharge (hept_check_perms_ensures_ret_0_0) ---
(push)
(assert hept_check_perms_requires_0)
(assert hept_check_perms_requires_1)
(assert hept_check_perms_requires_2)
(assert hept_check_perms_requires_3)
(assert hept_check_perms_requires_4)
(assert (= (bvand ((_ int2bv 64) (select p_hept_check_perms_table (div p_hept_check_perms_gpa 4096))) p_hept_check_perms_required) p_hept_check_perms_required))
(assert (not hept_check_perms_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(pop)

; hept_check_perms_ensures_ret_1_1 (return-site ensures, source line 0)
(define-fun hept_check_perms_ensures_ret_1_1 () Bool (or (not (= 1 1)) (= (bvand ((_ int2bv 64) (select p_hept_check_perms_table (div p_hept_check_perms_gpa 4096))) p_hept_check_perms_required) p_hept_check_perms_required)))

; --- discharge (hept_check_perms_ensures_ret_1_1) ---
(push)
(assert hept_check_perms_requires_0)
(assert hept_check_perms_requires_1)
(assert hept_check_perms_requires_2)
(assert hept_check_perms_requires_3)
(assert hept_check_perms_requires_4)
(assert (= (bvand ((_ int2bv 64) (select p_hept_check_perms_table (div p_hept_check_perms_gpa 4096))) p_hept_check_perms_required) p_hept_check_perms_required))
(assert (not hept_check_perms_ensures_ret_1_1))
(check-sat-using (then simplify smt))
(pop)

; hept_check_perms_ensures_ret_2_0 (return-site ensures, source line 0)
(define-fun hept_check_perms_ensures_ret_2_0 () Bool (or (= 0 1) (= 0 0)))

; --- discharge (hept_check_perms_ensures_ret_2_0) ---
(push)
(assert hept_check_perms_requires_0)
(assert hept_check_perms_requires_1)
(assert hept_check_perms_requires_2)
(assert hept_check_perms_requires_3)
(assert hept_check_perms_requires_4)
(assert (not hept_check_perms_ensures_ret_2_0))
(check-sat-using (then simplify smt))
(pop)

; hept_check_perms_ensures_ret_3_1 (return-site ensures, source line 0)
(define-fun hept_check_perms_ensures_ret_3_1 () Bool (or (not (= 0 1)) (= (bvand ((_ int2bv 64) (select p_hept_check_perms_table (div p_hept_check_perms_gpa 4096))) p_hept_check_perms_required) p_hept_check_perms_required)))

; --- discharge (hept_check_perms_ensures_ret_3_1) ---
(push)
(assert hept_check_perms_requires_0)
(assert hept_check_perms_requires_1)
(assert hept_check_perms_requires_2)
(assert hept_check_perms_requires_3)
(assert hept_check_perms_requires_4)
(assert (not hept_check_perms_ensures_ret_3_1))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function hept_diff
; ============================================================
(declare-const p_hept_diff_host_ept (Array Int Int))
(declare-const p_hept_diff_golden (Array Int Int))
(declare-const hept_diff_result Int)

; ---- ensures (signature-level, fallback) ----
; hept_diff_ensures_0 (source line 0)
(define-fun hept_diff_ensures_0 () Bool (and (<= 0 hept_diff_result) (<= hept_diff_result 16)))

; hept_diff_ensures_0#s0 (source line 0)
; --- discharge (hept_diff_ensures_0#s0) ---
(push)
(define-fun hept_diff_ensures_0#s0_ds () Bool (<= 0 hept_diff_result))
(assert (not hept_diff_ensures_0#s0_ds))
(check-sat-using (then simplify smt))
(pop)

; hept_diff_ensures_0#s1 (source line 0)
; --- discharge (hept_diff_ensures_0#s1) ---
(push)
(define-fun hept_diff_ensures_0#s1_ds () Bool (<= hept_diff_result 16))
(assert (not hept_diff_ensures_0#s1_ds))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; hept_diff_ensures_ret_0_0 (return-site ensures, source line 0)
(define-fun hept_diff_ensures_ret_0_0 () Bool (and (<= 0 0) (<= 0 16)))

; --- discharge (hept_diff_ensures_ret_0_0) ---
(push)
(assert (not (= (select p_hept_diff_host_ept 0) (select p_hept_diff_golden 0))))
(assert (not hept_diff_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(pop)

; hept_diff_ensures_ret_1_0 (return-site ensures, source line 0)
(define-fun hept_diff_ensures_ret_1_0 () Bool (and (<= 0 1) (<= 1 16)))

; --- discharge (hept_diff_ensures_ret_1_0) ---
(push)
(assert (not (= (select p_hept_diff_host_ept 1) (select p_hept_diff_golden 1))))
(assert (not hept_diff_ensures_ret_1_0))
(check-sat-using (then simplify smt))
(pop)

; hept_diff_ensures_ret_2_0 (return-site ensures, source line 0)
(define-fun hept_diff_ensures_ret_2_0 () Bool (and (<= 0 2) (<= 2 16)))

; --- discharge (hept_diff_ensures_ret_2_0) ---
(push)
(assert (not (= (select p_hept_diff_host_ept 2) (select p_hept_diff_golden 2))))
(assert (not hept_diff_ensures_ret_2_0))
(check-sat-using (then simplify smt))
(pop)

; hept_diff_ensures_ret_3_0 (return-site ensures, source line 0)
(define-fun hept_diff_ensures_ret_3_0 () Bool (and (<= 0 3) (<= 3 16)))

; --- discharge (hept_diff_ensures_ret_3_0) ---
(push)
(assert (not (= (select p_hept_diff_host_ept 3) (select p_hept_diff_golden 3))))
(assert (not hept_diff_ensures_ret_3_0))
(check-sat-using (then simplify smt))
(pop)

; hept_diff_ensures_ret_4_0 (return-site ensures, source line 0)
(define-fun hept_diff_ensures_ret_4_0 () Bool (and (<= 0 4) (<= 4 16)))

; --- discharge (hept_diff_ensures_ret_4_0) ---
(push)
(assert (not (= (select p_hept_diff_host_ept 4) (select p_hept_diff_golden 4))))
(assert (not hept_diff_ensures_ret_4_0))
(check-sat-using (then simplify smt))
(pop)

; hept_diff_ensures_ret_5_0 (return-site ensures, source line 0)
(define-fun hept_diff_ensures_ret_5_0 () Bool (and (<= 0 5) (<= 5 16)))

; --- discharge (hept_diff_ensures_ret_5_0) ---
(push)
(assert (not (= (select p_hept_diff_host_ept 5) (select p_hept_diff_golden 5))))
(assert (not hept_diff_ensures_ret_5_0))
(check-sat-using (then simplify smt))
(pop)

; hept_diff_ensures_ret_6_0 (return-site ensures, source line 0)
(define-fun hept_diff_ensures_ret_6_0 () Bool (and (<= 0 6) (<= 6 16)))

; --- discharge (hept_diff_ensures_ret_6_0) ---
(push)
(assert (not (= (select p_hept_diff_host_ept 6) (select p_hept_diff_golden 6))))
(assert (not hept_diff_ensures_ret_6_0))
(check-sat-using (then simplify smt))
(pop)

; hept_diff_ensures_ret_7_0 (return-site ensures, source line 0)
(define-fun hept_diff_ensures_ret_7_0 () Bool (and (<= 0 7) (<= 7 16)))

; --- discharge (hept_diff_ensures_ret_7_0) ---
(push)
(assert (not (= (select p_hept_diff_host_ept 7) (select p_hept_diff_golden 7))))
(assert (not hept_diff_ensures_ret_7_0))
(check-sat-using (then simplify smt))
(pop)

; hept_diff_ensures_ret_8_0 (return-site ensures, source line 0)
(define-fun hept_diff_ensures_ret_8_0 () Bool (and (<= 0 8) (<= 8 16)))

; --- discharge (hept_diff_ensures_ret_8_0) ---
(push)
(assert (not (= (select p_hept_diff_host_ept 8) (select p_hept_diff_golden 8))))
(assert (not hept_diff_ensures_ret_8_0))
(check-sat-using (then simplify smt))
(pop)

; hept_diff_ensures_ret_9_0 (return-site ensures, source line 0)
(define-fun hept_diff_ensures_ret_9_0 () Bool (and (<= 0 9) (<= 9 16)))

; --- discharge (hept_diff_ensures_ret_9_0) ---
(push)
(assert (not (= (select p_hept_diff_host_ept 9) (select p_hept_diff_golden 9))))
(assert (not hept_diff_ensures_ret_9_0))
(check-sat-using (then simplify smt))
(pop)

; hept_diff_ensures_ret_10_0 (return-site ensures, source line 0)
(define-fun hept_diff_ensures_ret_10_0 () Bool (and (<= 0 10) (<= 10 16)))

; --- discharge (hept_diff_ensures_ret_10_0) ---
(push)
(assert (not (= (select p_hept_diff_host_ept 10) (select p_hept_diff_golden 10))))
(assert (not hept_diff_ensures_ret_10_0))
(check-sat-using (then simplify smt))
(pop)

; hept_diff_ensures_ret_11_0 (return-site ensures, source line 0)
(define-fun hept_diff_ensures_ret_11_0 () Bool (and (<= 0 11) (<= 11 16)))

; --- discharge (hept_diff_ensures_ret_11_0) ---
(push)
(assert (not (= (select p_hept_diff_host_ept 11) (select p_hept_diff_golden 11))))
(assert (not hept_diff_ensures_ret_11_0))
(check-sat-using (then simplify smt))
(pop)

; hept_diff_ensures_ret_12_0 (return-site ensures, source line 0)
(define-fun hept_diff_ensures_ret_12_0 () Bool (and (<= 0 12) (<= 12 16)))

; --- discharge (hept_diff_ensures_ret_12_0) ---
(push)
(assert (not (= (select p_hept_diff_host_ept 12) (select p_hept_diff_golden 12))))
(assert (not hept_diff_ensures_ret_12_0))
(check-sat-using (then simplify smt))
(pop)

; hept_diff_ensures_ret_13_0 (return-site ensures, source line 0)
(define-fun hept_diff_ensures_ret_13_0 () Bool (and (<= 0 13) (<= 13 16)))

; --- discharge (hept_diff_ensures_ret_13_0) ---
(push)
(assert (not (= (select p_hept_diff_host_ept 13) (select p_hept_diff_golden 13))))
(assert (not hept_diff_ensures_ret_13_0))
(check-sat-using (then simplify smt))
(pop)

; hept_diff_ensures_ret_14_0 (return-site ensures, source line 0)
(define-fun hept_diff_ensures_ret_14_0 () Bool (and (<= 0 14) (<= 14 16)))

; --- discharge (hept_diff_ensures_ret_14_0) ---
(push)
(assert (not (= (select p_hept_diff_host_ept 14) (select p_hept_diff_golden 14))))
(assert (not hept_diff_ensures_ret_14_0))
(check-sat-using (then simplify smt))
(pop)

; hept_diff_ensures_ret_15_0 (return-site ensures, source line 0)
(define-fun hept_diff_ensures_ret_15_0 () Bool (and (<= 0 15) (<= 15 16)))

; --- discharge (hept_diff_ensures_ret_15_0) ---
(push)
(assert (not (= (select p_hept_diff_host_ept 15) (select p_hept_diff_golden 15))))
(assert (not hept_diff_ensures_ret_15_0))
(check-sat-using (then simplify smt))
(pop)

; hept_diff_ensures_ret_16_0 (return-site ensures, source line 0)
(define-fun hept_diff_ensures_ret_16_0 () Bool (and (<= 0 16) (<= 16 16)))

; --- discharge (hept_diff_ensures_ret_16_0) ---
(push)
(assert (not hept_diff_ensures_ret_16_0))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function main
; ============================================================

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
(declare-const main_ph0 Int)
; note: replaced an unsupported subform (unsupported expr) with the uninterpreted constant main_ph0
(declare-const main_call_result_0 (Array Int Int))
; main_call_requires_1_0 (call requires, source line 25)
(define-fun main_call_requires_1_0 () Bool (<= 0 0))

; --- discharge (main_call_requires_1_0) ---
(push)
(assert (not main_call_requires_1_0))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_2_1 (call requires, source line 26)
(define-fun main_call_requires_2_1 () Bool (< 0 (* 16 4096)))

; --- discharge (main_call_requires_2_1) ---
(push)
(assert (not main_call_requires_2_1))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_3_2 (call requires, source line 27)
(define-fun main_call_requires_3_2 () Bool (= (mod 0 4096) 0))

; --- discharge (main_call_requires_3_2) ---
(push)
(assert (not main_call_requires_3_2))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_4_3 (call requires, source line 28)
(define-fun main_call_requires_4_3 () Bool (<= 0 1048576))

; --- discharge (main_call_requires_4_3) ---
(push)
(assert (not main_call_requires_4_3))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_5_4 (call requires, source line 29)
(define-fun main_call_requires_5_4 () Bool (= (mod 1048576 4096) 0))

; --- discharge (main_call_requires_5_4) ---
(push)
(assert (not main_call_requires_5_4))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_6_5 (call requires, source line 0)
(define-fun main_call_requires_6_5 () Bool (= (mod ((_ bv2int 64) (bvor (bvor ((_ int2bv 64) 1) ((_ int2bv 64) 2)) ((_ int2bv 64) 4))) 2) 1))

; --- discharge (main_call_requires_6_5) ---
(push)
(assert (not main_call_requires_6_5))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_7_6 (call requires, source line 0)
(define-fun main_call_requires_7_6 () Bool (= (bvand (bvor (bvor ((_ int2bv 64) 1) ((_ int2bv 64) 2)) ((_ int2bv 64) 4)) (bvnot (_ bv7 64))) (_ bv0 64)))

; --- discharge (main_call_requires_7_6) ---
(push)
(assert (not main_call_requires_7_6))
(check-sat-using (then simplify smt))
(pop)

(define-fun main_inline_8_req0 () Bool (<= 0 0))
(define-fun main_inline_8_req1 () Bool (< 0 (* 16 4096)))
(define-fun main_inline_8_req2 () Bool (= (mod 0 4096) 0))
(define-fun main_inline_8_req3 () Bool (<= 0 1048576))
(define-fun main_inline_8_req4 () Bool (= (mod 1048576 4096) 0))
(define-fun main_inline_8_req5 () Bool (= (mod ((_ bv2int 64) (bvor (bvor ((_ int2bv 64) 1) ((_ int2bv 64) 2)) ((_ int2bv 64) 4))) 2) 1))
(define-fun main_inline_8_req6 () Bool (= (bvand (bvor (bvor ((_ int2bv 64) 1) ((_ int2bv 64) 2)) ((_ int2bv 64) 4)) (bvnot (_ bv7 64))) (_ bv0 64)))
(declare-const main_call_result_9 Int)
; main_call_requires_10_0 (call requires, source line 68)
(define-fun main_call_requires_10_0 () Bool (<= 0 0))

; --- discharge (main_call_requires_10_0) ---
(push)
(assert (not main_call_requires_10_0))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_11_1 (call requires, source line 69)
(define-fun main_call_requires_11_1 () Bool (< 0 (* 16 4096)))

; --- discharge (main_call_requires_11_1) ---
(push)
(assert (not main_call_requires_11_1))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_12_2 (call requires, source line 70)
(define-fun main_call_requires_12_2 () Bool (= (mod 0 4096) 0))

; --- discharge (main_call_requires_12_2) ---
(push)
(assert (not main_call_requires_12_2))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_13_3 (call requires, source line 0)
(define-fun main_call_requires_13_3 () Bool (= (bvand ((_ int2bv 64) 4) (bvnot (_ bv7 64))) (_ bv0 64)))

; --- discharge (main_call_requires_13_3) ---
(push)
(assert (not main_call_requires_13_3))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_14_4 (call requires, source line 72)
(define-fun main_call_requires_14_4 () Bool (= (not (= (select (store main_ph0 (div 0 4096) ((_ bv2int 64) (bvor ((_ int2bv 64) 1048576) (bvor (bvor ((_ int2bv 64) 1) ((_ int2bv 64) 2)) ((_ int2bv 64) 4))))) (div 0 4096)) 0)) true))

; --- discharge (main_call_requires_14_4) ---
(push)
(assert (not main_call_requires_14_4))
(check-sat-using (then simplify smt))
(pop)

(define-fun main_inline_15_req0 () Bool (<= 0 0))
(define-fun main_inline_15_req1 () Bool (< 0 (* 16 4096)))
(define-fun main_inline_15_req2 () Bool (= (mod 0 4096) 0))
(define-fun main_inline_15_req3 () Bool (= (bvand ((_ int2bv 64) 4) (bvnot (_ bv7 64))) (_ bv0 64)))
(define-fun main_inline_15_req4 () Bool (= (not (= (select (store main_ph0 (div 0 4096) ((_ bv2int 64) (bvor ((_ int2bv 64) 1048576) (bvor (bvor ((_ int2bv 64) 1) ((_ int2bv 64) 2)) ((_ int2bv 64) 4))))) (div 0 4096)) 0)) true))
(declare-const main_ph1 Int)
; note: replaced an unsupported subform (unsupported expr) with the uninterpreted constant main_ph1
(declare-const main_ph2 Int)
; note: replaced an unsupported subform (unsupported expr) with the uninterpreted constant main_ph2
(declare-const main_call_result_16 Int)
; main_assert_18 (source line 129)
(define-fun main_assert_18 () Bool (or (< 16 16) (= 16 16)))

; --- discharge (main_assert_18) ---
(push)
(assert (not main_assert_18))
(check-sat-using (then simplify smt))
(pop)

; main_assert_19 (source line 130)
(define-fun main_assert_19 () Bool (= 16 16))

; --- discharge (main_assert_19) ---
(push)
(assert (not main_assert_19))
(check-sat-using (then simplify smt))
(pop)

(declare-const main_ph3 Int)
; note: replaced an unsupported subform (unsupported expr) with the uninterpreted constant main_ph3
(declare-const main_call_result_20 Int)
; main_assert_22 (source line 137)
(define-fun main_assert_22 () Bool (< 16 16))

; --- discharge (main_assert_22) ---
(push)
(assert (not main_assert_22))
(check-sat-using (then simplify smt))
(pop)

(declare-const main_call_result_23 Int)
; main_call_requires_24_0 (call requires, source line 54)
(define-fun main_call_requires_24_0 () Bool (<= 0 0))

; --- discharge (main_call_requires_24_0) ---
(push)
(assert (not main_call_requires_24_0))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_25_1 (call requires, source line 55)
(define-fun main_call_requires_25_1 () Bool (< 0 (* 16 4096)))

; --- discharge (main_call_requires_25_1) ---
(push)
(assert (not main_call_requires_25_1))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_26_2 (call requires, source line 56)
(define-fun main_call_requires_26_2 () Bool (= (mod 0 4096) 0))

; --- discharge (main_call_requires_26_2) ---
(push)
(assert (not main_call_requires_26_2))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_27_3 (call requires, source line 57)
(define-fun main_call_requires_27_3 () Bool (= (not (= (select (store main_ph0 (div 0 4096) ((_ bv2int 64) (bvor ((_ int2bv 64) 1048576) (bvor (bvor ((_ int2bv 64) 1) ((_ int2bv 64) 2)) ((_ int2bv 64) 4))))) (div 0 4096)) 0)) true))

; --- discharge (main_call_requires_27_3) ---
(push)
(assert (not main_call_requires_27_3))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_28_4 (call requires, source line 58)
(define-fun main_call_requires_28_4 () Bool (<= 0 (select (store main_ph0 (div 0 4096) ((_ bv2int 64) (bvor ((_ int2bv 64) 1048576) (bvor (bvor ((_ int2bv 64) 1) ((_ int2bv 64) 2)) ((_ int2bv 64) 4))))) (div 0 4096))))

; --- discharge (main_call_requires_28_4) ---
(push)
(assert (not main_call_requires_28_4))
(check-sat-using (then simplify smt))
(pop)

(define-fun main_inline_29_req0 () Bool (<= 0 0))
(define-fun main_inline_29_req1 () Bool (< 0 (* 16 4096)))
(define-fun main_inline_29_req2 () Bool (= (mod 0 4096) 0))
(define-fun main_inline_29_req3 () Bool (= (not (= (select (store main_ph0 (div 0 4096) ((_ bv2int 64) (bvor ((_ int2bv 64) 1048576) (bvor (bvor ((_ int2bv 64) 1) ((_ int2bv 64) 2)) ((_ int2bv 64) 4))))) (div 0 4096)) 0)) true))
(define-fun main_inline_29_req4 () Bool (<= 0 (select (store main_ph0 (div 0 4096) ((_ bv2int 64) (bvor ((_ int2bv 64) 1048576) (bvor (bvor ((_ int2bv 64) 1) ((_ int2bv 64) 2)) ((_ int2bv 64) 4))))) (div 0 4096))))
(declare-const main_call_result_30 (Array Int Int))
; main_call_requires_31_0 (call requires, source line 42)
(define-fun main_call_requires_31_0 () Bool (<= 0 0))

; --- discharge (main_call_requires_31_0) ---
(push)
(assert (not main_call_requires_31_0))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_32_1 (call requires, source line 43)
(define-fun main_call_requires_32_1 () Bool (< 0 (* 16 4096)))

; --- discharge (main_call_requires_32_1) ---
(push)
(assert (not main_call_requires_32_1))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_33_2 (call requires, source line 44)
(define-fun main_call_requires_33_2 () Bool (= (mod 0 4096) 0))

; --- discharge (main_call_requires_33_2) ---
(push)
(assert (not main_call_requires_33_2))
(check-sat-using (then simplify smt))
(pop)

(define-fun main_inline_34_req0 () Bool (<= 0 0))
(define-fun main_inline_34_req1 () Bool (< 0 (* 16 4096)))
(define-fun main_inline_34_req2 () Bool (= (mod 0 4096) 0))
(declare-const main_ph4 Int)
; note: replaced an unsupported subform (unsupported expr) with the uninterpreted constant main_ph4


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
; spec fn hept_entry (source line 10)
(declare-fun sf_hept_entry ((Array Int Int) Int) Int)
; note: spec fn hept_entry body references calls/array/field — declared uninterpreted

; spec fn hept_present (source line 11)
(define-fun sf_hept_present ((p_sf_hept_present_e Int)) Bool (= (mod p_sf_hept_present_e 2) 1))

; spec fn hept_writeable (source line 12)
(define-fun sf_hept_writeable ((p_sf_hept_writeable_e Int)) Bool (= (bvand ((_ int2bv 64) p_sf_hept_writeable_e) (_ bv2 64)) (_ bv2 64)))

; spec fn hept_executable (source line 13)
(define-fun sf_hept_executable ((p_sf_hept_executable_e Int)) Bool (= (bvand ((_ int2bv 64) p_sf_hept_executable_e) (_ bv4 64)) (_ bv4 64)))

; spec fn hept_frame (source line 14)
(define-fun sf_hept_frame ((p_sf_hept_frame_e Int)) Int ((_ bv2int 64) (bvshl (bvlshr ((_ int2bv 64) p_sf_hept_frame_e) ((_ int2bv 64) 12)) ((_ int2bv 64) 12))))

; spec fn hept_mapped (source line 15)
(declare-fun sf_hept_mapped ((Array Int Int) Int) Bool)
; note: spec fn hept_mapped body references calls/array/field — declared uninterpreted

; spec fn hept_table_invariant (source line 16)
(define-fun sf_hept_table_invariant ((p_sf_hept_table_invariant_table (Array Int Int))) Int (forall ((sf_hept_table_invariant_q_i_0 Int)) (=> (and (>= sf_hept_table_invariant_q_i_0 0) (< sf_hept_table_invariant_q_i_0 16)) (or (= (select p_sf_hept_table_invariant_table sf_hept_table_invariant_q_i_0) 0) (and (= (mod (select p_sf_hept_table_invariant_table sf_hept_table_invariant_q_i_0) 2) 1) (= (bvshl (bvlshr ((_ int2bv 64) (select p_sf_hept_table_invariant_table sf_hept_table_invariant_q_i_0)) ((_ int2bv 64) 12)) ((_ int2bv 64) 12)) (bvand ((_ int2bv 64) (select p_sf_hept_table_invariant_table sf_hept_table_invariant_q_i_0)) (bvnot (_ bv7 64)))))))))

; spec fn hept_preserves_untouched (source line 20)
(define-fun sf_hept_preserves_untouched ((p_sf_hept_preserves_untouched_table_in (Array Int Int)) (p_sf_hept_preserves_untouched_table_out (Array Int Int)) (p_sf_hept_preserves_untouched_idx Int)) Int (forall ((sf_hept_preserves_untouched_q_i_0 Int)) (=> (and (>= sf_hept_preserves_untouched_q_i_0 0) (< sf_hept_preserves_untouched_q_i_0 16)) (or (not (not (= sf_hept_preserves_untouched_q_i_0 p_sf_hept_preserves_untouched_idx))) (= (select p_sf_hept_preserves_untouched_table_in sf_hept_preserves_untouched_q_i_0) (select p_sf_hept_preserves_untouched_table_out sf_hept_preserves_untouched_q_i_0))))))

(exit)
