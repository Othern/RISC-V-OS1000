#include "process.h"
#include "common.h"
#include "allocator.h"
#include "virtio.h"
#include "plic.h"
#include "kernel.h"

struct process procs[PROCS_MAX]; // All process control structures.
struct process *current_proc; // Currently running process
struct process *idle_proc;    // Idle process

extern char __kernel_base[], __free_ram_end[];

__attribute__((naked)) void switch_context(uint32_t *prev_sp,
                                           uint32_t *next_sp) {
    __asm__ __volatile__(
        // Save callee-saved registers onto the current process's stack.
        "addi sp, sp, -13 * 4\n" // Allocate stack space for 13 4-byte registers
        "sw ra,  0  * 4(sp)\n"   // Save callee-saved registers only
        "sw s0,  1  * 4(sp)\n"
        "sw s1,  2  * 4(sp)\n"
        "sw s2,  3  * 4(sp)\n"
        "sw s3,  4  * 4(sp)\n"
        "sw s4,  5  * 4(sp)\n"
        "sw s5,  6  * 4(sp)\n"
        "sw s6,  7  * 4(sp)\n"
        "sw s7,  8  * 4(sp)\n"
        "sw s8,  9  * 4(sp)\n"
        "sw s9,  10 * 4(sp)\n"
        "sw s10, 11 * 4(sp)\n"
        "sw s11, 12 * 4(sp)\n"

        // Switch the stack pointer.
        "sw sp, (a0)\n"         // *prev_sp = sp;
        "lw sp, (a1)\n"         // Switch stack pointer (sp) here

        // Restore callee-saved registers from the next process's stack.
        "lw ra,  0  * 4(sp)\n"  // Restore callee-saved registers only
        "lw s0,  1  * 4(sp)\n"
        "lw s1,  2  * 4(sp)\n"
        "lw s2,  3  * 4(sp)\n"
        "lw s3,  4  * 4(sp)\n"
        "lw s4,  5  * 4(sp)\n"
        "lw s5,  6  * 4(sp)\n"
        "lw s6,  7  * 4(sp)\n"
        "lw s7,  8  * 4(sp)\n"
        "lw s8,  9  * 4(sp)\n"
        "lw s9,  10 * 4(sp)\n"
        "lw s10, 11 * 4(sp)\n"
        "lw s11, 12 * 4(sp)\n"
        "addi sp, sp, 13 * 4\n"  // We've popped 13 4-byte registers from the stack
        "ret\n"
    );
}
void init_processes(void) {
    for (int i = 0; i < PROCS_MAX; i++) {
        procs[i].pid = 0;
        procs[i].state = PROC_UNUSED;
        procs[i].sp = 0;

        for (int j = 0; j < MAX_REGIONS; j++) {
            procs[i].regions[j] = 0;
        }
    }

    // Create an idle process that runs when no other process is runnable.
    idle_proc = create_process(NULL, 0);
    idle_proc->pid = 0; // PID 0 is reserved for the idle process
    current_proc = idle_proc;
}

static void release_process(struct process *proc) {
    for (uint32_t i = 0; i < proc->current_idx; i++) {
        if (proc->regions[i]) {
            release_pages(proc->regions[i]);
            proc->regions[i] = 0;
        }
    }

    proc->pid = 0;
    proc->state = PROC_UNUSED;
    proc->sp = 0;
    proc->page_table = NULL;
    proc->current_idx = 0;
}

void yield(void) {
    uint32_t saved_sstatus = READ_CSR(sstatus);
    WRITE_CSR(sstatus, saved_sstatus & ~SSTATUS_SIE);

    struct process *next = idle_proc;
    if (current_proc -> state == PROC_EXITED) {
        release_process(current_proc);
    }
    // Find next process to run
    for (int i = 0; i < PROCS_MAX; i++) {
        struct process *proc = &procs[(current_proc->pid + i) % PROCS_MAX];
        if (proc->state == PROC_RUNNABLE && proc->pid > 0) {
            next = proc;
            break;
        }
    }

    // If there's no runnable process other than the current one, return and continue processing
    if (next == current_proc) {
        WRITE_CSR(sstatus, saved_sstatus);
        return;
    }

    // Set sscratch to the top of the next process's stack. This allows the trap handler to save the
    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        "csrw sscratch, %[sscratch]\n"
        :
        // Don't forget the trailing comma!
        : [satp] "r" (SATP_SV32 | ((uint32_t) next->page_table / PAGE_SIZE)),
          [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)])
    );

    // Context switch
    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);

    WRITE_CSR(sscratch, 0);
    WRITE_CSR(sstatus, saved_sstatus);
}

// ↓ __attribute__((naked)) is very important!
__attribute__((naked)) void user_entry(void) {
    __asm__ __volatile__(
        "csrw sepc, %[sepc]        \n"
        "csrw sstatus, %[sstatus]  \n"
        "sret                      \n"
        :
        : [sepc] "r" (USER_BASE),
          [sstatus] "r" (SSTATUS_SPIE | SSTATUS_SUM)
    );
}


struct process *create_process(const void *image, size_t image_size) {
    // Find an unused process control structure.
    struct process *proc = NULL;
    int i;
    for (i = 0; i < PROCS_MAX; i++) {
        if (procs[i].state == PROC_UNUSED) {
            proc = &procs[i];
            break;
        }
    }

    if (!proc)
        PANIC("no free process slots");
    proc->current_idx = 0;

    for (int j = 0; j < MAX_REGIONS; j++) {
        proc->regions[j] = 0;
    }
    // Stack callee-saved registers. These register values will be restored in
    // the first context switch in switch_context.
    uint32_t *sp = (uint32_t *) &proc->stack[sizeof(proc->stack)];
    *--sp = 0;                      // s11
    *--sp = 0;                      // s10
    *--sp = 0;                      // s9
    *--sp = 0;                      // s8
    *--sp = 0;                      // s7
    *--sp = 0;                      // s6
    *--sp = 0;                      // s5
    *--sp = 0;                      // s4
    *--sp = 0;                      // s3
    *--sp = 0;                      // s2
    *--sp = 0;                      // s1
    *--sp = 0;                      // s0
    *--sp = (uint32_t) user_entry;  // ra (changed!)

    // Map kernel pages.
    uint32_t page_table_region;
    uint32_t *page_table = (uint32_t *) alloc_pages(&page_table_region,1);
    proc -> page_table = page_table;
    proc -> regions[(proc->current_idx)++] = page_table_region;
    for (paddr_t paddr = (paddr_t) __kernel_base;
         paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE)
        map_page(proc, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);
        
    // virtio-blk
    map_page(proc, VIRTIO_BLK_PADDR, VIRTIO_BLK_PADDR, PAGE_R | PAGE_W);
    map_page(proc, VIRTIO_NET_PADDR, VIRTIO_NET_PADDR, PAGE_R | PAGE_W);
    map_page(proc, PLIC_BASE, PLIC_BASE, PAGE_R | PAGE_W);
    map_page(proc, PLIC_BASE + 0x2000, PLIC_BASE + 0x2000, PAGE_R | PAGE_W);
    map_page(proc, PLIC_BASE + 0x201000, PLIC_BASE + 0x201000, PAGE_R | PAGE_W);

    // Map user pages.
    for (uint32_t off = 0; off < image_size; off += PAGE_SIZE) {
        uint32_t page_region;
        paddr_t page = alloc_pages(&page_region, 1);
        proc->regions[(proc->current_idx)++] = page_region;

        // Handle the case where the data to be copied is smaller than the
        // page size.
        size_t remaining = image_size - off;
        size_t copy_size = PAGE_SIZE <= remaining ? PAGE_SIZE : remaining;

        // Fill and map the page.
        memcpy((void *) page, image + off, copy_size);
        map_page(proc, USER_BASE + off, page,
                 PAGE_U | PAGE_R | PAGE_W | PAGE_X);
    }
    
    // Initialize fields.
    proc->pid = i + 1;
    proc->state = PROC_RUNNABLE;
    proc->sp = (uint32_t) sp;
    return proc;
}

void dump_procs(void) {
    for(uint32_t i=0; i< PROCS_MAX; i++) {
        struct process *proc = &procs[i];
        printf("process {\n");
        printf("  pid        = %d\n", proc->pid);
        printf("  state      = %d", proc->state);

        if (proc->state == PROC_UNUSED) {
            printf(" (PROC_UNUSED)\n");
        } else if (proc->state == PROC_RUNNABLE) {
            printf(" (PROC_RUNNABLE)\n");
        } else {
            printf(" (UNKNOWN)\n");
        }

        printf("  sp         = 0x%x\n", proc->sp);
        printf("  page_table = 0x%x\n", (uint32_t) proc->page_table);
        printf("  stack      = 0x%x ~ 0x%x\n",
            (uint32_t) &proc->stack[0],
            (uint32_t) &proc->stack[sizeof(proc->stack)]);
        printf("}\n");
    }
}

void map_page(struct process *proc, uint32_t vaddr, paddr_t paddr, uint32_t flags) {
    uint32_t *table1 = proc->page_table;
    if (!is_aligned(vaddr, PAGE_SIZE))
        PANIC("unaligned vaddr %x", vaddr);

    if (!is_aligned(paddr, PAGE_SIZE))
        PANIC("unaligned paddr %x", paddr);

    uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
    if ((table1[vpn1] & PAGE_V) == 0) {
        // Create the 2nd level page table if it doesn't exist.
        uint32_t region_idx; 
        uint32_t pt_paddr = alloc_pages(&region_idx, 1);
        proc -> regions[(proc->current_idx)++] = region_idx;
        table1[vpn1] = ((pt_paddr / PAGE_SIZE) << 10) | PAGE_V;
    }

    // Set the 2nd level page table entry to map the physical page.
    uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
    uint32_t *table0 = (uint32_t *) ((table1[vpn1] >> 10) * PAGE_SIZE);
    table0[vpn0] = ((paddr / PAGE_SIZE) << 10) | flags | PAGE_V;
}
