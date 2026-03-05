#ifndef _CONFIG_H_
#define _CONFIG_H_

// we use two HART (cpu) in challenge3
#define NCPU 2

//interval of timer interrupt. added @lab1_3
#define TIMER_INTERVAL 1000000

#define DRAM_BASE 0x80000000

/* we use fixed physical (also logical) addresses for the stacks and trap frames as in
 Bare memory-mapping mode. For multi-core, each hart uses a separate memory region. */

// Hart0: app loaded at 0x81000000
// user stack top
#define USER_STACK_0 0x81100000
// the stack used by PKE kernel when a syscall happens
#define USER_KSTACK_0 0x81200000
// the trap frame used to assemble the user "process"
#define USER_TRAP_FRAME_0 0x81300000

// Hart1: app loaded at 0x85000000
// user stack top
#define USER_STACK_1 0x85100000
// the stack used by PKE kernel when a syscall happens
#define USER_KSTACK_1 0x85200000
// the trap frame used to assemble the user "process"
#define USER_TRAP_FRAME_1 0x85300000

// arrays for easy indexing by hartid
#define USER_STACKS       { USER_STACK_0, USER_STACK_1 }
#define USER_KSTACKS      { USER_KSTACK_0, USER_KSTACK_1 }
#define USER_TRAP_FRAMES  { USER_TRAP_FRAME_0, USER_TRAP_FRAME_1 }

#endif
