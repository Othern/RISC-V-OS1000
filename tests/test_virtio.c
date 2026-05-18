#include "test_virtio.h"
#include "common.h"
#include "virtio.h"

void test_virtio(void) {
    char buf[SECTOR_SIZE];

    read_write_disk(buf, 0, false /* read from the disk */);
    printf("first sector: %s\n", buf);

    strcpy(buf, "hello from kernel!!!\n");
    read_write_disk(buf, 0, true /* write to the disk */);
}
