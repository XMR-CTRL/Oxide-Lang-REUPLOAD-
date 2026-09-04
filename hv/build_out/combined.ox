


const MSR_IA32_VMX_BASIC            = 0x480;
const MSR_IA32_VMX_PINBASED_CTLS    = 0x481;
const MSR_IA32_VMX_PROCBASED_CTLS   = 0x482;
const MSR_IA32_VMX_EXIT_CTLS        = 0x483;
const MSR_IA32_VMX_ENTRY_CTLS       = 0x484;
const MSR_IA32_VMX_TRUE_PINBASED    = 0x48D;
const MSR_IA32_VMX_TRUE_PROCBASED   = 0x48E;
const MSR_IA32_VMX_TRUE_EXIT        = 0x48F;
const MSR_IA32_VMX_TRUE_ENTRY       = 0x490;
const MSR_IA32_VMX_PROCBASED2_CTLS  = 0x491;


const VMX_BASIC_DUAL_MONITOR_BIT  = 1 << 49;
const VMX_BASIC_MEM_TYPE_WB       = 6;
const VMX_BASIC_INS_OUTS_BIT      = 1 << 54;
const VMX_BASIC_TRUE_CTLS_BIT     = 1 << 55;


const VMCS_GUEST_CS_SEL          = 0x0802;
const VMCS_GUEST_SS_SEL          = 0x0804;
const VMCS_GUEST_DS_SEL          = 0x0806;
const VMCS_GUEST_ES_SEL          = 0x0800;
const VMCS_GUEST_FS_SEL          = 0x0808;
const VMCS_GUEST_GS_SEL          = 0x080A;
const VMCS_GUEST_LDTR_SEL        = 0x080C;
const VMCS_GUEST_TR_SEL          = 0x080E;


const VMCS_VPID                  = 0x0000;


const VMCS_EPT_POINTER            = 0x001A;


const VMCS_POSTED_INTR_NV          = 0x0002;
const VMCS_EPTP_LIST_ADDR          = 0x0024;
const VMCS_VPID_PTR                = 0x0000;


const VMCS_HOST_CS_SEL           = 0x0C02;
const VMCS_HOST_SS_SEL           = 0x0C04;
const VMCS_HOST_DS_SEL           = 0x0C06;
const VMCS_HOST_ES_SEL           = 0x0C00;
const VMCS_HOST_FS_SEL           = 0x0C08;
const VMCS_HOST_GS_SEL           = 0x0C0A;
const VMCS_HOST_TR_SEL           = 0x0C0C;


const VMCS_ADDR_IO_BITMAP_A      = 0x2000;
const VMCS_ADDR_IO_BITMAP_B      = 0x2002;
const VMCS_ADDR_MSR_BITMAP       = 0x2004;
const VMCS_EXECUTIVE_VMCS_PTR    = 0x200C;


const VMCS_GUEST_PHYSICAL_ADDR   = 0x2400;


const VMCS_GUEST_VMCS_LINK_PTR   = 0x2800;
const VMCS_GUEST_IA32_EFER       = 0x2806;
const VMCS_GUEST_PDPTE_BASE0     = 0x280A;
const VMCS_GUEST_PDPTE_BASE1     = 0x280C;
const VMCS_GUEST_PDPTE_BASE2     = 0x280E;
const VMCS_GUEST_PDPTE_BASE3     = 0x2810;


const VMCS_PIN_BASED             = 0x4000;
const VMCS_PROC_BASED            = 0x4002;
const VMCS_EXC_BITMAP            = 0x4004;
const VMCS_PROC_BASED2           = 0x401E;
const VMCS_EXIT_CTLS             = 0x400C;
const VMCS_EXIT_MSR_STORE_COUNT  = 0x400E;
const VMCS_EXIT_MSR_LOAD_COUNT   = 0x4010;
const VMCS_ENTRY_CTLS            = 0x4012;
const VMCS_ENTRY_MSR_LOAD_COUNT = 0x4014;
const VMCS_ENTRY_INTERRUPTION_INFO = 0x4016;
const VMCS_ENTRY_EXCEPTION_ERRORCODE = 0x4018;
const VMCS_ENTRY_INSTRUCTION_LENGTH = 0x401A;


const VMCS_INSTRUCTION_ERROR     = 0x4400;
const VMCS_EXIT_REASON          = 0x4402;
const VMCS_VMEXIT_INSTRUCTION_LENGTH = 0x440C;
const VMCS_VMEXIT_INTERRUPTION_INFO   = 0x4404;
const VMCS_EXIT_QUALIFICATION     = 0x6400;


const VMCS_GUEST_CS_LIMIT        = 0x4802;
const VMCS_GUEST_SS_LIMIT        = 0x4804;
const VMCS_GUEST_DS_LIMIT        = 0x4806;
const VMCS_GUEST_ES_LIMIT        = 0x4800;
const VMCS_GUEST_FS_LIMIT        = 0x4808;
const VMCS_GUEST_GS_LIMIT        = 0x480A;
const VMCS_GUEST_LDTR_LIMIT      = 0x480C;
const VMCS_GUEST_TR_LIMIT        = 0x480E;
const VMCS_GUEST_CS_ACCESS_RIGHTS  = 0x4816;
const VMCS_GUEST_SS_ACCESS_RIGHTS  = 0x4818;
const VMCS_GUEST_DS_ACCESS_RIGHTS  = 0x481A;
const VMCS_GUEST_ES_ACCESS_RIGHTS  = 0x4814;
const VMCS_GUEST_FS_ACCESS_RIGHTS  = 0x481C;
const VMCS_GUEST_GS_ACCESS_RIGHTS  = 0x481E;
const VMCS_GUEST_LDTR_ACCESS_RIGHTS = 0x4820;
const VMCS_GUEST_TR_ACCESS_RIGHTS  = 0x4822;
const VMCS_GUEST_INTERRUPT_STATUS = 0x4824;
const VMCS_GUEST_SYSENTER_CS      = 0x482A;


const VMCS_HOST_SYSENTER_CS       = 0x4C00;


const VMCS_CR0_GUEST_HOST_MASK    = 0x6000;
const VMCS_CR4_GUEST_HOST_MASK    = 0x6002;
const VMCS_CR0_READ_SHADOW        = 0x6004;
const VMCS_CR4_READ_SHADOW        = 0x6006;


const VMCS_GUEST_CR0              = 0x6800;
const VMCS_GUEST_CR3              = 0x6802;
const VMCS_GUEST_CR4              = 0x6804;
const VMCS_GUEST_ES_BASE          = 0x6806;
const VMCS_GUEST_CS_BASE          = 0x6808;
const VMCS_GUEST_SS_BASE          = 0x680A;
const VMCS_GUEST_DS_BASE          = 0x680C;
const VMCS_GUEST_FS_BASE          = 0x680E;
const VMCS_GUEST_GS_BASE          = 0x6810;
const VMCS_GUEST_LDTR_BASE        = 0x6812;
const VMCS_GUEST_TR_BASE          = 0x6814;
const VMCS_GUEST_GDTR_BASE        = 0x6816;
const VMCS_GUEST_IDTR_BASE        = 0x6818;
const VMCS_GUEST_DR7              = 0x681A;
const VMCS_GUEST_RSP              = 0x681C;
const VMCS_GUEST_RIP              = 0x681E;
const VMCS_GUEST_RFLAGS           = 0x6820;
const VMCS_GUEST_SYSENTER_ESP     = 0x6824;
const VMCS_GUEST_SYSENTER_EIP     = 0x6826;


const VMCS_HOST_CR0               = 0x6C00;
const VMCS_HOST_CR3               = 0x6C02;
const VMCS_HOST_CR4               = 0x6C04;
const VMCS_HOST_FS_BASE           = 0x6C06;
const VMCS_HOST_GS_BASE           = 0x6C08;
const VMCS_HOST_TR_BASE           = 0x6C0A;
const VMCS_HOST_GDTR_BASE         = 0x6C0C;
const VMCS_HOST_IDTR_BASE         = 0x6C0E;
const VMCS_HOST_SYSENTER_ESP      = 0x6C10;
const VMCS_HOST_SYSENTER_EIP      = 0x6C12;
const VMCS_HOST_RSP               = 0x6C14;
const VMCS_HOST_RIP               = 0x6C16;


const AR_TR_BUSY_64       = 0x8B;

const AR_CODE64_EXEC_READ_DPL0 = 0xA09B;
const AR_CODE64_RO_DPL0   = 0x809B;

const AR_DATA_RW_DPL0     = 0xC093;

const AR_UNUSABLE         = 0x10000;


const PIN_EXTERNAL_INTERRUPT_EXITING = 1 << 0;
const PIN_NMI_EXITING                  = 1 << 3;
const PIN_VIRTUAL_NMI                 = 1 << 5;
const PIN_PREEMPT_TIMER               = 1 << 6;
const PIN_POSTED_INTERRUPT            = 1 << 7;


const CPU_BASED_USE_MSR_BITMAPS        = 1 << 28;
const CPU_BASED_ACTIVATE_SECONDARY     = 1 << 31;
const CPU_BASED_HLT_EXITING            = 1 << 7;
const CPU_BASED_INVLPG_EXITING         = 1 << 9;
const CPU_BASED_MOV_DR_EXITING         = 1 << 23;
const CPU_BASED_UNCONDITIONAL_IO_EXITING = 1 << 24;
const CPU_BASED_USE_IO_BITMAPS         = 1 << 25;


const CPU_BASED2_ENABLE_EPT            = 1 << 1;
const CPU_BASED2_ENABLE_VPID           = 1 << 0;
const CPU_BASED2_UNRESTRICTED_GUEST     = 1 << 7;
const CPU_BASED2_ENABLE_RDTSCP          = 1 << 3;
const CPU_BASED2_ENABLE_VIRT_EXCEPTIONS = 1 << 4;
const CPU_BASED2_ENABLE_INVPCID         = 1 << 12;


const EPT_MEM_TYPE_WB        = 6;
const EPT_PAGE_WALK_4        = 3;   // (levels - 1) goes in EPTP bits [5:3]


const EPT_ENTRY_PRESENT      = 1 << 0;
const EPT_ENTRY_WRITE        = 1 << 1;
const EPT_ENTRY_EXECUTE      = 1 << 2;
const EPT_ENTRY_LARGE_PAGE   = 1 << 7;


const VM_EXIT_SAVE_DEBUG_CONTROLS      = 1 << 2;
const VM_EXIT_HOST_ADDR_SPACE_SIZE     = 1 << 9;
const VM_EXIT_ACK_INTR_ON_EXIT         = 1 << 15;
const VM_EXIT_SAVE_IA32_EFER           = 1 << 20;
const VM_EXIT_LOAD_IA32_EFER           = 1 << 21;


const VM_ENTRY_LOAD_DEBUG_CONTROLS     = 1 << 2;
const VM_ENTRY_IA32E_MODE              = 1 << 9;
const VM_ENTRY_LOAD_IA32_EFER          = 1 << 15;


const EXIT_REASON_EXCEPTION_NMI       = 0;
const EXIT_REASON_EXTERNAL_INTERRUPT  = 1;
const EXIT_REASON_TRIPLE_FAULT        = 2;
const EXIT_REASON_INIT_SIGNAL          = 3;
const EXIT_REASON_TASK_SWITCH            = 9;
const EXIT_REASON_CPUID                  = 10;
const EXIT_REASON_HLT                   = 12;
const EXIT_REASON_INVD                   = 13;
const EXIT_REASON_INVLPG                 = 14;
const EXIT_REASON_RDPMC                  = 15;
const EXIT_REASON_RDTSC                  = 16;
const EXIT_REASON_VMCALL                 = 18;
const EXIT_REASON_VMCLEAR                = 19;
const EXIT_REASON_VMLAUNCH               = 20;
const EXIT_REASON_VMPTRLD                = 21;
const EXIT_REASON_VMPTRST                = 22;
const EXIT_REASON_VMREAD                 = 23;
const EXIT_REASON_VMRESUME               = 24;
const EXIT_REASON_VMWRITE                = 25;
const EXIT_REASON_VMXOFF                 = 26;
const EXIT_REASON_VMXON                   = 27;
const EXIT_REASON_CR_ACCESS               = 28;
const EXIT_REASON_DR_ACCESS               = 29;
const EXIT_REASON_IO_INSTRUCTION         = 30;
const EXIT_REASON_RDMSR                  = 31;
const EXIT_REASON_WRMSR                  = 32;
const EXIT_REASON_INVALID_GUEST_STATE    = 33;
const EXIT_REASON_MSR_LOADING             = 34;
const EXIT_REASON_MWAIT                    = 36;
const EXIT_REASON_MONITOR                  = 39;
const EXIT_REASON_PAUSE                    = 40;
const EXIT_REASON_MACHINE_CHECK            = 41;
const EXIT_REASON_APIC_ACCESS              = 44;
const EXIT_REASON_GDTR_OR_IDTR_ACCESS      = 46;
const EXIT_REASON_LDTR_OR_TR_ACCESS        = 47;
const EXIT_REASON_EPT_VIOLATION            = 48;
const EXIT_REASON_EPT_MISCONFIG            = 49;
const EXIT_REASON_INVEPT                   = 50;
const EXIT_REASON_VMX_PREEMPT_TIMER_EXPIRED = 52;
const EXIT_REASON_INVVPID                  = 53;
const EXIT_REASON_WBINVD                  = 54;
const EXIT_REASON_XSETBV                  = 55;


fn vmcs_field_width(enc: i64) -> i64 {

  return (enc >> 10) & 0xF;
}

fn vmcs_field_is_wide(enc: i64) -> bool {
  let w = vmcs_field_width(enc);
  return w == 1 || w == 3;
}


fn vmx_adjust_ctl(msr: i64, want: i64) -> i64 {
  let v = rdmsr(msr);
  let allowed1 = v & 0xFFFFFFFF;
  let allowed0 = (v >> 32) & 0xFFFFFFFF;


  let required1 = allowed1 & (~allowed0 & 0xFFFFFFFF);
  return ((want & allowed1) | required1) & 0xFFFFFFFF;
}

// SDM Appendix B VMCS field encoding cross-references.
// Each assert pins a `const` declared above to its canonical
// Intel SDM Appendix B encoding; under `oxide verify` these
// become machine-checked facts, regressions if any constant
// drifts away from the SDM value.
fn _sdm_appendix_b_cross_refs() {
  assert VMCS_EPT_POINTER           == 0x001A;
  assert VMCS_GUEST_VMCS_LINK_PTR   == 0x2800;
  assert VMCS_PIN_BASED             == 0x4000;
  assert VMCS_PROC_BASED            == 0x4002;
  assert VMCS_EXIT_CTLS             == 0x400C;
  assert VMCS_ENTRY_CTLS            == 0x4012;
  assert VMCS_EXIT_REASON           == 0x4402;
  assert VMCS_GUEST_CR0             == 0x6800;
  assert VMCS_GUEST_CR3             == 0x6802;
  assert VMCS_GUEST_CR4             == 0x6804;
  assert VMCS_GUEST_RFLAGS          == 0x6820;
  assert VMCS_HOST_CR0              == 0x6C00;
  assert VMCS_HOST_CR3              == 0x6C02;
  assert VMCS_HOST_CR4              == 0x6C04;
}



fn vmx_instruct_error() -> i64 {
  let mut err: i64 = 0;
  asm!("vmread %rdx, %rax", in("{rdx}") 0x4400, out("{rax}") err);
  return err;
}


fn vmclear(region_phys: i64) -> i64 {
  asm!("vmclear (%rax)", in("{rax}") region_phys, sideeffect=true);
  return 0;
}


fn vmptrld(region_phys: i64) -> i64 {
  asm!("vmptrld (%rax)", in("{rax}") region_phys, sideeffect=true);
  return 0;
}


fn vmwrite(field: i64, value: i64) {
  asm!("vmwrite %rax, %rdx", in("{rax}") field, in("{rdx}") value, sideeffect=true);
}


fn vmwrite32(field: i64, value: i64) {
  vmwrite(field, value & 0xFFFFFFFF);
}


fn vmread(field: i64) -> i64 {
  let mut v: i64 = 0;
  asm!("vmread %rdx, %rax", in("{rdx}") field, out("{rax}") v);
  return v;
}


fn vmlaunch() -> i64 {
  let mut rc: i64 = 0;
  asm!("vmlaunch", out("{rax}") rc);
  return 1;
}


fn vmresume() -> i64 {
  let mut rc: i64 = 0;
  asm!("vmresume", out("{rax}") rc);
  return 1;
}


fn vmxoff() -> i64 {
  asm!("vmxoff", sideeffect=true);
  return 0;
}


fn vmcall() {
  asm!("vmcall", sideeffect=true);
}



let mut vmcs_region: [u8; 0x1000];


let mut host_stack: [u8; 0x2000];


let mut ept_pml4: [u8; 0x1000];
let mut ept_pdpt: [u8; 0x1000];
let mut ept_pd:   [u8; 0x1000];


extern fn ox_host_rip() -> i64;
extern fn ox_guest_rip() -> i64;
extern fn ox_host_stack_top_addr() -> i64;


fn vmwrite16(field: i64, value: i64) {
  vmwrite(field, value & 0xFFFF);
}


fn read_cr3_phys() -> i64 {
  let mut v: i64 = 0;
  asm!("mov %cr3, %rax", out("{rax}") v);
  return v & 0xFFFFFFFFFFFFF000;
}


fn ept_store_entry(table: &u8, idx: i64, val: i64) {
  let ep: &u64 = (table + (idx << 3)) as &u64;
  mmio_store(ep, val as u64);
}


fn ept_entry(next_table_phys: i64) -> i64 {

  return (next_table_phys & 0xFFFFFFFFFFFFF000)
       | (EPT_ENTRY_PRESENT | EPT_ENTRY_WRITE | EPT_ENTRY_EXECUTE);
}


fn ept_large_entry(page_phys: i64) -> i64 {

  return (page_phys & 0xFFFFFFFFFFE00000)
       | (EPT_ENTRY_PRESENT | EPT_ENTRY_WRITE | EPT_ENTRY_EXECUTE
          | EPT_ENTRY_LARGE_PAGE)
       | (EPT_MEM_TYPE_WB << 3);
}


fn setup_ept() -> i64 {



  let pml4_phys: i64 = (&ept_pml4[0]) as usize;
  let pdpt_phys: i64 = (&ept_pdpt[0]) as usize;
  let pd_phys:   i64 = (&ept_pd[0])   as usize;


  ept_store_entry(&ept_pml4[0], 0, ept_entry(pdpt_phys));
  ept_store_entry(&ept_pdpt[0], 0, ept_entry(pd_phys));



  let mut i: i64 = 0;
  while i < 512 {
    ept_store_entry(&ept_pd[0], i, ept_large_entry(i << 21));
    i = i + 1;
  }


  return pml4_phys | ((EPT_PAGE_WALK_4 as i64) << 3) | EPT_MEM_TYPE_WB;
}


fn launch_guest() -> i64 {

  let basic = rdmsr(MSR_IA32_VMX_BASIC);
  let rev: u32 = (basic & 0x7FFFFFFF) as u32;
  let vmcs_ptr: &u8 = &vmcs_region[0];
  let rev_ptr: &u32 = vmcs_ptr as &u32;
  mmio_store(rev_ptr, rev);
  let abort_ptr: &u32 = (vmcs_ptr + 4) as &u32;
  mmio_store(abort_ptr, 0);


  let vmcs_phys: i64 = vmcs_ptr as usize;
  vmclear(vmcs_phys);
  vmptrld(vmcs_phys);


  vmwrite(VMCS_HOST_RIP, ox_host_rip());
  vmwrite(VMCS_HOST_RSP, ox_host_stack_top_addr());

  vmwrite16(VMCS_HOST_CS_SEL, 0x08);
  vmwrite16(VMCS_HOST_SS_SEL, 0x10);
  vmwrite16(VMCS_HOST_DS_SEL, 0x10);
  vmwrite16(VMCS_HOST_ES_SEL, 0x10);
  vmwrite16(VMCS_HOST_FS_SEL, 0x00);
  vmwrite16(VMCS_HOST_GS_SEL, 0x00);
  vmwrite16(VMCS_HOST_TR_SEL, 0x18);
  vmwrite(VMCS_HOST_CR0, read_cr0());
  vmwrite(VMCS_HOST_CR3, read_cr3_phys());
  vmwrite(VMCS_HOST_CR4, read_cr4());
  vmwrite(VMCS_HOST_FS_BASE, 0);
  vmwrite(VMCS_HOST_GS_BASE, rdmsr(0xC0000101));


  vmwrite(VMCS_HOST_GDTR_BASE, 0);
  vmwrite(VMCS_HOST_IDTR_BASE, 0);
  vmwrite(VMCS_HOST_TR_BASE,  0);
  vmwrite(VMCS_HOST_SYSENTER_CS, 0);
  vmwrite(VMCS_HOST_SYSENTER_ESP, 0);
  vmwrite(VMCS_HOST_SYSENTER_EIP, 0);


  let exit_ctl = vmx_adjust_ctl(MSR_IA32_VMX_EXIT_CTLS,
        VM_EXIT_HOST_ADDR_SPACE_SIZE | VM_EXIT_SAVE_IA32_EFER
      | VM_EXIT_LOAD_IA32_EFER);
  vmwrite32(VMCS_EXIT_CTLS, exit_ctl);


  let entry_ctl = vmx_adjust_ctl(MSR_IA32_VMX_ENTRY_CTLS,
        VM_ENTRY_IA32E_MODE | VM_ENTRY_LOAD_IA32_EFER);
  vmwrite32(VMCS_ENTRY_CTLS, entry_ctl);


  let pin = vmx_adjust_ctl(MSR_IA32_VMX_PINBASED_CTLS, 0);
  vmwrite32(VMCS_PIN_BASED, pin);
  let proc = vmx_adjust_ctl(MSR_IA32_VMX_PROCBASED_CTLS,
        CPU_BASED_HLT_EXITING | CPU_BASED_ACTIVATE_SECONDARY);
  vmwrite32(VMCS_PROC_BASED, proc);
  let proc2 = vmx_adjust_ctl(MSR_IA32_VMX_PROCBASED2_CTLS,
        CPU_BASED2_ENABLE_EPT);
  vmwrite32(VMCS_PROC_BASED2, proc2);
  vmwrite32(VMCS_EXC_BITMAP, 0);


  let eptp = setup_ept();
  vmwrite(VMCS_EPT_POINTER, eptp);


  let gcr0 = 0x80000033;
  vmwrite(VMCS_GUEST_CR0, gcr0);
  vmwrite(VMCS_CR0_GUEST_HOST_MASK, 0);
  vmwrite(VMCS_CR0_READ_SHADOW, gcr0);
  vmwrite(VMCS_GUEST_CR3, read_cr3_phys());

  let gcr4 = (1 as i64) << 5;
  vmwrite(VMCS_GUEST_CR4, gcr4);
  vmwrite(VMCS_CR4_GUEST_HOST_MASK, 0);
  vmwrite(VMCS_CR4_READ_SHADOW, gcr4);


  vmwrite(VMCS_GUEST_IA32_EFER, 0x300);


  vmwrite16(VMCS_GUEST_CS_SEL, 0x08);
  vmwrite16(VMCS_GUEST_SS_SEL, 0x10);
  vmwrite16(VMCS_GUEST_DS_SEL, 0x10);
  vmwrite16(VMCS_GUEST_ES_SEL, 0x10);
  vmwrite16(VMCS_GUEST_FS_SEL, 0x00);
  vmwrite16(VMCS_GUEST_GS_SEL, 0x00);
  vmwrite16(VMCS_GUEST_LDTR_SEL, 0x00);
  vmwrite16(VMCS_GUEST_TR_SEL, 0x18);
  vmwrite32(VMCS_GUEST_CS_ACCESS_RIGHTS, AR_CODE64_EXEC_READ_DPL0);
  vmwrite32(VMCS_GUEST_SS_ACCESS_RIGHTS, AR_DATA_RW_DPL0);
  vmwrite32(VMCS_GUEST_DS_ACCESS_RIGHTS, AR_DATA_RW_DPL0);
  vmwrite32(VMCS_GUEST_ES_ACCESS_RIGHTS, AR_DATA_RW_DPL0);
  vmwrite32(VMCS_GUEST_FS_ACCESS_RIGHTS, AR_UNUSABLE);
  vmwrite32(VMCS_GUEST_GS_ACCESS_RIGHTS, AR_UNUSABLE);
  vmwrite32(VMCS_GUEST_LDTR_ACCESS_RIGHTS, AR_UNUSABLE);
  vmwrite32(VMCS_GUEST_TR_ACCESS_RIGHTS, AR_TR_BUSY_64);

  vmwrite32(VMCS_GUEST_CS_LIMIT, 0xFFFFF);
  vmwrite32(VMCS_GUEST_SS_LIMIT, 0xFFFFF);
  vmwrite32(VMCS_GUEST_DS_LIMIT, 0xFFFFF);
  vmwrite32(VMCS_GUEST_ES_LIMIT, 0xFFFFF);
  vmwrite32(VMCS_GUEST_FS_LIMIT, 0xFFFFF);
  vmwrite32(VMCS_GUEST_GS_LIMIT, 0xFFFFF);
  vmwrite32(VMCS_GUEST_LDTR_LIMIT, 0xFFFFF);
  vmwrite32(VMCS_GUEST_TR_LIMIT, 0x6F);


  vmwrite(VMCS_GUEST_CS_BASE, 0);
  vmwrite(VMCS_GUEST_SS_BASE, 0);
  vmwrite(VMCS_GUEST_DS_BASE, 0);
  vmwrite(VMCS_GUEST_ES_BASE, 0);
  vmwrite(VMCS_GUEST_FS_BASE, 0);
  vmwrite(VMCS_GUEST_GS_BASE, 0);
  vmwrite(VMCS_GUEST_LDTR_BASE, 0);
  vmwrite(VMCS_GUEST_TR_BASE, 0);
  vmwrite(VMCS_GUEST_GDTR_BASE, 0);
  vmwrite(VMCS_GUEST_IDTR_BASE, 0);


  vmwrite(VMCS_GUEST_RIP, ox_guest_rip());
  vmwrite(VMCS_GUEST_RSP, ox_host_stack_top_addr());
  vmwrite(VMCS_GUEST_RFLAGS, 0x2);
  vmwrite(VMCS_GUEST_DR7, 0);
  vmwrite(VMCS_GUEST_SYSENTER_CS, 0);
  vmwrite(VMCS_GUEST_SYSENTER_ESP, 0);
  vmwrite(VMCS_GUEST_SYSENTER_EIP, 0);


  vmwrite(VMCS_GUEST_VMCS_LINK_PTR, 0xFFFFFFFFFFFFFFFF);


  let launched = vmlaunch();
  if launched != 0 {
    let err = vmx_instruct_error();
    serial_puts(str_ptr("[oxide-hv] vmlaunch FAILED\n"));
    return (err & 0xFF) | 0x100;
  }
  return 0;
}


fn guest_payload() {
  asm!("xor %rax, %rax\nxor %rcx, %rcx\ncpuid\nmov $$0x41, %rax\nvmcall\nmov $$0x42, %rax\nvmcall\nmov $$0x43, %rax\nvmcall\nhlt",
       sideeffect=true);
}


fn advance_guest_rip() {
  let len = vmread(VMCS_VMEXIT_INSTRUCTION_LENGTH);
  let rip = vmread(VMCS_GUEST_RIP);
  vmwrite(VMCS_GUEST_RIP, rip + len);
}


fn service_vmcall(g: &i64) {
  let arg: &i64 = g + 0;
  let val: i64 = mmio_load(arg);
  if val != 0 {
    serial_putc((val & 0xFF) as u8);
  }
  advance_guest_rip();
}


fn store_gpr(g: &i64, slot: i64, val: i64) {
  let dst: &i64 = g + slot;
  mmio_store(dst, val);
}


fn load_gpr(g: &i64, slot: i64) -> i64 {
  let src: &i64 = g + slot;
  return mmio_load(src);
}


fn emulate_cpuid(g: &i64) {
  let leaf = load_gpr(g, 0);
  let subleaf = load_gpr(g, 2);
  let mut rax: i64 = 0;
  let mut rbx: i64 = 0;
  let mut rcx: i64 = 0;
  let mut rdx: i64 = 0;
  if leaf == 0 {

    rax = 16;
    rbx = 0x756E6547;
    rdx = 0x49656E69;
    rcx = 0x6C65746E;
  } else if leaf == 1 {

    let mut a: i64 = 1;
    let mut b: i64 = 0;
    let mut c: i64 = 0;
    let mut d: i64 = 0;
    asm!("cpuid", inout("{eax}") a, out("{ebx}") b, inout("{ecx}") c, out("{edx}") d);

    c = (c & 0x7FFFFFFF) | (1 << 31);
    rax = a; rbx = b; rcx = c; rdx = d;
  } else {

    let mut a: i64 = leaf;
    let mut b: i64 = 0;
    let mut c2: i64 = subleaf;
    let mut d: i64 = 0;
    asm!("cpuid", inout("{eax}") a, out("{ebx}") b, inout("{ecx}") c2, out("{edx}") d);
    c2 = 0;
    rax = a; rbx = b; rcx = c2; rdx = d;
  }
  store_gpr(g, 0, rax);
  store_gpr(g, 1, rbx);
  store_gpr(g, 2, rcx);
  store_gpr(g, 3, rdx);
  advance_guest_rip();
}


fn emulate_rdmsr(g: &i64) {
  let msr = load_gpr(g, 2);
  let v = rdmsr(msr);
  store_gpr(g, 0, v & 0xFFFFFFFF);
  store_gpr(g, 3, (v >> 32) & 0xFFFFFFFF);
  advance_guest_rip();
}


fn emulate_wrmsr(g: &i64) {


  advance_guest_rip();
}


fn vmexit_c_handler(g: &i64) -> i64 {
  let reason = vmread(VMCS_EXIT_REASON) & 0xFFFF;
  if reason == EXIT_REASON_VMCALL {
    service_vmcall(g);
    return 0;
  } else if reason == EXIT_REASON_CPUID {
    emulate_cpuid(g);
    return 0;
  } else if reason == EXIT_REASON_RDMSR {
    emulate_rdmsr(g);
    return 0;
  } else if reason == EXIT_REASON_WRMSR {
    emulate_wrmsr(g);
    return 0;
  } else if reason == EXIT_REASON_HLT {
    serial_puts(str_ptr("\n[oxide-hv] guest HLT -> clean shutdown\n"));
    return 1;
  } else if reason == EXIT_REASON_TRIPLE_FAULT {
    serial_puts(str_ptr("[oxide-hv] TRIPLE FAULT\n"));
    return 1;
  } else {
    serial_puts(str_ptr("[oxide-hv] unhandled exit reason=0x"));
    serial_put_hex(reason);
    serial_puts(str_ptr(" qual=0x"));
    serial_put_hex(vmread(VMCS_EXIT_QUALIFICATION));
    serial_putc('\n' as u8);
    return 1;
  }
  return 1;
}


fn vmexit_done() {
  serial_puts(str_ptr("[oxide-hv] host shutting down after guest exit\n"));
}





fn ox_com1_panic_put(c: i64) {
  asm!("outb %al, %dx", in("{dx}") 0x3F8, in("{al}") c);
}


fn ox_bounds_fail(idx: i64, bound: i64) {

  ox_com1_panic_put(0x4B);
  ox_com1_panic_put(0x70);
  ox_com1_panic_put(0x4C);
  ox_com1_panic_put(0x2E);
  ox_com1_panic_put(0x2E);


  ox_com1_panic_put((idx ^ bound) & 0xFF);

  let mut i: i64 = 0;
  while i < 1000 {
    asm!("cli", sideeffect=true);
    asm!("hlt", sideeffect=true);
    i = i + 1;
  }
  while true { asm!("hlt", sideeffect=true); }
}



const COM1 = 0x3F8;


fn rdmsr(msr: i64) -> i64 {
  let mut lo: i64 = 0;
  let mut hi: i64 = 0;
  let ecx: i64 = msr;
  asm!("rdmsr", in("{ecx}") ecx, out("{eax}") lo, out("{edx}") hi);
  return (hi << 32) | lo;
}

fn wrmsr(msr: i64, val: i64) {
  let ecx: i64 = msr;
  let lo: i64 = val & 0xFFFFFFFF;
  let hi: i64 = (val >> 32) & 0xFFFFFFFF;
  asm!("wrmsr", in("{ecx}") ecx, in("{eax}") lo, in("{edx}") hi);
}
fn read_cr0() -> i64 {
  let mut v: i64 = 0;
  asm!("mov %cr0, %rax", out("{rax}") v);
  return v;
}
fn write_cr0(v: i64) {
  asm!("mov %rax, %cr0", in("{rax}") v);
}
fn read_cr4() -> i64 {
  let mut v: i64 = 0;
  asm!("mov %cr4, %rax", out("{rax}") v);
  return v;
}
fn write_cr4(v: i64) {
  asm!("mov %rax, %cr4", in("{rax}") v);
}

fn write_cr3(v: i64) { asm!("mov %rax, %cr3", in("{rax}") v); }


fn outb(port: i64, val: i64) {
  let dx: i16 = port as i16;
  let al: i8 = val as i8;
  asm!("outb %al, %dx", in("{dx}") dx, in("{al}") al);
}
fn inb(port: i64) -> i64 {
  let dx: i16 = port as i16;
  let mut al: i8 = 0;
  asm!("inb %dx, %al", in("{dx}") dx, out("{al}") al);
  return al as i64;
}


fn halt_forever() {

  asm!("cli", sideeffect=true);
  asm!("hlt", sideeffect=true);
  halt_forever();
}


fn serial_init() {
  outb(COM1 + 1, 0);
  outb(COM1 + 3, 0x80);
  outb(COM1 + 0, 3);
  outb(COM1 + 1, 0);
  outb(COM1 + 3, 3);
  outb(COM1 + 2, 0xC7);
  outb(COM1 + 4, 0x0B);
}

fn serial_can_send() -> bool {
  return (inb(COM1 + 5) & 0x20) != 0;
}
fn serial_putc(c: u8) {

  let mut guard = 0;
  while !serial_can_send() {
    guard = guard + 1;
    if guard > 100000 { return; }
  }
  outb(COM1, c as i64);
}


fn serial_put_hex_digit(n: i64) {
  let d: i64 = n & 0xF;
  let c: u8 = ((d < 10) ? (d + 0x30) : (d + 0x37)) as u8;
  serial_putc(c);
}


fn serial_put_hex(v: i64) {
  serial_putc('0' as u8);
  serial_putc('x' as u8);
  let mut i: i64 = 15;
  while i >= 0 {
    serial_put_hex_digit((v >> (i << 2)) as i64);
    i = i - 1;
  }
}


fn serial_put_dec(v: i64) {
  if v < 0 {
    serial_putc('-' as u8);
    v = 0 - v;
  }
  if v == 0 {
    serial_putc('0' as u8);
    return;
  }
  let mut buf: [u8; 20];
  let mut n: i64 = 0;
  while v > 0 {
    buf[n] = ((v % 10) + 0x30) as u8;
    n = n + 1;
    v = v / 10;
  }
  let mut i: i64 = n - 1;
  while i >= 0 {
    serial_putc(buf[i]);
    i = i - 1;
  }
}
fn serial_puts(s: &u8) {


  let mut p: &u8 = s;
  while true {
    let c: u8 = mmio_load(p);
    if c == 0 { return; }
    serial_putc(c);
    p = p + 1;
  }
}


fn has_vmx() -> bool {
  let mut eax: i64 = 1;
  let mut ecx: i64 = 0;
  let mut ebx: i64 = 0;
  let mut edx: i64 = 0;


  asm!("cpuid", inout("{eax}") eax, out("{ebx}") ebx,
                 out("{ecx}") ecx, out("{edx}") edx);
  return (ecx & (1 << 5)) != 0;
}


let mut vmxon_region: [u8; 0x1000];

fn vmx_on() -> i64 {

  let cr4v = read_cr4();
  write_cr4(cr4v | (1 << 13));

  let basic = rdmsr(MSR_IA32_VMX_BASIC);
  let rev: u32 = (basic & 0x7FFFFFFF) as u32;


  let region_ptr: &u8 = &vmxon_region[0];
  let rev_ptr: &u32 = region_ptr as &u32;
  mmio_store(rev_ptr, rev);


  let region_phys: i64 = region_ptr as usize;
  asm!("vmxon (%rax)", in("{rax}") region_phys, sideeffect=true);
  return 0;
}


const MSG_BOOT: str = "[oxide-hv] long-mode entry reached\n";


fn oxide_long_mode_entry() -> i64 {


  let sp = ox_entry_stack_top_addr();
  asm!("mov %rax, %rsp", in("{rax}") sp);


  asm!("ltr %ax", in("{ax}") 0x18);


  serial_init();
  serial_puts(str_ptr(MSG_BOOT));


  if !has_vmx() {
    serial_puts(str_ptr("[oxide-hv] VT-x NOT available\n"));
    halt_forever();
  }


  let status = vmx_on();
  serial_puts(str_ptr("[oxide-hv] VMX root entered\n"));


  let launch_rc = launch_guest();
  serial_puts(str_ptr("[oxide-hv] vm-entry failed\n"));
  halt_forever();
  return 0;
}


extern fn ox_entry_stack_top_addr() -> i64;
