; oxide-generated SMT-LIB (contracts)
; Encoding: requires/ensures/invariant/assert as boolean terms;
;           discharge query per clause is (assert (not <term>)) then (check-sat).
;           unsat  => clause holds for all inputs (static discharge OK).
;           sat/unknown => clause could not be discharged (or uses an
;                        uninterpreted placeholder, flagged above).
; Types: Bool / Int / Real; bools are ints widened for arithmetic.

(set-logic ALL)
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
; function vcpu_a_map
; ============================================================
(declare-const p_vcpu_a_map_ept0 (_ BitVec 64))
(declare-const p_vcpu_a_map_ept1 (_ BitVec 64))
(declare-const vcpu_a_map_result (_ BitVec 64))

; ---- requires (assumed, not discharged) ----
; vcpu_a_map_requires_0 (source line 0)
(define-fun vcpu_a_map_requires_0 () Bool (= (mod ((_ bv2int 64) p_vcpu_a_map_ept0) 4096) 0))

; vcpu_a_map_requires_1 (source line 0)
(define-fun vcpu_a_map_requires_1 () Bool (or (= ((_ bv2int 64) p_vcpu_a_map_ept1) 0) (= (mod ((_ bv2int 64) p_vcpu_a_map_ept1) 4096) 0)))

; ---- ensures (signature-level, fallback) ----
; vcpu_a_map_ensures_0 (source line 0)
(define-fun vcpu_a_map_ensures_0 () Bool (= (mod ((_ bv2int 64) vcpu_a_map_result) 4096) 0))

; --- discharge (vcpu_a_map_ensures_0) ---
(push)
(assert vcpu_a_map_requires_0)
(assert vcpu_a_map_requires_1)
(assert (not vcpu_a_map_ensures_0))
(check-sat-using (then simplify smt))
(check-sat-using (then simplify (using-params smt :mbqi true)))
(check-sat-using (then simplify (using-params smt :mbqi false) (using-params smt :restart-strategy 0)))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; vcpu_a_map_ensures_ret_0_0 (return-site ensures, source line 0)
(define-fun vcpu_a_map_ensures_ret_0_0 () Bool (= (mod ((_ bv2int 64) p_vcpu_a_map_ept0) 4096) 0))

; --- discharge (vcpu_a_map_ensures_ret_0_0) ---
(push)
(assert vcpu_a_map_requires_0)
(assert vcpu_a_map_requires_1)
(assert (not vcpu_a_map_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(check-sat-using (then simplify (using-params smt :mbqi true)))
(check-sat-using (then simplify (using-params smt :mbqi false) (using-params smt :restart-strategy 0)))
(pop)


; ============================================================
; function vcpu_b_map
; ============================================================
(declare-const p_vcpu_b_map_ept0 (_ BitVec 64))
(declare-const p_vcpu_b_map_ept1 (_ BitVec 64))
(declare-const vcpu_b_map_result (_ BitVec 64))

; ---- requires (assumed, not discharged) ----
; vcpu_b_map_requires_0 (source line 0)
(define-fun vcpu_b_map_requires_0 () Bool (or (= ((_ bv2int 64) p_vcpu_b_map_ept0) 0) (= (mod ((_ bv2int 64) p_vcpu_b_map_ept0) 4096) 0)))

; vcpu_b_map_requires_1 (source line 0)
(define-fun vcpu_b_map_requires_1 () Bool (= (mod ((_ bv2int 64) p_vcpu_b_map_ept1) 4096) 0))

; ---- ensures (signature-level, fallback) ----
; vcpu_b_map_ensures_0 (source line 0)
(define-fun vcpu_b_map_ensures_0 () Bool (= (mod ((_ bv2int 64) vcpu_b_map_result) 4096) 0))

; --- discharge (vcpu_b_map_ensures_0) ---
(push)
(assert vcpu_b_map_requires_0)
(assert vcpu_b_map_requires_1)
(assert (not vcpu_b_map_ensures_0))
(check-sat-using (then simplify smt))
(check-sat-using (then simplify (using-params smt :mbqi true)))
(check-sat-using (then simplify (using-params smt :mbqi false) (using-params smt :restart-strategy 0)))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; vcpu_b_map_ensures_ret_0_0 (return-site ensures, source line 0)
(define-fun vcpu_b_map_ensures_ret_0_0 () Bool (= (mod ((_ bv2int 64) p_vcpu_b_map_ept1) 4096) 0))

; --- discharge (vcpu_b_map_ensures_ret_0_0) ---
(push)
(assert vcpu_b_map_requires_0)
(assert vcpu_b_map_requires_1)
(assert (not vcpu_b_map_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(check-sat-using (then simplify (using-params smt :mbqi true)))
(check-sat-using (then simplify (using-params smt :mbqi false) (using-params smt :restart-strategy 0)))
(pop)


; ============================================================
; function main
; ============================================================

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
(declare-const main_call_result_0 Int)
; main_call_requires_1_0 (call requires, source line 0)
(define-fun main_call_requires_1_0 () Bool (= (mod 8192 4096) 0))

; --- discharge (main_call_requires_1_0) ---
(push)
(assert (not main_call_requires_1_0))
(check-sat-using (then simplify smt))
(check-sat-using (then simplify (using-params smt :mbqi true)))
(check-sat-using (then simplify (using-params smt :mbqi false) (using-params smt :restart-strategy 0)))
(pop)

; main_call_requires_2_1 (call requires, source line 0)
(define-fun main_call_requires_2_1 () Bool (or (= 12288 0) (= (mod 12288 4096) 0)))

; --- discharge (main_call_requires_2_1) ---
(push)
(assert (not main_call_requires_2_1))
(check-sat-using (then simplify smt))
(check-sat-using (then simplify (using-params smt :mbqi true)))
(check-sat-using (then simplify (using-params smt :mbqi false) (using-params smt :restart-strategy 0)))
(pop)

(declare-const main_call_result_3 Int)
; main_call_requires_4_0 (call requires, source line 0)
(define-fun main_call_requires_4_0 () Bool (or (= 8192 0) (= (mod 8192 4096) 0)))

; --- discharge (main_call_requires_4_0) ---
(push)
(assert (= (mod 8192 4096) 0))
(assert (not main_call_requires_4_0))
(check-sat-using (then simplify smt))
(check-sat-using (then simplify (using-params smt :mbqi true)))
(check-sat-using (then simplify (using-params smt :mbqi false) (using-params smt :restart-strategy 0)))
(pop)

; main_call_requires_5_1 (call requires, source line 0)
(define-fun main_call_requires_5_1 () Bool (= (mod 12288 4096) 0))

; --- discharge (main_call_requires_5_1) ---
(push)
(assert (= (mod 8192 4096) 0))
(assert (not main_call_requires_5_1))
(check-sat-using (then simplify smt))
(check-sat-using (then simplify (using-params smt :mbqi true)))
(check-sat-using (then simplify (using-params smt :mbqi false) (using-params smt :restart-strategy 0)))
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
; spec fn ept_aligned (source line 18)
(define-fun sf_ept_aligned ((p_sf_ept_aligned_ept0 Int) (p_sf_ept_aligned_ept1 Int)) Bool (and (or (= p_sf_ept_aligned_ept0 0) (= (mod p_sf_ept_aligned_ept0 4096) 0)) (or (= p_sf_ept_aligned_ept1 0) (= (mod p_sf_ept_aligned_ept1 4096) 0))))


; ============================================================
; D8 — noninterference (Owicki-Gries cross-handler stability)
; ============================================================

; noninterference vcpu_a_map vcpu_b_map <= ept_aligned  (source line 49)

; --- pair: vcpu_a_map step vs vcpu_b_map assertion ---
(declare-const noninterference_vcpu_a_map_vs_vcpu_b_map_arg_0 Int)
(declare-const noninterference_vcpu_a_map_vs_vcpu_b_map_arg_1 Int)
(declare-const noninterference_vcpu_a_map_vs_vcpu_b_map_call_result_0 Int)
; noninterference_vcpu_a_map_vs_vcpu_b_map_call_requires_1_0 (call requires, source line 0)
(define-fun noninterference_vcpu_a_map_vs_vcpu_b_map_call_requires_1_0 () Bool (= (mod noninterference_vcpu_a_map_vs_vcpu_b_map_arg_0 4096) 0))

(exit)
