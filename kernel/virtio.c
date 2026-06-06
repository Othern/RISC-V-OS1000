#include "virtio.h"
#include "allocator.h"

#define VIRTIO_MAGIC 0x74726976

static uint32_t virtq_region;

uint32_t virtio_reg_read32(struct virtio_device *dev, unsigned offset) {
    return *((volatile uint32_t *) (dev->base + offset));
}

uint64_t virtio_reg_read64(struct virtio_device *dev, unsigned offset) {
    return *((volatile uint64_t *) (dev->base + offset));
}

void virtio_reg_write32(struct virtio_device *dev, unsigned offset, uint32_t value) {
    *((volatile uint32_t *) (dev->base + offset)) = value;
}

void virtio_reg_fetch_and_or32(struct virtio_device *dev, unsigned offset, uint32_t value) {
    virtio_reg_write32(dev, offset, virtio_reg_read32(dev, offset) | value);
}

uint32_t virtio_irq_status(struct virtio_device *dev) {
    return virtio_reg_read32(dev, VIRTIO_REG_INTERRUPT_STATUS);
}

void virtio_irq_ack(struct virtio_device *dev, uint32_t status) {
    virtio_reg_write32(dev, VIRTIO_REG_INTERRUPT_ACK, status);
}

bool virtio_probe(struct virtio_device *dev, paddr_t base, uint32_t device_id) {
    dev->base = base;
    dev->device_id = virtio_reg_read32(dev, VIRTIO_REG_DEVICE_ID);

    if (virtio_reg_read32(dev, VIRTIO_REG_MAGIC) != VIRTIO_MAGIC)
        return false;
    if (virtio_reg_read32(dev, VIRTIO_REG_VERSION) != 1)
        return false;
    return dev->device_id == device_id;
}

bool virtio_find_device(struct virtio_device *dev, uint32_t device_id) {
    for (paddr_t base = VIRTIO_MMIO_PADDR_START;
         base <= VIRTIO_MMIO_PADDR_END;
         base += VIRTIO_MMIO_STRIDE) {
        if (virtio_probe(dev, base, device_id))
            return true;
    }

    return false;
}

void virtio_begin_init(struct virtio_device *dev) {
    virtio_reg_write32(dev, VIRTIO_REG_DEVICE_STATUS, 0);
    virtio_reg_fetch_and_or32(dev, VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACK);
    virtio_reg_fetch_and_or32(dev, VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER);
    virtio_reg_write32(dev, VIRTIO_REG_PAGE_SIZE, PAGE_SIZE);
}

void virtio_finish_init(struct virtio_device *dev) {
    virtio_reg_write32(dev, VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER_OK);
}

struct virtio_virtq *virtq_init(struct virtio_device *dev, unsigned index) {
    paddr_t virtq_paddr = alloc_pages(&virtq_region,
                                      align_up(sizeof(struct virtio_virtq), PAGE_SIZE) / PAGE_SIZE);
    struct virtio_virtq *vq = (struct virtio_virtq *) virtq_paddr;

    vq->queue_index = index;
    vq->used_index = (volatile uint16_t *) &vq->used.index;
    vq->submitted_index = 0;
    vq->consumed_used_index = 0;

    virtio_reg_write32(dev, VIRTIO_REG_QUEUE_SEL, index);
    virtio_reg_write32(dev, VIRTIO_REG_QUEUE_NUM, VIRTQ_ENTRY_NUM);
    virtio_reg_write32(dev, VIRTIO_REG_QUEUE_PFN, virtq_paddr / PAGE_SIZE);
    return vq;
}

void virtq_push(struct virtio_virtq *vq, int desc_index) {
    uint16_t avail_index = vq->avail.index;
    vq->avail.ring[avail_index % VIRTQ_ENTRY_NUM] = desc_index;
    __sync_synchronize();
    vq->avail.index = avail_index + 1;
    __sync_synchronize();
}

void virtq_notify(struct virtio_device *dev, struct virtio_virtq *vq) {
    virtio_reg_write32(dev, VIRTIO_REG_QUEUE_NOTIFY, vq->queue_index);
}

void virtq_kick(struct virtio_device *dev, struct virtio_virtq *vq, int desc_index) {
    virtq_push(vq, desc_index);
    vq->submitted_index++;
    virtq_notify(dev, vq);
}

bool virtq_pop_used(struct virtio_virtq *vq, struct virtq_used_elem *elem) {
    if (vq->consumed_used_index == *vq->used_index)
        return false;

    __sync_synchronize();
    *elem = vq->used.ring[vq->consumed_used_index % VIRTQ_ENTRY_NUM];
    vq->consumed_used_index++;
    return true;
}

bool virtq_is_busy(struct virtio_virtq *vq) {
    return vq->consumed_used_index != vq->submitted_index;
}
