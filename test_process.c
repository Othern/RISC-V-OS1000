#include "common.h"
#include "process.h"

struct process *proc_a;
struct process *proc_b;

static inline void proc_a_entry(void) {
    printf("starting process A\n");
    while (1) {
        putchar('A');
        delay();
        yield();
    }
}

static inline void proc_b_entry(void) {
    printf("starting process B\n");
    while (1) {
        putchar('B');
        delay();
        yield();
    }
}

void test_process(void) {
    proc_a = create_process((uint32_t) proc_a_entry);
    proc_b = create_process((uint32_t) proc_b_entry);
    yield();
}