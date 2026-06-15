#pragma once

#define SYS_PUTCHAR 1
#define SYS_GETCHAR 2
#define SYS_EXIT    3
#define SYS_READFILE  4
#define SYS_WRITEFILE 5
#define SYS_SEND 6
#define SYS_ARP_REQUEST 7
#define SYS_ARP_DUMP 8
struct trap_frame;

void handle_syscall(struct trap_frame *f);
