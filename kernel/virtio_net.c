#include "virtio_net.h"
#include "allocator.h"

#define NET_RX_QUEUE 0
#define NET_TX_QUEUE 1
#define NET_RX_BUF_NUM VIRTQ_ENTRY_NUM

struct virtio_net_buf {
    struct virtio_net_hdr hdr;
    uint8_t frame[ETH_MAX_FRAME_SIZE];
} __attribute__((packed));

static struct virtio_device net_dev;
static struct virtio_virtq *net_rx_vq;
static struct virtio_virtq *net_tx_vq;
static struct virtio_net_buf *net_rx_bufs;
static struct virtio_net_buf *net_tx_buf;
static uint32_t net_rx_region;
static uint32_t net_tx_region;
static paddr_t net_rx_paddr;
static paddr_t net_tx_paddr;
static uint16_t net_rx_used_index;
static uint32_t net_rx_packets;
static uint32_t net_rx_test_packets;
static uint32_t net_last_rx_len;
static uint16_t net_last_rx_ethertype;
static uint32_t net_tx_packets;
static uint32_t net_last_tx_len;

static void virtio_net_enqueue_rx(unsigned index) {
    net_rx_vq->descs[index].addr = net_rx_paddr + index * sizeof(struct virtio_net_buf);
    net_rx_vq->descs[index].len = sizeof(struct virtio_net_buf);
    net_rx_vq->descs[index].flags = VIRTQ_DESC_F_WRITE;
    net_rx_vq->descs[index].next = 0;

    virtq_push(net_rx_vq, index);
}

void virtio_net_init(void) {
    if (!virtio_find_device(&net_dev, VIRTIO_DEVICE_NET)) {
        printf("virtio-net: device not found\n");
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
    net_rx_used_index = 0;
    net_rx_packets = 0;
    net_rx_test_packets = 0;
    net_last_rx_len = 0;
    net_last_rx_ethertype = 0;
    net_tx_packets = 0;
    net_last_tx_len = 0;

    for (unsigned i = 0; i < NET_RX_BUF_NUM; i++)
        virtio_net_enqueue_rx(i);
    virtio_finish_init(&net_dev);
    virtq_notify(&net_dev, net_rx_vq);

    printf("virtio-net: base=0x%x rxq=%d txq=%d\n",
           net_dev.base, NET_RX_QUEUE, NET_TX_QUEUE);
}

void virtio_net_poll(void) {
    if (!net_rx_vq)
        return;

    while (net_rx_used_index != *net_rx_vq->used_index) {
        struct virtq_used_elem *used =
            &net_rx_vq->used.ring[net_rx_used_index % VIRTQ_ENTRY_NUM];
        unsigned desc_index = used->id;
        unsigned packet_len = used->len;

        if (packet_len >= sizeof(struct virtio_net_hdr) + 14) {
            uint8_t *frame = net_rx_bufs[desc_index].frame;
            uint16_t eth_type = ((uint16_t) frame[12] << 8) | frame[13];
            net_rx_packets++;
            net_last_rx_len = packet_len - sizeof(struct virtio_net_hdr);
            net_last_rx_ethertype = eth_type;
            if (eth_type == VIRTIO_NET_TEST_ETHERTYPE)
                net_rx_test_packets++;
            printf("virtio-net: rx len=%d ethertype=0x%x\n",
                   net_last_rx_len, eth_type);
        } else {
            net_rx_packets++;
            net_last_rx_len = packet_len;
            net_last_rx_ethertype = 0;
            printf("virtio-net: rx short packet len=%d\n", packet_len);
        }

        net_rx_used_index++;
        virtio_net_enqueue_rx(desc_index);
    }

    virtq_notify(&net_dev, net_rx_vq);
}

void virtio_net_send_test_packet(void) {
    if (!net_tx_vq)
        return;

    memset(net_tx_buf, 0, sizeof(*net_tx_buf));

    uint8_t *frame = net_tx_buf->frame;
    for (int i = 0; i < 6; i++)
        frame[i] = 0xff;

    frame[6] = 0x52;
    frame[7] = 0x54;
    frame[8] = 0x00;
    frame[9] = 0x12;
    frame[10] = 0x34;
    frame[11] = 0x56;
    frame[12] = VIRTIO_NET_TEST_ETHERTYPE >> 8;
    frame[13] = VIRTIO_NET_TEST_ETHERTYPE & 0xff;

    const unsigned frame_len = 60;
    net_tx_vq->descs[0].addr = net_tx_paddr;
    net_tx_vq->descs[0].len = sizeof(struct virtio_net_hdr) + frame_len;
    net_tx_vq->descs[0].flags = 0;
    net_tx_vq->descs[0].next = 0;

    virtq_kick(&net_dev, net_tx_vq, 0);

    while (virtq_is_busy(net_tx_vq))
        ;

    net_tx_packets++;
    net_last_tx_len = frame_len;

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

uint32_t virtio_net_tx_packets(void) {
    return net_tx_packets;
}

uint32_t virtio_net_last_tx_len(void) {
    return net_last_tx_len;
}
