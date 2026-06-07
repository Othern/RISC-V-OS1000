#pragma once
#include "common.h"
#include "syscall.h"

int syscall(int sysno, int arg0, int arg1, int arg2);
__attribute__((noreturn)) void exit(void);
void putchar(char ch);
int getchar(void);
int readfile(const char *filename, char *buf, int len);
int writefile(const char *filename, const char *buf, int len);
int send(const char *buf, int len);