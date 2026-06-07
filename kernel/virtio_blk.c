#include "virtio_blk.h"
#include "allocator.h"

static struct virtio_device blk_dev;
static struct virtio_virtq *blk_request_vq;
static struct virtio_blk_req *blk_req;
static uint32_t blk_req_region;
static paddr_t blk_req_paddr;
static uint64_t blk_capacity;
static volatile bool blk_done;

void virtio_blk_init(void) {
    if (!virtio_probe(&blk_dev, VIRTIO_BLK_PADDR, VIRTIO_DEVICE_BLK))
        PANIC("virtio-blk: device not found at 0x%x", VIRTIO_BLK_PADDR);

    virtio_begin_init(&blk_dev);
    blk_request_vq = virtq_init(&blk_dev, 0);
    virtio_finish_init(&blk_dev);

    blk_capacity = virtio_reg_read64(&blk_dev, VIRTIO_REG_DEVICE_CONFIG + 0) * SECTOR_SIZE;
    printf("virtio-blk: base=0x%x capacity=%d bytes\n",
           blk_dev.base, (int) blk_capacity);

    blk_req_paddr = alloc_pages(&blk_req_region,
                                align_up(sizeof(*blk_req), PAGE_SIZE) / PAGE_SIZE);
    blk_req = (struct virtio_blk_req *) blk_req_paddr;
    blk_done = false;

    uint32_t irq_status = virtio_irq_status(&blk_dev);
    if (irq_status)
        virtio_irq_ack(&blk_dev, irq_status);
}

void virtio_blk_irq(void) {
    // 讀取中斷原因
    uint32_t status = virtio_irq_status(&blk_dev);
    if (status)
        // ACK VirtIO device 將讀到的 status 寫入 InterruptACK register，通知裝置：我已經知道這次中斷的原因。
        virtio_irq_ack(&blk_dev, status);
    // 確認是否為 request completion，如果 bit 0 沒有設定，表示這次不是 used-buffer completion，函式直接返回。例如只有 config-change bit 時，目前 driver 不做進一步處理。
    if (!(status & VIRTIO_INT_USED_BUFFER))
        return;

    struct virtq_used_elem used;
    bool completed = false;
    while (virtq_pop_used(blk_request_vq, &used))
        completed = true;

    if (completed)
        blk_done = true;
}

void read_write_disk(void *buf, unsigned sector, int is_write) {
    if (sector >= blk_capacity / SECTOR_SIZE) {
        printf("virtio: tried to read/write sector=%d, but capacity is %d\n",
              sector, blk_capacity / SECTOR_SIZE);
        return;
    }

    blk_req->sector = sector;
    blk_req->type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    if (is_write)
        memcpy(blk_req->data, buf, SECTOR_SIZE);

    struct virtio_virtq *vq = blk_request_vq;
    vq->descs[0].addr = blk_req_paddr;
    vq->descs[0].len = sizeof(uint32_t) * 2 + sizeof(uint64_t);
    vq->descs[0].flags = VIRTQ_DESC_F_NEXT;
    vq->descs[0].next = 1;

    vq->descs[1].addr = blk_req_paddr + offsetof(struct virtio_blk_req, data);
    vq->descs[1].len = SECTOR_SIZE;
    vq->descs[1].flags = VIRTQ_DESC_F_NEXT | (is_write ? 0 : VIRTQ_DESC_F_WRITE);
    vq->descs[1].next = 2;

    vq->descs[2].addr = blk_req_paddr + offsetof(struct virtio_blk_req, status);
    vq->descs[2].len = sizeof(uint8_t);
    vq->descs[2].flags = VIRTQ_DESC_F_WRITE;

    blk_done = false;
    virtq_kick(&blk_dev, vq, 0);

    wait_for_interrupt(&blk_done);

    if (blk_req->status != 0) {
        printf("virtio: warn: failed to read/write sector=%d status=%d\n",
               sector, blk_req->status);
        return;
    }

    if (!is_write)
        memcpy(buf, blk_req->data, SECTOR_SIZE);
}
