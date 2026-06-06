#include "test_virtio_net.h"
#include "common.h"
#include "virtio_net.h"

#define VIRTIO_NET_RX_TEST_ROUNDS 30

void test_virtio_net_tx(void) {
    uint32_t before = virtio_net_tx_packets();

    printf("test_virtio_net_tx: sending ethertype 0x%x broadcast frame\n",
           VIRTIO_NET_TEST_ETHERTYPE);
    virtio_net_send_test_packet();

    uint32_t after = virtio_net_tx_packets();
    if (after == before) {
        printf("test_virtio_net_tx: WARN tx completion not observed\n");
        return;
    }

    printf("test_virtio_net_tx: PASS packets=%d last_len=%d\n",
           after - before,
           virtio_net_last_tx_len());
}

void test_virtio_net_rx(void) {
    uint32_t packets_before = virtio_net_rx_packets();
    uint32_t test_packets_before = virtio_net_rx_test_packets();

    printf("test_virtio_net_rx: waiting for host-injected ethernet frames\n");
    printf("test_virtio_net_rx: send ethertype 0x%x to dst mac 52:54:00:12:34:56\n",
           VIRTIO_NET_TEST_ETHERTYPE);

    for (int i = 0; i < VIRTIO_NET_RX_TEST_ROUNDS; i++) {
        printf("test_virtio_net_rx: receive round %d\n", i);
        if (virtio_net_rx_test_packets() != test_packets_before)
            break;
        for(int j=0; j < 1000; j++) delay();
    }

    uint32_t packets_after = virtio_net_rx_packets();
    uint32_t test_packets_after = virtio_net_rx_test_packets();
    if (test_packets_after == test_packets_before) {
        printf("test_virtio_net_rx: WARN no test packets observed");
        printf(" total_rx=%d last_ethertype=0x%x\n",
               packets_after - packets_before,
               virtio_net_last_rx_ethertype());
        return;
    }

    printf("test_virtio_net_rx: PASS packets=%d last_len=%d last_ethertype=0x%x\n",
           test_packets_after - test_packets_before,
           virtio_net_last_rx_len(),
           virtio_net_last_rx_ethertype());
}
