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
#include "pmm.h"
#include "vmm.h"
#include "memlayout.h"
#include "util/string.h"
#include "spike_interface/spike_utils.h"

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  assert( current );
  char* pa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)buf);
  sprint(pa);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  shutdown(code);
}

//
// helper: round up size to 8-byte alignment
//
static uint64 align8(uint64 size) {
  return (size + 7) & ~7;
}

//
// helper: get kernel-accessible MCB pointer from a user VA
//
static mcb* va_to_mcb(uint64 uva) {
  return (mcb*)(uint64)user_va_to_pa((pagetable_t)current->pagetable, (void*)uva);
}

//
// helper: allocate and map pages to extend the heap
//
static void extend_heap(uint64 pages) {
  for (uint64 i = 0; i < pages; i++) {
    void *pa = alloc_page();
    if (pa == 0) panic("better_malloc: out of memory\n");
    memset(pa, 0, PGSIZE);
    user_vm_map((pagetable_t)current->pagetable, current->heap_top, PGSIZE,
           (uint64)pa, prot_to_type(PROT_WRITE | PROT_READ, 1));
    current->heap_top += PGSIZE;
  }
}

//
// helper: split a block if there is enough remaining space after alloc_size
//
static void try_split(mcb *block, uint64 alloc_size) {
  if (block->size >= alloc_size + MCB_SIZE + 8) {
    uint64 new_va = block->va + MCB_SIZE + alloc_size;
    mcb *new_block = va_to_mcb(new_va);
    new_block->size = block->size - alloc_size - MCB_SIZE;
    new_block->is_free = 1;
    new_block->va = new_va;
    new_block->next = block->next;
    block->next = new_block;
    block->size = alloc_size;
  }
}

//
// better_malloc: MCB linked list with first-fit, cross-page support.
//
uint64 sys_user_allocate_page(int size) {
  assert(current);
  uint64 alloc_size = align8(size);

  // 1. Walk MCB list for first-fit; also track last block
  mcb *cur = current->heap_mcb;
  mcb *last = NULL;
  while (cur) {
    if (cur->is_free && cur->size >= alloc_size) {
      try_split(cur, alloc_size);
      cur->is_free = 0;
      return cur->va + MCB_SIZE;
    }
    last = cur;
    cur = cur->next;
  }

  // 2. If the last block is free and at the heap boundary, extend it
  if (last && last->is_free) {
    uint64 end_va = last->va + MCB_SIZE + last->size;
    if (end_va == current->heap_top) {
      uint64 deficit = alloc_size - last->size;
      uint64 pages_needed = (deficit + PGSIZE - 1) / PGSIZE;
      extend_heap(pages_needed);
      last->size += pages_needed * PGSIZE;
      try_split(last, alloc_size);
      last->is_free = 0;
      return last->va + MCB_SIZE;
    }
  }

  // 3. Allocate new page(s) from scratch
  uint64 total_needed = MCB_SIZE + alloc_size;
  uint64 pages_needed = (total_needed + PGSIZE - 1) / PGSIZE;
  uint64 va_base = current->heap_top;
  extend_heap(pages_needed);

  mcb *new_mcb = va_to_mcb(va_base);
  new_mcb->size = alloc_size;
  new_mcb->is_free = 0;
  new_mcb->va = va_base;
  new_mcb->next = NULL;

  // Create free MCB for remaining space
  uint64 total_space = pages_needed * PGSIZE;
  uint64 remaining = total_space - MCB_SIZE - alloc_size;
  if (remaining > MCB_SIZE + 8) {
    uint64 free_va = va_base + MCB_SIZE + alloc_size;
    mcb *free_block = va_to_mcb(free_va);
    free_block->size = remaining - MCB_SIZE;
    free_block->is_free = 1;
    free_block->va = free_va;
    free_block->next = NULL;
    new_mcb->next = free_block;
  } else {
    new_mcb->size = total_space - MCB_SIZE;
  }

  // Append to MCB linked list
  if (current->heap_mcb == NULL) {
    current->heap_mcb = new_mcb;
  } else {
    mcb *tail = current->heap_mcb;
    while (tail->next) tail = tail->next;
    tail->next = new_mcb;
  }

  return va_base + MCB_SIZE;
}

//
// better_free: mark block free and merge adjacent free blocks (VA-based).
//
uint64 sys_user_free_page(uint64 va) {
  assert(current);

  mcb *block = va_to_mcb(va - MCB_SIZE);
  if (block == 0) panic("better_free: invalid address 0x%lx\n", va);
  block->is_free = 1;

  // Merge with next block if free and contiguous in VA space
  while (block->next && block->next->is_free) {
    uint64 expected_next_va = block->va + MCB_SIZE + block->size;
    if (block->next->va == expected_next_va) {
      block->size += MCB_SIZE + block->next->size;
      block->next = block->next->next;
    } else {
      break;
    }
  }

  return 0;
}

//
// [a0]: the syscall number; [a1] ... [a7]: arguments to the syscalls.
//
long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    case SYS_user_allocate_page:
      return sys_user_allocate_page((int)a1);
    case SYS_user_free_page:
      return sys_user_free_page(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
