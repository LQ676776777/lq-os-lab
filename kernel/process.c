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

//Two functions defined in kernel/usertrap.S
extern char smode_trap_vector[];
extern void return_to_user(trapframe *, uint64 satp);

// trap_sec_start points to the beginning of S-mode trap segment (i.e., the entry point
// of S-mode trap vector).
extern char trap_sec_start[];

// process pool. added @lab3_1
process procs[NPROC];

// current points to the currently running user-mode application.
process* current = NULL;

//
// switch to a user-mode process
//
void switch_to(process* proc) {
  assert(proc);
  current = proc;

  // write the smode_trap_vector (64-bit func. address) defined in kernel/strap_vector.S
  // to the stvec privilege register, such that trap handler pointed by smode_trap_vector
  // will be triggered when an interrupt occurs in S mode.
  write_csr(stvec, (uint64)smode_trap_vector);

  // set up trapframe values (in process structure) that smode_trap_vector will need when
  // the process next re-enters the kernel.
  proc->trapframe->kernel_sp = proc->kstack;      // process's kernel stack
  proc->trapframe->kernel_satp = read_csr(satp);  // kernel page table
  proc->trapframe->kernel_trap = (uint64)smode_trap_handler;

  // SSTATUS_SPP and SSTATUS_SPIE are defined in kernel/riscv.h
  // set S Previous Privilege mode (the SSTATUS_SPP bit in sstatus register) to User mode.
  unsigned long x = read_csr(sstatus);
  x &= ~SSTATUS_SPP;  // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE;  // enable interrupts in user mode

  // write x back to 'sstatus' register to enable interrupts, and sret destination mode.
  write_csr(sstatus, x);

  // set S Exception Program Counter (sepc register) to the elf entry pc.
  write_csr(sepc, proc->trapframe->epc);

  // make user page table. macro MAKE_SATP is defined in kernel/riscv.h. added @lab2_1
  uint64 user_satp = MAKE_SATP(proc->pagetable);

  // return_to_user() is defined in kernel/strap_vector.S. switch to user mode with sret.
  // note, return_to_user takes two parameters @ and after lab2_1.
  return_to_user(proc->trapframe, user_satp);
}

//
// initialize process pool (the procs[] array). added @lab3_1
//
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

  // initialize files_struct
  procs[i].pfiles = init_proc_file_management();
  sprint("in alloc_proc. build proc_file_management successfully.\n");

  // return after initialization.
  return &procs[i];
}

//
// reclaim a process. added @lab3_1
//
int free_process( process* proc ) {
  // we set the status to ZOMBIE, but cannot destruct its vm space immediately.
  // since proc can be current process, and its user kernel stack is currently in use!
  // but for proxy kernel, it (memory leaking) may NOT be a really serious issue,
  // as it is different from regular OS, which needs to run 7x24.
  proc->status = ZOMBIE;

  // wake up parent if it's blocked (waiting for this child). added @lab4_challenge3
  if (proc->parent != NULL && proc->parent->status == BLOCKED) {
    proc->parent->status = READY;
    insert_to_ready_queue(proc->parent);
  }

  return 0;
}

//
// implements fork syscal in kernel. added @lab3_1
// basic idea here is to first allocate an empty process (child), then duplicate the
// context and data segments of parent process to the child, and lastly, map other
// segments (code, system) of the parent to child. the stack segment remains unchanged
// for the child.
//
int do_fork( process* parent)
{
  sprint( "will fork a child from parent %d.\n", parent->pid );
  process* child = alloc_process();

  for( int i=0; i<parent->total_mapped_region; i++ ){
    // browse parent's vm space, and copy its trapframe and data segments,
    // map its code segment.
    switch( parent->mapped_info[i].seg_type ){
      case CONTEXT_SEGMENT:
        *child->trapframe = *parent->trapframe;
        break;
      case STACK_SEGMENT:
        memcpy( (void*)lookup_pa(child->pagetable, child->mapped_info[STACK_SEGMENT].va),
          (void*)lookup_pa(parent->pagetable, parent->mapped_info[i].va), PGSIZE );
        break;
      case HEAP_SEGMENT:{
        // build a same heap for child process.

        // convert free_pages_address into a filter to skip reclaimed blocks in the heap
        // when mapping the heap blocks
        int free_block_filter[MAX_HEAP_PAGES];
        memset(free_block_filter, 0, MAX_HEAP_PAGES);
        uint64 heap_bottom = parent->user_heap.heap_bottom;
        for (int i = 0; i < parent->user_heap.free_pages_count; i++) {
          int index = (parent->user_heap.free_pages_address[i] - heap_bottom) / PGSIZE;
          free_block_filter[index] = 1;
        }

        // copy and map the heap blocks
        for (uint64 heap_block = current->user_heap.heap_bottom;
             heap_block < current->user_heap.heap_top; heap_block += PGSIZE) {
          if (free_block_filter[(heap_block - heap_bottom) / PGSIZE])  // skip free blocks
            continue;

          void* child_pa = alloc_page();
          memcpy(child_pa, (void*)lookup_pa(parent->pagetable, heap_block), PGSIZE);
          user_vm_map((pagetable_t)child->pagetable, heap_block, PGSIZE, (uint64)child_pa,
                      prot_to_type(PROT_WRITE | PROT_READ, 1));
        }

        child->mapped_info[HEAP_SEGMENT].npages = parent->mapped_info[HEAP_SEGMENT].npages;

        // copy the heap manager from parent to child
        memcpy((void*)&child->user_heap, (void*)&parent->user_heap, sizeof(parent->user_heap));
      }
        break;
      case CODE_SEGMENT:{
        // TODO (lab3_1): implment the mapping of child code segment to parent's
        // code segment.
        // hint: the virtual address mapping of code segment is tracked in mapped_info
        // page of parent's process structure. use the information in mapped_info to
        // retrieve the virtual to physical mapping of code segment.
        // after having the mapping information, just map the corresponding virtual
        // address region of child to the physical pages that actually store the code
        // segment of parent process.
        // DO NOT COPY THE PHYSICAL PAGES, JUST MAP THEM.
        // panic( "You need to implement the code segment mapping of child in lab3_1.\n" );
        // 1. 获取父进程代码段的起始虚拟地址 (va)
        uint64 code_va = parent->mapped_info[i].va;
        
        // 2. 通过父进程的页表查找该虚拟地址对应的物理地址 (pa)
        // lookup_pa 定义在 vmm.c 中
        uint64 code_pa = lookup_pa(parent->pagetable, code_va);
        
        // 3. 将父进程的物理地址映射到子进程的页表中
        // 使用 user_vm_map，权限设为可读、可执行 (user=1)
        // prot_to_type(PROT_READ | PROT_EXEC, 1) 会生成正确的 PTE 标志位
        user_vm_map(child->pagetable, code_va, PGSIZE, code_pa,
                    prot_to_type(PROT_READ | PROT_EXEC, 1));

        sprint("do_fork map code segment at pa:%lx of parent to child at va:%lx.\n", 
               code_pa, code_va);

        // after mapping, register the vm region (do not delete codes below!)
        child->mapped_info[child->total_mapped_region].va = parent->mapped_info[i].va;
        child->mapped_info[child->total_mapped_region].npages =
          parent->mapped_info[i].npages;
        child->mapped_info[child->total_mapped_region].seg_type = CODE_SEGMENT;
        child->total_mapped_region++;
      }
        break;
    }
  }

  child->status = READY;
  child->trapframe->regs.a0 = 0;
  child->parent = parent;
  insert_to_ready_queue( child );

  return child->pid;
}

//
// implements exec syscall in kernel. added @lab4_challenge3
// replaces the current process with a new ELF program, passing one argument string.
//
int do_exec(char *path, char *arg) {
  process *p = current;

  // IMPORTANT: copy path and arg to local buffers BEFORE freeing old segments,
  // because they may reside on user heap pages that will be freed below.
  char path_buf[256], arg_buf[256];
  strcpy(path_buf, path);
  if (arg != NULL && arg[0] != '\0')
    strcpy(arg_buf, arg);
  else
    arg_buf[0] = '\0';

  // 1. unmap and free old code and data segments
  for (int i = 0; i < p->total_mapped_region; i++) {
    if (p->mapped_info[i].seg_type == CODE_SEGMENT ||
        p->mapped_info[i].seg_type == DATA_SEGMENT) {
      uint64 va = p->mapped_info[i].va;
      uint64 npages = p->mapped_info[i].npages;
      user_vm_unmap((pagetable_t)p->pagetable, va, npages * PGSIZE, 1);
      p->mapped_info[i].va = 0;
      p->mapped_info[i].npages = 0;
      p->mapped_info[i].seg_type = 0;
    }
  }

  // 2. unmap and free heap pages
  for (uint64 va = p->user_heap.heap_bottom; va < p->user_heap.heap_top; va += PGSIZE) {
    int is_free = 0;
    for (int j = 0; j < p->user_heap.free_pages_count; j++) {
      if (p->user_heap.free_pages_address[j] == va) { is_free = 1; break; }
    }
    if (!is_free)
      user_vm_unmap((pagetable_t)p->pagetable, va, PGSIZE, 1);
  }

  // 3. reset heap manager
  p->user_heap.heap_top = USER_FREE_ADDRESS_START;
  p->user_heap.heap_bottom = USER_FREE_ADDRESS_START;
  p->user_heap.free_pages_count = 0;
  p->mapped_info[HEAP_SEGMENT].npages = 0;

  // 4. reset total_mapped_region to 4 (STACK, CONTEXT, SYSTEM, HEAP)
  p->total_mapped_region = 4;

  // 5. reset user stack and registers
  uint64 stack_pa = lookup_pa(p->pagetable, p->mapped_info[STACK_SEGMENT].va);
  memset((void *)stack_pa, 0, PGSIZE);
  memset(&(p->trapframe->regs), 0, sizeof(p->trapframe->regs));
  p->trapframe->regs.sp = USER_STACK_TOP;

  // 6. reset file management: close all opened files and reset count
  for (int fd = 0; fd < MAX_FILES; fd++)
    p->pfiles->opened_files[fd].status = FD_NONE;
  p->pfiles->nfiles = 0;

  // 7. load new ELF from VFS
  load_bincode_from_host_elf(p, path_buf);

  // 8. pass argument to new program via user stack
  // The new program's main(argc, argv) expects:
  //   a0 = argc, a1 = argv (pointer to array of char*)
  // We place the arg string, argv[0] pointer, and argv array on the user stack.
  if (arg_buf[0] != '\0') {
    uint64 sp = p->trapframe->regs.sp;

    // copy the argument string to the stack
    int arg_len = strlen(arg_buf) + 1; // including null terminator
    sp -= arg_len;
    sp &= ~0x7ULL; // align to 8 bytes
    uint64 arg_str_va = sp;
    // copy arg string to user stack (physical address)
    char *arg_str_pa = (char *)user_va_to_pa(p->pagetable, (void *)arg_str_va);
    strcpy(arg_str_pa, arg_buf);

    // place argv[0] = pointer to the arg string
    sp -= sizeof(uint64);
    uint64 argv_va = sp;
    *(uint64 *)user_va_to_pa(p->pagetable, (void *)argv_va) = arg_str_va;

    // align sp to 16 bytes (RISC-V calling convention)
    sp &= ~0xFULL;
    p->trapframe->regs.sp = sp;

    // set argc = 1, argv = pointer to argv array
    p->trapframe->regs.a0 = 1;
    p->trapframe->regs.a1 = argv_va;
  }

  return 0;
}

//
// implements wait syscall in kernel. added @lab4_challenge3
// parent process waits for a child process to exit.
//
int do_wait(int pid) {
  // find the child process
  process *child = &procs[pid];

  // block the parent until the child finishes
  // the child's free_process will wake us up by inserting us to ready queue
  current->status = BLOCKED;
  schedule();

  // child has exited, mark it as FREE for reuse
  child->status = FREE;

  return 0;
}
