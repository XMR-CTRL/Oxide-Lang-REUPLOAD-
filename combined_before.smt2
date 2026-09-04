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
; function vmcs_field_width
; ============================================================
(declare-const p_vmcs_field_width_enc Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function vmcs_field_is_wide
; ============================================================
(declare-const p_vmcs_field_is_wide_enc Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function vmx_adjust_ctl
; ============================================================
(declare-const p_vmx_adjust_ctl_msr Int)
(declare-const p_vmx_adjust_ctl_want Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function _sdm_appendix_b_cross_refs
; ============================================================

; ---- body contracts (invariants/asserts) ----
; _sdm_appendix_b_cross_refs_assert_0 (source line 281)
(define-fun _sdm_appendix_b_cross_refs_assert_0 () Bool (= 26 26))

; --- discharge (_sdm_appendix_b_cross_refs_assert_0) ---
(push)
(assert (not _sdm_appendix_b_cross_refs_assert_0))
(check-sat)
(pop)

; _sdm_appendix_b_cross_refs_assert_1 (source line 282)
(define-fun _sdm_appendix_b_cross_refs_assert_1 () Bool (= 10240 10240))

; --- discharge (_sdm_appendix_b_cross_refs_assert_1) ---
(push)
(assert (not _sdm_appendix_b_cross_refs_assert_1))
(check-sat)
(pop)

; _sdm_appendix_b_cross_refs_assert_2 (source line 283)
(define-fun _sdm_appendix_b_cross_refs_assert_2 () Bool (= 16384 16384))

; --- discharge (_sdm_appendix_b_cross_refs_assert_2) ---
(push)
(assert (not _sdm_appendix_b_cross_refs_assert_2))
(check-sat)
(pop)

; _sdm_appendix_b_cross_refs_assert_3 (source line 284)
(define-fun _sdm_appendix_b_cross_refs_assert_3 () Bool (= 16386 16386))

; --- discharge (_sdm_appendix_b_cross_refs_assert_3) ---
(push)
(assert (not _sdm_appendix_b_cross_refs_assert_3))
(check-sat)
(pop)

; _sdm_appendix_b_cross_refs_assert_4 (source line 285)
(define-fun _sdm_appendix_b_cross_refs_assert_4 () Bool (= 16396 16396))

; --- discharge (_sdm_appendix_b_cross_refs_assert_4) ---
(push)
(assert (not _sdm_appendix_b_cross_refs_assert_4))
(check-sat)
(pop)

; _sdm_appendix_b_cross_refs_assert_5 (source line 286)
(define-fun _sdm_appendix_b_cross_refs_assert_5 () Bool (= 16402 16402))

; --- discharge (_sdm_appendix_b_cross_refs_assert_5) ---
(push)
(assert (not _sdm_appendix_b_cross_refs_assert_5))
(check-sat)
(pop)

; _sdm_appendix_b_cross_refs_assert_6 (source line 287)
(define-fun _sdm_appendix_b_cross_refs_assert_6 () Bool (= 17410 17410))

; --- discharge (_sdm_appendix_b_cross_refs_assert_6) ---
(push)
(assert (not _sdm_appendix_b_cross_refs_assert_6))
(check-sat)
(pop)

; _sdm_appendix_b_cross_refs_assert_7 (source line 288)
(define-fun _sdm_appendix_b_cross_refs_assert_7 () Bool (= 26624 26624))

; --- discharge (_sdm_appendix_b_cross_refs_assert_7) ---
(push)
(assert (not _sdm_appendix_b_cross_refs_assert_7))
(check-sat)
(pop)

; _sdm_appendix_b_cross_refs_assert_8 (source line 289)
(define-fun _sdm_appendix_b_cross_refs_assert_8 () Bool (= 26626 26626))

; --- discharge (_sdm_appendix_b_cross_refs_assert_8) ---
(push)
(assert (not _sdm_appendix_b_cross_refs_assert_8))
(check-sat)
(pop)

; _sdm_appendix_b_cross_refs_assert_9 (source line 290)
(define-fun _sdm_appendix_b_cross_refs_assert_9 () Bool (= 26628 26628))

; --- discharge (_sdm_appendix_b_cross_refs_assert_9) ---
(push)
(assert (not _sdm_appendix_b_cross_refs_assert_9))
(check-sat)
(pop)

; _sdm_appendix_b_cross_refs_assert_10 (source line 291)
(define-fun _sdm_appendix_b_cross_refs_assert_10 () Bool (= 26656 26656))

; --- discharge (_sdm_appendix_b_cross_refs_assert_10) ---
(push)
(assert (not _sdm_appendix_b_cross_refs_assert_10))
(check-sat)
(pop)

; _sdm_appendix_b_cross_refs_assert_11 (source line 292)
(define-fun _sdm_appendix_b_cross_refs_assert_11 () Bool (= 27648 27648))

; --- discharge (_sdm_appendix_b_cross_refs_assert_11) ---
(push)
(assert (not _sdm_appendix_b_cross_refs_assert_11))
(check-sat)
(pop)

; _sdm_appendix_b_cross_refs_assert_12 (source line 293)
(define-fun _sdm_appendix_b_cross_refs_assert_12 () Bool (= 27650 27650))

; --- discharge (_sdm_appendix_b_cross_refs_assert_12) ---
(push)
(assert (not _sdm_appendix_b_cross_refs_assert_12))
(check-sat)
(pop)

; _sdm_appendix_b_cross_refs_assert_13 (source line 294)
(define-fun _sdm_appendix_b_cross_refs_assert_13 () Bool (= 27652 27652))

; --- discharge (_sdm_appendix_b_cross_refs_assert_13) ---
(push)
(assert (not _sdm_appendix_b_cross_refs_assert_13))
(check-sat)
(pop)


; ============================================================
; function vmx_instruct_error
; ============================================================

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function vmclear
; ============================================================
(declare-const p_vmclear_region_phys Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function vmptrld
; ============================================================
(declare-const p_vmptrld_region_phys Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function vmwrite
; ============================================================
(declare-const p_vmwrite_field Int)
(declare-const p_vmwrite_value Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function vmwrite32
; ============================================================
(declare-const p_vmwrite32_field Int)
(declare-const p_vmwrite32_value Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function vmread
; ============================================================
(declare-const p_vmread_field Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function vmlaunch
; ============================================================

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function vmresume
; ============================================================

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function vmxoff
; ============================================================

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function vmcall
; ============================================================

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function vmwrite16
; ============================================================
(declare-const p_vmwrite16_field Int)
(declare-const p_vmwrite16_value Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function read_cr3_phys
; ============================================================

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function ept_store_entry
; ============================================================
(declare-const p_ept_store_entry_table Int)
(declare-const p_ept_store_entry_idx Int)
(declare-const p_ept_store_entry_val Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function ept_entry
; ============================================================
(declare-const p_ept_entry_next_table_phys Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function ept_large_entry
; ============================================================
(declare-const p_ept_large_entry_page_phys Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function setup_ept
; ============================================================

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function launch_guest
; ============================================================

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function guest_payload
; ============================================================

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function advance_guest_rip
; ============================================================

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function service_vmcall
; ============================================================
(declare-const p_service_vmcall_g Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function store_gpr
; ============================================================
(declare-const p_store_gpr_g Int)
(declare-const p_store_gpr_slot Int)
(declare-const p_store_gpr_val Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function load_gpr
; ============================================================
(declare-const p_load_gpr_g Int)
(declare-const p_load_gpr_slot Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function emulate_cpuid
; ============================================================
(declare-const p_emulate_cpuid_g Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function emulate_rdmsr
; ============================================================
(declare-const p_emulate_rdmsr_g Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function emulate_wrmsr
; ============================================================
(declare-const p_emulate_wrmsr_g Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function vmexit_c_handler
; ============================================================
(declare-const p_vmexit_c_handler_g Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function vmexit_done
; ============================================================

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function ox_com1_panic_put
; ============================================================
(declare-const p_ox_com1_panic_put_c Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function ox_bounds_fail
; ============================================================
(declare-const p_ox_bounds_fail_idx Int)
(declare-const p_ox_bounds_fail_bound Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function rdmsr
; ============================================================
(declare-const p_rdmsr_msr Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function wrmsr
; ============================================================
(declare-const p_wrmsr_msr Int)
(declare-const p_wrmsr_val Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function read_cr0
; ============================================================

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function write_cr0
; ============================================================
(declare-const p_write_cr0_v Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function read_cr4
; ============================================================

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function write_cr4
; ============================================================
(declare-const p_write_cr4_v Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function write_cr3
; ============================================================
(declare-const p_write_cr3_v Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function outb
; ============================================================
(declare-const p_outb_port Int)
(declare-const p_outb_val Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function inb
; ============================================================
(declare-const p_inb_port Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function halt_forever
; ============================================================

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function serial_init
; ============================================================

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function serial_can_send
; ============================================================

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function serial_putc
; ============================================================
(declare-const p_serial_putc_c Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function serial_put_hex_digit
; ============================================================
(declare-const p_serial_put_hex_digit_n Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function serial_put_hex
; ============================================================
(declare-const p_serial_put_hex_v Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function serial_put_dec
; ============================================================
(declare-const p_serial_put_dec_v Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function serial_puts
; ============================================================
(declare-const p_serial_puts_s Int)

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function has_vmx
; ============================================================

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function vmx_on
; ============================================================

; ---- body contracts (invariants/asserts) ----

; ============================================================
; function oxide_long_mode_entry
; ============================================================

; ---- body contracts (invariants/asserts) ----

(exit)
