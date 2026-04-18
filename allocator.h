#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "common.h"

#define MAX_REGIONS 128
#define PAGE_SIZE   4096

typedef enum {
    REGION_UNUSED = 0,
    REGION_FREE,
    REGION_ALLOCATED
} region_state_t;

typedef struct {
    paddr_t start;
    uint32_t size;      // bytes
    region_state_t state;
} region_t;

extern region_t regions[MAX_REGIONS];

void init_regions(void);

/* 回傳 region index，失敗回傳 -1 */
int alloc_pages(uint32_t num_pages);

/* 透過 index 釋放 */
void release_pages(int region_idx);

/* helper: 取得位址 */
paddr_t region_to_addr(int region_idx);

#endif