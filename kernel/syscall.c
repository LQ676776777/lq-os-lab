/*
 * contains the implementation of all syscalls.
 */

#include <stdint.h>
#include <errno.h>

#include "riscv.h"
#include "util/types.h"
#include "syscall.h"
#include "string.h"
#include "process.h"
#include "util/functions.h"
#include "pmm.h"
#include "vmm.h"
#include "sched.h"
#include "spike_interface/spike_utils.h"

extern process procs[NPROC];

ssize_t sys_user_print(const char* buf, size_t n) {
  assert( current );
  char* pa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)buf);
  sprint(pa);
  return 0;
}

ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  free_process(current);
  wake_up(current);
  schedule();
  return 0;
} 

uint64 sys_user_allocate_page() {

  void* pa = alloc_page();
  uint64 va;
  if (current->user_heap.free_pages_count > 0) {
    va =  current->user_heap.free_pages_address[--current->user_heap.free_pages_count];
    assert(va < current->user_heap.heap_top);
  } else {
    va = current->user_heap.heap_top;
    current->user_heap.heap_top += PGSIZE;
    current->mapped_info[HEAP_SEGMENT].npages++;
  }
  user_vm_map((pagetable_t)current->pagetable, va, PGSIZE, (uint64)pa,
         prot_to_type(PROT_WRITE | PROT_READ, 1));
  return va;
}

uint64 sys_user_free_page(uint64 va) {

  user_vm_unmap((pagetable_t)current->pagetable, va, PGSIZE, 1);
  current->user_heap.free_pages_address[current->user_heap.free_pages_count++] = va;
  return 0;
}

ssize_t sys_user_fork() {
  sprint("User call fork.\n");
  return do_fork( current );
}


ssize_t sys_user_yield() {
  // 主动让出 CPU
  // 1. 设置状态为就绪
  current->status = READY;
  // 2. 加入就绪队列
  insert_to_ready_queue(current);
  // 3. 触发调度
  schedule();
  return 0;
}

// 这里的逻辑配合 process.c 中的 do_wait 实现
ssize_t sys_user_wait(uint64 pid) {
  int result;
  
  // 循环等待，直到子进程退出
  while (1) {
      result = do_wait(pid);
      
      // 如果返回值不为0，说明有结果了（找到了僵尸子进程并回收，或者出错）
      if (result != 0) {
          return result;
      }
      
      // result 为 0，说明子进程还在运行，父进程需要阻塞等待
      insert_to_wait_queue(current);
      schedule();
  }
}

long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    case SYS_user_allocate_page:
      return sys_user_allocate_page();
    case SYS_user_free_page:
      return sys_user_free_page(a1);
    case SYS_user_fork:
      return sys_user_fork();
    case SYS_user_yield:
      return sys_user_yield();
    case SYS_user_wait:
      return sys_user_wait(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}