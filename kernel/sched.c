/*
 * implementing the scheduler
 */

#include "sched.h"
#include "spike_interface/spike_utils.h"

process* ready_queue_head = NULL;
process *wait_queue_head = NULL;

extern process procs[NPROC];

//
// insert a process, proc, into the END of ready queue.
//
void insert_to_ready_queue( process* proc ) {
  sprint( "going to insert process %d to ready queue.\n", proc->pid );
  // if the queue is empty in the beginning
  if( ready_queue_head == NULL ){
    proc->status = READY;
    proc->queue_next = NULL;
    ready_queue_head = proc;
    return;
  }

  // ready queue is not empty
  process *p;
  // browse the ready queue to see if proc is already in-queue
  for( p=ready_queue_head; p->queue_next!=NULL; p=p->queue_next )
    if( p == proc ) return;  //already in queue

  // p points to the last element of the ready queue
  if( p==proc ) return;
  p->queue_next = proc;
  proc->status = READY;
  proc->queue_next = NULL;

  return;
}

void insert_to_wait_queue(process *proc)
{

  proc->status = BLOCKED;
  proc->queue_next = NULL;

  if (wait_queue_head == NULL)
  {
    wait_queue_head = proc;
    return;
  }
  
  // wait queue is not empty
  process *ptr = wait_queue_head;
  // 遍历到队尾
  while (ptr->queue_next != NULL) {
      if (ptr == proc) return; // already in queue
      ptr = ptr->queue_next;
  }
  
  if (ptr == proc) return;
  ptr->queue_next = proc;
  return;
}


void wake_up(process *curr_proc)
{
  if (wait_queue_head == NULL)
  {
    return;
  }

  process *parent = curr_proc->parent;
  
  // Case 1: 队头就是要唤醒的父进程
  if (wait_queue_head == parent)
  {
    process *target = wait_queue_head;
    wait_queue_head = wait_queue_head->queue_next;
    
    target->status = READY;
    insert_to_ready_queue(target);
    return;
  }
  
  // Case 2: 遍历队列查找父进程
  process *prev = wait_queue_head;
  while (prev->queue_next != NULL)
  {
    if (prev->queue_next == parent)
    {
      process *target = prev->queue_next;
      // 从等待队列移除
      prev->queue_next = target->queue_next;
      
      // 加入就绪队列
      target->status = READY;
      insert_to_ready_queue(target);
      return;
    }
    prev = prev->queue_next;
  }
}

//
// choose a proc from the ready queue, and put it to run.
//
void schedule() {
  if ( !ready_queue_head ){
    // by default, if there are no ready process, and all processes are in the status of
    // FREE and ZOMBIE, we should shutdown the emulated RISC-V machine.
    int should_shutdown = 1;

    for( int i=0; i<NPROC; i++ )
      if( (procs[i].status != FREE) && (procs[i].status != ZOMBIE) ){
        should_shutdown = 0;
        sprint( "ready queue empty, but process %d is not in free/zombie state:%d\n", 
          i, procs[i].status );
      }

    if( should_shutdown ){
      sprint( "no more ready processes, system shutdown now.\n" );
      shutdown( 0 );
    }else{
      panic( "Not handled: we should let system wait for unfinished processes.\n" );
    }
  }

  current = ready_queue_head;
  assert( current->status == READY );
  ready_queue_head = ready_queue_head->queue_next;

  current->status = RUNNING;
  sprint( "going to schedule process %d to run.\n", current->pid );
  switch_to( current );
}