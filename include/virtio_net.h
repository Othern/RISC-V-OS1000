#pragma once
#include "virtio.h"

#define ETH_MAX_FRAME_SIZE 1514
#define VIRTIO_NET_TEST_ETHERTYPE 0x88b5

struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed));

void virtio_net_init(void);
void virtio_net_irq(void);
void virtio_net_poll(void);
int virtio_net_send_packet(const void *payload, int len);
void virtio_net_send_test_packet(void);
uint32_t virtio_net_rx_packets(void);
uint32_t virtio_net_rx_test_packets(void);
uint32_t virtio_net_last_rx_len(void);
uint16_t virtio_net_last_rx_ethertype(void);
uint32_t virtio_net_copy_last_test_frame(void *dst, uint32_t capacity);
bool virtio_net_pop_rx_frame(void *dst, uint32_t capacity,
                             uint32_t *frame_len, uint16_t *ethertype);
uint32_t virtio_net_rx_capture_dropped(void);
uint32_t virtio_net_tx_packets(void);
uint32_t virtio_net_last_tx_len(void);
