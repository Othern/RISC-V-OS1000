#include "user.h"

void main(void) {
    // *((volatile int *) 0x80200000) = 0x1234; 
    // process in user mode try to access kernel page
    for (;;);
}
