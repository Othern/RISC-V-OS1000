#define PROCS_MAX 8       // Maximum number of processes
#define PROC_REGION_MAX 64 // Maximum region of a process
#define PROC_UNUSED   0   // Unused process control structure
#define PROC_RUNNABLE 1   // Runnable process
#define PROC_EXITED   2   // Finished process
#define USER_BASE 0x1000000
#define SSTATUS_SPIE (1 << 5) // enable interrupt when going into user mode 
#include "common.h"
struct process {
    int pid;             // Process ID
    int state;           // Process state: PROC_UNUSED or PROC_RUNNABLE
    vaddr_t sp;          // Stack pointer
    uint32_t *page_table;// Page Table 
    uint32_t regions[PROC_REGION_MAX];
    uint32_t current_idx;
    uint8_t stack[8192]; // Kernel stack
};

void init_processes(void);

__attribute__((naked)) void switch_context(uint32_t *prev_sp,
                                           uint32_t *next_sp);
void yield(void);
void dump_procs(void);
void map_page(struct process *proc, uint32_t vaddr, paddr_t paddr, uint32_t flags);
struct process *create_process(const void *image, size_t image_size);
