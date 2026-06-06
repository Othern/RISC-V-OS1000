#include "kernel.h"
#include "process.h"
#include "common.h"
#include "test_common.h"
#include "test_process.h"
#include "test_virtio.h"
#include "test_virtio_net.h"
#include "allocator.h"
#include "syscall.h"
#include "virtio_blk.h"
#include "virtio_net.h"
#include "filesystem.h"
#include "plic.h"
#include "sbi.h"
//#define TEST  1
extern char __bss[], __bss_end[], __stack_top[], __free_ram[], __free_ram_end[], __kernel_base[];
extern char _binary_shell_bin_start[], _binary_shell_bin_size[];
extern struct process *current_proc;
__attribute__((naked))
__attribute__((aligned(4)))
void kernel_entry(void) {
    __asm__ __volatile__(
        // sscratch is zero in kernel mode and contains the kernel stack top
        // while user mode is running.
        "csrrw sp, sscratch, sp\n"
        "bnez sp, 1f\n"
        "csrrw sp, sscratch, sp\n"
        "1:\n"
        "addi sp, sp, -4 * 32\n"
        "sw ra,  4 * 0(sp)\n"
        "sw gp,  4 * 1(sp)\n"
        "sw tp,  4 * 2(sp)\n"
        "sw t0,  4 * 3(sp)\n"
        "sw t1,  4 * 4(sp)\n"
        "sw t2,  4 * 5(sp)\n"
        "sw t3,  4 * 6(sp)\n"
        "sw t4,  4 * 7(sp)\n"
        "sw t5,  4 * 8(sp)\n"
        "sw t6,  4 * 9(sp)\n"
        "sw a0,  4 * 10(sp)\n"
        "sw a1,  4 * 11(sp)\n"
        "sw a2,  4 * 12(sp)\n"
        "sw a3,  4 * 13(sp)\n"
        "sw a4,  4 * 14(sp)\n"
        "sw a5,  4 * 15(sp)\n"
        "sw a6,  4 * 16(sp)\n"
        "sw a7,  4 * 17(sp)\n"
        "sw s0,  4 * 18(sp)\n"
        "sw s1,  4 * 19(sp)\n"
        "sw s2,  4 * 20(sp)\n"
        "sw s3,  4 * 21(sp)\n"
        "sw s4,  4 * 22(sp)\n"
        "sw s5,  4 * 23(sp)\n"
        "sw s6,  4 * 24(sp)\n"
        "sw s7,  4 * 25(sp)\n"
        "sw s8,  4 * 26(sp)\n"
        "sw s9,  4 * 27(sp)\n"
        "sw s10, 4 * 28(sp)\n"
        "sw s11, 4 * 29(sp)\n"

        // Save the interrupted stack pointer. For user traps it is in
        // sscratch. For kernel traps it is the stack top before this frame.
        "csrr t0, sstatus\n"
        "andi t0, t0, %[spp]\n"
        "bnez t0, 2f\n"
        "csrr a0, sscratch\n"
        "sw a0, 4 * 30(sp)\n"
        "csrw sscratch, zero\n"
        "j 3f\n"
        "2:\n"
        "addi a0, sp, 4 * 32\n"
        "sw a0, 4 * 30(sp)\n"
        "3:\n"
        "mv a0, sp\n"
        "call handle_trap\n"

        "csrr t0, sstatus\n"
        "andi t0, t0, %[spp]\n"
        "beqz t0, 4f\n"

        // Return to supervisor mode using the existing kernel stack.
        "lw ra,  4 * 0(sp)\n"
        "lw gp,  4 * 1(sp)\n"
        "lw tp,  4 * 2(sp)\n"
        "lw t0,  4 * 3(sp)\n"
        "lw t1,  4 * 4(sp)\n"
        "lw t2,  4 * 5(sp)\n"
        "lw t3,  4 * 6(sp)\n"
        "lw t4,  4 * 7(sp)\n"
        "lw t5,  4 * 8(sp)\n"
        "lw t6,  4 * 9(sp)\n"
        "lw a0,  4 * 10(sp)\n"
        "lw a1,  4 * 11(sp)\n"
        "lw a2,  4 * 12(sp)\n"
        "lw a3,  4 * 13(sp)\n"
        "lw a4,  4 * 14(sp)\n"
        "lw a5,  4 * 15(sp)\n"
        "lw a6,  4 * 16(sp)\n"
        "lw a7,  4 * 17(sp)\n"
        "lw s0,  4 * 18(sp)\n"
        "lw s1,  4 * 19(sp)\n"
        "lw s2,  4 * 20(sp)\n"
        "lw s3,  4 * 21(sp)\n"
        "lw s4,  4 * 22(sp)\n"
        "lw s5,  4 * 23(sp)\n"
        "lw s6,  4 * 24(sp)\n"
        "lw s7,  4 * 25(sp)\n"
        "lw s8,  4 * 26(sp)\n"
        "lw s9,  4 * 27(sp)\n"
        "lw s10, 4 * 28(sp)\n"
        "lw s11, 4 * 29(sp)\n"
        "addi sp, sp, 4 * 32\n"
        "sret\n"

        // Return to user mode. Restore the kernel stack top in sscratch.
        "4:\n"
        "addi t0, sp, 4 * 32\n"
        "csrw sscratch, t0\n"
        "lw ra,  4 * 0(sp)\n"
        "lw gp,  4 * 1(sp)\n"
        "lw tp,  4 * 2(sp)\n"
        "lw t0,  4 * 3(sp)\n"
        "lw t1,  4 * 4(sp)\n"
        "lw t2,  4 * 5(sp)\n"
        "lw t3,  4 * 6(sp)\n"
        "lw t4,  4 * 7(sp)\n"
        "lw t5,  4 * 8(sp)\n"
        "lw t6,  4 * 9(sp)\n"
        "lw a0,  4 * 10(sp)\n"
        "lw a1,  4 * 11(sp)\n"
        "lw a2,  4 * 12(sp)\n"
        "lw a3,  4 * 13(sp)\n"
        "lw a4,  4 * 14(sp)\n"
        "lw a5,  4 * 15(sp)\n"
        "lw a6,  4 * 16(sp)\n"
        "lw a7,  4 * 17(sp)\n"
        "lw s0,  4 * 18(sp)\n"
        "lw s1,  4 * 19(sp)\n"
        "lw s2,  4 * 20(sp)\n"
        "lw s3,  4 * 21(sp)\n"
        "lw s4,  4 * 22(sp)\n"
        "lw s5,  4 * 23(sp)\n"
        "lw s6,  4 * 24(sp)\n"
        "lw s7,  4 * 25(sp)\n"
        "lw s8,  4 * 26(sp)\n"
        "lw s9,  4 * 27(sp)\n"
        "lw s10, 4 * 28(sp)\n"
        "lw s11, 4 * 29(sp)\n"
        "lw sp,  4 * 30(sp)\n"
        "sret\n"
        :
        : [spp] "i" (SSTATUS_SPP)
    );
}

static void handle_external_interrupt(void) {
    uint32_t irq = plic_claim();

    switch (irq) {
    case VIRTIO_BLK_IRQ:
        virtio_blk_irq();
        break;
    case VIRTIO_NET_IRQ:
        virtio_net_irq();
        break;
    case 0:
        return;
    default:
        printf("unexpected external irq=%d\n", irq);
        break;
    }

    plic_complete(irq);
}

void handle_trap(struct trap_frame *f) {
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);

    if (scause & SCAUSE_INTERRUPT) {
        uint32_t code = scause & SCAUSE_CODE_MASK;
        if (code == SCAUSE_SUPERVISOR_EXT) {
            handle_external_interrupt();
            return;
        }
        PANIC("unexpected interrupt scause=%x, stval=%x, sepc=%x\n",
              scause, stval, user_pc);
    }

    if ((scause & SCAUSE_CODE_MASK) == SCAUSE_ECALL) {
        handle_syscall(f);
        user_pc += 4;
    } else {
        PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval, user_pc);
    }

    WRITE_CSR(sepc, user_pc);
}

void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    WRITE_CSR(stvec, (uint32_t) kernel_entry);
    WRITE_CSR(sscratch, 0);
    init_regions();
    virtio_blk_init();
    virtio_net_init();
    plic_init();
    WRITE_CSR(sie, READ_CSR(sie) | SIE_SEIE);
    WRITE_CSR(sstatus, READ_CSR(sstatus) | SSTATUS_SIE);
    virtio_net_send_test_packet();
    fs_init();
    init_processes();
    #ifdef TEST
        test_virtio_net_tx();
        test_virtio_net_rx();

        // Test common functions
        // test_common();
        // __asm__ __volatile__("unimp");

        // Test process creation and context switching
        // test_process();
        test_virtio();
    #endif

    // dump_procs();
    // create_process(_binary_shell_bin_start, (size_t) _binary_shell_bin_size);
    // yield();
    PANIC("switched to idle process");
}

__attribute__((section(".text.boot")))
__attribute__((naked))
void boot(void) {
    __asm__ __volatile__(
        "mv sp, %[stack_top]\n" // Set the stack pointer
        "j kernel_main\n"       // Jump to the kernel main function
        :
        : [stack_top] "r" (__stack_top) // Pass the stack top address as %[stack_top]
    );
}
