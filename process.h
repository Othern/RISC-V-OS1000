#define PROCS_MAX 8       // Maximum number of processes
#define PROC_UNUSED   0   // Unused process control structure
#define PROC_RUNNABLE 1   // Runnable process
#include "common.h"
struct process {
    int pid;             // Process ID
    int state;           // Process state: PROC_UNUSED or PROC_RUNNABLE
    vaddr_t sp;          // Stack pointer
    uint8_t stack[8192]; // Kernel stack
};

void init_processes(void);

__attribute__((naked)) void switch_context(uint32_t *prev_sp,
                                           uint32_t *next_sp);
void yield(void);

struct process *create_process(uint32_t pc);
