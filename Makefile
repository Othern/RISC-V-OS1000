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
DISK := lorem.txt
OPEN_SBI :=  opensbi-riscv32-generic-fw_dynamic.bin

USER_SRCS := user/shell.c user/user.c kernel/common.c
KERNEL_SRCS := \
	kernel/kernel.c \
	kernel/syscall.c \
	kernel/common.c \
	tests/test_common.c \
	kernel/allocator.c \
	kernel/process.c \
	tests/test_process.c \
	tests/test_virtio.c \
	kernel/virtio.c \
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
	@echo "Lorem ipsum dolor sit amet, consectetur adipiscing elit. In ut magna consequat, cursus velit aliquam, scelerisque odio. Ut lorem eros, feugiat quis bibendum vitae, malesuada ac orci. Praesent eget quam non nunc fringilla cursus imperdiet non tellus. Aenean dictum lobortis turpis, non interdum leo rhoncus sed. Cras in tellus auctor, faucibus tortor ut, maximus metus. Praesent placerat ut magna non tristique. Pellentesque at nunc quis dui tempor vulputate. Vestibulum vitae massa orci. Mauris et tellus quis risus sagittis placerat. Integer lorem leo, feugiat sed molestie non, viverra a tellus." > $(DISK)

$(OPEN_SBI):
	curl -LO https://github.com/qemu/qemu/raw/v8.0.4/pc-bios/$(OPEN_SBI)

run: $(KERNEL_ELF) $(DISK) $(OPEN_SBI)
	$(QEMU) -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
		-d unimp,guest_errors,int,cpu_reset -D $(BUILD_DIR)/qemu.log \
		-drive id=drive0,file=$(DISK),format=raw,if=none \
		-device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0 \
		-kernel $(KERNEL_ELF)

test:
	$(MAKE) clean
	$(MAKE) CFLAGS_EXTRA=-DTEST run

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(DISK)
