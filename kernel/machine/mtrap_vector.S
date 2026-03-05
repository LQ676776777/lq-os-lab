#include "util/load_store.S"

#
# M-mode trap entry point
#
.globl mtrapvec
.align 4
mtrapvec:
    # Each hart has its own g_itrframe slot.
    # mscratch already points to the correct g_itrframe[hartid] (set in minit.c).
    # swap a0 and mscratch, so that a0 points to this hart's interrupt frame.
    csrrw a0, mscratch, a0

    # save the registers in g_itrframe[hartid]
    addi t6, a0, 0
    store_all_registers
    # save the original content of a0 in g_itrframe
    csrr t0, mscratch
    sd t0, 72(a0)

    # switch stack (to use stack0) for the rest of machine mode
    # trap handling. Each hart gets its own stack region.
    la sp, stack0
    li a3, 4096
    csrr a4, mhartid
    addi a4, a4, 1
    mul a3, a3, a4
    add sp, sp, a3

    # pointing mscratch back to g_itrframe[hartid]
    csrw mscratch, a0

    # call machine mode trap handling function
    call handle_mtrap

    # restore all registers, come back to the status before entering
    # machine mode handling.
    csrr t6, mscratch
    restore_all_registers

    mret
