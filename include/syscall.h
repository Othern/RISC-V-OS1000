#pragma once

#define SYS_PUTCHAR 1
#define SYS_GETCHAR 2
#define SYS_EXIT    3
#define SYS_READFILE  4
#define SYS_WRITEFILE 5
#define SYS_SEND 6
#define SYS_ARP_REQUEST 7
#define SYS_ARP_DUMP 8
#define SYS_IPV4_DUMP 9
#define SYS_SHM_GET 10
#define SYS_SHM_ATTACH 11
#define SYS_SHM_DETACH 12
#define SYS_SHM_DUMP 13
struct trap_frame;

void handle_syscall(struct trap_frame *f);
