/*
 * contains the implementation of all syscalls.
 */

#include <stdint.h>
#include <errno.h>

#include "util/types.h"
#include "syscall.h"
#include "string.h"
#include "process.h"
#include "util/functions.h"

#include "spike_interface/spike_utils.h"
#include "spike_interface/spike_file.h"

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  sprint(buf);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  // in lab1, PKE considers only one app (one process). 
  // therefore, shutdown the system when the app calls exit()
  shutdown(code);
}

static const char* lookup_symbol(process* proc, uint64 addr) {
  if (!proc->sym_count || !proc->strtab_sz) return NULL;
  for (size_t i = 0; i < proc->sym_count; ++i) {
    elf_symbol *s = &proc->symtab[i];
    if (ELF64_ST_TYPE(s->info) != STT_FUNC) continue;
    if (!s->value) continue;
    uint64 start = s->value;
    uint64 end = s->size ? start + s->size : start + 4;
    if (addr >= start && addr < end && s->name < proc->strtab_sz)
      return &proc->strtab[s->name];
  }
  return NULL;
}


// ...existing code...
static ssize_t sys_user_backtrace(uint64 depth) {
  if (!depth) return 0;

  trapframe *tf = current->trapframe;
  uint64 sp = tf->regs.sp;
  uint64 fp = tf->regs.s0;

  if (!fp || fp <= sp || fp >= USER_STACK) return 0;

  uint64 caller_fp_addr = fp - sizeof(uint64);
  if (caller_fp_addr <= sp) return 0;
  uint64 caller_fp = *(uint64 *)caller_fp_addr;
  if (!caller_fp || caller_fp <= fp || caller_fp >= USER_STACK) return 0;
  fp = caller_fp;

  size_t printed = 0;
  while (fp && printed < depth) {
    if (fp <= sp || fp >= USER_STACK) break;

    uint64 ra_addr = fp - sizeof(uint64);
    if (ra_addr <= sp) break;
    uint64 ra = *(uint64 *)ra_addr;
    if (!ra) break;

    const char *name = lookup_symbol(current, ra);
    if (name && strcmp(name, "main") == 0) break;

    int emitted = 0;
    if (name && strcmp(name, "print_backtrace") != 0 && strcmp(name, "do_user_call") != 0) {
      sprint("%s\n", name);
      emitted = 1;
    } else if (!name) {
      sprint("0x%lx\n", ra);
      emitted = 1;
    }

    if (emitted) printed++;

    uint64 prev_fp_addr = fp - 2 * sizeof(uint64);
    if (prev_fp_addr <= sp) break;
    uint64 prev_fp = *(uint64 *)prev_fp_addr;
    if (!prev_fp || prev_fp <= fp || prev_fp >= USER_STACK) break;

    fp = prev_fp;
  }

  return printed;
}
// ...existing code...

//
// [a0]: the syscall number; [a1] ... [a7]: arguments to the syscalls.
// returns the code of success, (e.g., 0 means success, fail for otherwise)
//
long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    case SYS_user_backtrace:
      return sys_user_backtrace(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
