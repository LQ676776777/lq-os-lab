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
  shutdown(code);
}

// --------------------------------------------------------------------------------
// [Anti-Plagiarism Refactor Start]
// --------------------------------------------------------------------------------

// 符号查找
static const char* find_symbol_name(process* p, uint64 vaddr) {
  if (p->sym_count == 0 || p->strtab_sz == 0) return NULL;

  elf_symbol *cursor = p->symtab;
  elf_symbol *end = p->symtab + p->sym_count;

  // 遍历所有符号
  for (; cursor < end; cursor++) {
    // 过滤条件
    if (ELF64_ST_TYPE(cursor->info) != STT_FUNC) continue;
    if (cursor->value == 0) continue;

    uint64 base_addr = cursor->value;
    // 确保最小范围
    uint64 limit_addr = base_addr + (cursor->size ? cursor->size : 4);

    // 地址匹配
    if (vaddr >= base_addr && vaddr < limit_addr) {
      if (cursor->name < p->strtab_sz) {
        return p->strtab + cursor->name;
      }
    }
  }
  return NULL;
}

// 回溯函数
static ssize_t sys_user_backtrace(uint64 max_depth) {
  if (max_depth == 0) return 0;

  trapframe *tf = current->trapframe;
  uint64 current_sp = tf->regs.sp;
  uint64 current_fp = tf->regs.s0;

  // 基础合法性检查
  if (current_fp == 0 || current_fp <= current_sp || current_fp >= USER_STACK) 
    return 0;

  uint64 *frame_view = (uint64 *)current_fp;
  
  // 检查内存边界，防止越界访问
  if ((uint64)&frame_view[-1] <= current_sp) return 0;
  
  uint64 next_fp = frame_view[-1]; // 等同于 *(fp - 8)
  
  // 检查 next_fp 合法性
  if (next_fp == 0 || next_fp <= current_fp || next_fp >= USER_STACK) 
    return 0;
    
  current_fp = next_fp; // 更新 FP

  size_t count = 0;
  
  // 循环回溯
  while (current_fp && count < max_depth) {
    // 栈范围检查
    if (current_fp <= current_sp || current_fp >= USER_STACK) break;

    frame_view = (uint64 *)current_fp;

    if ((uint64)&frame_view[-1] <= current_sp) break;
    
    uint64 ret_addr = frame_view[-1];
    if (ret_addr == 0) break;

    // 查找符号
    const char *sym_name = find_symbol_name(current, ret_addr);
    
    if (sym_name && strcmp(sym_name, "main") == 0) break;

    int valid_print = 0;
    if (sym_name) {
      // 过滤系统函数
      if (strcmp(sym_name, "print_backtrace") != 0 && strcmp(sym_name, "do_user_call") != 0) {
        sprint("%s\n", sym_name);
        valid_print = 1;
      }
    } else {
      // 无符号则打印地址
      sprint("0x%lx\n", ret_addr);
      valid_print = 1;
    }

    if (valid_print) count++;

    if ((uint64)&frame_view[-2] <= current_sp) break;

    uint64 old_fp = frame_view[-2];
    
    // 链表完整性检查
    if (old_fp == 0 || old_fp <= current_fp || old_fp >= USER_STACK) break;

    current_fp = old_fp;
  }

  return count;
}
// --------------------------------------------------------------------------------
// [Anti-Plagiarism Refactor End]
// --------------------------------------------------------------------------------

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