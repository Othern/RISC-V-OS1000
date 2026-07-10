#pragma once

#include "common.h"
#include "process.h"

#define SHM_MAX 8
#define SHM_PAGE_SIZE PAGE_SIZE
#define SHM_USER_BASE 0x01700000

void shm_init(void);
int shm_get(int key);
int shm_attach(struct process *proc, int id);
int shm_detach(struct process *proc, uint32_t vaddr);
void shm_process_cleanup(struct process *proc);
void shm_dump(void);
