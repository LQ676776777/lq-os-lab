/*
 * Utility functions for trap handling in Supervisor mode.
 */

#include "riscv.h"
#include "process.h"
#include "strap.h"
#include "syscall.h"
#include "pmm.h"
#include "vmm.h"
#include "sched.h"
#include "util/functions.h"
#include "util/string.h"

#include "spike_interface/spike_utils.h"

//
// handling the syscalls. will call do_syscall() defined in kernel/syscall.c
//
static void handle_syscall(trapframe *tf) {
  // tf->epc points to the address that our computer will jump to after the trap handling.
  // for a syscall, we should return to the NEXT instruction after its handling.
  // in RV64G, each instruction occupies exactly 32 bits (i.e., 4 Bytes)
  tf->epc += 4;

  // TODO (lab1_1): remove the panic call below, and call do_syscall (defined in
  // kernel/syscall.c) to conduct real operations of the kernel side for a syscall.
  // IMPORTANT: return value should be returned to user app, or else, you will encounter
  // problems in later experiments!
  // panic( "call do_syscall to accomplish the syscall and lab1_1 here.\n" );
  // 处理系统调用参数并获取返回值
  long ret = do_syscall(tf->regs.a0, tf->regs.a1, tf->regs.a2, tf->regs.a3,
                          tf->regs.a4, tf->regs.a5, tf->regs.a6, tf->regs.a7);
  tf->regs.a0 = ret; // 返回值写回 a0

}

//
// global variable that store the recorded "ticks". added @lab1_3
static uint64 g_ticks = 0;
//
// added @lab1_3
//
void handle_mtimer_trap() {
  sprint("Ticks %d\n", g_ticks);
  // TODO (lab1_3): increase g_ticks to record this "tick", and then clear the "SIP"
  // field in sip register.
  // hint: use write_csr to disable the SIP_SSIP bit in sip.
  // panic( "lab1_3: increase g_ticks by one, and clear SIP field in sip register.\n" );
  g_ticks++; 
    // 清除 SIP 寄存器的 SSIP 位
  write_csr(sip, read_csr(sip) & ~SIP_SSIP);

}

//
// the page fault handler. added @lab2_3. parameters:
// sepc: the pc when fault happens;
// stval: the virtual address that causes pagefault when being accessed.
//
void handle_user_page_fault(uint64 mcause, uint64 sepc, uint64 stval) {
  sprint("handle_page_fault: %lx\n", stval);
  switch (mcause) {
    case CAUSE_STORE_PAGE_FAULT:
      {
      // check if this is a COW page fault. added @lab3_challenge3
      uint64 va = ROUNDDOWN(stval, PGSIZE);
      pte_t *pte = page_walk((pagetable_t)current->pagetable, va, 0);

      if (pte != 0 && (*pte & PTE_V) && (*pte & PTE_COW)) {
        // This is a COW page fault: allocate new page, copy old data, remap with write perm
        uint64 old_pa = PTE2PA(*pte);
        void *new_pa = alloc_page();
        memcpy(new_pa, (void *)old_pa, PGSIZE);

        // update PTE: point to new page, add write perm, remove COW flag
        *pte = PA2PTE(new_pa) | PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D;

        // flush TLB to make the new mapping effective
        flush_tlb();
      } else {
        // regular stack expansion page fault
        void *pa = alloc_page();
        if (pa == 0) {
            panic("Out of memory during stack expansion!");
        }
        memset(pa, 0, PGSIZE);
        user_vm_map((pagetable_t)current->pagetable, va, PGSIZE, (uint64)pa,
               prot_to_type(PROT_WRITE | PROT_READ, 1));
      }
      break;
      }
    default:
      sprint("unknown page fault.\n");
      break;
  }
}

//
// implements round-robin scheduling. added @lab3_3
//
void rrsched() {
  // TODO (lab3_3): implements round-robin scheduling.
  // hint: increase the tick_count member of current process by one, if it is bigger than
  // TIME_SLICE_LEN (means it has consumed its time slice), change its status into READY,
  // place it in the rear of ready queue, and finally schedule next process to run.
  // panic( "You need to further implement the timer handling in lab3_3.\n" );
  //先进行tick_count++，如果判断为假，直接返回
  current->tick_count++;
  if(current->tick_count>=TIME_SLICE_LEN)
  {
    current->tick_count = 0;
    current->status = READY;  // 修改状态为就绪
    insert_to_ready_queue(current);  // 放入就绪队列末尾
    schedule();  // 调度下一个进程
  }

}

//
// kernel/smode_trap.S will pass control to smode_trap_handler, when a trap happens
// in S-mode.
//
void smode_trap_handler(void) {
  // make sure we are in User mode before entering the trap handling.
  // we will consider other previous case in lab1_3 (interrupt).
  if ((read_csr(sstatus) & SSTATUS_SPP) != 0) panic("usertrap: not from user mode");

  assert(current);
  // save user process counter.
  current->trapframe->epc = read_csr(sepc);

  // if the cause of trap is syscall from user application.
  // read_csr() and CAUSE_USER_ECALL are macros defined in kernel/riscv.h
  uint64 cause = read_csr(scause);

  // use switch-case instead of if-else, as there are many cases since lab2_3.
  switch (cause) {
    case CAUSE_USER_ECALL:
      handle_syscall(current->trapframe);
      break;
    case CAUSE_MTIMER_S_TRAP:
      handle_mtimer_trap();
      // invoke round-robin scheduler. added @lab3_3
      rrsched();
      break;
    case CAUSE_STORE_PAGE_FAULT:
    case CAUSE_LOAD_PAGE_FAULT:
      // the address of missing page is stored in stval
      // call handle_user_page_fault to process page faults
      handle_user_page_fault(cause, read_csr(sepc), read_csr(stval));
      break;
    default:
      sprint("smode_trap_handler(): unexpected scause %p\n", read_csr(scause));
      sprint("            sepc=%p stval=%p\n", read_csr(sepc), read_csr(stval));
      panic( "unexpected exception happened.\n" );
      break;
  }

  // continue (come back to) the execution of current process.
  switch_to(current);
}
