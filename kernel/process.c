/*
 * Utility functions for process management. 
 *
 * Note: in Lab1, only one process (i.e., our user application) exists. Therefore, 
 * PKE OS at this stage will set "current" to the loaded user application, and also
 * switch to the old "current" process after trap handling.
 */

#include "riscv.h"
#include "strap.h"
#include "config.h"
#include "process.h"
#include "elf.h"
#include "string.h"
#include "vmm.h"
#include "pmm.h"
#include "memlayout.h"
#include "sched.h"
#include "spike_interface/spike_utils.h"


extern char smode_trap_vector[];
extern void return_to_user(trapframe *, uint64 satp);
extern char trap_sec_start[];
process procs[NPROC];
process* current = NULL;

void switch_to(process* proc) {

  assert(proc);
  current = proc;
  write_csr(stvec, (uint64)smode_trap_vector);
  proc->trapframe->kernel_sp = proc->kstack;      
  proc->trapframe->kernel_satp = read_csr(satp);  
  proc->trapframe->kernel_trap = (uint64)smode_trap_handler;
  unsigned long x = read_csr(sstatus);
  x &= ~SSTATUS_SPP;  
  x |= SSTATUS_SPIE;  
  write_csr(sstatus, x);
  write_csr(sepc, proc->trapframe->epc);
  uint64 user_satp = MAKE_SATP(proc->pagetable);
  return_to_user(proc->trapframe, user_satp);
}

void init_proc_pool() {

  memset( procs, 0, sizeof(process)*NPROC );
  for (int i = 0; i < NPROC; ++i) {
    procs[i].status = FREE;
    procs[i].pid = i;
  }
}

//
// allocate an empty process, init its vm space. returns the pointer to
// process strcuture. added @lab3_1
//
process* alloc_process() {
  // locate the first usable process structure
  int i;

  for( i=0; i<NPROC; i++ )
    if( procs[i].status == FREE ) break;

  if( i>=NPROC ){
    panic( "cannot find any free process structure.\n" );
    return 0;
  }

  // init proc[i]'s vm space
  procs[i].trapframe = (trapframe *)alloc_page();  //trapframe, used to save context
  memset(procs[i].trapframe, 0, sizeof(trapframe));

  // page directory
  procs[i].pagetable = (pagetable_t)alloc_page();
  memset((void *)procs[i].pagetable, 0, PGSIZE);

  procs[i].kstack = (uint64)alloc_page() + PGSIZE;   //user kernel stack top
  uint64 user_stack = (uint64)alloc_page();       //phisical address of user stack bottom
  procs[i].trapframe->regs.sp = USER_STACK_TOP;  //virtual address of user stack top

  // allocates a page to record memory regions (segments)
  procs[i].mapped_info = (mapped_region*)alloc_page();
  memset( procs[i].mapped_info, 0, PGSIZE );

  // map user stack in userspace
  user_vm_map((pagetable_t)procs[i].pagetable, USER_STACK_TOP - PGSIZE, PGSIZE,
    user_stack, prot_to_type(PROT_WRITE | PROT_READ, 1));
  procs[i].mapped_info[STACK_SEGMENT].va = USER_STACK_TOP - PGSIZE;
  procs[i].mapped_info[STACK_SEGMENT].npages = 1;
  procs[i].mapped_info[STACK_SEGMENT].seg_type = STACK_SEGMENT;

  // map trapframe in user space (direct mapping as in kernel space).
  user_vm_map((pagetable_t)procs[i].pagetable, (uint64)procs[i].trapframe, PGSIZE,
    (uint64)procs[i].trapframe, prot_to_type(PROT_WRITE | PROT_READ, 0));
  procs[i].mapped_info[CONTEXT_SEGMENT].va = (uint64)procs[i].trapframe;
  procs[i].mapped_info[CONTEXT_SEGMENT].npages = 1;
  procs[i].mapped_info[CONTEXT_SEGMENT].seg_type = CONTEXT_SEGMENT;

  // map S-mode trap vector section in user space (direct mapping as in kernel space)
  // we assume that the size of usertrap.S is smaller than a page.
  user_vm_map((pagetable_t)procs[i].pagetable, (uint64)trap_sec_start, PGSIZE,
    (uint64)trap_sec_start, prot_to_type(PROT_READ | PROT_EXEC, 0));
  procs[i].mapped_info[SYSTEM_SEGMENT].va = (uint64)trap_sec_start;
  procs[i].mapped_info[SYSTEM_SEGMENT].npages = 1;
  procs[i].mapped_info[SYSTEM_SEGMENT].seg_type = SYSTEM_SEGMENT;

  sprint("in alloc_proc. user frame 0x%lx, user stack 0x%lx, user kstack 0x%lx \n",
    procs[i].trapframe, procs[i].trapframe->regs.sp, procs[i].kstack);

  // initialize the process's heap manager
  procs[i].user_heap.heap_top = USER_FREE_ADDRESS_START;
  procs[i].user_heap.heap_bottom = USER_FREE_ADDRESS_START;
  procs[i].user_heap.free_pages_count = 0;

  // map user heap in userspace
  procs[i].mapped_info[HEAP_SEGMENT].va = USER_FREE_ADDRESS_START;
  procs[i].mapped_info[HEAP_SEGMENT].npages = 0;  // no pages are mapped to heap yet.
  procs[i].mapped_info[HEAP_SEGMENT].seg_type = HEAP_SEGMENT;

  procs[i].total_mapped_region = 4;

  // return after initialization.
  return &procs[i];
}


int free_process( process* proc ) {

  proc->status = ZOMBIE;
  return 0;
}


int do_wait(int pid) {
    int target_pid = -1; // 默认返回值

    // 遍历所有进程寻找子进程
    for (int i = 0; i < NPROC; i++) {
        // 如果不是当前进程的子进程，或者是未使用的槽位，直接跳过
        if (procs[i].parent != current || procs[i].status == FREE) {
            continue;
        }

        // 如果指定了特定 PID 且不匹配，跳过
        if (pid != -1 && procs[i].pid != pid) {
            continue;
        }

        // 找到了符合条件的子进程
        
        // 只有当它是 ZOMBIE 时我们才回收
        if (procs[i].status == ZOMBIE) {
            target_pid = procs[i].pid;
            procs[i].status = FREE;
            procs[i].parent = NULL; // 断绝父子关系
            return target_pid;     // 成功回收，返回 PID
        } else {
            // 找到了子进程但它还没死，标记一下我们至少找到了一个
            if (target_pid == -1) target_pid = 0; 
        }
    }
    
    return target_pid;
}

int do_fork(process *parent) {
  sprint("will fork a child from parent %d.\n", parent->pid);
  process *child = alloc_process();

  for (int i = 0; i < parent->total_mapped_region; i++) {
    // 提取段信息，使代码更易读
    mapped_region *region = &parent->mapped_info[i];
    
    switch (region->seg_type) {
    case CONTEXT_SEGMENT:
      *child->trapframe = *parent->trapframe;
      break;
      
    case STACK_SEGMENT:
      memcpy((void *)lookup_pa(child->pagetable, child->mapped_info[STACK_SEGMENT].va),
             (void *)lookup_pa(parent->pagetable, region->va), PGSIZE);
      break;
      
    case HEAP_SEGMENT: {

        int free_block_filter[MAX_HEAP_PAGES];
        memset(free_block_filter, 0, MAX_HEAP_PAGES);
        uint64 heap_bottom = parent->user_heap.heap_bottom;
        for (int k = 0; k < parent->user_heap.free_pages_count; k++) {
          int index = (parent->user_heap.free_pages_address[k] - heap_bottom) / PGSIZE;
          free_block_filter[index] = 1;
        }
        for (uint64 heap_block = current->user_heap.heap_bottom;
             heap_block < current->user_heap.heap_top; heap_block += PGSIZE) {
          if (free_block_filter[(heap_block - heap_bottom) / PGSIZE]) continue;
          void *child_pa = alloc_page();
          memcpy(child_pa, (void *)lookup_pa(parent->pagetable, heap_block), PGSIZE);
          user_vm_map((pagetable_t)child->pagetable, heap_block, PGSIZE, (uint64)child_pa,
                      prot_to_type(PROT_WRITE | PROT_READ, 1));
        }
        child->mapped_info[HEAP_SEGMENT].npages = region->npages;
        memcpy((void *)&child->user_heap, (void *)&parent->user_heap, sizeof(parent->user_heap));
        break;
    }
    
    case CODE_SEGMENT: {
    
        uint64 vaddr = region->va;
        // 直接从父进程页表查找物理地址
        uint64 paddr = lookup_pa(parent->pagetable, vaddr);
        
        // 映射到子进程，权限设为 R|X (以及User位)
        map_pages(child->pagetable, vaddr, PGSIZE, paddr, prot_to_type(PROT_EXEC | PROT_READ, 1));

        // 注册映射信息
        int idx = child->total_mapped_region;
        child->mapped_info[idx].va = vaddr;
        child->mapped_info[idx].npages = region->npages;
        child->mapped_info[idx].seg_type = CODE_SEGMENT;
        child->total_mapped_region++;
        break;
    }
    
    case DATA_SEGMENT: {
 
        uint64 start_va = region->va;
        uint32 page_cnt = region->npages;
        
        for (int page_idx = 0; page_idx < page_cnt; page_idx++) {
            uint64 current_va = start_va + page_idx * PGSIZE;
            
            // 1. 分配新页
            void* new_page_pa = alloc_page();
            // 2. 找到父进程该页的物理地址
            void* parent_page_pa = (void*)lookup_pa(parent->pagetable, current_va);
            
            // 3. 复制数据
            memcpy(new_page_pa, parent_page_pa, PGSIZE);
            
            // 4. 建立映射
            user_vm_map(child->pagetable, current_va, PGSIZE, (uint64)new_page_pa, 
                        prot_to_type(PROT_WRITE | PROT_READ, 1));
        }

        // 更新子进程的 mapped_info
        int idx = child->total_mapped_region;
        child->mapped_info[idx].va = start_va;
        child->mapped_info[idx].npages = page_cnt;
        child->mapped_info[idx].seg_type = DATA_SEGMENT;
        child->total_mapped_region++;
        break;
    }
    }
  }

  child->status = READY;
  child->trapframe->regs.a0 = 0;
  child->parent = parent;
  insert_to_ready_queue(child);

  return child->pid;
}
