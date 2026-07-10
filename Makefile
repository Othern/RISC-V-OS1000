QEMU := qemu-system-riscv32
CC := clang
OBJCOPY := llvm-objcopy

BUILD_DIR := build
INCLUDES := -Iinclude -Itests
CFLAGS_EXTRA ?=
CFLAGS := -std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf \
	-fuse-ld=lld -fno-stack-protector -ffreestanding -nostdlib $(INCLUDES) \
	$(CFLAGS_EXTRA)

USER_LD := linker/user.ld
KERNEL_LD := linker/kernel.ld
DISK := disk.tar
OPEN_SBI :=  opensbi-riscv32-generic-fw_dynamic.bin
NETDEV ?= user

ifeq ($(NETDEV),tap)
NETDEV_ARGS := -netdev tap,id=net0,ifname=tap0,script=no,downscript=no
else
NETDEV_ARGS := -netdev user,id=net0
endif

USER_SRCS := user/shell.c user/user.c kernel/common.c
KERNEL_SRCS := \
	kernel/kernel.c \
	kernel/syscall.c \
	kernel/common.c \
	kernel/interrupt.c \
	tests/test_common.c \
	kernel/allocator.c \
	kernel/plic.c \
	kernel/process.c \
	tests/test_process.c \
	tests/test_virtio.c \
	tests/test_virtio_net.c \
	kernel/virtio.c \
	kernel/virtio_blk.c \
	kernel/virtio_net.c \
	kernel/arp.c \
	kernel/ipv4.c \
	kernel/shm.c \
	kernel/filesystem.c \
	kernel/sbi.c

HEADERS := $(wildcard include/*.h tests/*.h)

SHELL_ELF := $(BUILD_DIR)/shell.elf
SHELL_BIN := $(BUILD_DIR)/shell.bin
SHELL_OBJ := $(BUILD_DIR)/shell.bin.o
KERNEL_ELF := $(BUILD_DIR)/kernel.elf

.PHONY: all shell kernel run test clean

all: kernel $(OPEN_SBI)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

shell: $(SHELL_OBJ)

$(SHELL_ELF): $(USER_SRCS) $(USER_LD) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Wl,-T$(USER_LD) -Wl,-Map=$(BUILD_DIR)/shell.map -o $@ $(USER_SRCS)

$(SHELL_BIN): $(SHELL_ELF)
	$(OBJCOPY) --set-section-flags .bss=alloc,contents -O binary $< $@

$(SHELL_OBJ): $(SHELL_BIN)
	cd $(BUILD_DIR) && $(OBJCOPY) -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o

kernel: $(KERNEL_ELF) $(DISK)

$(KERNEL_ELF): $(KERNEL_SRCS) $(KERNEL_LD) $(SHELL_OBJ) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Wl,-T$(KERNEL_LD) -Wl,-Map=$(BUILD_DIR)/kernel.map -o $@ $(KERNEL_SRCS) $(SHELL_OBJ)

$(DISK):
	@echo "==> Building $(DISK)"
	@echo "    Source directory: disk/"
	@echo "    Input files: disk/*.txt"
	@echo "    Output file: $(DISK)"
	@cd disk && tar cf ../$(DISK) --format=ustar *.txt
	@echo "==> Finished building $(DISK)"

$(OPEN_SBI):
	curl -LO https://github.com/qemu/qemu/raw/v8.0.4/pc-bios/$(OPEN_SBI)

run: $(KERNEL_ELF) $(DISK) $(OPEN_SBI)
	$(QEMU) -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
		-d unimp,guest_errors,int,cpu_reset -D $(BUILD_DIR)/qemu.log \
		-drive id=drive0,file=$(DISK),format=raw,if=none \
		-device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0 \
		$(NETDEV_ARGS) \
		-device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.1,mac=52:54:00:12:34:56 \
		-kernel $(KERNEL_ELF)

test:
	$(MAKE) clean
	$(MAKE) CFLAGS_EXTRA=-DTEST run

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(DISK)
