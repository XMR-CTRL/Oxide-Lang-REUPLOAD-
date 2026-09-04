; oxide generated ir
target triple = "x86_64-elf"
declare i32 @ox_puts(i8*)
declare i32 @ox_puti(i64)
declare i32 @ox_putf(double)
declare i32 @ox_newline()
declare i32 @ox_putc(i64)
declare i64 @ox_abs_i64(i64)
declare double @llvm.fabs.f64(double)
declare double @ox_sqrt(double)
declare i64 @ox_imin(i64, i64)
declare i64 @ox_imax(i64, i64)
declare double @ox_fmin2(double, double)
declare double @ox_fmax2(double, double)
declare i8* @ox_itos(i64)
declare i64 @ox_stoi(i8*)
declare double @ox_stod(i8*)
declare i64 @ox_strlen(i8*)
declare i64 @ox_strcmp(i8*, i8*)
declare i8* @ox_substr(i8*, i64, i64)
declare i64 @ox_strchr(i8*, i64)
declare i8* @ox_char_str(i64)
declare i8* @ox_ftos(double)
declare i8* @ox_sb_new()
declare void @ox_sb_puts(i8*, i8*)
declare i8* @ox_sb_finish(i8*)
declare i8* @ox_read_line()
declare i8* @ox_read_file(i8*)
declare i64 @ox_file_open(i8*, i8*)
declare i64 @ox_file_close(i64)
declare i8* @ox_file_read(i64)
declare i64 @ox_file_write(i64, i8*)
declare i1 @ox_file_exists(i8*)

; extern functions
declare i64 @ox_entry_stack_top_addr()
declare i64 @ox_guest_rip()
declare i64 @ox_host_rip()
declare i64 @ox_host_stack_top_addr()

; top-level globals
@AR_CODE64_EXEC_READ_DPL0 = private constant i64 41115
@AR_CODE64_RO_DPL0 = private constant i64 32923
@AR_DATA_RW_DPL0 = private constant i64 49299
@AR_TR_BUSY_64 = private constant i64 139
@AR_UNUSABLE = private constant i64 65536
@COM1 = private constant i64 1016
@CPU_BASED2_ENABLE_EPT = private constant i64 2
@CPU_BASED2_ENABLE_INVPCID = private constant i64 4096
@CPU_BASED2_ENABLE_RDTSCP = private constant i64 8
@CPU_BASED2_ENABLE_VIRT_EXCEPTIONS = private constant i64 16
@CPU_BASED2_ENABLE_VPID = private constant i64 1
@CPU_BASED2_UNRESTRICTED_GUEST = private constant i64 128
@CPU_BASED_ACTIVATE_SECONDARY = private constant i64 2147483648
@CPU_BASED_HLT_EXITING = private constant i64 128
@CPU_BASED_INVLPG_EXITING = private constant i64 512
@CPU_BASED_MOV_DR_EXITING = private constant i64 8388608
@CPU_BASED_UNCONDITIONAL_IO_EXITING = private constant i64 16777216
@CPU_BASED_USE_IO_BITMAPS = private constant i64 33554432
@CPU_BASED_USE_MSR_BITMAPS = private constant i64 268435456
@EPT_ENTRY_EXECUTE = private constant i64 4
@EPT_ENTRY_LARGE_PAGE = private constant i64 128
@EPT_ENTRY_PRESENT = private constant i64 1
@EPT_ENTRY_WRITE = private constant i64 2
@EPT_MEM_TYPE_WB = private constant i64 6
@EPT_PAGE_WALK_4 = private constant i64 3
@EXIT_REASON_APIC_ACCESS = private constant i64 44
@EXIT_REASON_CPUID = private constant i64 10
@EXIT_REASON_CR_ACCESS = private constant i64 28
@EXIT_REASON_DR_ACCESS = private constant i64 29
@EXIT_REASON_EPT_MISCONFIG = private constant i64 49
@EXIT_REASON_EPT_VIOLATION = private constant i64 48
@EXIT_REASON_EXCEPTION_NMI = private constant i64 0
@EXIT_REASON_EXTERNAL_INTERRUPT = private constant i64 1
@EXIT_REASON_GDTR_OR_IDTR_ACCESS = private constant i64 46
@EXIT_REASON_HLT = private constant i64 12
@EXIT_REASON_INIT_SIGNAL = private constant i64 3
@EXIT_REASON_INVALID_GUEST_STATE = private constant i64 33
@EXIT_REASON_INVD = private constant i64 13
@EXIT_REASON_INVEPT = private constant i64 50
@EXIT_REASON_INVLPG = private constant i64 14
@EXIT_REASON_INVVPID = private constant i64 53
@EXIT_REASON_IO_INSTRUCTION = private constant i64 30
@EXIT_REASON_LDTR_OR_TR_ACCESS = private constant i64 47
@EXIT_REASON_MACHINE_CHECK = private constant i64 41
@EXIT_REASON_MONITOR = private constant i64 39
@EXIT_REASON_MSR_LOADING = private constant i64 34
@EXIT_REASON_MWAIT = private constant i64 36
@EXIT_REASON_PAUSE = private constant i64 40
@EXIT_REASON_RDMSR = private constant i64 31
@EXIT_REASON_RDPMC = private constant i64 15
@EXIT_REASON_RDTSC = private constant i64 16
@EXIT_REASON_TASK_SWITCH = private constant i64 9
@EXIT_REASON_TRIPLE_FAULT = private constant i64 2
@EXIT_REASON_VMCALL = private constant i64 18
@EXIT_REASON_VMCLEAR = private constant i64 19
@EXIT_REASON_VMLAUNCH = private constant i64 20
@EXIT_REASON_VMPTRLD = private constant i64 21
@EXIT_REASON_VMPTRST = private constant i64 22
@EXIT_REASON_VMREAD = private constant i64 23
@EXIT_REASON_VMRESUME = private constant i64 24
@EXIT_REASON_VMWRITE = private constant i64 25
@EXIT_REASON_VMXOFF = private constant i64 26
@EXIT_REASON_VMXON = private constant i64 27
@EXIT_REASON_VMX_PREEMPT_TIMER_EXPIRED = private constant i64 52
@EXIT_REASON_WBINVD = private constant i64 54
@EXIT_REASON_WRMSR = private constant i64 32
@EXIT_REASON_XSETBV = private constant i64 55
@MSG_BOOT = private constant i8* getelementptr ([36 x i8], [36 x i8]* @str9, i32 0, i32 0)
@MSR_IA32_VMX_BASIC = private constant i64 1152
@MSR_IA32_VMX_ENTRY_CTLS = private constant i64 1156
@MSR_IA32_VMX_EXIT_CTLS = private constant i64 1155
@MSR_IA32_VMX_PINBASED_CTLS = private constant i64 1153
@MSR_IA32_VMX_PROCBASED2_CTLS = private constant i64 1169
@MSR_IA32_VMX_PROCBASED_CTLS = private constant i64 1154
@MSR_IA32_VMX_TRUE_ENTRY = private constant i64 1168
@MSR_IA32_VMX_TRUE_EXIT = private constant i64 1167
@MSR_IA32_VMX_TRUE_PINBASED = private constant i64 1165
@MSR_IA32_VMX_TRUE_PROCBASED = private constant i64 1166
@PIN_EXTERNAL_INTERRUPT_EXITING = private constant i64 1
@PIN_NMI_EXITING = private constant i64 8
@PIN_POSTED_INTERRUPT = private constant i64 128
@PIN_PREEMPT_TIMER = private constant i64 64
@PIN_VIRTUAL_NMI = private constant i64 32
@VMCS_ADDR_IO_BITMAP_A = private constant i64 8192
@VMCS_ADDR_IO_BITMAP_B = private constant i64 8194
@VMCS_ADDR_MSR_BITMAP = private constant i64 8196
@VMCS_CR0_GUEST_HOST_MASK = private constant i64 24576
@VMCS_CR0_READ_SHADOW = private constant i64 24580
@VMCS_CR4_GUEST_HOST_MASK = private constant i64 24578
@VMCS_CR4_READ_SHADOW = private constant i64 24582
@VMCS_ENTRY_CTLS = private constant i64 16402
@VMCS_ENTRY_EXCEPTION_ERRORCODE = private constant i64 16408
@VMCS_ENTRY_INSTRUCTION_LENGTH = private constant i64 16410
@VMCS_ENTRY_INTERRUPTION_INFO = private constant i64 16406
@VMCS_ENTRY_MSR_LOAD_COUNT = private constant i64 16404
@VMCS_EPTP_LIST_ADDR = private constant i64 36
@VMCS_EPT_POINTER = private constant i64 26
@VMCS_EXC_BITMAP = private constant i64 16388
@VMCS_EXECUTIVE_VMCS_PTR = private constant i64 8204
@VMCS_EXIT_CTLS = private constant i64 16396
@VMCS_EXIT_MSR_LOAD_COUNT = private constant i64 16400
@VMCS_EXIT_MSR_STORE_COUNT = private constant i64 16398
@VMCS_EXIT_QUALIFICATION = private constant i64 25600
@VMCS_EXIT_REASON = private constant i64 17410
@VMCS_GUEST_CR0 = private constant i64 26624
@VMCS_GUEST_CR3 = private constant i64 26626
@VMCS_GUEST_CR4 = private constant i64 26628
@VMCS_GUEST_CS_ACCESS_RIGHTS = private constant i64 18454
@VMCS_GUEST_CS_BASE = private constant i64 26632
@VMCS_GUEST_CS_LIMIT = private constant i64 18434
@VMCS_GUEST_CS_SEL = private constant i64 2050
@VMCS_GUEST_DR7 = private constant i64 26650
@VMCS_GUEST_DS_ACCESS_RIGHTS = private constant i64 18458
@VMCS_GUEST_DS_BASE = private constant i64 26636
@VMCS_GUEST_DS_LIMIT = private constant i64 18438
@VMCS_GUEST_DS_SEL = private constant i64 2054
@VMCS_GUEST_ES_ACCESS_RIGHTS = private constant i64 18452
@VMCS_GUEST_ES_BASE = private constant i64 26630
@VMCS_GUEST_ES_LIMIT = private constant i64 18432
@VMCS_GUEST_ES_SEL = private constant i64 2048
@VMCS_GUEST_FS_ACCESS_RIGHTS = private constant i64 18460
@VMCS_GUEST_FS_BASE = private constant i64 26638
@VMCS_GUEST_FS_LIMIT = private constant i64 18440
@VMCS_GUEST_FS_SEL = private constant i64 2056
@VMCS_GUEST_GDTR_BASE = private constant i64 26646
@VMCS_GUEST_GS_ACCESS_RIGHTS = private constant i64 18462
@VMCS_GUEST_GS_BASE = private constant i64 26640
@VMCS_GUEST_GS_LIMIT = private constant i64 18442
@VMCS_GUEST_GS_SEL = private constant i64 2058
@VMCS_GUEST_IA32_EFER = private constant i64 10246
@VMCS_GUEST_IDTR_BASE = private constant i64 26648
@VMCS_GUEST_INTERRUPT_STATUS = private constant i64 18468
@VMCS_GUEST_LDTR_ACCESS_RIGHTS = private constant i64 18464
@VMCS_GUEST_LDTR_BASE = private constant i64 26642
@VMCS_GUEST_LDTR_LIMIT = private constant i64 18444
@VMCS_GUEST_LDTR_SEL = private constant i64 2060
@VMCS_GUEST_PDPTE_BASE0 = private constant i64 10250
@VMCS_GUEST_PDPTE_BASE1 = private constant i64 10252
@VMCS_GUEST_PDPTE_BASE2 = private constant i64 10254
@VMCS_GUEST_PDPTE_BASE3 = private constant i64 10256
@VMCS_GUEST_PHYSICAL_ADDR = private constant i64 9216
@VMCS_GUEST_RFLAGS = private constant i64 26656
@VMCS_GUEST_RIP = private constant i64 26654
@VMCS_GUEST_RSP = private constant i64 26652
@VMCS_GUEST_SS_ACCESS_RIGHTS = private constant i64 18456
@VMCS_GUEST_SS_BASE = private constant i64 26634
@VMCS_GUEST_SS_LIMIT = private constant i64 18436
@VMCS_GUEST_SS_SEL = private constant i64 2052
@VMCS_GUEST_SYSENTER_CS = private constant i64 18474
@VMCS_GUEST_SYSENTER_EIP = private constant i64 26662
@VMCS_GUEST_SYSENTER_ESP = private constant i64 26660
@VMCS_GUEST_TR_ACCESS_RIGHTS = private constant i64 18466
@VMCS_GUEST_TR_BASE = private constant i64 26644
@VMCS_GUEST_TR_LIMIT = private constant i64 18446
@VMCS_GUEST_TR_SEL = private constant i64 2062
@VMCS_GUEST_VMCS_LINK_PTR = private constant i64 10240
@VMCS_HOST_CR0 = private constant i64 27648
@VMCS_HOST_CR3 = private constant i64 27650
@VMCS_HOST_CR4 = private constant i64 27652
@VMCS_HOST_CS_SEL = private constant i64 3074
@VMCS_HOST_DS_SEL = private constant i64 3078
@VMCS_HOST_ES_SEL = private constant i64 3072
@VMCS_HOST_FS_BASE = private constant i64 27654
@VMCS_HOST_FS_SEL = private constant i64 3080
@VMCS_HOST_GDTR_BASE = private constant i64 27660
@VMCS_HOST_GS_BASE = private constant i64 27656
@VMCS_HOST_GS_SEL = private constant i64 3082
@VMCS_HOST_IDTR_BASE = private constant i64 27662
@VMCS_HOST_RIP = private constant i64 27670
@VMCS_HOST_RSP = private constant i64 27668
@VMCS_HOST_SS_SEL = private constant i64 3076
@VMCS_HOST_SYSENTER_CS = private constant i64 19456
@VMCS_HOST_SYSENTER_EIP = private constant i64 27666
@VMCS_HOST_SYSENTER_ESP = private constant i64 27664
@VMCS_HOST_TR_BASE = private constant i64 27658
@VMCS_HOST_TR_SEL = private constant i64 3084
@VMCS_INSTRUCTION_ERROR = private constant i64 17408
@VMCS_PIN_BASED = private constant i64 16384
@VMCS_POSTED_INTR_NV = private constant i64 2
@VMCS_PROC_BASED = private constant i64 16386
@VMCS_PROC_BASED2 = private constant i64 16414
@VMCS_VMEXIT_INSTRUCTION_LENGTH = private constant i64 17420
@VMCS_VMEXIT_INTERRUPTION_INFO = private constant i64 17412
@VMCS_VPID = private constant i64 0
@VMCS_VPID_PTR = private constant i64 0
@VMX_BASIC_DUAL_MONITOR_BIT = private constant i64 562949953421312
@VMX_BASIC_INS_OUTS_BIT = private constant i64 18014398509481984
@VMX_BASIC_MEM_TYPE_WB = private constant i64 6
@VMX_BASIC_TRUE_CTLS_BIT = private constant i64 36028797018963968
@VM_ENTRY_IA32E_MODE = private constant i64 512
@VM_ENTRY_LOAD_DEBUG_CONTROLS = private constant i64 4
@VM_ENTRY_LOAD_IA32_EFER = private constant i64 32768
@VM_EXIT_ACK_INTR_ON_EXIT = private constant i64 32768
@VM_EXIT_HOST_ADDR_SPACE_SIZE = private constant i64 512
@VM_EXIT_LOAD_IA32_EFER = private constant i64 2097152
@VM_EXIT_SAVE_DEBUG_CONTROLS = private constant i64 4
@VM_EXIT_SAVE_IA32_EFER = private constant i64 1048576
@ept_pd = global [4096 x i8] zeroinitializer
@ept_pdpt = global [4096 x i8] zeroinitializer
@ept_pml4 = global [4096 x i8] zeroinitializer
@host_stack = global [8192 x i8] zeroinitializer
@vmcs_region = global [4096 x i8] zeroinitializer
@vmxon_region = global [4096 x i8] zeroinitializer

@str0 = private constant [28 x i8] c"[oxide-hv] vmlaunch FAILED\0A\00"
@str1 = private constant [41 x i8] c"\0A[oxide-hv] guest HLT -> clean shutdown\0A\00"
@str2 = private constant [25 x i8] c"[oxide-hv] TRIPLE FAULT\0A\00"
@str3 = private constant [36 x i8] c"[oxide-hv] unhandled exit reason=0x\00"
@str4 = private constant [9 x i8] c" qual=0x\00"
@str5 = private constant [48 x i8] c"[oxide-hv] host shutting down after guest exit\0A\00"
@str6 = private constant [31 x i8] c"[oxide-hv] VT-x NOT available\0A\00"
@str7 = private constant [29 x i8] c"[oxide-hv] VMX root entered\0A\00"
@str8 = private constant [28 x i8] c"[oxide-hv] vm-entry failed\0A\00"
@str9 = private constant [36 x i8] c"[oxide-hv] long-mode entry reached\0A\00"

define i64 @vmcs_field_width(i64 %arg_enc) {
entry0:
  %var_enc_1 = alloca i64
  store i64 %arg_enc, i64* %var_enc_1
  %t0 = load i64, i64* %var_enc_1
  %t1 = ashr i64 %t0, 10
  %t2 = and i64 %t1, 15
  ret i64 %t2
}

define i1 @vmcs_field_is_wide(i64 %arg_enc) {
entry0:
  %var_enc_1 = alloca i64
  store i64 %arg_enc, i64* %var_enc_1
  %var_w_2 = alloca i64
  %t3 = load i64, i64* %var_enc_1
  %t4 = call i64 @vmcs_field_width(i64 %t3)
  store i64 %t4, i64* %var_w_2
  %t6 = load i64, i64* %var_w_2
  %t7 = icmp eq i64 %t6, 1
  br i1 %t7, label %sc_true5, label %sc_rhs3
sc_rhs3:
  %t8 = load i64, i64* %var_w_2
  %t9 = icmp eq i64 %t8, 3
  br label %sc_done4
sc_true5:
  br label %sc_done4
sc_done4:
  %t5 = phi i1 [ %t9, %sc_rhs3 ], [ true, %sc_true5 ]
  ret i1 %t5
}

define i64 @vmx_adjust_ctl(i64 %arg_msr, i64 %arg_want) {
entry0:
  %var_msr_1 = alloca i64
  store i64 %arg_msr, i64* %var_msr_1
  %var_want_2 = alloca i64
  store i64 %arg_want, i64* %var_want_2
  %var_v_3 = alloca i64
  %t10 = load i64, i64* %var_msr_1
  %t11 = call i64 @rdmsr(i64 %t10)
  store i64 %t11, i64* %var_v_3
  %var_allowed1_4 = alloca i64
  %t12 = load i64, i64* %var_v_3
  %t13 = and i64 %t12, 4294967295
  store i64 %t13, i64* %var_allowed1_4
  %var_allowed0_5 = alloca i64
  %t14 = load i64, i64* %var_v_3
  %t15 = ashr i64 %t14, 32
  %t16 = and i64 %t15, 4294967295
  store i64 %t16, i64* %var_allowed0_5
  %var_required1_6 = alloca i64
  %t17 = load i64, i64* %var_allowed1_4
  %t18 = load i64, i64* %var_allowed0_5
  %t19 = xor i64 %t18, -1
  %t20 = and i64 %t19, 4294967295
  %t21 = and i64 %t17, %t20
  store i64 %t21, i64* %var_required1_6
  %t22 = load i64, i64* %var_want_2
  %t23 = load i64, i64* %var_allowed1_4
  %t24 = and i64 %t22, %t23
  %t25 = load i64, i64* %var_required1_6
  %t26 = or i64 %t24, %t25
  %t27 = and i64 %t26, 4294967295
  ret i64 %t27
}

define i64 @vmx_instruct_error() {
entry0:
  %var_err_1 = alloca i64
  store i64 0, i64* %var_err_1
  %t28 = load i64, i64* %var_err_1
  %asm2 = call i64 asm sideeffect "vmread %rdx, %rax", "={rax},{rdx}"(i64 17408)
  store i64 %asm2, i64* %var_err_1
  %t29 = load i64, i64* %var_err_1
  ret i64 %t29
}

define i64 @vmclear(i64 %arg_region_phys) {
entry0:
  %var_region_phys_1 = alloca i64
  store i64 %arg_region_phys, i64* %var_region_phys_1
  %t30 = load i64, i64* %var_region_phys_1
  call void asm sideeffect "vmclear (%rax)", "{rax}"(i64 %t30)
  ret i64 0
}

define i64 @vmptrld(i64 %arg_region_phys) {
entry0:
  %var_region_phys_1 = alloca i64
  store i64 %arg_region_phys, i64* %var_region_phys_1
  %t31 = load i64, i64* %var_region_phys_1
  call void asm sideeffect "vmptrld (%rax)", "{rax}"(i64 %t31)
  ret i64 0
}

define void @vmwrite(i64 %arg_field, i64 %arg_value) {
entry0:
  %var_field_1 = alloca i64
  store i64 %arg_field, i64* %var_field_1
  %var_value_2 = alloca i64
  store i64 %arg_value, i64* %var_value_2
  %t32 = load i64, i64* %var_field_1
  %t33 = load i64, i64* %var_value_2
  call void asm sideeffect "vmwrite %rax, %rdx", "{rax},{rdx}"(i64 %t32, i64 %t33)
  ret void
}

define void @vmwrite32(i64 %arg_field, i64 %arg_value) {
entry0:
  %var_field_1 = alloca i64
  store i64 %arg_field, i64* %var_field_1
  %var_value_2 = alloca i64
  store i64 %arg_value, i64* %var_value_2
  %t34 = load i64, i64* %var_field_1
  %t35 = load i64, i64* %var_value_2
  %t36 = and i64 %t35, 4294967295
  call void @vmwrite(i64 %t34, i64 %t36)
  ret void
}

define i64 @vmread(i64 %arg_field) {
entry0:
  %var_field_1 = alloca i64
  store i64 %arg_field, i64* %var_field_1
  %var_v_2 = alloca i64
  store i64 0, i64* %var_v_2
  %t37 = load i64, i64* %var_v_2
  %t38 = load i64, i64* %var_field_1
  %asm3 = call i64 asm sideeffect "vmread %rdx, %rax", "={rax},{rdx}"(i64 %t38)
  store i64 %asm3, i64* %var_v_2
  %t39 = load i64, i64* %var_v_2
  ret i64 %t39
}

define i64 @vmlaunch() {
entry0:
  %var_rc_1 = alloca i64
  store i64 0, i64* %var_rc_1
  %t40 = load i64, i64* %var_rc_1
  %asm2 = call i64 asm sideeffect "vmlaunch", "={rax}"()
  store i64 %asm2, i64* %var_rc_1
  ret i64 1
}

define i64 @vmresume() {
entry0:
  %var_rc_1 = alloca i64
  store i64 0, i64* %var_rc_1
  %t41 = load i64, i64* %var_rc_1
  %asm2 = call i64 asm sideeffect "vmresume", "={rax}"()
  store i64 %asm2, i64* %var_rc_1
  ret i64 1
}

define i64 @vmxoff() {
entry0:
  call void asm sideeffect "vmxoff", ""()
  ret i64 0
}

define void @vmcall() {
entry0:
  call void asm sideeffect "vmcall", ""()
  ret void
}

define void @vmwrite16(i64 %arg_field, i64 %arg_value) {
entry0:
  %var_field_1 = alloca i64
  store i64 %arg_field, i64* %var_field_1
  %var_value_2 = alloca i64
  store i64 %arg_value, i64* %var_value_2
  %t42 = load i64, i64* %var_field_1
  %t43 = load i64, i64* %var_value_2
  %t44 = and i64 %t43, 65535
  call void @vmwrite(i64 %t42, i64 %t44)
  ret void
}

define i64 @read_cr3_phys() {
entry0:
  %var_v_1 = alloca i64
  store i64 0, i64* %var_v_1
  %t45 = load i64, i64* %var_v_1
  %asm2 = call i64 asm sideeffect "mov %cr3, %rax", "={rax}"()
  store i64 %asm2, i64* %var_v_1
  %t46 = load i64, i64* %var_v_1
  %t47 = and i64 %t46, 18446744073709547520
  ret i64 %t47
}

define void @ept_store_entry(i8* %arg_table, i64 %arg_idx, i64 %arg_val) {
entry0:
  %var_table_1 = alloca i8*
  store i8* %arg_table, i8** %var_table_1
  %var_idx_2 = alloca i64
  store i64 %arg_idx, i64* %var_idx_2
  %var_val_3 = alloca i64
  store i64 %arg_val, i64* %var_val_3
  %var_ep_4 = alloca i64*
  %t48 = load i8*, i8** %var_table_1
  %t49 = load i64, i64* %var_idx_2
  %t50 = shl i64 %t49, 3
  %gep5 = getelementptr inbounds i8, i8* %t48, i64 %t50
  %cast6 = bitcast i8* %gep5 to i64*
  store i64* %cast6, i64** %var_ep_4
  %t51 = load i64*, i64** %var_ep_4
  %t52 = load i64, i64* %var_val_3
  store volatile i64 %t52, i64* %t51
  ret void
}

define i64 @ept_entry(i64 %arg_next_table_phys) {
entry0:
  %var_next_table_phys_1 = alloca i64
  store i64 %arg_next_table_phys, i64* %var_next_table_phys_1
  %t53 = load i64, i64* %var_next_table_phys_1
  %t54 = and i64 %t53, 18446744073709547520
  %t55 = load i64, i64* @EPT_ENTRY_PRESENT
  %t56 = load i64, i64* @EPT_ENTRY_WRITE
  %t57 = or i64 %t55, %t56
  %t58 = load i64, i64* @EPT_ENTRY_EXECUTE
  %t59 = or i64 %t57, %t58
  %t60 = or i64 %t54, %t59
  ret i64 %t60
}

define i64 @ept_large_entry(i64 %arg_page_phys) {
entry0:
  %var_page_phys_1 = alloca i64
  store i64 %arg_page_phys, i64* %var_page_phys_1
  %t61 = load i64, i64* %var_page_phys_1
  %t62 = and i64 %t61, 18446744073707454464
  %t63 = load i64, i64* @EPT_ENTRY_PRESENT
  %t64 = load i64, i64* @EPT_ENTRY_WRITE
  %t65 = or i64 %t63, %t64
  %t66 = load i64, i64* @EPT_ENTRY_EXECUTE
  %t67 = or i64 %t65, %t66
  %t68 = load i64, i64* @EPT_ENTRY_LARGE_PAGE
  %t69 = or i64 %t67, %t68
  %t70 = or i64 %t62, %t69
  %t71 = load i64, i64* @EPT_MEM_TYPE_WB
  %t72 = shl i64 %t71, 3
  %t73 = or i64 %t70, %t72
  ret i64 %t73
}

define i64 @setup_ept() {
entry0:
  %var_pml4_phys_1 = alloca i64
  %t74 = load [4096 x i8], [4096 x i8]* @ept_pml4
  %ic2 = icmp slt i64 0, 0
  %ic3 = icmp sge i64 0, 4096
  %bad4 = or i1 %ic2, %ic3
  br i1 %bad4, label %idx_fail6, label %idx_ok5
idx_fail6:
  call void @ox_bounds_fail(i64 0, i64 4096)
  unreachable
idx_ok5:
  %ep7 = getelementptr inbounds [4096 x i8], [4096 x i8]* @ept_pml4, i64 0, i64 0
  %idx8 = load i8, i8* %ep7
  %ic9 = icmp slt i64 0, 0
  %ic10 = icmp sge i64 0, 4096
  %bad11 = or i1 %ic9, %ic10
  br i1 %bad11, label %idx_fail13, label %idx_ok12
idx_fail13:
  call void @ox_bounds_fail(i64 0, i64 4096)
  unreachable
idx_ok12:
  %ep14 = getelementptr inbounds [4096 x i8], [4096 x i8]* @ept_pml4, i64 0, i64 0
  %cast15 = ptrtoint i8* %ep14 to i64
  store i64 %cast15, i64* %var_pml4_phys_1
  %var_pdpt_phys_16 = alloca i64
  %t75 = load [4096 x i8], [4096 x i8]* @ept_pdpt
  %ic17 = icmp slt i64 0, 0
  %ic18 = icmp sge i64 0, 4096
  %bad19 = or i1 %ic17, %ic18
  br i1 %bad19, label %idx_fail21, label %idx_ok20
idx_fail21:
  call void @ox_bounds_fail(i64 0, i64 4096)
  unreachable
idx_ok20:
  %ep22 = getelementptr inbounds [4096 x i8], [4096 x i8]* @ept_pdpt, i64 0, i64 0
  %idx23 = load i8, i8* %ep22
  %ic24 = icmp slt i64 0, 0
  %ic25 = icmp sge i64 0, 4096
  %bad26 = or i1 %ic24, %ic25
  br i1 %bad26, label %idx_fail28, label %idx_ok27
idx_fail28:
  call void @ox_bounds_fail(i64 0, i64 4096)
  unreachable
idx_ok27:
  %ep29 = getelementptr inbounds [4096 x i8], [4096 x i8]* @ept_pdpt, i64 0, i64 0
  %cast30 = ptrtoint i8* %ep29 to i64
  store i64 %cast30, i64* %var_pdpt_phys_16
  %var_pd_phys_31 = alloca i64
  %t76 = load [4096 x i8], [4096 x i8]* @ept_pd
  %ic32 = icmp slt i64 0, 0
  %ic33 = icmp sge i64 0, 4096
  %bad34 = or i1 %ic32, %ic33
  br i1 %bad34, label %idx_fail36, label %idx_ok35
idx_fail36:
  call void @ox_bounds_fail(i64 0, i64 4096)
  unreachable
idx_ok35:
  %ep37 = getelementptr inbounds [4096 x i8], [4096 x i8]* @ept_pd, i64 0, i64 0
  %idx38 = load i8, i8* %ep37
  %ic39 = icmp slt i64 0, 0
  %ic40 = icmp sge i64 0, 4096
  %bad41 = or i1 %ic39, %ic40
  br i1 %bad41, label %idx_fail43, label %idx_ok42
idx_fail43:
  call void @ox_bounds_fail(i64 0, i64 4096)
  unreachable
idx_ok42:
  %ep44 = getelementptr inbounds [4096 x i8], [4096 x i8]* @ept_pd, i64 0, i64 0
  %cast45 = ptrtoint i8* %ep44 to i64
  store i64 %cast45, i64* %var_pd_phys_31
  %t77 = load [4096 x i8], [4096 x i8]* @ept_pml4
  %ic46 = icmp slt i64 0, 0
  %ic47 = icmp sge i64 0, 4096
  %bad48 = or i1 %ic46, %ic47
  br i1 %bad48, label %idx_fail50, label %idx_ok49
idx_fail50:
  call void @ox_bounds_fail(i64 0, i64 4096)
  unreachable
idx_ok49:
  %ep51 = getelementptr inbounds [4096 x i8], [4096 x i8]* @ept_pml4, i64 0, i64 0
  %idx52 = load i8, i8* %ep51
  %ic53 = icmp slt i64 0, 0
  %ic54 = icmp sge i64 0, 4096
  %bad55 = or i1 %ic53, %ic54
  br i1 %bad55, label %idx_fail57, label %idx_ok56
idx_fail57:
  call void @ox_bounds_fail(i64 0, i64 4096)
  unreachable
idx_ok56:
  %ep58 = getelementptr inbounds [4096 x i8], [4096 x i8]* @ept_pml4, i64 0, i64 0
  %t78 = load i64, i64* %var_pdpt_phys_16
  %t79 = call i64 @ept_entry(i64 %t78)
  call void @ept_store_entry(i8* %ep58, i64 0, i64 %t79)
  %t80 = load [4096 x i8], [4096 x i8]* @ept_pdpt
  %ic59 = icmp slt i64 0, 0
  %ic60 = icmp sge i64 0, 4096
  %bad61 = or i1 %ic59, %ic60
  br i1 %bad61, label %idx_fail63, label %idx_ok62
idx_fail63:
  call void @ox_bounds_fail(i64 0, i64 4096)
  unreachable
idx_ok62:
  %ep64 = getelementptr inbounds [4096 x i8], [4096 x i8]* @ept_pdpt, i64 0, i64 0
  %idx65 = load i8, i8* %ep64
  %ic66 = icmp slt i64 0, 0
  %ic67 = icmp sge i64 0, 4096
  %bad68 = or i1 %ic66, %ic67
  br i1 %bad68, label %idx_fail70, label %idx_ok69
idx_fail70:
  call void @ox_bounds_fail(i64 0, i64 4096)
  unreachable
idx_ok69:
  %ep71 = getelementptr inbounds [4096 x i8], [4096 x i8]* @ept_pdpt, i64 0, i64 0
  %t81 = load i64, i64* %var_pd_phys_31
  %t82 = call i64 @ept_entry(i64 %t81)
  call void @ept_store_entry(i8* %ep71, i64 0, i64 %t82)
  %var_i_72 = alloca i64
  store i64 0, i64* %var_i_72
  br label %while_cond73
while_cond73:
  %t83 = load i64, i64* %var_i_72
  %t84 = icmp slt i64 %t83, 512
  br i1 %t84, label %while_body74, label %while_end75
while_body74:
  %t85 = load [4096 x i8], [4096 x i8]* @ept_pd
  %ic76 = icmp slt i64 0, 0
  %ic77 = icmp sge i64 0, 4096
  %bad78 = or i1 %ic76, %ic77
  br i1 %bad78, label %idx_fail80, label %idx_ok79
idx_fail80:
  call void @ox_bounds_fail(i64 0, i64 4096)
  unreachable
idx_ok79:
  %ep81 = getelementptr inbounds [4096 x i8], [4096 x i8]* @ept_pd, i64 0, i64 0
  %idx82 = load i8, i8* %ep81
  %ic83 = icmp slt i64 0, 0
  %ic84 = icmp sge i64 0, 4096
  %bad85 = or i1 %ic83, %ic84
  br i1 %bad85, label %idx_fail87, label %idx_ok86
idx_fail87:
  call void @ox_bounds_fail(i64 0, i64 4096)
  unreachable
idx_ok86:
  %ep88 = getelementptr inbounds [4096 x i8], [4096 x i8]* @ept_pd, i64 0, i64 0
  %t86 = load i64, i64* %var_i_72
  %t87 = load i64, i64* %var_i_72
  %t88 = shl i64 %t87, 21
  %t89 = call i64 @ept_large_entry(i64 %t88)
  call void @ept_store_entry(i8* %ep88, i64 %t86, i64 %t89)
  %t90 = load i64, i64* %var_i_72
  %t91 = add i64 %t90, 1
  store i64 %t91, i64* %var_i_72
  br label %while_cond73
while_end75:
  %t92 = load i64, i64* %var_pml4_phys_1
  %t93 = load i64, i64* @EPT_PAGE_WALK_4
  %t94 = shl i64 %t93, 3
  %t95 = or i64 %t92, %t94
  %t96 = load i64, i64* @EPT_MEM_TYPE_WB
  %t97 = or i64 %t95, %t96
  ret i64 %t97
}

define i64 @launch_guest() {
entry0:
  %var_basic_1 = alloca i64
  %t98 = load i64, i64* @MSR_IA32_VMX_BASIC
  %t99 = call i64 @rdmsr(i64 %t98)
  store i64 %t99, i64* %var_basic_1
  %var_rev_2 = alloca i32
  %t100 = load i64, i64* %var_basic_1
  %t101 = and i64 %t100, 2147483647
  %cast3 = trunc i64 %t101 to i32
  store i32 %cast3, i32* %var_rev_2
  %var_vmcs_ptr_4 = alloca i8*
  %t102 = load [4096 x i8], [4096 x i8]* @vmcs_region
  %ic5 = icmp slt i64 0, 0
  %ic6 = icmp sge i64 0, 4096
  %bad7 = or i1 %ic5, %ic6
  br i1 %bad7, label %idx_fail9, label %idx_ok8
idx_fail9:
  call void @ox_bounds_fail(i64 0, i64 4096)
  unreachable
idx_ok8:
  %ep10 = getelementptr inbounds [4096 x i8], [4096 x i8]* @vmcs_region, i64 0, i64 0
  %idx11 = load i8, i8* %ep10
  %ic12 = icmp slt i64 0, 0
  %ic13 = icmp sge i64 0, 4096
  %bad14 = or i1 %ic12, %ic13
  br i1 %bad14, label %idx_fail16, label %idx_ok15
idx_fail16:
  call void @ox_bounds_fail(i64 0, i64 4096)
  unreachable
idx_ok15:
  %ep17 = getelementptr inbounds [4096 x i8], [4096 x i8]* @vmcs_region, i64 0, i64 0
  store i8* %ep17, i8** %var_vmcs_ptr_4
  %var_rev_ptr_18 = alloca i32*
  %t103 = load i8*, i8** %var_vmcs_ptr_4
  %cast19 = bitcast i8* %t103 to i32*
  store i32* %cast19, i32** %var_rev_ptr_18
  %t104 = load i32*, i32** %var_rev_ptr_18
  %t105 = load i32, i32* %var_rev_2
  store volatile i32 %t105, i32* %t104
  %var_abort_ptr_20 = alloca i32*
  %t106 = load i8*, i8** %var_vmcs_ptr_4
  %gep21 = getelementptr inbounds i8, i8* %t106, i64 4
  %cast22 = bitcast i8* %gep21 to i32*
  store i32* %cast22, i32** %var_abort_ptr_20
  %t107 = load i32*, i32** %var_abort_ptr_20
  %cast23 = trunc i64 0 to i32
  store volatile i32 %cast23, i32* %t107
  %var_vmcs_phys_24 = alloca i64
  %t108 = load i8*, i8** %var_vmcs_ptr_4
  %cast25 = ptrtoint i8* %t108 to i64
  store i64 %cast25, i64* %var_vmcs_phys_24
  %t109 = load i64, i64* %var_vmcs_phys_24
  %t110 = call i64 @vmclear(i64 %t109)
  %t111 = load i64, i64* %var_vmcs_phys_24
  %t112 = call i64 @vmptrld(i64 %t111)
  %t113 = load i64, i64* @VMCS_HOST_RIP
  %t114 = call i64 @ox_host_rip()
  call void @vmwrite(i64 %t113, i64 %t114)
  %t115 = load i64, i64* @VMCS_HOST_RSP
  %t116 = call i64 @ox_host_stack_top_addr()
  call void @vmwrite(i64 %t115, i64 %t116)
  %t117 = load i64, i64* @VMCS_HOST_CS_SEL
  call void @vmwrite16(i64 %t117, i64 8)
  %t118 = load i64, i64* @VMCS_HOST_SS_SEL
  call void @vmwrite16(i64 %t118, i64 16)
  %t119 = load i64, i64* @VMCS_HOST_DS_SEL
  call void @vmwrite16(i64 %t119, i64 16)
  %t120 = load i64, i64* @VMCS_HOST_ES_SEL
  call void @vmwrite16(i64 %t120, i64 16)
  %t121 = load i64, i64* @VMCS_HOST_FS_SEL
  call void @vmwrite16(i64 %t121, i64 0)
  %t122 = load i64, i64* @VMCS_HOST_GS_SEL
  call void @vmwrite16(i64 %t122, i64 0)
  %t123 = load i64, i64* @VMCS_HOST_TR_SEL
  call void @vmwrite16(i64 %t123, i64 24)
  %t124 = load i64, i64* @VMCS_HOST_CR0
  %t125 = call i64 @read_cr0()
  call void @vmwrite(i64 %t124, i64 %t125)
  %t126 = load i64, i64* @VMCS_HOST_CR3
  %t127 = call i64 @read_cr3_phys()
  call void @vmwrite(i64 %t126, i64 %t127)
  %t128 = load i64, i64* @VMCS_HOST_CR4
  %t129 = call i64 @read_cr4()
  call void @vmwrite(i64 %t128, i64 %t129)
  %t130 = load i64, i64* @VMCS_HOST_FS_BASE
  call void @vmwrite(i64 %t130, i64 0)
  %t131 = load i64, i64* @VMCS_HOST_GS_BASE
  %t132 = call i64 @rdmsr(i64 3221225729)
  call void @vmwrite(i64 %t131, i64 %t132)
  %t133 = load i64, i64* @VMCS_HOST_GDTR_BASE
  call void @vmwrite(i64 %t133, i64 0)
  %t134 = load i64, i64* @VMCS_HOST_IDTR_BASE
  call void @vmwrite(i64 %t134, i64 0)
  %t135 = load i64, i64* @VMCS_HOST_TR_BASE
  call void @vmwrite(i64 %t135, i64 0)
  %t136 = load i64, i64* @VMCS_HOST_SYSENTER_CS
  call void @vmwrite(i64 %t136, i64 0)
  %t137 = load i64, i64* @VMCS_HOST_SYSENTER_ESP
  call void @vmwrite(i64 %t137, i64 0)
  %t138 = load i64, i64* @VMCS_HOST_SYSENTER_EIP
  call void @vmwrite(i64 %t138, i64 0)
  %var_exit_ctl_26 = alloca i64
  %t139 = load i64, i64* @MSR_IA32_VMX_EXIT_CTLS
  %t140 = load i64, i64* @VM_EXIT_HOST_ADDR_SPACE_SIZE
  %t141 = load i64, i64* @VM_EXIT_SAVE_IA32_EFER
  %t142 = or i64 %t140, %t141
  %t143 = load i64, i64* @VM_EXIT_LOAD_IA32_EFER
  %t144 = or i64 %t142, %t143
  %t145 = call i64 @vmx_adjust_ctl(i64 %t139, i64 %t144)
  store i64 %t145, i64* %var_exit_ctl_26
  %t146 = load i64, i64* @VMCS_EXIT_CTLS
  %t147 = load i64, i64* %var_exit_ctl_26
  call void @vmwrite32(i64 %t146, i64 %t147)
  %var_entry_ctl_27 = alloca i64
  %t148 = load i64, i64* @MSR_IA32_VMX_ENTRY_CTLS
  %t149 = load i64, i64* @VM_ENTRY_IA32E_MODE
  %t150 = load i64, i64* @VM_ENTRY_LOAD_IA32_EFER
  %t151 = or i64 %t149, %t150
  %t152 = call i64 @vmx_adjust_ctl(i64 %t148, i64 %t151)
  store i64 %t152, i64* %var_entry_ctl_27
  %t153 = load i64, i64* @VMCS_ENTRY_CTLS
  %t154 = load i64, i64* %var_entry_ctl_27
  call void @vmwrite32(i64 %t153, i64 %t154)
  %var_pin_28 = alloca i64
  %t155 = load i64, i64* @MSR_IA32_VMX_PINBASED_CTLS
  %t156 = call i64 @vmx_adjust_ctl(i64 %t155, i64 0)
  store i64 %t156, i64* %var_pin_28
  %t157 = load i64, i64* @VMCS_PIN_BASED
  %t158 = load i64, i64* %var_pin_28
  call void @vmwrite32(i64 %t157, i64 %t158)
  %var_proc_29 = alloca i64
  %t159 = load i64, i64* @MSR_IA32_VMX_PROCBASED_CTLS
  %t160 = load i64, i64* @CPU_BASED_HLT_EXITING
  %t161 = load i64, i64* @CPU_BASED_ACTIVATE_SECONDARY
  %t162 = or i64 %t160, %t161
  %t163 = call i64 @vmx_adjust_ctl(i64 %t159, i64 %t162)
  store i64 %t163, i64* %var_proc_29
  %t164 = load i64, i64* @VMCS_PROC_BASED
  %t165 = load i64, i64* %var_proc_29
  call void @vmwrite32(i64 %t164, i64 %t165)
  %var_proc2_30 = alloca i64
  %t166 = load i64, i64* @MSR_IA32_VMX_PROCBASED2_CTLS
  %t167 = load i64, i64* @CPU_BASED2_ENABLE_EPT
  %t168 = call i64 @vmx_adjust_ctl(i64 %t166, i64 %t167)
  store i64 %t168, i64* %var_proc2_30
  %t169 = load i64, i64* @VMCS_PROC_BASED2
  %t170 = load i64, i64* %var_proc2_30
  call void @vmwrite32(i64 %t169, i64 %t170)
  %t171 = load i64, i64* @VMCS_EXC_BITMAP
  call void @vmwrite32(i64 %t171, i64 0)
  %var_eptp_31 = alloca i64
  %t172 = call i64 @setup_ept()
  store i64 %t172, i64* %var_eptp_31
  %t173 = load i64, i64* @VMCS_EPT_POINTER
  %t174 = load i64, i64* %var_eptp_31
  call void @vmwrite(i64 %t173, i64 %t174)
  %var_gcr0_32 = alloca i64
  store i64 2147483699, i64* %var_gcr0_32
  %t175 = load i64, i64* @VMCS_GUEST_CR0
  %t176 = load i64, i64* %var_gcr0_32
  call void @vmwrite(i64 %t175, i64 %t176)
  %t177 = load i64, i64* @VMCS_CR0_GUEST_HOST_MASK
  call void @vmwrite(i64 %t177, i64 0)
  %t178 = load i64, i64* @VMCS_CR0_READ_SHADOW
  %t179 = load i64, i64* %var_gcr0_32
  call void @vmwrite(i64 %t178, i64 %t179)
  %t180 = load i64, i64* @VMCS_GUEST_CR3
  %t181 = call i64 @read_cr3_phys()
  call void @vmwrite(i64 %t180, i64 %t181)
  %var_gcr4_33 = alloca i64
  %t182 = shl i64 1, 5
  store i64 %t182, i64* %var_gcr4_33
  %t183 = load i64, i64* @VMCS_GUEST_CR4
  %t184 = load i64, i64* %var_gcr4_33
  call void @vmwrite(i64 %t183, i64 %t184)
  %t185 = load i64, i64* @VMCS_CR4_GUEST_HOST_MASK
  call void @vmwrite(i64 %t185, i64 0)
  %t186 = load i64, i64* @VMCS_CR4_READ_SHADOW
  %t187 = load i64, i64* %var_gcr4_33
  call void @vmwrite(i64 %t186, i64 %t187)
  %t188 = load i64, i64* @VMCS_GUEST_IA32_EFER
  call void @vmwrite(i64 %t188, i64 768)
  %t189 = load i64, i64* @VMCS_GUEST_CS_SEL
  call void @vmwrite16(i64 %t189, i64 8)
  %t190 = load i64, i64* @VMCS_GUEST_SS_SEL
  call void @vmwrite16(i64 %t190, i64 16)
  %t191 = load i64, i64* @VMCS_GUEST_DS_SEL
  call void @vmwrite16(i64 %t191, i64 16)
  %t192 = load i64, i64* @VMCS_GUEST_ES_SEL
  call void @vmwrite16(i64 %t192, i64 16)
  %t193 = load i64, i64* @VMCS_GUEST_FS_SEL
  call void @vmwrite16(i64 %t193, i64 0)
  %t194 = load i64, i64* @VMCS_GUEST_GS_SEL
  call void @vmwrite16(i64 %t194, i64 0)
  %t195 = load i64, i64* @VMCS_GUEST_LDTR_SEL
  call void @vmwrite16(i64 %t195, i64 0)
  %t196 = load i64, i64* @VMCS_GUEST_TR_SEL
  call void @vmwrite16(i64 %t196, i64 24)
  %t197 = load i64, i64* @VMCS_GUEST_CS_ACCESS_RIGHTS
  %t198 = load i64, i64* @AR_CODE64_EXEC_READ_DPL0
  call void @vmwrite32(i64 %t197, i64 %t198)
  %t199 = load i64, i64* @VMCS_GUEST_SS_ACCESS_RIGHTS
  %t200 = load i64, i64* @AR_DATA_RW_DPL0
  call void @vmwrite32(i64 %t199, i64 %t200)
  %t201 = load i64, i64* @VMCS_GUEST_DS_ACCESS_RIGHTS
  %t202 = load i64, i64* @AR_DATA_RW_DPL0
  call void @vmwrite32(i64 %t201, i64 %t202)
  %t203 = load i64, i64* @VMCS_GUEST_ES_ACCESS_RIGHTS
  %t204 = load i64, i64* @AR_DATA_RW_DPL0
  call void @vmwrite32(i64 %t203, i64 %t204)
  %t205 = load i64, i64* @VMCS_GUEST_FS_ACCESS_RIGHTS
  %t206 = load i64, i64* @AR_UNUSABLE
  call void @vmwrite32(i64 %t205, i64 %t206)
  %t207 = load i64, i64* @VMCS_GUEST_GS_ACCESS_RIGHTS
  %t208 = load i64, i64* @AR_UNUSABLE
  call void @vmwrite32(i64 %t207, i64 %t208)
  %t209 = load i64, i64* @VMCS_GUEST_LDTR_ACCESS_RIGHTS
  %t210 = load i64, i64* @AR_UNUSABLE
  call void @vmwrite32(i64 %t209, i64 %t210)
  %t211 = load i64, i64* @VMCS_GUEST_TR_ACCESS_RIGHTS
  %t212 = load i64, i64* @AR_TR_BUSY_64
  call void @vmwrite32(i64 %t211, i64 %t212)
  %t213 = load i64, i64* @VMCS_GUEST_CS_LIMIT
  call void @vmwrite32(i64 %t213, i64 1048575)
  %t214 = load i64, i64* @VMCS_GUEST_SS_LIMIT
  call void @vmwrite32(i64 %t214, i64 1048575)
  %t215 = load i64, i64* @VMCS_GUEST_DS_LIMIT
  call void @vmwrite32(i64 %t215, i64 1048575)
  %t216 = load i64, i64* @VMCS_GUEST_ES_LIMIT
  call void @vmwrite32(i64 %t216, i64 1048575)
  %t217 = load i64, i64* @VMCS_GUEST_FS_LIMIT
  call void @vmwrite32(i64 %t217, i64 1048575)
  %t218 = load i64, i64* @VMCS_GUEST_GS_LIMIT
  call void @vmwrite32(i64 %t218, i64 1048575)
  %t219 = load i64, i64* @VMCS_GUEST_LDTR_LIMIT
  call void @vmwrite32(i64 %t219, i64 1048575)
  %t220 = load i64, i64* @VMCS_GUEST_TR_LIMIT
  call void @vmwrite32(i64 %t220, i64 111)
  %t221 = load i64, i64* @VMCS_GUEST_CS_BASE
  call void @vmwrite(i64 %t221, i64 0)
  %t222 = load i64, i64* @VMCS_GUEST_SS_BASE
  call void @vmwrite(i64 %t222, i64 0)
  %t223 = load i64, i64* @VMCS_GUEST_DS_BASE
  call void @vmwrite(i64 %t223, i64 0)
  %t224 = load i64, i64* @VMCS_GUEST_ES_BASE
  call void @vmwrite(i64 %t224, i64 0)
  %t225 = load i64, i64* @VMCS_GUEST_FS_BASE
  call void @vmwrite(i64 %t225, i64 0)
  %t226 = load i64, i64* @VMCS_GUEST_GS_BASE
  call void @vmwrite(i64 %t226, i64 0)
  %t227 = load i64, i64* @VMCS_GUEST_LDTR_BASE
  call void @vmwrite(i64 %t227, i64 0)
  %t228 = load i64, i64* @VMCS_GUEST_TR_BASE
  call void @vmwrite(i64 %t228, i64 0)
  %t229 = load i64, i64* @VMCS_GUEST_GDTR_BASE
  call void @vmwrite(i64 %t229, i64 0)
  %t230 = load i64, i64* @VMCS_GUEST_IDTR_BASE
  call void @vmwrite(i64 %t230, i64 0)
  %t231 = load i64, i64* @VMCS_GUEST_RIP
  %t232 = call i64 @ox_guest_rip()
  call void @vmwrite(i64 %t231, i64 %t232)
  %t233 = load i64, i64* @VMCS_GUEST_RSP
  %t234 = call i64 @ox_host_stack_top_addr()
  call void @vmwrite(i64 %t233, i64 %t234)
  %t235 = load i64, i64* @VMCS_GUEST_RFLAGS
  call void @vmwrite(i64 %t235, i64 2)
  %t236 = load i64, i64* @VMCS_GUEST_DR7
  call void @vmwrite(i64 %t236, i64 0)
  %t237 = load i64, i64* @VMCS_GUEST_SYSENTER_CS
  call void @vmwrite(i64 %t237, i64 0)
  %t238 = load i64, i64* @VMCS_GUEST_SYSENTER_ESP
  call void @vmwrite(i64 %t238, i64 0)
  %t239 = load i64, i64* @VMCS_GUEST_SYSENTER_EIP
  call void @vmwrite(i64 %t239, i64 0)
  %t240 = load i64, i64* @VMCS_GUEST_VMCS_LINK_PTR
  call void @vmwrite(i64 %t240, i64 18446744073709551615)
  %var_launched_34 = alloca i64
  %t241 = call i64 @vmlaunch()
  store i64 %t241, i64* %var_launched_34
  %t242 = load i64, i64* %var_launched_34
  %t243 = icmp ne i64 %t242, 0
  br i1 %t243, label %then35, label %else36
then35:
  %var_err_38 = alloca i64
  %t244 = call i64 @vmx_instruct_error()
  store i64 %t244, i64* %var_err_38
  %t245 = getelementptr inbounds [28 x i8], [28 x i8]* @str0, i64 0, i64 0
  call void @serial_puts(i8* %t245)
  %t246 = load i64, i64* %var_err_38
  %t247 = and i64 %t246, 255
  %t248 = or i64 %t247, 256
  ret i64 %t248
else36:
  br label %merge37
merge37:
  ret i64 0
}

define void @guest_payload() {
entry0:
  call void asm sideeffect "xor %rax, %rax\0Axor %rcx, %rcx\0Acpuid\0Amov $$0x41, %rax\0Avmcall\0Amov $$0x42, %rax\0Avmcall\0Amov $$0x43, %rax\0Avmcall\0Ahlt", ""()
  ret void
}

define void @advance_guest_rip() {
entry0:
  %var_len_1 = alloca i64
  %t249 = load i64, i64* @VMCS_VMEXIT_INSTRUCTION_LENGTH
  %t250 = call i64 @vmread(i64 %t249)
  store i64 %t250, i64* %var_len_1
  %var_rip_2 = alloca i64
  %t251 = load i64, i64* @VMCS_GUEST_RIP
  %t252 = call i64 @vmread(i64 %t251)
  store i64 %t252, i64* %var_rip_2
  %t253 = load i64, i64* @VMCS_GUEST_RIP
  %t254 = load i64, i64* %var_rip_2
  %t255 = load i64, i64* %var_len_1
  %t256 = add i64 %t254, %t255
  call void @vmwrite(i64 %t253, i64 %t256)
  ret void
}

define void @service_vmcall(i64* %arg_g) {
entry0:
  %var_g_1 = alloca i64*
  store i64* %arg_g, i64** %var_g_1
  %var_arg_2 = alloca i64*
  %t257 = load i64*, i64** %var_g_1
  %gep3 = getelementptr inbounds i64, i64* %t257, i64 0
  store i64* %gep3, i64** %var_arg_2
  %var_val_4 = alloca i64
  %t258 = load i64*, i64** %var_arg_2
  %mmio5 = load volatile i64, i64* %t258
  store i64 %mmio5, i64* %var_val_4
  %t259 = load i64, i64* %var_val_4
  %t260 = icmp ne i64 %t259, 0
  br i1 %t260, label %then6, label %else7
then6:
  %t261 = load i64, i64* %var_val_4
  %t262 = and i64 %t261, 255
  %cast9 = trunc i64 %t262 to i8
  call void @serial_putc(i8 %cast9)
  br label %merge8
else7:
  br label %merge8
merge8:
  call void @advance_guest_rip()
  ret void
}

define void @store_gpr(i64* %arg_g, i64 %arg_slot, i64 %arg_val) {
entry0:
  %var_g_1 = alloca i64*
  store i64* %arg_g, i64** %var_g_1
  %var_slot_2 = alloca i64
  store i64 %arg_slot, i64* %var_slot_2
  %var_val_3 = alloca i64
  store i64 %arg_val, i64* %var_val_3
  %var_dst_4 = alloca i64*
  %t263 = load i64*, i64** %var_g_1
  %t264 = load i64, i64* %var_slot_2
  %gep5 = getelementptr inbounds i64, i64* %t263, i64 %t264
  store i64* %gep5, i64** %var_dst_4
  %t265 = load i64*, i64** %var_dst_4
  %t266 = load i64, i64* %var_val_3
  store volatile i64 %t266, i64* %t265
  ret void
}

define i64 @load_gpr(i64* %arg_g, i64 %arg_slot) {
entry0:
  %var_g_1 = alloca i64*
  store i64* %arg_g, i64** %var_g_1
  %var_slot_2 = alloca i64
  store i64 %arg_slot, i64* %var_slot_2
  %var_src_3 = alloca i64*
  %t267 = load i64*, i64** %var_g_1
  %t268 = load i64, i64* %var_slot_2
  %gep4 = getelementptr inbounds i64, i64* %t267, i64 %t268
  store i64* %gep4, i64** %var_src_3
  %t269 = load i64*, i64** %var_src_3
  %mmio5 = load volatile i64, i64* %t269
  ret i64 %mmio5
}

define void @emulate_cpuid(i64* %arg_g) {
entry0:
  %var_g_1 = alloca i64*
  store i64* %arg_g, i64** %var_g_1
  %var_leaf_2 = alloca i64
  %t270 = load i64*, i64** %var_g_1
  %t271 = call i64 @load_gpr(i64* %t270, i64 0)
  store i64 %t271, i64* %var_leaf_2
  %var_subleaf_3 = alloca i64
  %t272 = load i64*, i64** %var_g_1
  %t273 = call i64 @load_gpr(i64* %t272, i64 2)
  store i64 %t273, i64* %var_subleaf_3
  %var_rax_4 = alloca i64
  store i64 0, i64* %var_rax_4
  %var_rbx_5 = alloca i64
  store i64 0, i64* %var_rbx_5
  %var_rcx_6 = alloca i64
  store i64 0, i64* %var_rcx_6
  %var_rdx_7 = alloca i64
  store i64 0, i64* %var_rdx_7
  %t274 = load i64, i64* %var_leaf_2
  %t275 = icmp eq i64 %t274, 0
  br i1 %t275, label %then8, label %else9
then8:
  store i64 16, i64* %var_rax_4
  store i64 1970169159, i64* %var_rbx_5
  store i64 1231384169, i64* %var_rdx_7
  store i64 1818588270, i64* %var_rcx_6
  br label %merge10
else9:
  %t276 = load i64, i64* %var_leaf_2
  %t277 = icmp eq i64 %t276, 1
  br i1 %t277, label %then11, label %else12
then11:
  %var_a_14 = alloca i64
  store i64 1, i64* %var_a_14
  %var_b_15 = alloca i64
  store i64 0, i64* %var_b_15
  %var_c_16 = alloca i64
  store i64 0, i64* %var_c_16
  %var_d_17 = alloca i64
  store i64 0, i64* %var_d_17
  %t278 = load i64, i64* %var_a_14
  %t279 = load i64, i64* %var_b_15
  %t280 = load i64, i64* %var_c_16
  %t281 = load i64, i64* %var_d_17
  %t282 = load i64, i64* %var_a_14
  %t283 = load i64, i64* %var_c_16
  %asm18 = call {i64, i64, i64, i64} asm sideeffect "cpuid", "={eax},={ebx},={ecx},={edx},0,2"(i64 %t282, i64 %t283)
  %asmout19 = extractvalue {i64, i64, i64, i64} %asm18, 0
  store i64 %asmout19, i64* %var_a_14
  %asmout20 = extractvalue {i64, i64, i64, i64} %asm18, 1
  store i64 %asmout20, i64* %var_b_15
  %asmout21 = extractvalue {i64, i64, i64, i64} %asm18, 2
  store i64 %asmout21, i64* %var_c_16
  %asmout22 = extractvalue {i64, i64, i64, i64} %asm18, 3
  store i64 %asmout22, i64* %var_d_17
  %t284 = load i64, i64* %var_c_16
  %t285 = and i64 %t284, 2147483647
  %t286 = shl i64 1, 31
  %t287 = or i64 %t285, %t286
  store i64 %t287, i64* %var_c_16
  %t288 = load i64, i64* %var_a_14
  store i64 %t288, i64* %var_rax_4
  %t289 = load i64, i64* %var_b_15
  store i64 %t289, i64* %var_rbx_5
  %t290 = load i64, i64* %var_c_16
  store i64 %t290, i64* %var_rcx_6
  %t291 = load i64, i64* %var_d_17
  store i64 %t291, i64* %var_rdx_7
  br label %merge13
else12:
  %var_a_23 = alloca i64
  %t292 = load i64, i64* %var_leaf_2
  store i64 %t292, i64* %var_a_23
  %var_b_24 = alloca i64
  store i64 0, i64* %var_b_24
  %var_c2_25 = alloca i64
  %t293 = load i64, i64* %var_subleaf_3
  store i64 %t293, i64* %var_c2_25
  %var_d_26 = alloca i64
  store i64 0, i64* %var_d_26
  %t294 = load i64, i64* %var_a_23
  %t295 = load i64, i64* %var_b_24
  %t296 = load i64, i64* %var_c2_25
  %t297 = load i64, i64* %var_d_26
  %t298 = load i64, i64* %var_a_23
  %t299 = load i64, i64* %var_c2_25
  %asm27 = call {i64, i64, i64, i64} asm sideeffect "cpuid", "={eax},={ebx},={ecx},={edx},0,2"(i64 %t298, i64 %t299)
  %asmout28 = extractvalue {i64, i64, i64, i64} %asm27, 0
  store i64 %asmout28, i64* %var_a_23
  %asmout29 = extractvalue {i64, i64, i64, i64} %asm27, 1
  store i64 %asmout29, i64* %var_b_24
  %asmout30 = extractvalue {i64, i64, i64, i64} %asm27, 2
  store i64 %asmout30, i64* %var_c2_25
  %asmout31 = extractvalue {i64, i64, i64, i64} %asm27, 3
  store i64 %asmout31, i64* %var_d_26
  store i64 0, i64* %var_c2_25
  %t300 = load i64, i64* %var_a_23
  store i64 %t300, i64* %var_rax_4
  %t301 = load i64, i64* %var_b_24
  store i64 %t301, i64* %var_rbx_5
  %t302 = load i64, i64* %var_c2_25
  store i64 %t302, i64* %var_rcx_6
  %t303 = load i64, i64* %var_d_26
  store i64 %t303, i64* %var_rdx_7
  br label %merge13
merge13:
  br label %merge10
merge10:
  %t304 = load i64*, i64** %var_g_1
  %t305 = load i64, i64* %var_rax_4
  call void @store_gpr(i64* %t304, i64 0, i64 %t305)
  %t306 = load i64*, i64** %var_g_1
  %t307 = load i64, i64* %var_rbx_5
  call void @store_gpr(i64* %t306, i64 1, i64 %t307)
  %t308 = load i64*, i64** %var_g_1
  %t309 = load i64, i64* %var_rcx_6
  call void @store_gpr(i64* %t308, i64 2, i64 %t309)
  %t310 = load i64*, i64** %var_g_1
  %t311 = load i64, i64* %var_rdx_7
  call void @store_gpr(i64* %t310, i64 3, i64 %t311)
  call void @advance_guest_rip()
  ret void
}

define void @emulate_rdmsr(i64* %arg_g) {
entry0:
  %var_g_1 = alloca i64*
  store i64* %arg_g, i64** %var_g_1
  %var_msr_2 = alloca i64
  %t312 = load i64*, i64** %var_g_1
  %t313 = call i64 @load_gpr(i64* %t312, i64 2)
  store i64 %t313, i64* %var_msr_2
  %var_v_3 = alloca i64
  %t314 = load i64, i64* %var_msr_2
  %t315 = call i64 @rdmsr(i64 %t314)
  store i64 %t315, i64* %var_v_3
  %t316 = load i64*, i64** %var_g_1
  %t317 = load i64, i64* %var_v_3
  %t318 = and i64 %t317, 4294967295
  call void @store_gpr(i64* %t316, i64 0, i64 %t318)
  %t319 = load i64*, i64** %var_g_1
  %t320 = load i64, i64* %var_v_3
  %t321 = ashr i64 %t320, 32
  %t322 = and i64 %t321, 4294967295
  call void @store_gpr(i64* %t319, i64 3, i64 %t322)
  call void @advance_guest_rip()
  ret void
}

define void @emulate_wrmsr(i64* %arg_g) {
entry0:
  %var_g_1 = alloca i64*
  store i64* %arg_g, i64** %var_g_1
  call void @advance_guest_rip()
  ret void
}

define i64 @vmexit_c_handler(i64* %arg_g) {
entry0:
  %var_g_1 = alloca i64*
  store i64* %arg_g, i64** %var_g_1
  %var_reason_2 = alloca i64
  %t323 = load i64, i64* @VMCS_EXIT_REASON
  %t324 = call i64 @vmread(i64 %t323)
  %t325 = and i64 %t324, 65535
  store i64 %t325, i64* %var_reason_2
  %t326 = load i64, i64* %var_reason_2
  %t327 = load i64, i64* @EXIT_REASON_VMCALL
  %t328 = icmp eq i64 %t326, %t327
  br i1 %t328, label %then3, label %else4
then3:
  %t329 = load i64*, i64** %var_g_1
  call void @service_vmcall(i64* %t329)
  ret i64 0
else4:
  %t330 = load i64, i64* %var_reason_2
  %t331 = load i64, i64* @EXIT_REASON_CPUID
  %t332 = icmp eq i64 %t330, %t331
  br i1 %t332, label %then6, label %else7
then6:
  %t333 = load i64*, i64** %var_g_1
  call void @emulate_cpuid(i64* %t333)
  ret i64 0
else7:
  %t334 = load i64, i64* %var_reason_2
  %t335 = load i64, i64* @EXIT_REASON_RDMSR
  %t336 = icmp eq i64 %t334, %t335
  br i1 %t336, label %then9, label %else10
then9:
  %t337 = load i64*, i64** %var_g_1
  call void @emulate_rdmsr(i64* %t337)
  ret i64 0
else10:
  %t338 = load i64, i64* %var_reason_2
  %t339 = load i64, i64* @EXIT_REASON_WRMSR
  %t340 = icmp eq i64 %t338, %t339
  br i1 %t340, label %then12, label %else13
then12:
  %t341 = load i64*, i64** %var_g_1
  call void @emulate_wrmsr(i64* %t341)
  ret i64 0
else13:
  %t342 = load i64, i64* %var_reason_2
  %t343 = load i64, i64* @EXIT_REASON_HLT
  %t344 = icmp eq i64 %t342, %t343
  br i1 %t344, label %then15, label %else16
then15:
  %t345 = getelementptr inbounds [41 x i8], [41 x i8]* @str1, i64 0, i64 0
  call void @serial_puts(i8* %t345)
  ret i64 1
else16:
  %t346 = load i64, i64* %var_reason_2
  %t347 = load i64, i64* @EXIT_REASON_TRIPLE_FAULT
  %t348 = icmp eq i64 %t346, %t347
  br i1 %t348, label %then18, label %else19
then18:
  %t349 = getelementptr inbounds [25 x i8], [25 x i8]* @str2, i64 0, i64 0
  call void @serial_puts(i8* %t349)
  ret i64 1
else19:
  %t350 = getelementptr inbounds [36 x i8], [36 x i8]* @str3, i64 0, i64 0
  call void @serial_puts(i8* %t350)
  %t351 = load i64, i64* %var_reason_2
  call void @serial_put_hex(i64 %t351)
  %t352 = getelementptr inbounds [9 x i8], [9 x i8]* @str4, i64 0, i64 0
  call void @serial_puts(i8* %t352)
  %t353 = load i64, i64* @VMCS_EXIT_QUALIFICATION
  %t354 = call i64 @vmread(i64 %t353)
  call void @serial_put_hex(i64 %t354)
  call void @serial_putc(i8 10)
  ret i64 1
merge20:
  br label %merge17
merge17:
  br label %merge14
merge14:
  br label %merge11
merge11:
  br label %merge8
merge8:
  br label %merge5
merge5:
  ret i64 1
}

define void @vmexit_done() {
entry0:
  %t355 = getelementptr inbounds [48 x i8], [48 x i8]* @str5, i64 0, i64 0
  call void @serial_puts(i8* %t355)
  ret void
}

define void @ox_com1_panic_put(i64 %arg_c) {
entry0:
  %var_c_1 = alloca i64
  store i64 %arg_c, i64* %var_c_1
  %t356 = load i64, i64* %var_c_1
  call void asm sideeffect "outb %al, %dx", "{dx},{al}"(i64 1016, i64 %t356)
  ret void
}

define void @ox_bounds_fail(i64 %arg_idx, i64 %arg_bound) {
entry0:
  %var_idx_1 = alloca i64
  store i64 %arg_idx, i64* %var_idx_1
  %var_bound_2 = alloca i64
  store i64 %arg_bound, i64* %var_bound_2
  call void @ox_com1_panic_put(i64 75)
  call void @ox_com1_panic_put(i64 112)
  call void @ox_com1_panic_put(i64 76)
  call void @ox_com1_panic_put(i64 46)
  call void @ox_com1_panic_put(i64 46)
  %t357 = load i64, i64* %var_idx_1
  %t358 = load i64, i64* %var_bound_2
  %t359 = xor i64 %t357, %t358
  %t360 = and i64 %t359, 255
  call void @ox_com1_panic_put(i64 %t360)
  %var_i_3 = alloca i64
  store i64 0, i64* %var_i_3
  br label %while_cond4
while_cond4:
  %t361 = load i64, i64* %var_i_3
  %t362 = icmp slt i64 %t361, 1000
  br i1 %t362, label %while_body5, label %while_end6
while_body5:
  call void asm sideeffect "cli", ""()
  call void asm sideeffect "hlt", ""()
  %t363 = load i64, i64* %var_i_3
  %t364 = add i64 %t363, 1
  store i64 %t364, i64* %var_i_3
  br label %while_cond4
while_end6:
  br label %while_cond7
while_cond7:
  br i1 1, label %while_body8, label %while_end9
while_body8:
  call void asm sideeffect "hlt", ""()
  br label %while_cond7
while_end9:
  ret void
}

define i64 @rdmsr(i64 %arg_msr) {
entry0:
  %var_msr_1 = alloca i64
  store i64 %arg_msr, i64* %var_msr_1
  %var_lo_2 = alloca i64
  store i64 0, i64* %var_lo_2
  %var_hi_3 = alloca i64
  store i64 0, i64* %var_hi_3
  %var_ecx_4 = alloca i64
  %t365 = load i64, i64* %var_msr_1
  store i64 %t365, i64* %var_ecx_4
  %t366 = load i64, i64* %var_lo_2
  %t367 = load i64, i64* %var_hi_3
  %t368 = load i64, i64* %var_ecx_4
  %asm5 = call {i64, i64} asm sideeffect "rdmsr", "={eax},={edx},{ecx}"(i64 %t368)
  %asmout6 = extractvalue {i64, i64} %asm5, 0
  store i64 %asmout6, i64* %var_lo_2
  %asmout7 = extractvalue {i64, i64} %asm5, 1
  store i64 %asmout7, i64* %var_hi_3
  %t369 = load i64, i64* %var_hi_3
  %t370 = shl i64 %t369, 32
  %t371 = load i64, i64* %var_lo_2
  %t372 = or i64 %t370, %t371
  ret i64 %t372
}

define void @wrmsr(i64 %arg_msr, i64 %arg_val) {
entry0:
  %var_msr_1 = alloca i64
  store i64 %arg_msr, i64* %var_msr_1
  %var_val_2 = alloca i64
  store i64 %arg_val, i64* %var_val_2
  %var_ecx_3 = alloca i64
  %t373 = load i64, i64* %var_msr_1
  store i64 %t373, i64* %var_ecx_3
  %var_lo_4 = alloca i64
  %t374 = load i64, i64* %var_val_2
  %t375 = and i64 %t374, 4294967295
  store i64 %t375, i64* %var_lo_4
  %var_hi_5 = alloca i64
  %t376 = load i64, i64* %var_val_2
  %t377 = ashr i64 %t376, 32
  %t378 = and i64 %t377, 4294967295
  store i64 %t378, i64* %var_hi_5
  %t379 = load i64, i64* %var_ecx_3
  %t380 = load i64, i64* %var_lo_4
  %t381 = load i64, i64* %var_hi_5
  call void asm sideeffect "wrmsr", "{ecx},{eax},{edx}"(i64 %t379, i64 %t380, i64 %t381)
  ret void
}

define i64 @read_cr0() {
entry0:
  %var_v_1 = alloca i64
  store i64 0, i64* %var_v_1
  %t382 = load i64, i64* %var_v_1
  %asm2 = call i64 asm sideeffect "mov %cr0, %rax", "={rax}"()
  store i64 %asm2, i64* %var_v_1
  %t383 = load i64, i64* %var_v_1
  ret i64 %t383
}

define void @write_cr0(i64 %arg_v) {
entry0:
  %var_v_1 = alloca i64
  store i64 %arg_v, i64* %var_v_1
  %t384 = load i64, i64* %var_v_1
  call void asm sideeffect "mov %rax, %cr0", "{rax}"(i64 %t384)
  ret void
}

define i64 @read_cr4() {
entry0:
  %var_v_1 = alloca i64
  store i64 0, i64* %var_v_1
  %t385 = load i64, i64* %var_v_1
  %asm2 = call i64 asm sideeffect "mov %cr4, %rax", "={rax}"()
  store i64 %asm2, i64* %var_v_1
  %t386 = load i64, i64* %var_v_1
  ret i64 %t386
}

define void @write_cr4(i64 %arg_v) {
entry0:
  %var_v_1 = alloca i64
  store i64 %arg_v, i64* %var_v_1
  %t387 = load i64, i64* %var_v_1
  call void asm sideeffect "mov %rax, %cr4", "{rax}"(i64 %t387)
  ret void
}

define void @write_cr3(i64 %arg_v) {
entry0:
  %var_v_1 = alloca i64
  store i64 %arg_v, i64* %var_v_1
  %t388 = load i64, i64* %var_v_1
  call void asm sideeffect "mov %rax, %cr3", "{rax}"(i64 %t388)
  ret void
}

define void @outb(i64 %arg_port, i64 %arg_val) {
entry0:
  %var_port_1 = alloca i64
  store i64 %arg_port, i64* %var_port_1
  %var_val_2 = alloca i64
  store i64 %arg_val, i64* %var_val_2
  %var_dx_3 = alloca i16
  %t389 = load i64, i64* %var_port_1
  %cast4 = trunc i64 %t389 to i16
  store i16 %cast4, i16* %var_dx_3
  %var_al_5 = alloca i8
  %t390 = load i64, i64* %var_val_2
  %cast6 = trunc i64 %t390 to i8
  store i8 %cast6, i8* %var_al_5
  %t391 = load i16, i16* %var_dx_3
  %t392 = load i8, i8* %var_al_5
  call void asm sideeffect "outb %al, %dx", "{dx},{al}"(i16 %t391, i8 %t392)
  ret void
}

define i64 @inb(i64 %arg_port) {
entry0:
  %var_port_1 = alloca i64
  store i64 %arg_port, i64* %var_port_1
  %var_dx_2 = alloca i16
  %t393 = load i64, i64* %var_port_1
  %cast3 = trunc i64 %t393 to i16
  store i16 %cast3, i16* %var_dx_2
  %var_al_4 = alloca i8
  %cast5 = trunc i64 0 to i8
  store i8 %cast5, i8* %var_al_4
  %t394 = load i8, i8* %var_al_4
  %t395 = load i16, i16* %var_dx_2
  %asm6 = call i8 asm sideeffect "inb %dx, %al", "={al},{dx}"(i16 %t395)
  store i8 %asm6, i8* %var_al_4
  %t396 = load i8, i8* %var_al_4
  %cast7 = sext i8 %t396 to i64
  ret i64 %cast7
}

define void @halt_forever() {
entry0:
  call void asm sideeffect "cli", ""()
  call void asm sideeffect "hlt", ""()
  call void @halt_forever()
  ret void
}

define void @serial_init() {
entry0:
  %t397 = load i64, i64* @COM1
  %t398 = add i64 %t397, 1
  call void @outb(i64 %t398, i64 0)
  %t399 = load i64, i64* @COM1
  %t400 = add i64 %t399, 3
  call void @outb(i64 %t400, i64 128)
  %t401 = load i64, i64* @COM1
  %t402 = add i64 %t401, 0
  call void @outb(i64 %t402, i64 3)
  %t403 = load i64, i64* @COM1
  %t404 = add i64 %t403, 1
  call void @outb(i64 %t404, i64 0)
  %t405 = load i64, i64* @COM1
  %t406 = add i64 %t405, 3
  call void @outb(i64 %t406, i64 3)
  %t407 = load i64, i64* @COM1
  %t408 = add i64 %t407, 2
  call void @outb(i64 %t408, i64 199)
  %t409 = load i64, i64* @COM1
  %t410 = add i64 %t409, 4
  call void @outb(i64 %t410, i64 11)
  ret void
}

define i1 @serial_can_send() {
entry0:
  %t411 = load i64, i64* @COM1
  %t412 = add i64 %t411, 5
  %t413 = call i64 @inb(i64 %t412)
  %t414 = and i64 %t413, 32
  %t415 = icmp ne i64 %t414, 0
  ret i1 %t415
}

define void @serial_putc(i8 %arg_c) {
entry0:
  %var_c_1 = alloca i8
  store i8 %arg_c, i8* %var_c_1
  %var_guard_2 = alloca i64
  store i64 0, i64* %var_guard_2
  br label %while_cond3
while_cond3:
  %t416 = call i1 @serial_can_send()
  %t417 = xor i1 %t416, true
  br i1 %t417, label %while_body4, label %while_end5
while_body4:
  %t418 = load i64, i64* %var_guard_2
  %t419 = add i64 %t418, 1
  store i64 %t419, i64* %var_guard_2
  %t420 = load i64, i64* %var_guard_2
  %t421 = icmp sgt i64 %t420, 100000
  br i1 %t421, label %then6, label %else7
then6:
  ret void
else7:
  br label %merge8
merge8:
  br label %while_cond3
while_end5:
  %t422 = load i64, i64* @COM1
  %t423 = load i8, i8* %var_c_1
  %cast9 = zext i8 %t423 to i64
  call void @outb(i64 %t422, i64 %cast9)
  ret void
}

define void @serial_put_hex_digit(i64 %arg_n) {
entry0:
  %var_n_1 = alloca i64
  store i64 %arg_n, i64* %var_n_1
  %var_d_2 = alloca i64
  %t424 = load i64, i64* %var_n_1
  %t425 = and i64 %t424, 15
  store i64 %t425, i64* %var_d_2
  %var_c_3 = alloca i8
  %t426 = load i64, i64* %var_d_2
  %t427 = icmp slt i64 %t426, 10
  br i1 %t427, label %tern_then4, label %tern_else5
tern_then4:
  %t428 = load i64, i64* %var_d_2
  %t429 = add i64 %t428, 48
  br label %tern_done6
tern_else5:
  %t430 = load i64, i64* %var_d_2
  %t431 = add i64 %t430, 55
  br label %tern_done6
tern_done6:
  %t432 = phi i64 [ %t429, %tern_then4 ], [ %t431, %tern_else5 ]
  %cast7 = trunc i64 %t432 to i8
  store i8 %cast7, i8* %var_c_3
  %t433 = load i8, i8* %var_c_3
  call void @serial_putc(i8 %t433)
  ret void
}

define void @serial_put_hex(i64 %arg_v) {
entry0:
  %var_v_1 = alloca i64
  store i64 %arg_v, i64* %var_v_1
  call void @serial_putc(i8 48)
  call void @serial_putc(i8 120)
  %var_i_2 = alloca i64
  store i64 15, i64* %var_i_2
  br label %while_cond3
while_cond3:
  %t434 = load i64, i64* %var_i_2
  %t435 = icmp sge i64 %t434, 0
  br i1 %t435, label %while_body4, label %while_end5
while_body4:
  %t436 = load i64, i64* %var_v_1
  %t437 = load i64, i64* %var_i_2
  %t438 = shl i64 %t437, 2
  %t439 = ashr i64 %t436, %t438
  call void @serial_put_hex_digit(i64 %t439)
  %t440 = load i64, i64* %var_i_2
  %t441 = sub i64 %t440, 1
  store i64 %t441, i64* %var_i_2
  br label %while_cond3
while_end5:
  ret void
}

define void @serial_put_dec(i64 %arg_v) {
entry0:
  %var_v_1 = alloca i64
  store i64 %arg_v, i64* %var_v_1
  %t442 = load i64, i64* %var_v_1
  %t443 = icmp slt i64 %t442, 0
  br i1 %t443, label %then2, label %else3
then2:
  call void @serial_putc(i8 45)
  %t444 = load i64, i64* %var_v_1
  %t445 = sub i64 0, %t444
  store i64 %t445, i64* %var_v_1
  br label %merge4
else3:
  br label %merge4
merge4:
  %t446 = load i64, i64* %var_v_1
  %t447 = icmp eq i64 %t446, 0
  br i1 %t447, label %then5, label %else6
then5:
  call void @serial_putc(i8 48)
  ret void
else6:
  br label %merge7
merge7:
  %var_buf_8 = alloca [20 x i8]
  %var_n_9 = alloca i64
  store i64 0, i64* %var_n_9
  br label %while_cond10
while_cond10:
  %t448 = load i64, i64* %var_v_1
  %t449 = icmp sgt i64 %t448, 0
  br i1 %t449, label %while_body11, label %while_end12
while_body11:
  %t450 = load i64, i64* %var_n_9
  %ic13 = icmp slt i64 %t450, 0
  %ic14 = icmp sge i64 %t450, 20
  %bad15 = or i1 %ic13, %ic14
  br i1 %bad15, label %idx_fail17, label %idx_ok16
idx_fail17:
  call void @ox_bounds_fail(i64 %t450, i64 20)
  unreachable
idx_ok16:
  %ep18 = getelementptr inbounds [20 x i8], [20 x i8]* %var_buf_8, i64 0, i64 %t450
  %t451 = load i64, i64* %var_v_1
  %t452 = srem i64 %t451, 10
  %t453 = add i64 %t452, 48
  %cast19 = trunc i64 %t453 to i8
  store i8 %cast19, i8* %ep18
  %t454 = load i64, i64* %var_n_9
  %t455 = add i64 %t454, 1
  store i64 %t455, i64* %var_n_9
  %t456 = load i64, i64* %var_v_1
  %t457 = sdiv i64 %t456, 10
  store i64 %t457, i64* %var_v_1
  br label %while_cond10
while_end12:
  %var_i_20 = alloca i64
  %t458 = load i64, i64* %var_n_9
  %t459 = sub i64 %t458, 1
  store i64 %t459, i64* %var_i_20
  br label %while_cond21
while_cond21:
  %t460 = load i64, i64* %var_i_20
  %t461 = icmp sge i64 %t460, 0
  br i1 %t461, label %while_body22, label %while_end23
while_body22:
  %t462 = load [20 x i8], [20 x i8]* %var_buf_8
  %t463 = load i64, i64* %var_i_20
  %ic24 = icmp slt i64 %t463, 0
  %ic25 = icmp sge i64 %t463, 20
  %bad26 = or i1 %ic24, %ic25
  br i1 %bad26, label %idx_fail28, label %idx_ok27
idx_fail28:
  call void @ox_bounds_fail(i64 %t463, i64 20)
  unreachable
idx_ok27:
  %ep29 = getelementptr inbounds [20 x i8], [20 x i8]* %var_buf_8, i64 0, i64 %t463
  %idx30 = load i8, i8* %ep29
  call void @serial_putc(i8 %idx30)
  %t464 = load i64, i64* %var_i_20
  %t465 = sub i64 %t464, 1
  store i64 %t465, i64* %var_i_20
  br label %while_cond21
while_end23:
  ret void
}

define void @serial_puts(i8* %arg_s) {
entry0:
  %var_s_1 = alloca i8*
  store i8* %arg_s, i8** %var_s_1
  %var_p_2 = alloca i8*
  %t466 = load i8*, i8** %var_s_1
  store i8* %t466, i8** %var_p_2
  br label %while_cond3
while_cond3:
  br i1 1, label %while_body4, label %while_end5
while_body4:
  %var_c_6 = alloca i8
  %t467 = load i8*, i8** %var_p_2
  %mmio7 = load volatile i8, i8* %t467
  store i8 %mmio7, i8* %var_c_6
  %t468 = load i8, i8* %var_c_6
  %cast8 = trunc i64 0 to i8
  %t469 = icmp eq i8 %t468, %cast8
  br i1 %t469, label %then9, label %else10
then9:
  ret void
else10:
  br label %merge11
merge11:
  %t470 = load i8, i8* %var_c_6
  call void @serial_putc(i8 %t470)
  %t471 = load i8*, i8** %var_p_2
  %gep12 = getelementptr inbounds i8, i8* %t471, i64 1
  store i8* %gep12, i8** %var_p_2
  br label %while_cond3
while_end5:
  ret void
}

define i1 @has_vmx() {
entry0:
  %var_eax_1 = alloca i64
  store i64 1, i64* %var_eax_1
  %var_ecx_2 = alloca i64
  store i64 0, i64* %var_ecx_2
  %var_ebx_3 = alloca i64
  store i64 0, i64* %var_ebx_3
  %var_edx_4 = alloca i64
  store i64 0, i64* %var_edx_4
  %t472 = load i64, i64* %var_eax_1
  %t473 = load i64, i64* %var_ebx_3
  %t474 = load i64, i64* %var_ecx_2
  %t475 = load i64, i64* %var_edx_4
  %t476 = load i64, i64* %var_eax_1
  %asm5 = call {i64, i64, i64, i64} asm sideeffect "cpuid", "={eax},={ebx},={ecx},={edx},0"(i64 %t476)
  %asmout6 = extractvalue {i64, i64, i64, i64} %asm5, 0
  store i64 %asmout6, i64* %var_eax_1
  %asmout7 = extractvalue {i64, i64, i64, i64} %asm5, 1
  store i64 %asmout7, i64* %var_ebx_3
  %asmout8 = extractvalue {i64, i64, i64, i64} %asm5, 2
  store i64 %asmout8, i64* %var_ecx_2
  %asmout9 = extractvalue {i64, i64, i64, i64} %asm5, 3
  store i64 %asmout9, i64* %var_edx_4
  %t477 = load i64, i64* %var_ecx_2
  %t478 = shl i64 1, 5
  %t479 = and i64 %t477, %t478
  %t480 = icmp ne i64 %t479, 0
  ret i1 %t480
}

define i64 @vmx_on() {
entry0:
  %var_cr4v_1 = alloca i64
  %t481 = call i64 @read_cr4()
  store i64 %t481, i64* %var_cr4v_1
  %t482 = load i64, i64* %var_cr4v_1
  %t483 = shl i64 1, 13
  %t484 = or i64 %t482, %t483
  call void @write_cr4(i64 %t484)
  %var_basic_2 = alloca i64
  %t485 = load i64, i64* @MSR_IA32_VMX_BASIC
  %t486 = call i64 @rdmsr(i64 %t485)
  store i64 %t486, i64* %var_basic_2
  %var_rev_3 = alloca i32
  %t487 = load i64, i64* %var_basic_2
  %t488 = and i64 %t487, 2147483647
  %cast4 = trunc i64 %t488 to i32
  store i32 %cast4, i32* %var_rev_3
  %var_region_ptr_5 = alloca i8*
  %t489 = load [4096 x i8], [4096 x i8]* @vmxon_region
  %ic6 = icmp slt i64 0, 0
  %ic7 = icmp sge i64 0, 4096
  %bad8 = or i1 %ic6, %ic7
  br i1 %bad8, label %idx_fail10, label %idx_ok9
idx_fail10:
  call void @ox_bounds_fail(i64 0, i64 4096)
  unreachable
idx_ok9:
  %ep11 = getelementptr inbounds [4096 x i8], [4096 x i8]* @vmxon_region, i64 0, i64 0
  %idx12 = load i8, i8* %ep11
  %ic13 = icmp slt i64 0, 0
  %ic14 = icmp sge i64 0, 4096
  %bad15 = or i1 %ic13, %ic14
  br i1 %bad15, label %idx_fail17, label %idx_ok16
idx_fail17:
  call void @ox_bounds_fail(i64 0, i64 4096)
  unreachable
idx_ok16:
  %ep18 = getelementptr inbounds [4096 x i8], [4096 x i8]* @vmxon_region, i64 0, i64 0
  store i8* %ep18, i8** %var_region_ptr_5
  %var_rev_ptr_19 = alloca i32*
  %t490 = load i8*, i8** %var_region_ptr_5
  %cast20 = bitcast i8* %t490 to i32*
  store i32* %cast20, i32** %var_rev_ptr_19
  %t491 = load i32*, i32** %var_rev_ptr_19
  %t492 = load i32, i32* %var_rev_3
  store volatile i32 %t492, i32* %t491
  %var_region_phys_21 = alloca i64
  %t493 = load i8*, i8** %var_region_ptr_5
  %cast22 = ptrtoint i8* %t493 to i64
  store i64 %cast22, i64* %var_region_phys_21
  %t494 = load i64, i64* %var_region_phys_21
  call void asm sideeffect "vmxon (%rax)", "{rax}"(i64 %t494)
  ret i64 0
}

define i64 @oxide_long_mode_entry() {
entry0:
  %var_sp_1 = alloca i64
  %t495 = call i64 @ox_entry_stack_top_addr()
  store i64 %t495, i64* %var_sp_1
  %t496 = load i64, i64* %var_sp_1
  call void asm sideeffect "mov %rax, %rsp", "{rax}"(i64 %t496)
  call void asm sideeffect "ltr %ax", "{ax}"(i64 24)
  call void @serial_init()
  %t497 = load i8*, i8** @MSG_BOOT
  call void @serial_puts(i8* %t497)
  %t498 = call i1 @has_vmx()
  %t499 = xor i1 %t498, true
  br i1 %t499, label %then2, label %else3
then2:
  %t500 = getelementptr inbounds [31 x i8], [31 x i8]* @str6, i64 0, i64 0
  call void @serial_puts(i8* %t500)
  call void @halt_forever()
  br label %merge4
else3:
  br label %merge4
merge4:
  %var_status_5 = alloca i64
  %t501 = call i64 @vmx_on()
  store i64 %t501, i64* %var_status_5
  %t502 = getelementptr inbounds [29 x i8], [29 x i8]* @str7, i64 0, i64 0
  call void @serial_puts(i8* %t502)
  %var_launch_rc_6 = alloca i64
  %t503 = call i64 @launch_guest()
  store i64 %t503, i64* %var_launch_rc_6
  %t504 = getelementptr inbounds [28 x i8], [28 x i8]* @str8, i64 0, i64 0
  call void @serial_puts(i8* %t504)
  call void @halt_forever()
  ret i64 0
}

