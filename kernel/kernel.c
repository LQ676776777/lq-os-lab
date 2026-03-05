/*
 * Supervisor-mode startup codes
 */

#include "riscv.h"
#include "string.h"
#include "elf.h"
#include "process.h"
#include "config.h"

#include "spike_interface/spike_utils.h"

// one process per hart
process user_app[NCPU];

// memory address arrays, indexed by hartid
static uint64 user_stacks[] = USER_STACKS;
static uint64 user_kstacks[] = USER_KSTACKS;
static uint64 user_trap_frames[] = USER_TRAP_FRAMES;

//
// load the elf, and construct a "process" (with only a trapframe).
// load_bincode_from_host_elf is defined in elf.c
//
void load_user_program(process *proc, int hart) {
  proc->trapframe = (trapframe *)user_trap_frames[hart];
  memset(proc->trapframe, 0, sizeof(trapframe));
  proc->kstack = user_kstacks[hart];
  proc->trapframe->regs.sp = user_stacks[hart];

  // load_bincode_from_host_elf() is defined in kernel/elf.c
  load_bincode_from_host_elf(proc);
}

//
// s_start: S-mode entry point of riscv-pke OS kernel.
//
int s_start(void) {
  int hart = read_tp();
  sprint("hartid = %d: Enter supervisor mode...\n", hart);
  // Note: we use direct (i.e., Bare mode) for memory mapping in lab1.
  // which means: Virtual Address = Physical Address
  // therefore, we need to set satp to be 0 for now. we will enable paging in lab2_x.
  write_csr(satp, 0);

  // the application code (elf) is first loaded into memory, and then put into execution
  load_user_program(&user_app[hart], hart);

  sprint("hartid = %d: Switch to user mode...\n", hart);
  // switch_to() is defined in kernel/process.c
  switch_to(&user_app[hart]);

  // we should never reach here.
  return 0;
}
