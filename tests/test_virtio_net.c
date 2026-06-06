#include "test_virtio_net.h"
#include "common.h"
#include "virtio_net.h"

static uint8_t rx_test_frame[ETH_MAX_FRAME_SIZE];

static void print_hex_byte(uint8_t value) {
    const char digits[] = "0123456789abcdef";
    putchar(digits[value >> 4]);
    putchar(digits[value & 0x0f]);
}

static void print_mac(const uint8_t *mac) {
    for (int i = 0; i < 6; i++) {
        if (i != 0)
            putchar(':');
        print_hex_byte(mac[i]);
    }
}

static void dump_frame(const uint8_t *frame, uint32_t len) {
    if (len >= 14) {
        printf("test_virtio_net_rx: dst=");
        print_mac(frame);
        printf(" src=");
        print_mac(frame + 6);
        printf(" ethertype=0x%x\n",
               ((uint16_t) frame[12] << 8) | frame[13]);
    }

    printf("test_virtio_net_rx: frame dump (%d bytes)\n", len);
    for (uint32_t offset = 0; offset < len; offset += 16) {
        printf("  %x: ", offset);

        for (uint32_t column = 0; column < 16; column++) {
            uint32_t index = offset + column;
            if (index < len) {
                print_hex_byte(frame[index]);
                putchar(' ');
            } else {
                printf("   ");
            }
        }

        printf(" |");
        for (uint32_t column = 0; column < 16 && offset + column < len; column++) {
            uint8_t ch = frame[offset + column];
            putchar(ch >= 32 && ch <= 126 ? ch : '.');
        }
        printf("|\n");
    }
}

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
    uint32_t displayed = 0;
    uint32_t reported_drops = virtio_net_rx_capture_dropped();

    printf("test_virtio_net_rx: monitoring received frames\n");
    printf("test_virtio_net_rx: press x to stop\n");

    for (;;) {
        uint32_t frame_len;
        uint16_t ethertype;

        while (virtio_net_pop_rx_frame(rx_test_frame,
                                       sizeof(rx_test_frame),
                                       &frame_len,
                                       &ethertype)) {
            displayed++;
            printf("\ntest_virtio_net_rx: packet=%d len=%d ethertype=0x%x\n",
                   displayed, frame_len, ethertype);
            dump_frame(rx_test_frame, frame_len);
        }

        uint32_t dropped = virtio_net_rx_capture_dropped();
        if (dropped != reported_drops) {
            printf("test_virtio_net_rx: WARN capture queue dropped=%d\n",
                   dropped - reported_drops);
            reported_drops = dropped;
        }

        int ch = getchar();
        if (ch == 'x' || ch == 'X')
            break;

        for (int i = 0; i < 1000; i++)
            __asm__ __volatile__("nop");
    }

    printf("test_virtio_net_rx: stopped packets=%d dropped=%d\n",
           displayed, reported_drops);
}
