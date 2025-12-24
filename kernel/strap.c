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

static void handle_syscall(trapframe *tf) {

  tf->epc += 4;
  long ret = do_syscall(tf->regs.a0, tf->regs.a1, tf->regs.a2, tf->regs.a3,
                          tf->regs.a4, tf->regs.a5, tf->regs.a6, tf->regs.a7);
  tf->regs.a0 = ret; 
}

static uint64 g_ticks = 0;

void handle_mtimer_trap() {
  sprint("Ticks %d\n", g_ticks);
  g_ticks++; 
  // 重置 SIP 寄存器的 SSIP 位，使用异或操作或取反与操作均可
  write_csr(sip, read_csr(sip) & ~SIP_SSIP);
}

void handle_user_page_fault(uint64 mcause, uint64 sepc, uint64 stval) {

  sprint("handle_page_fault: %lx\n", stval);
  switch (mcause) {
    case CAUSE_STORE_PAGE_FAULT: {
      void *pa = alloc_page();
      if (pa == 0) panic("Out of memory during stack expansion!");
      memset(pa, 0, PGSIZE);
      uint64 va = ROUNDDOWN(stval, PGSIZE);
      user_vm_map((pagetable_t)current->pagetable, va, PGSIZE, (uint64)pa,
             prot_to_type(PROT_WRITE | PROT_READ, 1));
      break;
    }
    default:
      sprint("unknown page fault.\n");
      break;
  }
}


void rrsched() {
  // 增加 tick 计数
  current->tick_count++;

  // 如果时间片还没用完，直接返回继续执行
  if (current->tick_count < TIME_SLICE_LEN) {
      return;
  }

  // 时间片耗尽，重置计数器
  current->tick_count = 0;
  
  // 更改进程状态并放入就绪队列尾部
  current->status = READY;
  insert_to_ready_queue(current);
  
  // 执行调度
  schedule();
}

void smode_trap_handler(void) {

  if ((read_csr(sstatus) & SSTATUS_SPP) != 0) panic("usertrap: not from user mode");

  assert(current);
  current->trapframe->epc = read_csr(sepc);
  uint64 cause = read_csr(scause);

  switch (cause) {
    case CAUSE_USER_ECALL:
      handle_syscall(current->trapframe);
      break;
    case CAUSE_MTIMER_S_TRAP:
      handle_mtimer_trap();
      // 调用时间片轮转调度
      rrsched();
      break;
    case CAUSE_STORE_PAGE_FAULT:
    case CAUSE_LOAD_PAGE_FAULT:
      handle_user_page_fault(cause, read_csr(sepc), read_csr(stval));
      break;
    default:
      sprint("smode_trap_handler(): unexpected scause %p\n", read_csr(scause));
      sprint("            sepc=%p stval=%p\n", read_csr(sepc), read_csr(stval));
      panic( "unexpected exception happened.\n" );
      break;
  }
  switch_to(current);
}