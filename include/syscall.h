#pragma once

#define SYS_PUTCHAR 1
#define SYS_GETCHAR 2
#define SYS_EXIT    3

struct trap_frame;

void handle_syscall(struct trap_frame *f);
