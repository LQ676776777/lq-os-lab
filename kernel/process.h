#ifndef _PROC_H_
#define _PROC_H_
#define MAX_USER_SYMS 512
#define MAX_USER_STRTAB (8*1024)

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
}trapframe;

typedef struct elf_symbol_t {
  uint32 name;  //符号名在字符串表中偏移量 得到名称
  uint8 info;   //符号类型
  uint8 other; 
  uint16 shndx; //节索引
  uint64 value;
  uint64 size;
} elf_symbol;

// the extremely simple definition of process, used for begining labs of PKE
typedef struct process_t {
  // pointing to the stack used in trap handling.
  uint64 kstack;
  // trapframe storing the context of a (User mode) process.
  trapframe* trapframe;
  elf_symbol symtab[MAX_USER_SYMS];
  size_t sym_count; //记录上面数组里有多少有效符号
  char strtab[MAX_USER_STRTAB]; 
  size_t strtab_sz;
}process;

#define ELF64_ST_TYPE(i) ((i) & 0xf)
#define STT_FUNC 2

void switch_to(process*);

extern process* current;

#endif
