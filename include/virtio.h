#pragma once
#include "common.h"
#include "kernel.h"
#define VIRTQ_ENTRY_NUM   16
#define VIRTIO_DEVICE_BLK 2
#define VIRTIO_DEVICE_NET 1
#define VIRTIO_BLK_PADDR  0x10001000
#define VIRTIO_NET_PADDR  0x10002000
#define VIRTIO_MMIO_PADDR_START 0x10001000
#define VIRTIO_MMIO_PADDR_END   0x10008000
#define VIRTIO_MMIO_STRIDE      0x1000
#define VIRTIO_REG_MAGIC         0x00
#define VIRTIO_REG_VERSION       0x04
#define VIRTIO_REG_DEVICE_ID     0x08
#define VIRTIO_REG_PAGE_SIZE     0x28
#define VIRTIO_REG_QUEUE_SEL     0x30
#define VIRTIO_REG_QUEUE_NUM_MAX 0x34
#define VIRTIO_REG_QUEUE_NUM     0x38
#define VIRTIO_REG_QUEUE_PFN     0x40
#define VIRTIO_REG_QUEUE_READY   0x44
#define VIRTIO_REG_QUEUE_NOTIFY  0x50
#define VIRTIO_REG_DEVICE_STATUS 0x70
#define VIRTIO_REG_DEVICE_CONFIG 0x100
#define VIRTIO_STATUS_ACK       1
#define VIRTIO_STATUS_DRIVER    2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTQ_DESC_F_NEXT          1
#define VIRTQ_DESC_F_WRITE         2
#define VIRTQ_AVAIL_F_NO_INTERRUPT 1

struct virtio_device {
    paddr_t base;
    uint32_t device_id;
};

// Virtqueue Descriptor Table entry.
struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

// Virtqueue Available Ring.
struct virtq_avail {
    uint16_t flags;
    uint16_t index;
    uint16_t ring[VIRTQ_ENTRY_NUM];
} __attribute__((packed));

// Virtqueue Used Ring entry.
struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

// Virtqueue Used Ring.
struct virtq_used {
    uint16_t flags;
    uint16_t index;
    struct virtq_used_elem ring[VIRTQ_ENTRY_NUM];
} __attribute__((packed));

// Virtqueue.
struct virtio_virtq {
    struct virtq_desc descs[VIRTQ_ENTRY_NUM];
    struct virtq_avail avail;
    struct virtq_used used __attribute__((aligned(PAGE_SIZE)));
    int queue_index;
    volatile uint16_t *used_index;
    uint16_t last_used_index;
} __attribute__((packed));

uint32_t virtio_reg_read32(struct virtio_device *dev, unsigned offset);
uint64_t virtio_reg_read64(struct virtio_device *dev, unsigned offset);
void virtio_reg_write32(struct virtio_device *dev, unsigned offset, uint32_t value);
void virtio_reg_fetch_and_or32(struct virtio_device *dev, unsigned offset, uint32_t value);
bool virtio_probe(struct virtio_device *dev, paddr_t base, uint32_t device_id);
bool virtio_find_device(struct virtio_device *dev, uint32_t device_id);
void virtio_begin_init(struct virtio_device *dev);
void virtio_finish_init(struct virtio_device *dev);
struct virtio_virtq *virtq_init(struct virtio_device *dev, unsigned index);
void virtq_push(struct virtio_virtq *vq, int desc_index);
void virtq_notify(struct virtio_device *dev, struct virtio_virtq *vq);
void virtq_kick(struct virtio_device *dev, struct virtio_virtq *vq, int desc_index);
bool virtq_is_busy(struct virtio_virtq *vq);
