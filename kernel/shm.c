#include "shm.h"
#include "allocator.h"
#include "kernel.h"

struct shm_object {
    int used;
    int id;
    int key;
    paddr_t pa;
    uint32_t region_idx;
    uint32_t size;
    int refcount;
};

static struct shm_object shm_table[SHM_MAX];
static int next_shm_id = 1;

static struct shm_object *find_by_id(int id) {
    for (int i = 0; i < SHM_MAX; i++) {
        if (shm_table[i].used && shm_table[i].id == id)
            return &shm_table[i];
    }
    return NULL;
}

static struct shm_object *find_by_key(int key) {
    for (int i = 0; i < SHM_MAX; i++) {
        if (shm_table[i].used && shm_table[i].key == key)
            return &shm_table[i];
    }
    return NULL;
}

void shm_init(void) {
    for (int i = 0; i < SHM_MAX; i++) {
        shm_table[i].used = 0;
        shm_table[i].id = 0;
        shm_table[i].key = 0;
        shm_table[i].pa = 0;
        shm_table[i].region_idx = 0;
        shm_table[i].size = 0;
        shm_table[i].refcount = 0;
    }
    next_shm_id = 1;
}

int shm_get(int key) {
    if (key <= 0)
        return -1;

    struct shm_object *obj = find_by_key(key);
    if (obj)
        return obj->id;

    for (int i = 0; i < SHM_MAX; i++) {
        if (!shm_table[i].used) {
            uint32_t region_idx;
            paddr_t pa = alloc_pages(&region_idx, 1);

            shm_table[i].used = 1;
            shm_table[i].id = next_shm_id++;
            shm_table[i].key = key;
            shm_table[i].pa = pa;
            shm_table[i].region_idx = region_idx;
            shm_table[i].size = SHM_PAGE_SIZE;
            shm_table[i].refcount = 0;
            return shm_table[i].id;
        }
    }

    return -1;
}

int shm_attach(struct process *proc, int id) {
    struct shm_object *obj = find_by_id(id);
    if (!obj)
        return 0;

    int slot = -1;
    for (int i = 0; i < PROC_SHM_MAX; i++) {
        if (!proc->shm_mappings[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return 0;

    uint32_t vaddr = SHM_USER_BASE + slot * SHM_PAGE_SIZE;
    map_page(proc, vaddr, obj->pa, PAGE_U | PAGE_R | PAGE_W);

    proc->shm_mappings[slot].used = 1;
    proc->shm_mappings[slot].shm_id = id;
    proc->shm_mappings[slot].vaddr = vaddr;
    obj->refcount++;

    return vaddr;
}

int shm_detach(struct process *proc, uint32_t vaddr) {
    for (int i = 0; i < PROC_SHM_MAX; i++) {
        struct shm_mapping *mapping = &proc->shm_mappings[i];
        if (!mapping->used || mapping->vaddr != vaddr)
            continue;

        struct shm_object *obj = find_by_id(mapping->shm_id);
        unmap_page(proc, mapping->vaddr);

        mapping->used = 0;
        mapping->shm_id = 0;
        mapping->vaddr = 0;

        if (obj) {
            obj->refcount--;
            if (obj->refcount <= 0) {
                release_pages(obj->region_idx);
                obj->used = 0;
                obj->id = 0;
                obj->key = 0;
                obj->pa = 0;
                obj->region_idx = 0;
                obj->size = 0;
                obj->refcount = 0;
            }
        }
        return 0;
    }

    return -1;
}

void shm_process_cleanup(struct process *proc) {
    for (int i = 0; i < PROC_SHM_MAX; i++) {
        if (proc->shm_mappings[i].used)
            shm_detach(proc, proc->shm_mappings[i].vaddr);
    }
}

void shm_dump(void) {
    printf("shared memory objects:\n");
    for (int i = 0; i < SHM_MAX; i++) {
        struct shm_object *obj = &shm_table[i];
        if (!obj->used)
            continue;

        printf("  id=%d key=%d pa=0x%x size=%d refcount=%d\n",
               obj->id, obj->key, obj->pa, obj->size, obj->refcount);
    }
}
