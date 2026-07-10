#pragma once
#include "common.h"

#define IPV4_ETHERTYPE 0x0800
#define IPV4_HEADER_MIN_SIZE 20
#define IPV4_MAX_PACKET_SIZE 1500
#define IPV4_LOCAL_ADDRESS 0x0a00020f
#define IPV4_BROADCAST_ADDRESS 0xffffffff

#define IPV4_PROTOCOL_ICMP 1
#define IPV4_PROTOCOL_TCP  6
#define IPV4_PROTOCOL_UDP  17

void ipv4_init(void);
void ipv4_receive(const uint8_t *frame, uint32_t frame_len);
int ipv4_send(uint32_t dst_ip, uint8_t protocol,
              const void *payload, uint16_t payload_len);
void ipv4_dump_stats(void);
