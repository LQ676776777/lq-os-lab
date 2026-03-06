#ifndef _PROC_H_
#define _PROC_H_

#include "riscv.h"

typedef struct trapframe_t {
  // space to store context (all common registers)
  /* offset:0   */ riscv_regs regs;

  // process's "user kernel" stack
  /* offset:248 */ uint64 kernel_sp;
  // pointer to smode_trap_handler
  /* offset:256 */ uint64 kernel_trap;
  // saved user process counter
  /* offset:264 */ uint64 epc;

  // kernel page table. added @lab2_1
  /* offset:272 */ uint64 kernel_satp;
}trapframe;

// memory control block for heap management. added @lab2_challenge2
typedef struct malloc_control_block {
  uint64 size;      // size of usable data area (excluding this header)
  uint64 is_free;   // 1=free, 0=allocated
  uint64 va;        // VA of this MCB header in user space
  struct malloc_control_block *next;  // next MCB (kernel PA pointer)
} mcb;

#define MCB_SIZE (sizeof(mcb))

// the extremely simple definition of process, used for begining labs of PKE
typedef struct process_t {
  // pointing to the stack used in trap handling.
  uint64 kstack;
  // user page table
  pagetable_t pagetable;
  // trapframe storing the context of a (User mode) process.
  trapframe* trapframe;

  // heap management fields. added @lab2_challenge2
  uint64 heap_top;    // next virtual address for heap page allocation
  mcb *heap_mcb;      // head of MCB linked list (kernel/physical address)
}process;

// switch to run user app
void switch_to(process*);

// current running process
extern process* current;

// address of the first free page in our simple heap. added @lab2_2
extern uint64 g_ufree_page;

#endif
