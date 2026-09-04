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

; ---- memory model axioms (auto-emitted) ----
(declare-fun invept (Int) Bool)
(declare-fun stale_tlb (Int Int) Bool)
(assert (forall ((cpu Int)) (=> (invept cpu) (forall ((addr Int)) (not (stale_tlb cpu addr))))))
(declare-fun happens_before (Int Int) Bool)
(assert (forall ((a Int) (b Int) (c Int)) (=> (and (happens_before a b) (happens_before b c)) (happens_before a c))))
(assert (forall ((a Int)) (not (happens_before a a))))
(declare-fun commit_order (Int Int Int) Bool)
(assert (forall ((cpu Int) (i Int) (j Int)) (=> (commit_order cpu i j) (< i j))))
(declare-fun mem_write (Int Int) Bool)
(declare-fun cached (Int Int) Bool)
(assert (forall ((w Int) (addr Int) (r Int)) (=> (and (mem_write w addr) (not (= r w))) (not (cached r addr)))))

; ============================================================
; function store_zero_load_back
; ============================================================
(declare-const p_store_zero_load_back_table Int)
(declare-const p_store_zero_load_back_idx Int)
(declare-const store_zero_load_back_result Int)

; ---- requires (assumed, not discharged) ----
; store_zero_load_back_requires_0 (source line 36)
(define-fun store_zero_load_back_requires_0 () Bool (<= 0 p_store_zero_load_back_idx))

; ---- ensures (signature-level, fallback) ----
; store_zero_load_back_ensures_0 (source line 37)
(define-fun store_zero_load_back_ensures_0 () Bool (= store_zero_load_back_result 0))

; --- discharge (store_zero_load_back_ensures_0) ---
(push)
(assert store_zero_load_back_requires_0)
(assert (not store_zero_load_back_ensures_0))
(check-sat-using (then simplify smt))
(check-sat-using (then simplify (using-params smt :mbqi true)))
(check-sat-using (then simplify (using-params smt :mbqi false) (using-params smt :restart-strategy 0)))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
(declare-const mmio_mem (Array Int Int))
; store_zero_load_back_ensures_ret_0_0 (return-site ensures, source line 37)
(define-fun store_zero_load_back_ensures_ret_0_0 () Bool (= (select (store mmio_mem ((_ bv2int 64) (bvadd ((_ int2bv 64) p_store_zero_load_back_table) (bvshl ((_ int2bv 64) p_store_zero_load_back_idx) ((_ int2bv 64) 3)))) 0) ((_ bv2int 64) (bvadd ((_ int2bv 64) p_store_zero_load_back_table) (bvshl ((_ int2bv 64) p_store_zero_load_back_idx) ((_ int2bv 64) 3))))) 0))

; --- discharge (store_zero_load_back_ensures_ret_0_0) ---
(push)
(assert store_zero_load_back_requires_0)
(assert (not store_zero_load_back_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(check-sat-using (then simplify (using-params smt :mbqi true)))
(check-sat-using (then simplify (using-params smt :mbqi false) (using-params smt :restart-strategy 0)))
(pop)


; ============================================================
; function store_zero_load_back_bad
; ============================================================
(declare-const p_store_zero_load_back_bad_table Int)
(declare-const p_store_zero_load_back_bad_idx Int)
(declare-const store_zero_load_back_bad_result Int)

; ---- requires (assumed, not discharged) ----
; store_zero_load_back_bad_requires_0 (source line 52)
(define-fun store_zero_load_back_bad_requires_0 () Bool (<= 0 p_store_zero_load_back_bad_idx))

; ---- ensures (signature-level, fallback) ----
; store_zero_load_back_bad_ensures_0 (source line 53)
(define-fun store_zero_load_back_bad_ensures_0 () Bool (= store_zero_load_back_bad_result 1))

; --- discharge (store_zero_load_back_bad_ensures_0) ---
(push)
(assert store_zero_load_back_bad_requires_0)
(assert (not store_zero_load_back_bad_ensures_0))
(check-sat-using (then simplify smt))
(check-sat-using (then simplify (using-params smt :mbqi true)))
(check-sat-using (then simplify (using-params smt :mbqi false) (using-params smt :restart-strategy 0)))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
(declare-const mmio_mem (Array Int Int))
; store_zero_load_back_bad_ensures_ret_0_0 (return-site ensures, source line 53)
(define-fun store_zero_load_back_bad_ensures_ret_0_0 () Bool (= (select (store mmio_mem ((_ bv2int 64) (bvadd ((_ int2bv 64) p_store_zero_load_back_bad_table) (bvshl ((_ int2bv 64) p_store_zero_load_back_bad_idx) ((_ int2bv 64) 3)))) 0) ((_ bv2int 64) (bvadd ((_ int2bv 64) p_store_zero_load_back_bad_table) (bvshl ((_ int2bv 64) p_store_zero_load_back_bad_idx) ((_ int2bv 64) 3))))) 1))

; --- discharge (store_zero_load_back_bad_ensures_ret_0_0) ---
(push)
(assert store_zero_load_back_bad_requires_0)
(assert (not store_zero_load_back_bad_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(check-sat-using (then simplify (using-params smt :mbqi true)))
(check-sat-using (then simplify (using-params smt :mbqi false) (using-params smt :restart-strategy 0)))
(pop)


; ============================================================
; function main
; ============================================================

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----

(exit)
