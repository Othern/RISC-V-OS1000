#include "kernel.h"

void wait_for_interrupt(volatile bool *done) {
    uint32_t saved_sstatus = READ_CSR(sstatus);
    uint32_t disabled_sstatus = saved_sstatus & ~SSTATUS_SIE;

    WRITE_CSR(sstatus, disabled_sstatus);
    while (!*done) {
        __asm__ __volatile__("wfi" ::: "memory");

        // A locally enabled pending interrupt wakes WFI even while sstatus.SIE
        // is clear. Briefly enable SIE so the pending trap can run.
        WRITE_CSR(sstatus, disabled_sstatus | SSTATUS_SIE);
        WRITE_CSR(sstatus, disabled_sstatus);
    }

    WRITE_CSR(sstatus, saved_sstatus);
}
