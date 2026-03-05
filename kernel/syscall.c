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
#include "config.h"

#include "spike_interface/spike_utils.h"

// counter for how many harts have finished their app
volatile int app_done_count = 0;

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  int hart = read_tp();
  sprint("hartid = %d: %s", hart, buf);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  int hart = read_tp();
  sprint("hartid = %d: User exit with code:%d.\n", hart, code);

  // atomically increment the done counter
  int local;
  asm volatile("amoadd.w %0, %2, (%1)\n"
               : "=r"(local)
               : "r"(&app_done_count), "r"(1)
               : "memory");

  if (hart == 0) {
    // hart0 is responsible for shutdown. Wait until all harts are done.
    while (app_done_count < NCPU) {
      // spin wait
    }
    sprint("hartid = %d: shutdown with code:%d.\n", hart, code);
    shutdown(code);
  } else {
    // non-hart0: just spin forever after marking done, 
    // hart0 will shut down the system.
    while (1) {
      // spin
    }
  }
}

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
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
