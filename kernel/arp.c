#include "arp.h"
#include "kernel.h"
#include "virtio_net.h"

#define ARP_HARDWARE_ETHERNET 1
#define ARP_PROTOCOL_IPV4     0x0800
#define ARP_OPERATION_REQUEST 1
#define ARP_OPERATION_REPLY   2

struct arp_packet {
    uint8_t hardware_type[2];
    uint8_t protocol_type[2];
    uint8_t hardware_len;
    uint8_t protocol_len;
    uint8_t operation[2];
    uint8_t sender_mac[6];
    uint8_t sender_ip[4];
    uint8_t target_mac[6];
    uint8_t target_ip[4];
} __attribute__((packed));

_Static_assert(sizeof(struct arp_packet) == 28,
               "Ethernet/IPv4 ARP packet must be 28 bytes");

struct arp_cache_entry {
    bool valid;
    uint32_t ip;
    uint8_t mac[6];
};

struct arp_pending_reply {
    bool pending;
    uint8_t target_mac[6];
    uint32_t target_ip;
};

static const uint32_t local_ip = 0x0a00020f;
static struct arp_cache_entry arp_cache[ARP_CACHE_SIZE];
static struct arp_pending_reply pending_reply;

static uint16_t read_be16(const uint8_t bytes[2]) {
    return ((uint16_t) bytes[0] << 8) | bytes[1];
}

static void write_be16(uint8_t bytes[2], uint16_t number) {
    bytes[0] = number >> 8;
    bytes[1] = number & 0xff;
}

static uint32_t read_ipv4(const uint8_t ip[4]) {
    return ((uint32_t) ip[0] << 24) |
           ((uint32_t) ip[1] << 16) |
           ((uint32_t) ip[2] << 8) |
           ip[3];
}

static void write_ipv4(uint8_t output[4], uint32_t ip) {
    output[0] = ip >> 24;
    output[1] = ip >> 16;
    output[2] = ip >> 8;
    output[3] = ip;
}

static void copy_mac(uint8_t dst[6], const uint8_t src[6]) {
    memcpy(dst, src, 6);
}

static void print_mac(const uint8_t mac[6]) {
    const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 6; i++) {
        if (i)
            putchar(':');
        putchar(digits[mac[i] >> 4]);
        putchar(digits[mac[i] & 0x0f]);
    }
}

static void print_ipv4(uint32_t ip) {
    printf("%d.%d.%d.%d",
           (ip >> 24) & 0xff,
           (ip >> 16) & 0xff,
           (ip >> 8) & 0xff,
           ip & 0xff);
}

static void arp_cache_update(uint32_t ip, const uint8_t mac[6]) {
    int free_index = -1;

    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            copy_mac(arp_cache[i].mac, mac);
            return;
        }
        if (!arp_cache[i].valid && free_index < 0)
            free_index = i;
    }

    if (free_index < 0)
        free_index = 0;
    arp_cache[free_index].valid = true;
    arp_cache[free_index].ip = ip;
    copy_mac(arp_cache[free_index].mac, mac);
}

static void arp_build_packet(struct arp_packet *packet, uint16_t operation,
                             const uint8_t target_mac[6],
                             uint32_t target_ip) {
    memset(packet, 0, sizeof(*packet));
    write_be16(packet->hardware_type, ARP_HARDWARE_ETHERNET);
    write_be16(packet->protocol_type, ARP_PROTOCOL_IPV4);
    packet->hardware_len = 6;
    packet->protocol_len = 4;
    write_be16(packet->operation, operation);
    copy_mac(packet->sender_mac, virtio_net_mac());
    write_ipv4(packet->sender_ip, local_ip);
    copy_mac(packet->target_mac, target_mac);
    write_ipv4(packet->target_ip, target_ip);
}

void arp_init(void) {
    memset(arp_cache, 0, sizeof(arp_cache));
    memset(&pending_reply, 0, sizeof(pending_reply));
    printf("arp: local ip=");
    print_ipv4(local_ip);
    printf("\n");
}

void arp_receive(const uint8_t *frame, uint32_t frame_len) {
    if (frame_len < ETHERNET_HEADER_SIZE + sizeof(struct arp_packet))
        return;

    const struct arp_packet *packet =
        (const struct arp_packet *) (frame + ETHERNET_HEADER_SIZE);
    if (read_be16(packet->hardware_type) != ARP_HARDWARE_ETHERNET ||
        read_be16(packet->protocol_type) != ARP_PROTOCOL_IPV4 ||
        packet->hardware_len != 6 ||
        packet->protocol_len != 4)
        return;

    uint32_t sender_ip = read_ipv4(packet->sender_ip);
    uint32_t target_ip = read_ipv4(packet->target_ip);
    uint16_t operation = read_be16(packet->operation);
    arp_cache_update(sender_ip, packet->sender_mac);

    if (operation == ARP_OPERATION_REQUEST && target_ip == local_ip) {
        copy_mac(pending_reply.target_mac, packet->sender_mac);
        pending_reply.target_ip = sender_ip;
        __sync_synchronize();
        pending_reply.pending = true;
    }
}

void arp_poll(void) {
    uint8_t target_mac[6];
    uint32_t target_ip;
    uint32_t saved_sstatus = READ_CSR(sstatus);

    WRITE_CSR(sstatus, saved_sstatus & ~SSTATUS_SIE);
    if (!pending_reply.pending) {
        WRITE_CSR(sstatus, saved_sstatus);
        return;
    }
    copy_mac(target_mac, pending_reply.target_mac);
    target_ip = pending_reply.target_ip;
    pending_reply.pending = false;
    WRITE_CSR(sstatus, saved_sstatus);

    struct arp_packet reply;
    arp_build_packet(&reply, ARP_OPERATION_REPLY, target_mac, target_ip);
    if (virtio_net_send_ethernet(target_mac, ARP_ETHERTYPE,
                                 &reply, sizeof(reply)) >= 0) {
        printf("arp: replied to ");
        print_ipv4(target_ip);
        printf("\n");
    }
}

int arp_request(uint32_t target_ip) {
    static const uint8_t broadcast_mac[6] =
        {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    static const uint8_t unknown_mac[6] = {0, 0, 0, 0, 0, 0};
    struct arp_packet request;

    arp_build_packet(&request, ARP_OPERATION_REQUEST,
                     unknown_mac, target_ip);
    return virtio_net_send_ethernet(broadcast_mac, ARP_ETHERTYPE,
                                    &request, sizeof(request));
}

void arp_dump_cache(void) {
    bool found = false;

    printf("ARP cache:\n");
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid)
            continue;
        found = true;
        printf("  ");
        print_ipv4(arp_cache[i].ip);
        printf(" -> ");
        print_mac(arp_cache[i].mac);
        printf("\n");
    }

    if (!found)
        printf("  empty\n");
}
