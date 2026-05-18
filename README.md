# RISC-V OS1000

這是一個以 RISC-V 32-bit 為目標的簡易教學作業系統。專案目前可透過 `clang` 交叉編譯 kernel 與 user shell，並在 `qemu-system-riscv32` 的 `virt` machine 上執行。

## 專案架構

```text
.
├── Makefile             # build、run、test、clean 的主要入口
├── run.sh               # 相容用 wrapper，等同執行 make run
├── build/               # 編譯輸出目錄
├── include/             # 共用 header
│   ├── allocator.h
│   ├── common.h
│   ├── kernel.h
│   ├── process.h
│   ├── syscall.h
│   ├── user.h
│   └── virtio.h
├── kernel/              # kernel 與核心模組實作
│   ├── allocator.c      # 實體頁面配置器與 region 管理
│   ├── common.c         # printf、memcpy、memset、strcpy、strcmp 等共用函式
│   ├── kernel.c         # 開機入口、trap handler、syscall、SBI console I/O
│   ├── process.c        # process table、context switch、排程、page table mapping
│   ├── syscall.c        # syscall dispatch 與 kernel-side syscall handler
│   └── virtio.c         # VirtIO block 裝置初始化與 sector read/write
├── linker/              # linker scripts
│   ├── kernel.ld        # kernel 載入位置、stack、free RAM
│   └── user.ld          # user program base 與 user stack
├── scripts/
│   └── run.sh           # 進入專案根目錄後執行 make run
├── tests/               # kernel 內建測試程式
│   ├── test_common.c
│   ├── test_common.h
│   ├── test_process.c
│   ├── test_process.h
│   ├── test_virtio.c
│   └── test_virtio.h
└── user/                # user mode runtime 與 shell
    ├── shell.c          # user shell，支援 hello / exit
    └── user.c           # user runtime 與 syscall wrapper
```

## 主要模組說明

### Kernel

`kernel/kernel.c` 是核心主流程所在。`boot()` 由 linker script 指定為 kernel 入口，設定 stack 後跳到 `kernel_main()`。`kernel_main()` 會清空 BSS、設定 trap vector、初始化記憶體配置器、VirtIO block、process table，接著建立 user shell process 並開始排程。

trap 進入點是 `kernel_entry()`，它會保存暫存器後呼叫 `handle_trap()`。目前主要處理 user mode 的 `ecall`，並轉交給 `kernel/syscall.c` 中的 `handle_syscall()`。目前支援：

- `SYS_PUTCHAR`：輸出字元
- `SYS_GETCHAR`：讀取字元，沒有輸入時會 `yield()`
- `SYS_EXIT`：將目前 process 標記為結束並切換出去

### Process 與排程

`kernel/process.c` 管理固定大小的 process table，最大 process 數由 `PROCS_MAX` 定義。每個 process 擁有 PID、process state、kernel stack、saved stack pointer、page table，以及已配置的 memory region index。

`yield()` 會以簡單 round-robin 方式尋找下一個 runnable process。切換時會更新 `satp`、`sscratch`，再透過 `switch_context()` 保存與還原 callee-saved registers。

### 記憶體配置

`kernel/allocator.c` 使用 `region_t regions[MAX_REGIONS]` 管理 `linker/kernel.ld` 所定義的 `__free_ram` 到 `__free_ram_end` 範圍。`alloc_pages()` 以 page 為單位配置實體記憶體，`release_pages()` 釋放 region 並嘗試合併相鄰 free region。

### User Runtime 與 Shell

`user/user.c` 提供 user program 的進入點 `start()`，設定 user stack 後呼叫 `main()`。`putchar()`、`getchar()`、`exit()` 都透過 `syscall()` 觸發 `ecall` 進入 kernel。

`user/shell.c` 是目前的 user program，會顯示提示字元並讀取指令：

- `hello`：印出 `Hello world from shell!`
- `exit`：呼叫 `exit()` 結束 shell process
- 其他輸入：顯示 unknown command

### VirtIO Block

`kernel/virtio.c` 針對 QEMU virt machine 上的 VirtIO block MMIO 裝置進行初始化。`virtio_blk_init()` 檢查 magic、version、device id，建立 virtqueue，並取得磁碟容量。

`read_write_disk()` 使用 3 個 descriptor 組成 block request，可讀寫指定 sector。目前 VirtIO read/write demo 已整理到 `tests/test_virtio.c`，由 `kernel_main()` 呼叫 `test_virtio()` 執行。

## Build 與執行

需要先安裝下列工具：

- `clang`
- `lld`
- `llvm-objcopy`
- `qemu-system-riscv32`
- `make`

常用指令：

```sh
make shell   # 只編譯 user shell binary 與 shell.bin.o
make kernel  # 編譯 kernel.elf
make run     # 編譯後啟動 QEMU
make test    # 以 -DTEST 編譯並啟動 QEMU
make clean   # 清除 build/
```

也可以沿用：

```sh
./run.sh
```

`Makefile` 會依序：

1. 使用 `linker/user.ld` 編譯 `user/shell.c`、`user/user.c`、`kernel/common.c` 成 `build/shell.elf`
2. 將 `build/shell.elf` 轉成 `build/shell.bin`
3. 將 `shell.bin` 包成 RISC-V object file：`build/shell.bin.o`
4. 使用 `linker/kernel.ld` 編譯 kernel、tests 與 `build/shell.bin.o` 成 `build/kernel.elf`
5. `make run` 會啟動 QEMU 並載入 `build/kernel.elf`

> 注意：目前 QEMU 指令預設會掛載 `lorem.txt` 作為 raw block device；執行前需確認工作目錄中存在此檔案。若要指定其他檔案，可使用 `make run DISK=path/to/disk.img`。

## 測試

`make test` 會加上 `-DTEST` 編譯選項，啟用 `kernel/kernel.c` 中的測試流程：

- `test_common()`：測試 printf、memcpy、memset、strcpy、strcmp
- `test_process()`：測試 process 建立與 context switch
- `test_virtio()`：示範 VirtIO block sector read/write

## 產生檔案

編譯與執行過程會產生 `build/` 目錄，裡面包含：

- `kernel.elf`
- `kernel.map`
- `shell.elf`
- `shell.map`
- `shell.bin`
- `shell.bin.o`
- `qemu.log`
