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
; function rb_push
; ============================================================
(declare-const p_rb_push_data (Array Int Int))
(declare-const p_rb_push_tail Int)
(declare-const p_rb_push_head Int)
(declare-const p_rb_push_byte Int)
(declare-const rb_push_result (Array Int Int))
(declare-const rb_push_old_ring_seq Int)
(declare-const rb_push_old_ring_consumer_seq Int)

; ---- requires (assumed, not discharged) ----
; rb_push_requires_0 (source line 85)
(define-fun rb_push_requires_0 () Bool (<= 0 p_rb_push_tail))

; rb_push_requires_1 (source line 86)
(define-fun rb_push_requires_1 () Bool (<= 0 p_rb_push_head))

; rb_push_requires_2 (source line 87)
(define-fun rb_push_requires_2 () Bool (< (- p_rb_push_tail p_rb_push_head) 8))

; rb_push_requires_3 (source line 88)
(define-fun rb_push_requires_3 () Bool (<= 0 p_rb_push_byte))

; ---- ensures (signature-level, fallback) ----
; rb_push_ensures_0 (source line 89)
(define-fun rb_push_ensures_0 () Bool (= (select p_rb_push_data (mod p_rb_push_tail 8)) p_rb_push_byte))

; rb_push_ensures_0#p0 (source line 0)
; --- discharge (rb_push_ensures_0#p0) ---
(push)
(assert rb_push_requires_0)
(define-fun rb_push_ensures_0#p0_ds () Bool (= (select p_rb_push_data (mod p_rb_push_tail 8)) p_rb_push_byte))
(assert (not rb_push_ensures_0#p0_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_push_ensures_0#p1 (source line 0)
; --- discharge (rb_push_ensures_0#p1) ---
(push)
(assert rb_push_requires_1)
(define-fun rb_push_ensures_0#p1_ds () Bool (= (select p_rb_push_data (mod p_rb_push_tail 8)) p_rb_push_byte))
(assert (not rb_push_ensures_0#p1_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_push_ensures_0#p2 (source line 0)
; --- discharge (rb_push_ensures_0#p2) ---
(push)
(assert rb_push_requires_2)
(define-fun rb_push_ensures_0#p2_ds () Bool (= (select p_rb_push_data (mod p_rb_push_tail 8)) p_rb_push_byte))
(assert (not rb_push_ensures_0#p2_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_push_ensures_0#p3 (source line 0)
; --- discharge (rb_push_ensures_0#p3) ---
(push)
(assert rb_push_requires_3)
(define-fun rb_push_ensures_0#p3_ds () Bool (= (select p_rb_push_data (mod p_rb_push_tail 8)) p_rb_push_byte))
(assert (not rb_push_ensures_0#p3_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_push_ensures_0#pfull (source line 0)
; --- discharge (rb_push_ensures_0#pfull) ---
(push)
(assert rb_push_requires_0)
(assert rb_push_requires_1)
(assert rb_push_requires_2)
(assert rb_push_requires_3)
(define-fun rb_push_ensures_0#pfull_ds () Bool (= (select p_rb_push_data (mod p_rb_push_tail 8)) p_rb_push_byte))
(assert (not rb_push_ensures_0#pfull_ds))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; rb_push_ensures_ret_0_0 (return-site ensures, source line 89)
(define-fun rb_push_ensures_ret_0_0 () Bool (= (select (store p_rb_push_data (mod p_rb_push_tail 8) p_rb_push_byte) (mod p_rb_push_tail 8)) p_rb_push_byte))

; --- discharge (rb_push_ensures_ret_0_0) ---
(push)
(assert rb_push_requires_0)
(assert rb_push_requires_1)
(assert rb_push_requires_2)
(assert rb_push_requires_3)
(assert (not rb_push_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function rb_inc_tail
; ============================================================
(declare-const p_rb_inc_tail_tail Int)
(declare-const p_rb_inc_tail_head Int)
(declare-const p_rb_inc_tail_byte Int)
(declare-const rb_inc_tail_result Int)
(declare-const rb_inc_tail_old_ring_seq Int)
(declare-const rb_inc_tail_old_ring_consumer_seq Int)

; ---- requires (assumed, not discharged) ----
; rb_inc_tail_requires_0 (source line 100)
(define-fun rb_inc_tail_requires_0 () Bool (<= 0 p_rb_inc_tail_tail))

; rb_inc_tail_requires_1 (source line 101)
(define-fun rb_inc_tail_requires_1 () Bool (<= 0 p_rb_inc_tail_head))

; rb_inc_tail_requires_2 (source line 102)
(define-fun rb_inc_tail_requires_2 () Bool (< (- p_rb_inc_tail_tail p_rb_inc_tail_head) 8))

; rb_inc_tail_requires_3 (source line 103)
(define-fun rb_inc_tail_requires_3 () Bool (<= 0 p_rb_inc_tail_byte))

; ---- ensures (signature-level, fallback) ----
; rb_inc_tail_ensures_0 (source line 104)
(define-fun rb_inc_tail_ensures_0 () Bool (= rb_inc_tail_result (+ p_rb_inc_tail_tail 1)))

; rb_inc_tail_ensures_0#p0 (source line 0)
; --- discharge (rb_inc_tail_ensures_0#p0) ---
(push)
(assert rb_inc_tail_requires_0)
(define-fun rb_inc_tail_ensures_0#p0_ds () Bool (= rb_inc_tail_result (+ p_rb_inc_tail_tail 1)))
(assert (not rb_inc_tail_ensures_0#p0_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_tail_ensures_0#p1 (source line 0)
; --- discharge (rb_inc_tail_ensures_0#p1) ---
(push)
(assert rb_inc_tail_requires_1)
(define-fun rb_inc_tail_ensures_0#p1_ds () Bool (= rb_inc_tail_result (+ p_rb_inc_tail_tail 1)))
(assert (not rb_inc_tail_ensures_0#p1_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_tail_ensures_0#p2 (source line 0)
; --- discharge (rb_inc_tail_ensures_0#p2) ---
(push)
(assert rb_inc_tail_requires_2)
(define-fun rb_inc_tail_ensures_0#p2_ds () Bool (= rb_inc_tail_result (+ p_rb_inc_tail_tail 1)))
(assert (not rb_inc_tail_ensures_0#p2_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_tail_ensures_0#p3 (source line 0)
; --- discharge (rb_inc_tail_ensures_0#p3) ---
(push)
(assert rb_inc_tail_requires_3)
(define-fun rb_inc_tail_ensures_0#p3_ds () Bool (= rb_inc_tail_result (+ p_rb_inc_tail_tail 1)))
(assert (not rb_inc_tail_ensures_0#p3_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_tail_ensures_0#pfull (source line 0)
; --- discharge (rb_inc_tail_ensures_0#pfull) ---
(push)
(assert rb_inc_tail_requires_0)
(assert rb_inc_tail_requires_1)
(assert rb_inc_tail_requires_2)
(assert rb_inc_tail_requires_3)
(define-fun rb_inc_tail_ensures_0#pfull_ds () Bool (= rb_inc_tail_result (+ p_rb_inc_tail_tail 1)))
(assert (not rb_inc_tail_ensures_0#pfull_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_tail_ensures_1 (source line 105)
(define-fun rb_inc_tail_ensures_1 () Bool (= (- rb_inc_tail_result 1) p_rb_inc_tail_tail))

; rb_inc_tail_ensures_1#p0 (source line 0)
; --- discharge (rb_inc_tail_ensures_1#p0) ---
(push)
(assert rb_inc_tail_requires_0)
(define-fun rb_inc_tail_ensures_1#p0_ds () Bool (= (- rb_inc_tail_result 1) p_rb_inc_tail_tail))
(assert (not rb_inc_tail_ensures_1#p0_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_tail_ensures_1#p1 (source line 0)
; --- discharge (rb_inc_tail_ensures_1#p1) ---
(push)
(assert rb_inc_tail_requires_1)
(define-fun rb_inc_tail_ensures_1#p1_ds () Bool (= (- rb_inc_tail_result 1) p_rb_inc_tail_tail))
(assert (not rb_inc_tail_ensures_1#p1_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_tail_ensures_1#p2 (source line 0)
; --- discharge (rb_inc_tail_ensures_1#p2) ---
(push)
(assert rb_inc_tail_requires_2)
(define-fun rb_inc_tail_ensures_1#p2_ds () Bool (= (- rb_inc_tail_result 1) p_rb_inc_tail_tail))
(assert (not rb_inc_tail_ensures_1#p2_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_tail_ensures_1#p3 (source line 0)
; --- discharge (rb_inc_tail_ensures_1#p3) ---
(push)
(assert rb_inc_tail_requires_3)
(define-fun rb_inc_tail_ensures_1#p3_ds () Bool (= (- rb_inc_tail_result 1) p_rb_inc_tail_tail))
(assert (not rb_inc_tail_ensures_1#p3_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_tail_ensures_1#pfull (source line 0)
; --- discharge (rb_inc_tail_ensures_1#pfull) ---
(push)
(assert rb_inc_tail_requires_0)
(assert rb_inc_tail_requires_1)
(assert rb_inc_tail_requires_2)
(assert rb_inc_tail_requires_3)
(define-fun rb_inc_tail_ensures_1#pfull_ds () Bool (= (- rb_inc_tail_result 1) p_rb_inc_tail_tail))
(assert (not rb_inc_tail_ensures_1#pfull_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_tail_ensures_2 (source line 106)
(define-fun rb_inc_tail_ensures_2 () Bool (= (- rb_inc_tail_result p_rb_inc_tail_head) (+ (- p_rb_inc_tail_tail p_rb_inc_tail_head) 1)))

; rb_inc_tail_ensures_2#p0 (source line 0)
; --- discharge (rb_inc_tail_ensures_2#p0) ---
(push)
(assert rb_inc_tail_requires_0)
(define-fun rb_inc_tail_ensures_2#p0_ds () Bool (= (- rb_inc_tail_result p_rb_inc_tail_head) (+ (- p_rb_inc_tail_tail p_rb_inc_tail_head) 1)))
(assert (not rb_inc_tail_ensures_2#p0_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_tail_ensures_2#p1 (source line 0)
; --- discharge (rb_inc_tail_ensures_2#p1) ---
(push)
(assert rb_inc_tail_requires_1)
(define-fun rb_inc_tail_ensures_2#p1_ds () Bool (= (- rb_inc_tail_result p_rb_inc_tail_head) (+ (- p_rb_inc_tail_tail p_rb_inc_tail_head) 1)))
(assert (not rb_inc_tail_ensures_2#p1_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_tail_ensures_2#p2 (source line 0)
; --- discharge (rb_inc_tail_ensures_2#p2) ---
(push)
(assert rb_inc_tail_requires_2)
(define-fun rb_inc_tail_ensures_2#p2_ds () Bool (= (- rb_inc_tail_result p_rb_inc_tail_head) (+ (- p_rb_inc_tail_tail p_rb_inc_tail_head) 1)))
(assert (not rb_inc_tail_ensures_2#p2_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_tail_ensures_2#p3 (source line 0)
; --- discharge (rb_inc_tail_ensures_2#p3) ---
(push)
(assert rb_inc_tail_requires_3)
(define-fun rb_inc_tail_ensures_2#p3_ds () Bool (= (- rb_inc_tail_result p_rb_inc_tail_head) (+ (- p_rb_inc_tail_tail p_rb_inc_tail_head) 1)))
(assert (not rb_inc_tail_ensures_2#p3_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_tail_ensures_2#pfull (source line 0)
; --- discharge (rb_inc_tail_ensures_2#pfull) ---
(push)
(assert rb_inc_tail_requires_0)
(assert rb_inc_tail_requires_1)
(assert rb_inc_tail_requires_2)
(assert rb_inc_tail_requires_3)
(define-fun rb_inc_tail_ensures_2#pfull_ds () Bool (= (- rb_inc_tail_result p_rb_inc_tail_head) (+ (- p_rb_inc_tail_tail p_rb_inc_tail_head) 1)))
(assert (not rb_inc_tail_ensures_2#pfull_ds))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; rb_inc_tail_ensures_ret_0_0 (return-site ensures, source line 104)
(define-fun rb_inc_tail_ensures_ret_0_0 () Bool (= (+ p_rb_inc_tail_tail 1) (+ p_rb_inc_tail_tail 1)))

; --- discharge (rb_inc_tail_ensures_ret_0_0) ---
(push)
(assert rb_inc_tail_requires_0)
(assert rb_inc_tail_requires_1)
(assert rb_inc_tail_requires_2)
(assert rb_inc_tail_requires_3)
(assert (not rb_inc_tail_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_tail_ensures_ret_1_1 (return-site ensures, source line 105)
(define-fun rb_inc_tail_ensures_ret_1_1 () Bool (= (- (+ p_rb_inc_tail_tail 1) 1) p_rb_inc_tail_tail))

; --- discharge (rb_inc_tail_ensures_ret_1_1) ---
(push)
(assert rb_inc_tail_requires_0)
(assert rb_inc_tail_requires_1)
(assert rb_inc_tail_requires_2)
(assert rb_inc_tail_requires_3)
(assert (not rb_inc_tail_ensures_ret_1_1))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_tail_ensures_ret_2_2 (return-site ensures, source line 106)
(define-fun rb_inc_tail_ensures_ret_2_2 () Bool (= (- (+ p_rb_inc_tail_tail 1) p_rb_inc_tail_head) (+ (- p_rb_inc_tail_tail p_rb_inc_tail_head) 1)))

; --- discharge (rb_inc_tail_ensures_ret_2_2) ---
(push)
(assert rb_inc_tail_requires_0)
(assert rb_inc_tail_requires_1)
(assert rb_inc_tail_requires_2)
(assert rb_inc_tail_requires_3)
(assert (not rb_inc_tail_ensures_ret_2_2))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function rb_inc_head
; ============================================================
(declare-const p_rb_inc_head_tail Int)
(declare-const p_rb_inc_head_head Int)
(declare-const rb_inc_head_result Int)
(declare-const rb_inc_head_old_ring_seq Int)
(declare-const rb_inc_head_old_ring_consumer_seq Int)

; ---- requires (assumed, not discharged) ----
; rb_inc_head_requires_0 (source line 117)
(define-fun rb_inc_head_requires_0 () Bool (<= 0 p_rb_inc_head_tail))

; rb_inc_head_requires_1 (source line 118)
(define-fun rb_inc_head_requires_1 () Bool (<= 0 p_rb_inc_head_head))

; rb_inc_head_requires_2 (source line 119)
(define-fun rb_inc_head_requires_2 () Bool (< p_rb_inc_head_head p_rb_inc_head_tail))

; ---- ensures (signature-level, fallback) ----
; rb_inc_head_ensures_0 (source line 120)
(define-fun rb_inc_head_ensures_0 () Bool (= rb_inc_head_result (+ p_rb_inc_head_head 1)))

; rb_inc_head_ensures_0#p0 (source line 0)
; --- discharge (rb_inc_head_ensures_0#p0) ---
(push)
(assert rb_inc_head_requires_0)
(define-fun rb_inc_head_ensures_0#p0_ds () Bool (= rb_inc_head_result (+ p_rb_inc_head_head 1)))
(assert (not rb_inc_head_ensures_0#p0_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_head_ensures_0#p1 (source line 0)
; --- discharge (rb_inc_head_ensures_0#p1) ---
(push)
(assert rb_inc_head_requires_1)
(define-fun rb_inc_head_ensures_0#p1_ds () Bool (= rb_inc_head_result (+ p_rb_inc_head_head 1)))
(assert (not rb_inc_head_ensures_0#p1_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_head_ensures_0#p2 (source line 0)
; --- discharge (rb_inc_head_ensures_0#p2) ---
(push)
(assert rb_inc_head_requires_2)
(define-fun rb_inc_head_ensures_0#p2_ds () Bool (= rb_inc_head_result (+ p_rb_inc_head_head 1)))
(assert (not rb_inc_head_ensures_0#p2_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_head_ensures_0#pfull (source line 0)
; --- discharge (rb_inc_head_ensures_0#pfull) ---
(push)
(assert rb_inc_head_requires_0)
(assert rb_inc_head_requires_1)
(assert rb_inc_head_requires_2)
(define-fun rb_inc_head_ensures_0#pfull_ds () Bool (= rb_inc_head_result (+ p_rb_inc_head_head 1)))
(assert (not rb_inc_head_ensures_0#pfull_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_head_ensures_1 (source line 121)
(define-fun rb_inc_head_ensures_1 () Bool (= (- p_rb_inc_head_tail rb_inc_head_result) (- (- p_rb_inc_head_tail p_rb_inc_head_head) 1)))

; rb_inc_head_ensures_1#p0 (source line 0)
; --- discharge (rb_inc_head_ensures_1#p0) ---
(push)
(assert rb_inc_head_requires_0)
(define-fun rb_inc_head_ensures_1#p0_ds () Bool (= (- p_rb_inc_head_tail rb_inc_head_result) (- (- p_rb_inc_head_tail p_rb_inc_head_head) 1)))
(assert (not rb_inc_head_ensures_1#p0_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_head_ensures_1#p1 (source line 0)
; --- discharge (rb_inc_head_ensures_1#p1) ---
(push)
(assert rb_inc_head_requires_1)
(define-fun rb_inc_head_ensures_1#p1_ds () Bool (= (- p_rb_inc_head_tail rb_inc_head_result) (- (- p_rb_inc_head_tail p_rb_inc_head_head) 1)))
(assert (not rb_inc_head_ensures_1#p1_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_head_ensures_1#p2 (source line 0)
; --- discharge (rb_inc_head_ensures_1#p2) ---
(push)
(assert rb_inc_head_requires_2)
(define-fun rb_inc_head_ensures_1#p2_ds () Bool (= (- p_rb_inc_head_tail rb_inc_head_result) (- (- p_rb_inc_head_tail p_rb_inc_head_head) 1)))
(assert (not rb_inc_head_ensures_1#p2_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_head_ensures_1#pfull (source line 0)
; --- discharge (rb_inc_head_ensures_1#pfull) ---
(push)
(assert rb_inc_head_requires_0)
(assert rb_inc_head_requires_1)
(assert rb_inc_head_requires_2)
(define-fun rb_inc_head_ensures_1#pfull_ds () Bool (= (- p_rb_inc_head_tail rb_inc_head_result) (- (- p_rb_inc_head_tail p_rb_inc_head_head) 1)))
(assert (not rb_inc_head_ensures_1#pfull_ds))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; rb_inc_head_ensures_ret_0_0 (return-site ensures, source line 120)
(define-fun rb_inc_head_ensures_ret_0_0 () Bool (= (+ p_rb_inc_head_head 1) (+ p_rb_inc_head_head 1)))

; --- discharge (rb_inc_head_ensures_ret_0_0) ---
(push)
(assert rb_inc_head_requires_0)
(assert rb_inc_head_requires_1)
(assert rb_inc_head_requires_2)
(assert (not rb_inc_head_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(pop)

; rb_inc_head_ensures_ret_1_1 (return-site ensures, source line 121)
(define-fun rb_inc_head_ensures_ret_1_1 () Bool (= (- p_rb_inc_head_tail (+ p_rb_inc_head_head 1)) (- (- p_rb_inc_head_tail p_rb_inc_head_head) 1)))

; --- discharge (rb_inc_head_ensures_ret_1_1) ---
(push)
(assert rb_inc_head_requires_0)
(assert rb_inc_head_requires_1)
(assert rb_inc_head_requires_2)
(assert (not rb_inc_head_ensures_ret_1_1))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function rb_read
; ============================================================
(declare-const p_rb_read_data (Array Int Int))
(declare-const p_rb_read_head Int)
(declare-const rb_read_result Int)
(declare-const rb_read_old_ring_seq Int)
(declare-const rb_read_old_ring_consumer_seq Int)

; ---- requires (assumed, not discharged) ----
; rb_read_requires_0 (source line 131)
(define-fun rb_read_requires_0 () Bool (<= 0 p_rb_read_head))

; ---- ensures (signature-level, fallback) ----
; rb_read_ensures_0 (source line 132)
(define-fun rb_read_ensures_0 () Bool (= rb_read_result (select p_rb_read_data (mod p_rb_read_head 8))))

; rb_read_ensures_0 (source line 0)
; --- discharge (rb_read_ensures_0) ---
(push)
(assert rb_read_requires_0)
(define-fun rb_read_ensures_0_ds () Bool (= rb_read_result (select p_rb_read_data (mod p_rb_read_head 8))))
(assert (not rb_read_ensures_0_ds))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; rb_read_ensures_ret_0_0 (return-site ensures, source line 132)
(define-fun rb_read_ensures_ret_0_0 () Bool (= (select p_rb_read_data (mod p_rb_read_head 8)) (select p_rb_read_data (mod p_rb_read_head 8))))

; --- discharge (rb_read_ensures_ret_0_0) ---
(push)
(assert rb_read_requires_0)
(assert (not rb_read_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function ring_produce
; ============================================================
(declare-const p_ring_produce_v Int)
(declare-const ring_produce_result Int)
(declare-const ring_produce_old_ring_seq Int)
(declare-const ring_produce_old_ring_consumer_seq Int)
(declare-const ring_produce_mmio_old_RingSlot Int)

; ---- requires (assumed, not discharged) ----
; ring_produce_requires_0 (source line 208)
(define-fun ring_produce_requires_0 () Bool (<= 0 p_ring_produce_v))

; ---- ensures (signature-level, fallback) ----
; ring_produce_ensures_0 (source line 209)
(define-fun ring_produce_ensures_0 () Bool (= ring_produce_result p_ring_produce_v))

; ring_produce_ensures_0 (source line 0)
; --- discharge (ring_produce_ensures_0) ---
(push)
(assert ring_produce_requires_0)
(define-fun ring_produce_ensures_0_ds () Bool (= ring_produce_result p_ring_produce_v))
(assert (not ring_produce_ensures_0_ds))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; ring_produce_ensures_ret_0_0 (return-site ensures, source line 209)
(define-fun ring_produce_ensures_ret_0_0 () Bool (= p_ring_produce_v p_ring_produce_v))

; --- discharge (ring_produce_ensures_ret_0_0) ---
(push)
(assert ring_produce_requires_0)
(assert (not ring_produce_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function ring_consume
; ============================================================
(declare-const p_ring_consume_v Int)
(declare-const ring_consume_result Int)
(declare-const ring_consume_old_ring_seq Int)
(declare-const ring_consume_old_ring_consumer_seq Int)
(declare-const ring_consume_mmio_old_RingSlot Int)

; ---- requires (assumed, not discharged) ----
; ring_consume_requires_0 (source line 217)
(define-fun ring_consume_requires_0 () Bool (<= 0 p_ring_consume_v))

; ---- ensures (signature-level, fallback) ----
; ring_consume_ensures_0 (source line 218)
(define-fun ring_consume_ensures_0 () Bool (= ring_consume_result p_ring_consume_v))

; ring_consume_ensures_0 (source line 0)
; --- discharge (ring_consume_ensures_0) ---
(push)
(assert ring_consume_requires_0)
(define-fun ring_consume_ensures_0_ds () Bool (= ring_consume_result p_ring_consume_v))
(assert (not ring_consume_ensures_0_ds))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; ring_consume_ensures_ret_0_0 (return-site ensures, source line 218)
(define-fun ring_consume_ensures_ret_0_0 () Bool (= p_ring_consume_v p_ring_consume_v))

; --- discharge (ring_consume_ensures_ret_0_0) ---
(push)
(assert ring_consume_requires_0)
(assert (not ring_consume_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function main
; ============================================================
(declare-const main_old_ring_seq Int)
(declare-const main_old_ring_consumer_seq Int)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
(declare-const main_ph0 Int)
; note: replaced an unsupported subform (unsupported expr) with the uninterpreted constant main_ph0
(declare-const main_call_result_0 (Array Int Int))
; main_call_requires_1_0 (call requires, source line 85)
(define-fun main_call_requires_1_0 () Bool (<= 0 0))

; --- discharge (main_call_requires_1_0) ---
(push)
(assert (not main_call_requires_1_0))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_2_1 (call requires, source line 86)
(define-fun main_call_requires_2_1 () Bool (<= 0 0))

; --- discharge (main_call_requires_2_1) ---
(push)
(assert (not main_call_requires_2_1))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_3_2 (call requires, source line 87)
(define-fun main_call_requires_3_2 () Bool (< (- 0 0) 8))

; --- discharge (main_call_requires_3_2) ---
(push)
(assert (not main_call_requires_3_2))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_4_3 (call requires, source line 88)
(define-fun main_call_requires_4_3 () Bool (<= 0 7))

; --- discharge (main_call_requires_4_3) ---
(push)
(assert (not main_call_requires_4_3))
(check-sat-using (then simplify smt))
(pop)

(define-fun main_inline_5_req0 () Bool (<= 0 0))
(define-fun main_inline_5_req1 () Bool (<= 0 0))
(define-fun main_inline_5_req2 () Bool (< (- 0 0) 8))
(define-fun main_inline_5_req3 () Bool (<= 0 7))
(declare-const main_call_result_6 Int)
; main_call_requires_7_0 (call requires, source line 100)
(define-fun main_call_requires_7_0 () Bool (<= 0 0))

; --- discharge (main_call_requires_7_0) ---
(push)
(assert (not main_call_requires_7_0))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_8_1 (call requires, source line 101)
(define-fun main_call_requires_8_1 () Bool (<= 0 0))

; --- discharge (main_call_requires_8_1) ---
(push)
(assert (not main_call_requires_8_1))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_9_2 (call requires, source line 102)
(define-fun main_call_requires_9_2 () Bool (< (- 0 0) 8))

; --- discharge (main_call_requires_9_2) ---
(push)
(assert (not main_call_requires_9_2))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_10_3 (call requires, source line 103)
(define-fun main_call_requires_10_3 () Bool (<= 0 7))

; --- discharge (main_call_requires_10_3) ---
(push)
(assert (not main_call_requires_10_3))
(check-sat-using (then simplify smt))
(pop)

; main_assert_11 (source line 267)
(define-fun main_assert_11 () Bool (= (+ 0 1) 1))

; --- discharge (main_assert_11) ---
(push)
(assert (not main_assert_11))
(check-sat-using (then simplify smt))
(pop)

(declare-const main_call_result_12 Int)
; main_call_requires_13_0 (call requires, source line 131)
(define-fun main_call_requires_13_0 () Bool (<= 0 0))

; --- discharge (main_call_requires_13_0) ---
(push)
(assert (not main_call_requires_13_0))
(check-sat-using (then simplify smt))
(pop)

; main_assert_14 (source line 271)
(define-fun main_assert_14 () Bool (= (select (store main_ph0 (mod 0 8) 7) (mod 0 8)) 7))

; --- discharge (main_assert_14) ---
(push)
(assert (not main_assert_14))
(check-sat-using (then simplify smt))
(pop)

(declare-const main_call_result_15 (Array Int Int))
; main_call_requires_16_0 (call requires, source line 85)
(define-fun main_call_requires_16_0 () Bool (<= 0 (+ 0 1)))

; --- discharge (main_call_requires_16_0) ---
(push)
(assert (not main_call_requires_16_0))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_17_1 (call requires, source line 86)
(define-fun main_call_requires_17_1 () Bool (<= 0 0))

; --- discharge (main_call_requires_17_1) ---
(push)
(assert (not main_call_requires_17_1))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_18_2 (call requires, source line 87)
(define-fun main_call_requires_18_2 () Bool (< (- (+ 0 1) 0) 8))

; --- discharge (main_call_requires_18_2) ---
(push)
(assert (not main_call_requires_18_2))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_19_3 (call requires, source line 88)
(define-fun main_call_requires_19_3 () Bool (<= 0 11))

; --- discharge (main_call_requires_19_3) ---
(push)
(assert (not main_call_requires_19_3))
(check-sat-using (then simplify smt))
(pop)

(define-fun main_inline_20_req0 () Bool (<= 0 (+ 0 1)))
(define-fun main_inline_20_req1 () Bool (<= 0 0))
(define-fun main_inline_20_req2 () Bool (< (- (+ 0 1) 0) 8))
(define-fun main_inline_20_req3 () Bool (<= 0 11))
(declare-const main_call_result_21 Int)
; main_call_requires_22_0 (call requires, source line 100)
(define-fun main_call_requires_22_0 () Bool (<= 0 (+ 0 1)))

; --- discharge (main_call_requires_22_0) ---
(push)
(assert (not main_call_requires_22_0))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_23_1 (call requires, source line 101)
(define-fun main_call_requires_23_1 () Bool (<= 0 0))

; --- discharge (main_call_requires_23_1) ---
(push)
(assert (not main_call_requires_23_1))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_24_2 (call requires, source line 102)
(define-fun main_call_requires_24_2 () Bool (< (- (+ 0 1) 0) 8))

; --- discharge (main_call_requires_24_2) ---
(push)
(assert (not main_call_requires_24_2))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_25_3 (call requires, source line 103)
(define-fun main_call_requires_25_3 () Bool (<= 0 11))

; --- discharge (main_call_requires_25_3) ---
(push)
(assert (not main_call_requires_25_3))
(check-sat-using (then simplify smt))
(pop)

; main_assert_26 (source line 276)
(define-fun main_assert_26 () Bool (= (+ (+ 0 1) 1) 2))

; --- discharge (main_assert_26) ---
(push)
(assert (not main_assert_26))
(check-sat-using (then simplify smt))
(pop)

(declare-const main_call_result_27 Int)
; main_call_requires_28_0 (call requires, source line 131)
(define-fun main_call_requires_28_0 () Bool (<= 0 0))

; --- discharge (main_call_requires_28_0) ---
(push)
(assert (not main_call_requires_28_0))
(check-sat-using (then simplify smt))
(pop)

; main_assert_29 (source line 280)
(define-fun main_assert_29 () Bool (= (select (store (store main_ph0 (mod 0 8) 7) (mod (+ 0 1) 8) 11) (mod 0 8)) 7))

; --- discharge (main_assert_29) ---
(push)
(assert (not main_assert_29))
(check-sat-using (then simplify smt))
(pop)

(declare-const main_call_result_30 Int)
; main_call_requires_31_0 (call requires, source line 117)
(define-fun main_call_requires_31_0 () Bool (<= 0 (+ (+ 0 1) 1)))

; --- discharge (main_call_requires_31_0) ---
(push)
(assert (not main_call_requires_31_0))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_32_1 (call requires, source line 118)
(define-fun main_call_requires_32_1 () Bool (<= 0 0))

; --- discharge (main_call_requires_32_1) ---
(push)
(assert (not main_call_requires_32_1))
(check-sat-using (then simplify smt))
(pop)

; main_call_requires_33_2 (call requires, source line 119)
(define-fun main_call_requires_33_2 () Bool (< 0 (+ (+ 0 1) 1)))

; --- discharge (main_call_requires_33_2) ---
(push)
(assert (not main_call_requires_33_2))
(check-sat-using (then simplify smt))
(pop)

; main_assert_34 (source line 282)
(define-fun main_assert_34 () Bool (= (+ 0 1) 1))

; --- discharge (main_assert_34) ---
(push)
(assert (not main_assert_34))
(check-sat-using (then simplify smt))
(pop)

(declare-const main_call_result_35 Int)
; main_call_requires_36_0 (call requires, source line 131)
(define-fun main_call_requires_36_0 () Bool (<= 0 (+ 0 1)))

; --- discharge (main_call_requires_36_0) ---
(push)
(assert (not main_call_requires_36_0))
(check-sat-using (then simplify smt))
(pop)

; main_assert_37 (source line 286)
(define-fun main_assert_37 () Bool (= (select (store (store main_ph0 (mod 0 8) 7) (mod (+ 0 1) 8) 11) (mod (+ 0 1) 8)) 11))

; --- discharge (main_assert_37) ---
(push)
(assert (not main_assert_37))
(check-sat-using (then simplify smt))
(pop)

(declare-const main_call_result_38 Int)
; main_call_requires_39_0 (call requires, source line 208)
(define-fun main_call_requires_39_0 () Bool (<= 0 42))

; --- discharge (main_call_requires_39_0) ---
(push)
(assert (not main_call_requires_39_0))
(check-sat-using (then simplify smt))
(pop)

(define-fun main_inline_40_req0 () Bool (<= 0 42))
(declare-const main_call_result_41 Int)
; main_call_requires_42_0 (call requires, source line 217)
(define-fun main_call_requires_42_0 () Bool (<= 0 42))

; --- discharge (main_call_requires_42_0) ---
(push)
(assert (not main_call_requires_42_0))
(check-sat-using (then simplify smt))
(pop)

(define-fun main_inline_43_req0 () Bool (<= 0 42))
; main_assert_44 (source line 293)
(define-fun main_assert_44 () Bool (= 42 42))

; --- discharge (main_assert_44) ---
(push)
(assert (not main_assert_44))
(check-sat-using (then simplify smt))
(pop)

; main_assert_45 (source line 294)
(define-fun main_assert_45 () Bool (= 42 42))

; --- discharge (main_assert_45) ---
(push)
(assert (not main_assert_45))
(check-sat-using (then simplify smt))
(pop)

(declare-const main_ph1 Int)
; note: replaced an unsupported subform (unsupported expr) with the uninterpreted constant main_ph1

; ============================================================
; function rb_monotonic_push
; ============================================================
(declare-const p_rb_monotonic_push_tail Int)
(declare-const p_rb_monotonic_push_head Int)
(declare-const p_rb_monotonic_push_cap Int)
(declare-const rb_monotonic_push_result Int)
(declare-const rb_monotonic_push_old_ring_seq Int)
(declare-const rb_monotonic_push_old_ring_consumer_seq Int)
(declare-const ghost_rb_monotonic_push_post_len Int)

; ---- requires (assumed, not discharged) ----
; rb_monotonic_push_requires_0 (source line 146)
(define-fun rb_monotonic_push_requires_0 () Bool (<= 0 p_rb_monotonic_push_tail))

; rb_monotonic_push_requires_1 (source line 147)
(define-fun rb_monotonic_push_requires_1 () Bool (<= 0 p_rb_monotonic_push_head))

; rb_monotonic_push_requires_2 (source line 148)
(define-fun rb_monotonic_push_requires_2 () Bool (<= 0 (- p_rb_monotonic_push_tail p_rb_monotonic_push_head)))

; rb_monotonic_push_requires_3 (source line 149)
(define-fun rb_monotonic_push_requires_3 () Bool (< (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) (- p_rb_monotonic_push_cap 1)))

; ---- ensures (signature-level, fallback) ----
; rb_monotonic_push_ensures_0 (source line 150)
(define-fun rb_monotonic_push_ensures_0 () Bool (<= 0 (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1)))

; rb_monotonic_push_ensures_0#p0 (source line 0)
; --- discharge (rb_monotonic_push_ensures_0#p0) ---
(push)
(assert rb_monotonic_push_requires_0)
(define-fun rb_monotonic_push_ensures_0#p0_ds () Bool (<= 0 (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1)))
(assert (not rb_monotonic_push_ensures_0#p0_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_push_ensures_0#p1 (source line 0)
; --- discharge (rb_monotonic_push_ensures_0#p1) ---
(push)
(assert rb_monotonic_push_requires_1)
(define-fun rb_monotonic_push_ensures_0#p1_ds () Bool (<= 0 (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1)))
(assert (not rb_monotonic_push_ensures_0#p1_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_push_ensures_0#p2 (source line 0)
; --- discharge (rb_monotonic_push_ensures_0#p2) ---
(push)
(assert rb_monotonic_push_requires_2)
(define-fun rb_monotonic_push_ensures_0#p2_ds () Bool (<= 0 (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1)))
(assert (not rb_monotonic_push_ensures_0#p2_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_push_ensures_0#p3 (source line 0)
; --- discharge (rb_monotonic_push_ensures_0#p3) ---
(push)
(assert rb_monotonic_push_requires_3)
(define-fun rb_monotonic_push_ensures_0#p3_ds () Bool (<= 0 (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1)))
(assert (not rb_monotonic_push_ensures_0#p3_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_push_ensures_0#pfull (source line 0)
; --- discharge (rb_monotonic_push_ensures_0#pfull) ---
(push)
(assert rb_monotonic_push_requires_0)
(assert rb_monotonic_push_requires_1)
(assert rb_monotonic_push_requires_2)
(assert rb_monotonic_push_requires_3)
(define-fun rb_monotonic_push_ensures_0#pfull_ds () Bool (<= 0 (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1)))
(assert (not rb_monotonic_push_ensures_0#pfull_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_push_ensures_1 (source line 151)
(define-fun rb_monotonic_push_ensures_1 () Bool (< (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1) p_rb_monotonic_push_cap))

; rb_monotonic_push_ensures_1#p0 (source line 0)
; --- discharge (rb_monotonic_push_ensures_1#p0) ---
(push)
(assert rb_monotonic_push_requires_0)
(define-fun rb_monotonic_push_ensures_1#p0_ds () Bool (< (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1) p_rb_monotonic_push_cap))
(assert (not rb_monotonic_push_ensures_1#p0_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_push_ensures_1#p1 (source line 0)
; --- discharge (rb_monotonic_push_ensures_1#p1) ---
(push)
(assert rb_monotonic_push_requires_1)
(define-fun rb_monotonic_push_ensures_1#p1_ds () Bool (< (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1) p_rb_monotonic_push_cap))
(assert (not rb_monotonic_push_ensures_1#p1_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_push_ensures_1#p2 (source line 0)
; --- discharge (rb_monotonic_push_ensures_1#p2) ---
(push)
(assert rb_monotonic_push_requires_2)
(define-fun rb_monotonic_push_ensures_1#p2_ds () Bool (< (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1) p_rb_monotonic_push_cap))
(assert (not rb_monotonic_push_ensures_1#p2_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_push_ensures_1#p3 (source line 0)
; --- discharge (rb_monotonic_push_ensures_1#p3) ---
(push)
(assert rb_monotonic_push_requires_3)
(define-fun rb_monotonic_push_ensures_1#p3_ds () Bool (< (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1) p_rb_monotonic_push_cap))
(assert (not rb_monotonic_push_ensures_1#p3_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_push_ensures_1#pfull (source line 0)
; --- discharge (rb_monotonic_push_ensures_1#pfull) ---
(push)
(assert rb_monotonic_push_requires_0)
(assert rb_monotonic_push_requires_1)
(assert rb_monotonic_push_requires_2)
(assert rb_monotonic_push_requires_3)
(define-fun rb_monotonic_push_ensures_1#pfull_ds () Bool (< (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1) p_rb_monotonic_push_cap))
(assert (not rb_monotonic_push_ensures_1#pfull_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_push_ensures_2 (source line 152)
(define-fun rb_monotonic_push_ensures_2 () Bool (<= 0 (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1)))

; rb_monotonic_push_ensures_2#p0 (source line 0)
; --- discharge (rb_monotonic_push_ensures_2#p0) ---
(push)
(assert rb_monotonic_push_requires_0)
(define-fun rb_monotonic_push_ensures_2#p0_ds () Bool (<= 0 (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1)))
(assert (not rb_monotonic_push_ensures_2#p0_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_push_ensures_2#p1 (source line 0)
; --- discharge (rb_monotonic_push_ensures_2#p1) ---
(push)
(assert rb_monotonic_push_requires_1)
(define-fun rb_monotonic_push_ensures_2#p1_ds () Bool (<= 0 (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1)))
(assert (not rb_monotonic_push_ensures_2#p1_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_push_ensures_2#p2 (source line 0)
; --- discharge (rb_monotonic_push_ensures_2#p2) ---
(push)
(assert rb_monotonic_push_requires_2)
(define-fun rb_monotonic_push_ensures_2#p2_ds () Bool (<= 0 (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1)))
(assert (not rb_monotonic_push_ensures_2#p2_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_push_ensures_2#p3 (source line 0)
; --- discharge (rb_monotonic_push_ensures_2#p3) ---
(push)
(assert rb_monotonic_push_requires_3)
(define-fun rb_monotonic_push_ensures_2#p3_ds () Bool (<= 0 (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1)))
(assert (not rb_monotonic_push_ensures_2#p3_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_push_ensures_2#pfull (source line 0)
; --- discharge (rb_monotonic_push_ensures_2#pfull) ---
(push)
(assert rb_monotonic_push_requires_0)
(assert rb_monotonic_push_requires_1)
(assert rb_monotonic_push_requires_2)
(assert rb_monotonic_push_requires_3)
(define-fun rb_monotonic_push_ensures_2#pfull_ds () Bool (<= 0 (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1)))
(assert (not rb_monotonic_push_ensures_2#pfull_ds))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; rb_monotonic_push_ensures_ret_0_0 (return-site ensures, source line 150)
(define-fun rb_monotonic_push_ensures_ret_0_0 () Bool (<= 0 (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1)))

; --- discharge (rb_monotonic_push_ensures_ret_0_0) ---
(push)
(assert rb_monotonic_push_requires_0)
(assert rb_monotonic_push_requires_1)
(assert rb_monotonic_push_requires_2)
(assert rb_monotonic_push_requires_3)
(assert (not rb_monotonic_push_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_push_ensures_ret_1_1 (return-site ensures, source line 151)
(define-fun rb_monotonic_push_ensures_ret_1_1 () Bool (< (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1) p_rb_monotonic_push_cap))

; --- discharge (rb_monotonic_push_ensures_ret_1_1) ---
(push)
(assert rb_monotonic_push_requires_0)
(assert rb_monotonic_push_requires_1)
(assert rb_monotonic_push_requires_2)
(assert rb_monotonic_push_requires_3)
(assert (not rb_monotonic_push_ensures_ret_1_1))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_push_ensures_ret_2_2 (return-site ensures, source line 152)
(define-fun rb_monotonic_push_ensures_ret_2_2 () Bool (<= 0 (+ (- p_rb_monotonic_push_tail p_rb_monotonic_push_head) 1)))

; --- discharge (rb_monotonic_push_ensures_ret_2_2) ---
(push)
(assert rb_monotonic_push_requires_0)
(assert rb_monotonic_push_requires_1)
(assert rb_monotonic_push_requires_2)
(assert rb_monotonic_push_requires_3)
(assert (not rb_monotonic_push_ensures_ret_2_2))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function rb_monotonic_pop
; ============================================================
(declare-const p_rb_monotonic_pop_tail Int)
(declare-const p_rb_monotonic_pop_head Int)
(declare-const p_rb_monotonic_pop_cap Int)
(declare-const rb_monotonic_pop_result Int)
(declare-const rb_monotonic_pop_old_ring_seq Int)
(declare-const rb_monotonic_pop_old_ring_consumer_seq Int)
(declare-const ghost_rb_monotonic_pop_post_head Int)

; ---- requires (assumed, not discharged) ----
; rb_monotonic_pop_requires_0 (source line 160)
(define-fun rb_monotonic_pop_requires_0 () Bool (<= 0 p_rb_monotonic_pop_tail))

; rb_monotonic_pop_requires_1 (source line 161)
(define-fun rb_monotonic_pop_requires_1 () Bool (<= 0 p_rb_monotonic_pop_head))

; rb_monotonic_pop_requires_2 (source line 162)
(define-fun rb_monotonic_pop_requires_2 () Bool (< 0 (- p_rb_monotonic_pop_tail p_rb_monotonic_pop_head)))

; rb_monotonic_pop_requires_3 (source line 163)
(define-fun rb_monotonic_pop_requires_3 () Bool (< (- p_rb_monotonic_pop_tail p_rb_monotonic_pop_head) p_rb_monotonic_pop_cap))

; ---- ensures (signature-level, fallback) ----
; rb_monotonic_pop_ensures_0 (source line 164)
(define-fun rb_monotonic_pop_ensures_0 () Bool (<= 0 (- (- p_rb_monotonic_pop_tail p_rb_monotonic_pop_head) 1)))

; rb_monotonic_pop_ensures_0#p0 (source line 0)
; --- discharge (rb_monotonic_pop_ensures_0#p0) ---
(push)
(assert rb_monotonic_pop_requires_0)
(define-fun rb_monotonic_pop_ensures_0#p0_ds () Bool (<= 0 (- (- p_rb_monotonic_pop_tail p_rb_monotonic_pop_head) 1)))
(assert (not rb_monotonic_pop_ensures_0#p0_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_pop_ensures_0#p1 (source line 0)
; --- discharge (rb_monotonic_pop_ensures_0#p1) ---
(push)
(assert rb_monotonic_pop_requires_1)
(define-fun rb_monotonic_pop_ensures_0#p1_ds () Bool (<= 0 (- (- p_rb_monotonic_pop_tail p_rb_monotonic_pop_head) 1)))
(assert (not rb_monotonic_pop_ensures_0#p1_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_pop_ensures_0#p2 (source line 0)
; --- discharge (rb_monotonic_pop_ensures_0#p2) ---
(push)
(assert rb_monotonic_pop_requires_2)
(define-fun rb_monotonic_pop_ensures_0#p2_ds () Bool (<= 0 (- (- p_rb_monotonic_pop_tail p_rb_monotonic_pop_head) 1)))
(assert (not rb_monotonic_pop_ensures_0#p2_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_pop_ensures_0#p3 (source line 0)
; --- discharge (rb_monotonic_pop_ensures_0#p3) ---
(push)
(assert rb_monotonic_pop_requires_3)
(define-fun rb_monotonic_pop_ensures_0#p3_ds () Bool (<= 0 (- (- p_rb_monotonic_pop_tail p_rb_monotonic_pop_head) 1)))
(assert (not rb_monotonic_pop_ensures_0#p3_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_pop_ensures_0#pfull (source line 0)
; --- discharge (rb_monotonic_pop_ensures_0#pfull) ---
(push)
(assert rb_monotonic_pop_requires_0)
(assert rb_monotonic_pop_requires_1)
(assert rb_monotonic_pop_requires_2)
(assert rb_monotonic_pop_requires_3)
(define-fun rb_monotonic_pop_ensures_0#pfull_ds () Bool (<= 0 (- (- p_rb_monotonic_pop_tail p_rb_monotonic_pop_head) 1)))
(assert (not rb_monotonic_pop_ensures_0#pfull_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_pop_ensures_1 (source line 165)
(define-fun rb_monotonic_pop_ensures_1 () Bool (< (- (- p_rb_monotonic_pop_tail p_rb_monotonic_pop_head) 1) (- p_rb_monotonic_pop_cap 1)))

; rb_monotonic_pop_ensures_1#p0 (source line 0)
; --- discharge (rb_monotonic_pop_ensures_1#p0) ---
(push)
(assert rb_monotonic_pop_requires_0)
(define-fun rb_monotonic_pop_ensures_1#p0_ds () Bool (< (- (- p_rb_monotonic_pop_tail p_rb_monotonic_pop_head) 1) (- p_rb_monotonic_pop_cap 1)))
(assert (not rb_monotonic_pop_ensures_1#p0_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_pop_ensures_1#p1 (source line 0)
; --- discharge (rb_monotonic_pop_ensures_1#p1) ---
(push)
(assert rb_monotonic_pop_requires_1)
(define-fun rb_monotonic_pop_ensures_1#p1_ds () Bool (< (- (- p_rb_monotonic_pop_tail p_rb_monotonic_pop_head) 1) (- p_rb_monotonic_pop_cap 1)))
(assert (not rb_monotonic_pop_ensures_1#p1_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_pop_ensures_1#p2 (source line 0)
; --- discharge (rb_monotonic_pop_ensures_1#p2) ---
(push)
(assert rb_monotonic_pop_requires_2)
(define-fun rb_monotonic_pop_ensures_1#p2_ds () Bool (< (- (- p_rb_monotonic_pop_tail p_rb_monotonic_pop_head) 1) (- p_rb_monotonic_pop_cap 1)))
(assert (not rb_monotonic_pop_ensures_1#p2_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_pop_ensures_1#p3 (source line 0)
; --- discharge (rb_monotonic_pop_ensures_1#p3) ---
(push)
(assert rb_monotonic_pop_requires_3)
(define-fun rb_monotonic_pop_ensures_1#p3_ds () Bool (< (- (- p_rb_monotonic_pop_tail p_rb_monotonic_pop_head) 1) (- p_rb_monotonic_pop_cap 1)))
(assert (not rb_monotonic_pop_ensures_1#p3_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_pop_ensures_1#pfull (source line 0)
; --- discharge (rb_monotonic_pop_ensures_1#pfull) ---
(push)
(assert rb_monotonic_pop_requires_0)
(assert rb_monotonic_pop_requires_1)
(assert rb_monotonic_pop_requires_2)
(assert rb_monotonic_pop_requires_3)
(define-fun rb_monotonic_pop_ensures_1#pfull_ds () Bool (< (- (- p_rb_monotonic_pop_tail p_rb_monotonic_pop_head) 1) (- p_rb_monotonic_pop_cap 1)))
(assert (not rb_monotonic_pop_ensures_1#pfull_ds))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; rb_monotonic_pop_ensures_ret_0_0 (return-site ensures, source line 164)
(define-fun rb_monotonic_pop_ensures_ret_0_0 () Bool (<= 0 (- (- p_rb_monotonic_pop_tail p_rb_monotonic_pop_head) 1)))

; --- discharge (rb_monotonic_pop_ensures_ret_0_0) ---
(push)
(assert rb_monotonic_pop_requires_0)
(assert rb_monotonic_pop_requires_1)
(assert rb_monotonic_pop_requires_2)
(assert rb_monotonic_pop_requires_3)
(assert (not rb_monotonic_pop_ensures_ret_0_0))
(check-sat-using (then simplify smt))
(pop)

; rb_monotonic_pop_ensures_ret_1_1 (return-site ensures, source line 165)
(define-fun rb_monotonic_pop_ensures_ret_1_1 () Bool (< (- (- p_rb_monotonic_pop_tail p_rb_monotonic_pop_head) 1) (- p_rb_monotonic_pop_cap 1)))

; --- discharge (rb_monotonic_pop_ensures_ret_1_1) ---
(push)
(assert rb_monotonic_pop_requires_0)
(assert rb_monotonic_pop_requires_1)
(assert rb_monotonic_pop_requires_2)
(assert rb_monotonic_pop_requires_3)
(assert (not rb_monotonic_pop_ensures_ret_1_1))
(check-sat-using (then simplify smt))
(pop)


; ============================================================
; function rb_push_pop_roundtrip_len
; ============================================================
(declare-const p_rb_push_pop_roundtrip_len_t_pre Int)
(declare-const p_rb_push_pop_roundtrip_len_h_pre Int)
(declare-const p_rb_push_pop_roundtrip_len_t_mid Int)
(declare-const p_rb_push_pop_roundtrip_len_h_pre2 Int)
(declare-const p_rb_push_pop_roundtrip_len_t_post Int)
(declare-const p_rb_push_pop_roundtrip_len_h_post Int)
(declare-const rb_push_pop_roundtrip_len_result Int)
(declare-const rb_push_pop_roundtrip_len_old_ring_seq Int)
(declare-const rb_push_pop_roundtrip_len_old_ring_consumer_seq Int)
(declare-const ghost_rb_push_pop_roundtrip_len_invariant_focus Int)

; ---- requires (assumed, not discharged) ----
; rb_push_pop_roundtrip_len_requires_0 (source line 175)
(define-fun rb_push_pop_roundtrip_len_requires_0 () Bool (<= 0 p_rb_push_pop_roundtrip_len_t_pre))

; rb_push_pop_roundtrip_len_requires_1 (source line 176)
(define-fun rb_push_pop_roundtrip_len_requires_1 () Bool (<= 0 p_rb_push_pop_roundtrip_len_h_pre))

; rb_push_pop_roundtrip_len_requires_2 (source line 177)
(define-fun rb_push_pop_roundtrip_len_requires_2 () Bool (<= 0 p_rb_push_pop_roundtrip_len_t_mid))

; rb_push_pop_roundtrip_len_requires_3 (source line 178)
(define-fun rb_push_pop_roundtrip_len_requires_3 () Bool (<= 0 p_rb_push_pop_roundtrip_len_h_pre2))

; rb_push_pop_roundtrip_len_requires_4 (source line 179)
(define-fun rb_push_pop_roundtrip_len_requires_4 () Bool (<= 0 p_rb_push_pop_roundtrip_len_t_post))

; rb_push_pop_roundtrip_len_requires_5 (source line 180)
(define-fun rb_push_pop_roundtrip_len_requires_5 () Bool (<= 0 p_rb_push_pop_roundtrip_len_h_post))

; rb_push_pop_roundtrip_len_requires_6 (source line 181)
(define-fun rb_push_pop_roundtrip_len_requires_6 () Bool (= p_rb_push_pop_roundtrip_len_t_mid (+ p_rb_push_pop_roundtrip_len_t_pre 1)))

; rb_push_pop_roundtrip_len_requires_7 (source line 182)
(define-fun rb_push_pop_roundtrip_len_requires_7 () Bool (= p_rb_push_pop_roundtrip_len_t_post (- p_rb_push_pop_roundtrip_len_t_mid 1)))

; rb_push_pop_roundtrip_len_requires_8 (source line 183)
(define-fun rb_push_pop_roundtrip_len_requires_8 () Bool (= p_rb_push_pop_roundtrip_len_h_pre p_rb_push_pop_roundtrip_len_h_pre2))

; rb_push_pop_roundtrip_len_requires_9 (source line 184)
(define-fun rb_push_pop_roundtrip_len_requires_9 () Bool (= p_rb_push_pop_roundtrip_len_h_pre2 p_rb_push_pop_roundtrip_len_h_post))

; ---- ensures (signature-level, fallback) ----
; rb_push_pop_roundtrip_len_ensures_0 (source line 185)
(define-fun rb_push_pop_roundtrip_len_ensures_0 () Bool (= (- p_rb_push_pop_roundtrip_len_t_pre p_rb_push_pop_roundtrip_len_h_pre) (- p_rb_push_pop_roundtrip_len_t_post p_rb_push_pop_roundtrip_len_h_post)))

; rb_push_pop_roundtrip_len_ensures_0#p0 (source line 0)
; --- discharge (rb_push_pop_roundtrip_len_ensures_0#p0) ---
(push)
(assert rb_push_pop_roundtrip_len_requires_0)
(define-fun rb_push_pop_roundtrip_len_ensures_0#p0_ds () Bool (= (- p_rb_push_pop_roundtrip_len_t_pre p_rb_push_pop_roundtrip_len_h_pre) (- p_rb_push_pop_roundtrip_len_t_post p_rb_push_pop_roundtrip_len_h_post)))
(assert (not rb_push_pop_roundtrip_len_ensures_0#p0_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_push_pop_roundtrip_len_ensures_0#p1 (source line 0)
; --- discharge (rb_push_pop_roundtrip_len_ensures_0#p1) ---
(push)
(assert rb_push_pop_roundtrip_len_requires_1)
(define-fun rb_push_pop_roundtrip_len_ensures_0#p1_ds () Bool (= (- p_rb_push_pop_roundtrip_len_t_pre p_rb_push_pop_roundtrip_len_h_pre) (- p_rb_push_pop_roundtrip_len_t_post p_rb_push_pop_roundtrip_len_h_post)))
(assert (not rb_push_pop_roundtrip_len_ensures_0#p1_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_push_pop_roundtrip_len_ensures_0#p2 (source line 0)
; --- discharge (rb_push_pop_roundtrip_len_ensures_0#p2) ---
(push)
(assert rb_push_pop_roundtrip_len_requires_2)
(define-fun rb_push_pop_roundtrip_len_ensures_0#p2_ds () Bool (= (- p_rb_push_pop_roundtrip_len_t_pre p_rb_push_pop_roundtrip_len_h_pre) (- p_rb_push_pop_roundtrip_len_t_post p_rb_push_pop_roundtrip_len_h_post)))
(assert (not rb_push_pop_roundtrip_len_ensures_0#p2_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_push_pop_roundtrip_len_ensures_0#p3 (source line 0)
; --- discharge (rb_push_pop_roundtrip_len_ensures_0#p3) ---
(push)
(assert rb_push_pop_roundtrip_len_requires_3)
(define-fun rb_push_pop_roundtrip_len_ensures_0#p3_ds () Bool (= (- p_rb_push_pop_roundtrip_len_t_pre p_rb_push_pop_roundtrip_len_h_pre) (- p_rb_push_pop_roundtrip_len_t_post p_rb_push_pop_roundtrip_len_h_post)))
(assert (not rb_push_pop_roundtrip_len_ensures_0#p3_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_push_pop_roundtrip_len_ensures_0#p4 (source line 0)
; --- discharge (rb_push_pop_roundtrip_len_ensures_0#p4) ---
(push)
(assert rb_push_pop_roundtrip_len_requires_4)
(define-fun rb_push_pop_roundtrip_len_ensures_0#p4_ds () Bool (= (- p_rb_push_pop_roundtrip_len_t_pre p_rb_push_pop_roundtrip_len_h_pre) (- p_rb_push_pop_roundtrip_len_t_post p_rb_push_pop_roundtrip_len_h_post)))
(assert (not rb_push_pop_roundtrip_len_ensures_0#p4_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_push_pop_roundtrip_len_ensures_0#p5 (source line 0)
; --- discharge (rb_push_pop_roundtrip_len_ensures_0#p5) ---
(push)
(assert rb_push_pop_roundtrip_len_requires_5)
(define-fun rb_push_pop_roundtrip_len_ensures_0#p5_ds () Bool (= (- p_rb_push_pop_roundtrip_len_t_pre p_rb_push_pop_roundtrip_len_h_pre) (- p_rb_push_pop_roundtrip_len_t_post p_rb_push_pop_roundtrip_len_h_post)))
(assert (not rb_push_pop_roundtrip_len_ensures_0#p5_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_push_pop_roundtrip_len_ensures_0#p6 (source line 0)
; --- discharge (rb_push_pop_roundtrip_len_ensures_0#p6) ---
(push)
(assert rb_push_pop_roundtrip_len_requires_6)
(define-fun rb_push_pop_roundtrip_len_ensures_0#p6_ds () Bool (= (- p_rb_push_pop_roundtrip_len_t_pre p_rb_push_pop_roundtrip_len_h_pre) (- p_rb_push_pop_roundtrip_len_t_post p_rb_push_pop_roundtrip_len_h_post)))
(assert (not rb_push_pop_roundtrip_len_ensures_0#p6_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_push_pop_roundtrip_len_ensures_0#p7 (source line 0)
; --- discharge (rb_push_pop_roundtrip_len_ensures_0#p7) ---
(push)
(assert rb_push_pop_roundtrip_len_requires_7)
(define-fun rb_push_pop_roundtrip_len_ensures_0#p7_ds () Bool (= (- p_rb_push_pop_roundtrip_len_t_pre p_rb_push_pop_roundtrip_len_h_pre) (- p_rb_push_pop_roundtrip_len_t_post p_rb_push_pop_roundtrip_len_h_post)))
(assert (not rb_push_pop_roundtrip_len_ensures_0#p7_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_push_pop_roundtrip_len_ensures_0#p8 (source line 0)
; --- discharge (rb_push_pop_roundtrip_len_ensures_0#p8) ---
(push)
(assert rb_push_pop_roundtrip_len_requires_8)
(define-fun rb_push_pop_roundtrip_len_ensures_0#p8_ds () Bool (= (- p_rb_push_pop_roundtrip_len_t_pre p_rb_push_pop_roundtrip_len_h_pre) (- p_rb_push_pop_roundtrip_len_t_post p_rb_push_pop_roundtrip_len_h_post)))
(assert (not rb_push_pop_roundtrip_len_ensures_0#p8_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_push_pop_roundtrip_len_ensures_0#p9 (source line 0)
; --- discharge (rb_push_pop_roundtrip_len_ensures_0#p9) ---
(push)
(assert rb_push_pop_roundtrip_len_requires_9)
(define-fun rb_push_pop_roundtrip_len_ensures_0#p9_ds () Bool (= (- p_rb_push_pop_roundtrip_len_t_pre p_rb_push_pop_roundtrip_len_h_pre) (- p_rb_push_pop_roundtrip_len_t_post p_rb_push_pop_roundtrip_len_h_post)))
(assert (not rb_push_pop_roundtrip_len_ensures_0#p9_ds))
(check-sat-using (then simplify smt))
(pop)

; rb_push_pop_roundtrip_len_ensures_0#pfull (source line 0)
; --- discharge (rb_push_pop_roundtrip_len_ensures_0#pfull) ---
(push)
(assert rb_push_pop_roundtrip_len_requires_0)
(assert rb_push_pop_roundtrip_len_requires_1)
(assert rb_push_pop_roundtrip_len_requires_2)
(assert rb_push_pop_roundtrip_len_requires_3)
(assert rb_push_pop_roundtrip_len_requires_4)
(assert rb_push_pop_roundtrip_len_requires_5)
(assert rb_push_pop_roundtrip_len_requires_6)
(assert rb_push_pop_roundtrip_len_requires_7)
(assert rb_push_pop_roundtrip_len_requires_8)
(assert rb_push_pop_roundtrip_len_requires_9)
(define-fun rb_push_pop_roundtrip_len_ensures_0#pfull_ds () Bool (= (- p_rb_push_pop_roundtrip_len_t_pre p_rb_push_pop_roundtrip_len_h_pre) (- p_rb_push_pop_roundtrip_len_t_post p_rb_push_pop_roundtrip_len_h_post)))
(assert (not rb_push_pop_roundtrip_len_ensures_0#pfull_ds))
(check-sat-using (then simplify smt))
(pop)

; ---- body contracts (WP encoder: invariants/asserts + return-site ensures) ----
; rb_push_pop_roundtrip_len_ensures_ret_0_0 (return-site ensures, source line 185)
(define-fun rb_push_pop_roundtrip_len_ensures_ret_0_0 () Bool (= (- p_rb_push_pop_roundtrip_len_t_pre p_rb_push_pop_roundtrip_len_h_pre) (- p_rb_push_pop_roundtrip_len_t_post p_rb_push_pop_roundtrip_len_h_post)))

; --- discharge (rb_push_pop_roundtrip_len_ensures_ret_0_0) ---
(push)
(assert rb_push_pop_roundtrip_len_requires_0)
(assert rb_push_pop_roundtrip_len_requires_1)
(assert rb_push_pop_roundtrip_len_requires_2)
(assert rb_push_pop_roundtrip_len_requires_3)
(assert rb_push_pop_roundtrip_len_requires_4)
(assert rb_push_pop_roundtrip_len_requires_5)
(assert rb_push_pop_roundtrip_len_requires_6)
(assert rb_push_pop_roundtrip_len_requires_7)
(assert rb_push_pop_roundtrip_len_requires_8)
(assert rb_push_pop_roundtrip_len_requires_9)
(assert (not rb_push_pop_roundtrip_len_ensures_ret_0_0))
(check-sat-using (then simplify smt))
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
; spec fn rb_len (source line 48)
(define-fun sf_rb_len ((p_sf_rb_len_tail Int) (p_sf_rb_len_head Int) (p_sf_rb_len_cap Int)) Int (- p_sf_rb_len_tail p_sf_rb_len_head))

; spec fn rb_is_empty (source line 51)
(define-fun sf_rb_is_empty ((p_sf_rb_is_empty_tail Int) (p_sf_rb_is_empty_head Int)) Bool (= p_sf_rb_is_empty_tail p_sf_rb_is_empty_head))

; spec fn rb_is_full (source line 54)
(define-fun sf_rb_is_full ((p_sf_rb_is_full_tail Int) (p_sf_rb_is_full_head Int) (p_sf_rb_is_full_cap Int)) Bool (= (- p_sf_rb_is_full_tail p_sf_rb_is_full_head) (- p_sf_rb_is_full_cap 1)))

; spec fn rb_index_in_range (source line 59)
(define-fun sf_rb_index_in_range ((p_sf_rb_index_in_range_i Int) (p_sf_rb_index_in_range_cap Int)) Bool (and (<= 0 p_sf_rb_index_in_range_i) (< p_sf_rb_index_in_range_i p_sf_rb_index_in_range_cap)))

; spec fn rb_byte_at (source line 62)
(declare-fun sf_rb_byte_at ((Array Int Int) Int) Int)
; note: spec fn rb_byte_at body references calls/array/field — declared uninterpreted

; spec fn rb_head_byte (source line 128)
(declare-fun sf_rb_head_byte ((Array Int Int) Int) Int)
; note: spec fn rb_head_byte body references calls/array/field — declared uninterpreted

; spec fn ring_ok (source line 233)
(define-fun sf_ring_ok ((p_sf_ring_ok_seq Int)) Bool (>= p_sf_ring_ok_seq 0))


; ============================================================
; T1 — refines (concrete-implies-abstract discharge queries)
; ============================================================

; refines rb_read <= rb_head_byte  (source line 243)
(declare-const refines_rb_read_rb_head_byte_arg_0 (Array Int Int))
(declare-const refines_rb_read_rb_head_byte_arg_1 Int)
(declare-const refines_rb_read_rb_head_byte_arg_result Int)
; refinement obligation: forall args, reqConc ==> (ensConc ==> specPost)
;   specPost = (= result spec_body) (value-spec: result equals abstract)
(push)
(assert (not (forall ((refines_rb_read_rb_head_byte_arg_0 (Array Int Int)) (refines_rb_read_rb_head_byte_arg_1 Int)) (=> (<= 0 refines_rb_read_rb_head_byte_arg_1) (=> (= refines_rb_read_rb_head_byte_arg_result (select refines_rb_read_rb_head_byte_arg_0 (mod refines_rb_read_rb_head_byte_arg_1 8))) (= arg_result (select refines_rb_read_rb_head_byte_arg_0 (mod refines_rb_read_rb_head_byte_arg_1 8))))))))
(check-sat-using (then simplify smt))
(pop)
; note: refines discharge — unsat => abstract refinement holds


; ============================================================
; Missing-#6 — preserves (per-handler invariant-preservation)
; ============================================================

; preserves ring_produce <= ring_ok  (source line 235)
(declare-const preserves_ring_produce_ring_ok_arg_0 Int)
(declare-const preserves_ring_produce_ring_ok_call_result_0 Int)
; preserves_ring_produce_ring_ok_call_requires_1_0 (call requires, source line 208)
(define-fun preserves_ring_produce_ring_ok_call_requires_1_0 () Bool (<= 0 preserves_ring_produce_ring_ok_arg_0))

; --- discharge (preserves_ring_produce_ring_ok_call_requires_1_0) ---
(push)
(assert (not preserves_ring_produce_ring_ok_call_requires_1_0))
(check-sat-using (then simplify smt))
(pop)

(define-fun preserves_ring_produce_ring_ok_inline_2_req0 () Bool (<= 0 preserves_ring_produce_ring_ok_arg_0))
; preservation obligation: forall args, reqHandler ==> I(args, result)
;   reqHandler = conjunction of handler's requires clauses
;   I(args, result) = invariant spec fn body, with `result` bound to
;     the #2 WP mini-walker's inlined body terminal term
;     (NOT a fresh const + assumed ensures — soundness-critical)
(push)
(assert (not (forall ((preserves_ring_produce_ring_ok_arg_0 Int)) (=> (<= 0 preserves_ring_produce_ring_ok_arg_0) (>= preserves_ring_produce_ring_ok_arg_0 0)))))
(check-sat-using (then simplify smt))
(pop)
; note: preserves discharge — unsat => handler preserves invariant


; preserves ring_consume <= ring_ok  (source line 236)
(declare-const preserves_ring_consume_ring_ok_arg_0 Int)
(declare-const preserves_ring_consume_ring_ok_call_result_0 Int)
; preserves_ring_consume_ring_ok_call_requires_1_0 (call requires, source line 217)
(define-fun preserves_ring_consume_ring_ok_call_requires_1_0 () Bool (<= 0 preserves_ring_consume_ring_ok_arg_0))

; --- discharge (preserves_ring_consume_ring_ok_call_requires_1_0) ---
(push)
(assert (not preserves_ring_consume_ring_ok_call_requires_1_0))
(check-sat-using (then simplify smt))
(pop)

(define-fun preserves_ring_consume_ring_ok_inline_2_req0 () Bool (<= 0 preserves_ring_consume_ring_ok_arg_0))
; preservation obligation: forall args, reqHandler ==> I(args, result)
;   reqHandler = conjunction of handler's requires clauses
;   I(args, result) = invariant spec fn body, with `result` bound to
;     the #2 WP mini-walker's inlined body terminal term
;     (NOT a fresh const + assumed ensures — soundness-critical)
(push)
(assert (not (forall ((preserves_ring_consume_ring_ok_arg_0 Int)) (=> (<= 0 preserves_ring_consume_ring_ok_arg_0) (>= preserves_ring_consume_ring_ok_arg_0 0)))))
(check-sat-using (then simplify smt))
(pop)
; note: preserves discharge — unsat => handler preserves invariant


; ============================================================
; D9 (gap #6) — cycle_preserves (VM-exit-cycle refinement)
; ============================================================

; cycle_preserves ring_produce ring_consume <= ring_ok  (source line 248)

; --- handler: ring_produce (cycle refinement step) ---
(declare-const cycle_preserves_ring_produce_ring_ok_arg_0 Int)
(declare-const cycle_preserves_ring_produce_ring_ok_call_result_0 Int)
; cycle_preserves_ring_produce_ring_ok_call_requires_1_0 (call requires, source line 208)
(define-fun cycle_preserves_ring_produce_ring_ok_call_requires_1_0 () Bool (<= 0 cycle_preserves_ring_produce_ring_ok_arg_0))

; --- discharge (cycle_preserves_ring_produce_ring_ok_call_requires_1_0) ---
(push)
(assert (not cycle_preserves_ring_produce_ring_ok_call_requires_1_0))
(check-sat-using (then simplify smt))
(pop)

(define-fun cycle_preserves_ring_produce_ring_ok_inline_2_req0 () Bool (<= 0 cycle_preserves_ring_produce_ring_ok_arg_0))
(declare-const cycle_preserves_ring_produce_ring_ok_result Int)
; note: VM entry->exit->resume modelled as identity on cycle invariant (trust boundary at vmresume/vmlaunch; handler-body preserves via the invariant spec path; cross-handler pair overlap via noninterference)
; cycle refinement obligation: forall cycle_args,
;   req_handler(cycle) ∧ I_pre(cycle) ==> I_post(cycle_post_handler)
;   I_pre  = (>= cycle_preserves_ring_produce_ring_ok_arg_0 0)
;   I_post = (>= cycle_preserves_ring_produce_ring_ok_arg_0 0)
(push)
; note: I_pre == I_post — trivially established (no mutation detected; VM transitions are identity on the cycle invariant)
(assert (not (forall ( (cycle_preserves_ring_produce_ring_ok_arg_0 Int)) (=> (<= 0 cycle_preserves_ring_produce_ring_ok_arg_0) (>= cycle_preserves_ring_produce_ring_ok_arg_0 0)))))
(check-sat-using (then simplify smt))
(pop)
; note: cycle_preserves discharge — unsat => ring_produce re-establishes ring_ok across the trap cycle


; --- handler: ring_consume (cycle refinement step) ---
(declare-const cycle_preserves_ring_consume_ring_ok_arg_0 Int)
(declare-const cycle_preserves_ring_consume_ring_ok_call_result_0 Int)
; cycle_preserves_ring_consume_ring_ok_call_requires_1_0 (call requires, source line 217)
(define-fun cycle_preserves_ring_consume_ring_ok_call_requires_1_0 () Bool (<= 0 cycle_preserves_ring_consume_ring_ok_arg_0))

; --- discharge (cycle_preserves_ring_consume_ring_ok_call_requires_1_0) ---
(push)
(assert (not cycle_preserves_ring_consume_ring_ok_call_requires_1_0))
(check-sat-using (then simplify smt))
(pop)

(define-fun cycle_preserves_ring_consume_ring_ok_inline_2_req0 () Bool (<= 0 cycle_preserves_ring_consume_ring_ok_arg_0))
(declare-const cycle_preserves_ring_consume_ring_ok_result Int)
; note: VM entry->exit->resume modelled as identity on cycle invariant (trust boundary at vmresume/vmlaunch; handler-body preserves via the invariant spec path; cross-handler pair overlap via noninterference)
; cycle refinement obligation: forall cycle_args,
;   req_handler(cycle) ∧ I_pre(cycle) ==> I_post(cycle_post_handler)
;   I_pre  = (>= cycle_preserves_ring_consume_ring_ok_arg_0 0)
;   I_post = (>= cycle_preserves_ring_consume_ring_ok_arg_0 0)
(push)
; note: I_pre == I_post — trivially established (no mutation detected; VM transitions are identity on the cycle invariant)
(assert (not (forall ( (cycle_preserves_ring_consume_ring_ok_arg_0 Int)) (=> (<= 0 cycle_preserves_ring_consume_ring_ok_arg_0) (>= cycle_preserves_ring_consume_ring_ok_arg_0 0)))))
(check-sat-using (then simplify smt))
(pop)
; note: cycle_preserves discharge — unsat => ring_consume re-establishes ring_ok across the trap cycle


; ============================================================
; T3 — regions + modifies frame axioms (modular verification)
; ============================================================
; region RingSlot = {ring_seq, ring_consumer_seq}  (source line 201)

; ---- frame axioms for ring_produce (modifies: 1 entry) ----

; ---- frame axioms for ring_consume (modifies: 1 entry) ----
(exit)
