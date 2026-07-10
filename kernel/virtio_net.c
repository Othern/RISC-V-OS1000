#include "virtio_net.h"
#include "allocator.h"
#include "arp.h"
#include "ipv4.h"

#define NET_RX_QUEUE 0
#define NET_TX_QUEUE 1
#define NET_RX_BUF_NUM VIRTQ_ENTRY_NUM
#define NET_RX_CAPTURE_NUM VIRTQ_ENTRY_NUM

struct virtio_net_buf {
    struct virtio_net_hdr hdr;
    uint8_t frame[ETH_MAX_FRAME_SIZE];
} __attribute__((packed));

struct virtio_net_capture {
    uint32_t len;
    uint16_t ethertype;
    uint8_t frame[ETH_MAX_FRAME_SIZE];
};

static struct virtio_device net_dev;
static const uint8_t net_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static struct virtio_virtq *net_rx_vq;
static struct virtio_virtq *net_tx_vq;
static struct virtio_net_buf *net_rx_bufs;
static struct virtio_net_buf *net_tx_buf;
static uint32_t net_rx_region;
static uint32_t net_tx_region;
static paddr_t net_rx_paddr;
static paddr_t net_tx_paddr;
static uint32_t net_rx_packets;
static uint32_t net_rx_test_packets;
static uint32_t net_last_rx_len;
static uint16_t net_last_rx_ethertype;
static uint8_t net_last_test_frame[ETH_MAX_FRAME_SIZE];
static uint32_t net_last_test_frame_len;
static struct virtio_net_capture net_rx_captures[NET_RX_CAPTURE_NUM];
static volatile uint16_t net_rx_capture_write;
static volatile uint16_t net_rx_capture_read;
static volatile uint32_t net_rx_capture_drops;
static uint32_t net_tx_packets;
static uint32_t net_last_tx_len;
static uint32_t net_pending_tx_len;
static volatile bool net_tx_done;

static void virtio_net_enqueue_rx(unsigned index) {
    net_rx_vq->descs[index].addr = net_rx_paddr + index * sizeof(struct virtio_net_buf);
    net_rx_vq->descs[index].len = sizeof(struct virtio_net_buf);
    net_rx_vq->descs[index].flags = VIRTQ_DESC_F_WRITE;
    net_rx_vq->descs[index].next = 0;

    virtq_push(net_rx_vq, index);
}

void virtio_net_init(void) {
    if (!virtio_find_device(&net_dev, VIRTIO_DEVICE_NET)) {
        PANIC("virtio-net: device not found\n");
        return;
    }

    virtio_begin_init(&net_dev);
    net_rx_vq = virtq_init(&net_dev, NET_RX_QUEUE);
    net_tx_vq = virtq_init(&net_dev, NET_TX_QUEUE);

    net_rx_paddr = alloc_pages(&net_rx_region,
                               align_up(sizeof(struct virtio_net_buf) * NET_RX_BUF_NUM, PAGE_SIZE) / PAGE_SIZE);
    net_tx_paddr = alloc_pages(&net_tx_region,
                               align_up(sizeof(struct virtio_net_buf), PAGE_SIZE) / PAGE_SIZE);
    net_rx_bufs = (struct virtio_net_buf *) net_rx_paddr;
    net_tx_buf = (struct virtio_net_buf *) net_tx_paddr;
    net_rx_packets = 0;
    net_rx_test_packets = 0;
    net_last_rx_len = 0;
    net_last_rx_ethertype = 0;
    net_last_test_frame_len = 0;
    net_rx_capture_write = 0;
    net_rx_capture_read = 0;
    net_rx_capture_drops = 0;
    net_tx_packets = 0;
    net_last_tx_len = 0;
    net_pending_tx_len = 0;
    net_tx_done = false;

    for (unsigned i = 0; i < NET_RX_BUF_NUM; i++)
        virtio_net_enqueue_rx(i);
    virtio_finish_init(&net_dev);
    virtq_notify(&net_dev, net_rx_vq);

    printf("virtio-net: base=0x%x rxq=%d txq=%d\n",
           net_dev.base, NET_RX_QUEUE, NET_TX_QUEUE);
}

static void virtio_net_capture_rx(const uint8_t *frame, uint32_t frame_len,
                                  uint16_t ethertype) {
    if ((uint16_t) (net_rx_capture_write - net_rx_capture_read) >=
        NET_RX_CAPTURE_NUM) {
        net_rx_capture_drops++;
        return;
    }

    struct virtio_net_capture *capture =
        &net_rx_captures[net_rx_capture_write % NET_RX_CAPTURE_NUM];
    capture->len = frame_len;
    capture->ethertype = ethertype;
    memcpy(capture->frame, frame, frame_len);
    __sync_synchronize();
    net_rx_capture_write++;
}

static void virtio_net_drain_rx(void) {
    struct virtq_used_elem used;
    bool recycled = false;

    while (virtq_pop_used(net_rx_vq, &used)) {
        unsigned desc_index = used.id;
        unsigned packet_len = used.len;

        if (packet_len >= sizeof(struct virtio_net_hdr) + 14) {
            uint8_t *frame = net_rx_bufs[desc_index].frame;
            uint32_t frame_len = packet_len - sizeof(struct virtio_net_hdr);
            if (frame_len > ETH_MAX_FRAME_SIZE)
                frame_len = ETH_MAX_FRAME_SIZE;
            uint16_t eth_type = ((uint16_t) frame[12] << 8) | frame[13];
            net_rx_packets++;
            net_last_rx_len = frame_len;
            net_last_rx_ethertype = eth_type;
            virtio_net_capture_rx(frame, frame_len, eth_type);
            if (eth_type == ARP_ETHERTYPE)
                arp_receive(frame, frame_len);
            if (eth_type == IPV4_ETHERTYPE)
                ipv4_receive(frame, frame_len);
            if (eth_type == VIRTIO_NET_TEST_ETHERTYPE) {
                memcpy(net_last_test_frame, frame, frame_len);
                net_last_test_frame_len = frame_len;
                net_rx_test_packets++;
            }

        } else {
            net_rx_packets++;
            net_last_rx_len = packet_len;
            net_last_rx_ethertype = 0;
        }

        virtio_net_enqueue_rx(desc_index);
        recycled = true;
    }

    if (recycled)
        virtq_notify(&net_dev, net_rx_vq);
}

static void virtio_net_drain_tx(void) {
    struct virtq_used_elem used;

    while (virtq_pop_used(net_tx_vq, &used)) {
        net_tx_packets++;
        net_last_tx_len = net_pending_tx_len;
        net_tx_done = true;
    }
}

void virtio_net_irq(void) {
    uint32_t status = virtio_irq_status(&net_dev);
    if (status)
        virtio_irq_ack(&net_dev, status);

    if (status & VIRTIO_INT_USED_BUFFER) {
        virtio_net_drain_rx();
        virtio_net_drain_tx();
    }
}

void virtio_net_poll(void) {
    if (!net_rx_vq)
        return;

    virtio_net_drain_rx();
    virtio_net_drain_tx();
}

const uint8_t *virtio_net_mac(void) {
    return net_mac;
}

int virtio_net_send_ethernet(const uint8_t dst_mac[6], uint16_t ethertype,
                             const void *payload, int len) {
    const int max_payload_len = ETH_MAX_FRAME_SIZE - ETHERNET_HEADER_SIZE;

    if (!net_tx_vq || !dst_mac || !payload ||
        len < 0 || len > max_payload_len)
        return -1;

    memset(net_tx_buf, 0, sizeof(*net_tx_buf));

    uint8_t *frame = net_tx_buf->frame;
    memcpy(frame, dst_mac, 6);
    memcpy(frame + 6, net_mac, 6);
    frame[12] = ethertype >> 8;
    frame[13] = ethertype & 0xff;
    memcpy(frame + ETHERNET_HEADER_SIZE, payload, len);

    unsigned frame_len = ETHERNET_HEADER_SIZE + len;
    if (frame_len < 60)
        frame_len = 60;

    net_tx_vq->descs[0].addr = net_tx_paddr;
    net_tx_vq->descs[0].len = sizeof(struct virtio_net_hdr) + frame_len;
    net_tx_vq->descs[0].flags = 0;
    net_tx_vq->descs[0].next = 0;

    net_pending_tx_len = frame_len;
    net_tx_done = false;
    virtq_kick(&net_dev, net_tx_vq, 0);

    wait_for_interrupt(&net_tx_done);

    return len;
}

int virtio_net_send_packet(const void *payload, int len) {
    static const uint8_t broadcast_mac[6] =
        {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    return virtio_net_send_ethernet(broadcast_mac,
                                    VIRTIO_NET_TEST_ETHERTYPE,
                                    payload, len);
}

void virtio_net_send_test_packet(void) {
    if (!net_tx_vq)
        return;

    memset(net_tx_buf, 0, sizeof(*net_tx_buf));

    uint8_t *frame = net_tx_buf->frame;
    for (int i = 0; i < 6; i++)
        frame[i] = 0xff;

    memcpy(frame + 6, net_mac, 6);
    frame[12] = VIRTIO_NET_TEST_ETHERTYPE >> 8;
    frame[13] = VIRTIO_NET_TEST_ETHERTYPE & 0xff;

    const unsigned frame_len = 60;
    net_tx_vq->descs[0].addr = net_tx_paddr;
    net_tx_vq->descs[0].len = sizeof(struct virtio_net_hdr) + frame_len;
    net_tx_vq->descs[0].flags = 0;
    net_tx_vq->descs[0].next = 0;

    net_pending_tx_len = frame_len;
    net_tx_done = false;
    virtq_kick(&net_dev, net_tx_vq, 0);

    wait_for_interrupt(&net_tx_done);

    printf("virtio-net: tx test frame len=%d\n", frame_len);
}

uint32_t virtio_net_rx_packets(void) {
    return net_rx_packets;
}

uint32_t virtio_net_rx_test_packets(void) {
    return net_rx_test_packets;
}

uint32_t virtio_net_last_rx_len(void) {
    return net_last_rx_len;
}

uint16_t virtio_net_last_rx_ethertype(void) {
    return net_last_rx_ethertype;
}

uint32_t virtio_net_copy_last_test_frame(void *dst, uint32_t capacity) {
    uint32_t saved_sstatus = READ_CSR(sstatus);
    WRITE_CSR(sstatus, saved_sstatus & ~SSTATUS_SIE);

    uint32_t copy_len = net_last_test_frame_len;
    if (copy_len > capacity)
        copy_len = capacity;
    memcpy(dst, net_last_test_frame, copy_len);

    WRITE_CSR(sstatus, saved_sstatus);
    return copy_len;
}

bool virtio_net_pop_rx_frame(void *dst, uint32_t capacity,
                             uint32_t *frame_len, uint16_t *ethertype) {
    uint32_t saved_sstatus = READ_CSR(sstatus);
    WRITE_CSR(sstatus, saved_sstatus & ~SSTATUS_SIE);

    if (net_rx_capture_read == net_rx_capture_write) {
        WRITE_CSR(sstatus, saved_sstatus);
        return false;
    }

    struct virtio_net_capture *capture =
        &net_rx_captures[net_rx_capture_read % NET_RX_CAPTURE_NUM];
    uint32_t copy_len = capture->len;
    if (copy_len > capacity)
        copy_len = capacity;
    memcpy(dst, capture->frame, copy_len);
    *frame_len = copy_len;
    *ethertype = capture->ethertype;
    net_rx_capture_read++;

    WRITE_CSR(sstatus, saved_sstatus);
    return true;
}

uint32_t virtio_net_rx_capture_dropped(void) {
    return net_rx_capture_drops;
}

uint32_t virtio_net_tx_packets(void) {
    return net_tx_packets;
}

uint32_t virtio_net_last_tx_len(void) {
    return net_last_tx_len;
}
