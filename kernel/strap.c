/*
 * Utility functions for trap handling in Supervisor mode.
 */

#include "riscv.h"
#include "process.h"
#include "strap.h"
#include "syscall.h"
#include "config.h"

#include "spike_interface/spike_utils.h"

//
// handling the syscalls. will call do_syscall() defined in kernel/syscall.c
//
static void handle_syscall(trapframe *tf) {
  // tf->epc points to the address that our computer will jump to after the trap handling.
  // for a syscall, we should return to the NEXT instruction after its handling.
  // in RV64G, each instruction occupies exactly 32 bits (i.e., 4 Bytes)
  tf->epc += 4;

  // handle the syscall and return value to user app
  long ret = do_syscall(tf->regs.a0, tf->regs.a1, tf->regs.a2, tf->regs.a3,
                          tf->regs.a4, tf->regs.a5, tf->regs.a6, tf->regs.a7);
  tf->regs.a0 = ret;
}

//
// per-hart ticks counter. added @lab1_3, modified for multi-core
static uint64 g_ticks[NCPU] = {0};
//
// added @lab1_3
//
void handle_mtimer_trap() {
  int hart = read_tp();
  sprint("Ticks %d\n", g_ticks[hart]);
  g_ticks[hart]++;
  // clear the SIP field in sip register
  write_csr(sip, read_csr(sip) & ~SIP_SSIP);
}

//
// kernel/smode_trap.S will pass control to smode_trap_handler, when a trap happens
// in S-mode.
//
void smode_trap_handler(void) {
  // make sure we are in User mode before entering the trap handling.
  if ((read_csr(sstatus) & SSTATUS_SPP) != 0) panic("usertrap: not from user mode");

  int hart = read_tp();
  assert(current[hart]);
  // save user process counter.
  current[hart]->trapframe->epc = read_csr(sepc);

  // if the cause of trap is syscall from user application.
  uint64 cause = read_csr(scause);

  if (cause == CAUSE_USER_ECALL) {
    handle_syscall(current[hart]->trapframe);
  } else if (cause == CAUSE_MTIMER_S_TRAP) {
    handle_mtimer_trap();
  } else {
    sprint("smode_trap_handler(): unexpected scause %p\n", read_csr(scause));
    sprint("            sepc=%p stval=%p\n", read_csr(sepc), read_csr(stval));
    panic( "unexpected exception happened.\n" );
  }

  // continue (come back to) the execution of current process on this hart.
  switch_to(current[hart]);
}
