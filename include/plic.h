#pragma once
#include "common.h"

#define PLIC_BASE       0x0c000000
#define PLIC_SENABLE    (PLIC_BASE + 0x2080)
#define PLIC_STHRESHOLD (PLIC_BASE + 0x201000)
#define PLIC_SCLAIM     (PLIC_BASE + 0x201004)

#define VIRTIO_BLK_IRQ 1
#define VIRTIO_NET_IRQ 2

void plic_init(void);
void plic_enable(unsigned irq);
uint32_t plic_claim(void);
void plic_complete(uint32_t irq);
