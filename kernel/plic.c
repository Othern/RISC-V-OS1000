#include "plic.h"

static volatile uint32_t *plic_reg(uint32_t address) {
    return (volatile uint32_t *) address;
}

void plic_enable(unsigned irq) {
    volatile uint32_t *priority =
        plic_reg(PLIC_BASE + irq * sizeof(uint32_t));
    volatile uint32_t *enable =
        plic_reg(PLIC_SENABLE + (irq / 32) * sizeof(uint32_t));

    *priority = 1;
    *enable |= 1u << (irq % 32);
}

void plic_init(void) {
    *plic_reg(PLIC_STHRESHOLD) = 0;
    plic_enable(VIRTIO_BLK_IRQ);
    plic_enable(VIRTIO_NET_IRQ);
}

uint32_t plic_claim(void) {
    // read which plic id send interrupt
    // 0 if no pending interrupt
    // when reading this register, the PLIC will autometically mark this irq as handling 
    return *plic_reg(PLIC_SCLAIM);
}

void plic_complete(uint32_t irq) {
    // when finishing, write finished irq in this register 
    *plic_reg(PLIC_SCLAIM) = irq;
}
