#pragma once
#include "common.h"

#define ARP_ETHERTYPE 0x0806
#define ARP_CACHE_SIZE 8

void arp_init(void);
void arp_receive(const uint8_t *frame, uint32_t frame_len);
void arp_poll(void);
int arp_request(uint32_t target_ip);
bool arp_lookup(uint32_t ip, uint8_t mac[6]);
void arp_dump_cache(void);
