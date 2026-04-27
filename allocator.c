#include "common.h"
#include "allocator.h"
#include "kernel.h"

extern char __free_ram[], __free_ram_end[];

region_t regions[MAX_REGIONS];

static int find_unused_region_slot(void);
static int find_suitable_free_region(uint32_t required_size);
static void merge_free_regions(void);

void init_regions(void) {
    regions[0].start = (paddr_t)__free_ram;
    regions[0].size  = (uint32_t)(__free_ram_end - __free_ram);
    regions[0].state = REGION_FREE;

    for (int i = 1; i < MAX_REGIONS; i++) {
        regions[i].start = 0;
        regions[i].size  = 0;
        regions[i].state = REGION_UNUSED;
    }
}

int alloc_pages(uint32_t num_pages) {
    uint32_t required_size = num_pages * PAGE_SIZE;

    int free_idx = find_suitable_free_region(required_size);
    int new_idx  = find_unused_region_slot();

    if (free_idx < 0 || new_idx < 0) {
        PANIC("out of memory");
        return -1;  // 或 PANIC
    }

    paddr_t alloc_addr = regions[free_idx].start;

    /* 建立 allocated region */
    regions[new_idx].start = alloc_addr;
    regions[new_idx].size  = required_size;
    regions[new_idx].state = REGION_ALLOCATED;

    /* 更新原本 free region */
    regions[free_idx].start += required_size;
    regions[free_idx].size  -= required_size;

    if (regions[free_idx].size == 0) {
        regions[free_idx].start = 0;
        regions[free_idx].state = REGION_UNUSED;
    }

    memset((void *)alloc_addr, 0, required_size);

    return new_idx;
}

void release_pages(int region_idx) {
    if (region_idx < 0 || region_idx >= MAX_REGIONS) {
        PANIC("invalid region index");
    }

    if (regions[region_idx].state != REGION_ALLOCATED) {
        PANIC("region is not allocated");
    }

    regions[region_idx].state = REGION_FREE;

    /* 合併碎片 */
    merge_free_regions();
}

paddr_t region_to_addr(int region_idx) {
    if (region_idx < 0 || region_idx >= MAX_REGIONS) {
        PANIC("invalid region index");
    }
    if (regions[region_idx].state != REGION_ALLOCATED){
        PANIC("region %d is not allocated", region_idx);
    }
    return regions[region_idx].start;
}

static int find_unused_region_slot(void) {
    for (int i = 0; i < MAX_REGIONS; i++) {
        if (regions[i].state == REGION_UNUSED) {
            return i;
        }
    }
    return -1;
}

static int find_suitable_free_region(uint32_t required_size) {
    for (int i = 0; i < MAX_REGIONS; i++) {
        if (regions[i].state == REGION_FREE &&
            regions[i].size >= required_size) {
            return i;
        }
    }
    return -1;
}

static void merge_free_regions(void) {
    for (int i = 0; i < MAX_REGIONS; i++) {
        if (regions[i].state != REGION_FREE) continue;

        for (int j = 0; j < MAX_REGIONS; j++) {
            if (i == j || regions[j].state != REGION_FREE) continue;

            if (regions[i].start + regions[i].size == regions[j].start) {
                regions[i].size += regions[j].size;

                regions[j].start = 0;
                regions[j].size  = 0;
                regions[j].state = REGION_UNUSED;
            }
        }
    }
}

void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags) {
    if (!is_aligned(vaddr, PAGE_SIZE))
        PANIC("unaligned vaddr %x", vaddr);

    if (!is_aligned(paddr, PAGE_SIZE))
        PANIC("unaligned paddr %x", paddr);

    uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
    if ((table1[vpn1] & PAGE_V) == 0) {
        // Create the 2nd level page table if it doesn't exist.
        uint32_t pt_paddr = region_to_addr(alloc_pages(1));
        table1[vpn1] = ((pt_paddr / PAGE_SIZE) << 10) | PAGE_V;
    }

    // Set the 2nd level page table entry to map the physical page.
    uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
    uint32_t *table0 = (uint32_t *) ((table1[vpn1] >> 10) * PAGE_SIZE);
    table0[vpn0] = ((paddr / PAGE_SIZE) << 10) | flags | PAGE_V;
}