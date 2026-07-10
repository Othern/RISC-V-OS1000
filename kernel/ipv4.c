#include "ipv4.h"
#include "arp.h"
#include "kernel.h"
#include "virtio_net.h"

#define IPV4_VERSION 4
#define IPV4_TTL_DEFAULT 64
#define IPV4_FLAG_MORE_FRAGMENTS 0x2000
#define IPV4_FRAGMENT_OFFSET_MASK 0x1fff

struct ipv4_header {
    uint8_t version_ihl;
    uint8_t dscp_ecn;
    uint8_t total_length[2];
    uint8_t identification[2];
    uint8_t flags_fragment_offset[2];
    uint8_t ttl;
    uint8_t protocol;
    uint8_t header_checksum[2];
    uint8_t src_ip[4];
    uint8_t dst_ip[4];
} __attribute__((packed));

_Static_assert(sizeof(struct ipv4_header) == IPV4_HEADER_MIN_SIZE,
               "IPv4 header without options must be 20 bytes");

struct ipv4_stats {
    uint32_t rx_packets;
    uint32_t rx_dropped;
    uint32_t rx_icmp;
    uint32_t rx_udp;
    uint32_t rx_tcp;
    uint32_t rx_other;
    uint32_t tx_packets;
    uint32_t tx_dropped;
};

static struct ipv4_stats stats;
static uint16_t next_identification;

static uint16_t read_be16(const uint8_t bytes[2]) {
    return ((uint16_t) bytes[0] << 8) | bytes[1];
}

static void write_be16(uint8_t bytes[2], uint16_t number) {
    bytes[0] = number >> 8;
    bytes[1] = number & 0xff;
}

static uint32_t read_ipv4_addr(const uint8_t ip[4]) {
    return ((uint32_t) ip[0] << 24) |
           ((uint32_t) ip[1] << 16) |
           ((uint32_t) ip[2] << 8) |
           ip[3];
}

static void write_ipv4_addr(uint8_t output[4], uint32_t ip) {
    output[0] = ip >> 24;
    output[1] = ip >> 16;
    output[2] = ip >> 8;
    output[3] = ip;
}

static void print_ipv4(uint32_t ip) {
    printf("%d.%d.%d.%d",
           (ip >> 24) & 0xff,
           (ip >> 16) & 0xff,
           (ip >> 8) & 0xff,
           ip & 0xff);
}

static uint16_t ipv4_checksum(const void *data, uint32_t len) {
    const uint8_t *bytes = data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += ((uint16_t) bytes[0] << 8) | bytes[1];
        bytes += 2;
        len -= 2;
    }
    if (len)
        sum += (uint16_t) bytes[0] << 8;

    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);

    return ~sum;
}

static bool ipv4_is_for_us(uint32_t dst_ip) {
    return dst_ip == IPV4_LOCAL_ADDRESS ||
           dst_ip == IPV4_BROADCAST_ADDRESS;
}

void ipv4_init(void) {
    memset(&stats, 0, sizeof(stats));
    next_identification = 1;

    printf("ipv4: local ip=");
    print_ipv4(IPV4_LOCAL_ADDRESS);
    printf("\n");
}

void ipv4_receive(const uint8_t *frame, uint32_t frame_len) {
    if (frame_len < ETHERNET_HEADER_SIZE + IPV4_HEADER_MIN_SIZE) {
        stats.rx_dropped++;
        return;
    }

    const uint8_t *packet = frame + ETHERNET_HEADER_SIZE;
    uint32_t packet_capacity = frame_len - ETHERNET_HEADER_SIZE;
    const struct ipv4_header *header = (const struct ipv4_header *) packet;
    uint8_t version = header->version_ihl >> 4;
    uint8_t ihl = header->version_ihl & 0x0f;
    uint32_t header_len = ihl * 4;

    if (version != IPV4_VERSION ||
        ihl < 5 ||
        header_len > packet_capacity) {
        stats.rx_dropped++;
        return;
    }

    uint16_t total_length = read_be16(header->total_length);
    if (total_length < header_len || total_length > packet_capacity) {
        stats.rx_dropped++;
        return;
    }

    if (ipv4_checksum(header, header_len) != 0) {
        stats.rx_dropped++;
        return;
    }

    uint16_t flags_fragment = read_be16(header->flags_fragment_offset);
    if ((flags_fragment & IPV4_FLAG_MORE_FRAGMENTS) ||
        (flags_fragment & IPV4_FRAGMENT_OFFSET_MASK)) {
        stats.rx_dropped++;
        return;
    }

    uint32_t dst_ip = read_ipv4_addr(header->dst_ip);
    if (!ipv4_is_for_us(dst_ip)) {
        stats.rx_dropped++;
        return;
    }

    stats.rx_packets++;
    switch (header->protocol) {
    case IPV4_PROTOCOL_ICMP:
        stats.rx_icmp++;
        break;
    case IPV4_PROTOCOL_UDP:
        stats.rx_udp++;
        break;
    case IPV4_PROTOCOL_TCP:
        stats.rx_tcp++;
        break;
    default:
        stats.rx_other++;
        break;
    }
}

int ipv4_send(uint32_t dst_ip, uint8_t protocol,
              const void *payload, uint16_t payload_len) {
    if (!payload && payload_len > 0) {
        stats.tx_dropped++;
        return -1;
    }
    if (payload_len > IPV4_MAX_PACKET_SIZE - IPV4_HEADER_MIN_SIZE) {
        stats.tx_dropped++;
        return -1;
    }

    uint8_t dst_mac[6];
    if (dst_ip == IPV4_BROADCAST_ADDRESS) {
        memset(dst_mac, 0xff, sizeof(dst_mac));
    } else if (!arp_lookup(dst_ip, dst_mac)) {
        arp_request(dst_ip);
        stats.tx_dropped++;
        return -2;
    }

    uint8_t packet[IPV4_MAX_PACKET_SIZE];
    struct ipv4_header *header = (struct ipv4_header *) packet;
    uint16_t total_length = IPV4_HEADER_MIN_SIZE + payload_len;

    memset(packet, 0, sizeof(packet));
    header->version_ihl = (IPV4_VERSION << 4) | 5;
    write_be16(header->total_length, total_length);
    write_be16(header->identification, next_identification++);
    write_be16(header->flags_fragment_offset, 0);
    header->ttl = IPV4_TTL_DEFAULT;
    header->protocol = protocol;
    write_ipv4_addr(header->src_ip, IPV4_LOCAL_ADDRESS);
    write_ipv4_addr(header->dst_ip, dst_ip);
    if (payload_len)
        memcpy(packet + IPV4_HEADER_MIN_SIZE, payload, payload_len);
    write_be16(header->header_checksum,
               ipv4_checksum(header, IPV4_HEADER_MIN_SIZE));

    int sent = virtio_net_send_ethernet(dst_mac, IPV4_ETHERTYPE,
                                        packet, total_length);
    if (sent < 0) {
        stats.tx_dropped++;
        return sent;
    }

    stats.tx_packets++;
    return payload_len;
}

void ipv4_dump_stats(void) {
    printf("IPv4 local=");
    print_ipv4(IPV4_LOCAL_ADDRESS);
    printf("\n");
    printf("  rx=%d drop=%d icmp=%d udp=%d tcp=%d other=%d\n",
           stats.rx_packets, stats.rx_dropped, stats.rx_icmp,
           stats.rx_udp, stats.rx_tcp, stats.rx_other);
    printf("  tx=%d drop=%d\n", stats.tx_packets, stats.tx_dropped);
}
