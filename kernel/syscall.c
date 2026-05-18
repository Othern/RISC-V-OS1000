#include "syscall.h"
#include "common.h"
#include "kernel.h"
#include "process.h"

extern struct process *current_proc;

void handle_syscall(struct trap_frame *f) {
    switch (f->a3) {
        case SYS_PUTCHAR:
            putchar(f->a0);
            break;

        case SYS_GETCHAR:
            while (1) {
                long ch = getchar();
                if (ch >= 0) {
                    f->a0 = ch;
                    break;
                }
                yield();
            }
            break;

        case SYS_EXIT:
            printf("process %d exited\n", current_proc->pid);
            current_proc->state = PROC_EXITED;
            yield();
            PANIC("unreachable");

        default:
            PANIC("unexpected syscall a3=%x\n", f->a3);
    }
}
