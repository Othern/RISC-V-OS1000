# RISC-V OS1000

RISC-V OS1000 是一個以 RV32 為目標的教學作業系統。專案使用 `clang` 交叉編譯 kernel 與 user program，並在 QEMU `virt` machine 上執行。

目前核心功能包括：

- SBI console 輸入輸出
- supervisor trap 與 syscall
- 實體頁面配置與 Sv32 page table
- process table、context switch 與簡易排程
- tar-based filesystem
- VirtIO MMIO transport 與 virtqueue
- VirtIO block sector I/O
- PLIC 與 VirtIO interrupt-driven completion
- VirtIO network RX/TX queue、封包統計與測試

## 專案結構

```text
.
├── Makefile
├── README.md
├── disk/                       # 打包進 disk.tar 的檔案
├── docs/
│   ├── virtio-network.md       # VirtIO network 實作筆記
│   ├── virtio-interrupts.md    # polling 改為 interrupt-driven
│   ├── virtio-net-rx-test.md   # TAP、RX/TX 測試方式
│   ├── virtio-network-flow.html
│   └── virtio-data-structures.html
├── include/
│   ├── allocator.h
│   ├── common.h
│   ├── filesystem.h
│   ├── kernel.h
│   ├── process.h
│   ├── syscall.h
│   ├── user.h
│   ├── virtio.h               # 共用 VirtIO MMIO / virtqueue 定義
│   ├── virtio_blk.h           # block request 與 API
│   └── virtio_net.h           # network header 與 API
├── kernel/
│   ├── allocator.c
│   ├── common.c
│   ├── filesystem.c
│   ├── kernel.c
│   ├── process.c
│   ├── sbi.c
│   ├── syscall.c
│   ├── virtio.c               # 共用 transport / virtqueue helper
│   ├── virtio_blk.c           # VirtIO block driver
│   └── virtio_net.c           # VirtIO network driver
├── linker/
│   ├── kernel.ld
│   └── user.ld
├── tests/
│   ├── test_common.c
│   ├── test_process.c
│   ├── test_virtio.c
│   └── test_virtio_net.c
└── user/
    ├── shell.c
    └── user.c
```

## Kernel 啟動流程

`boot()` 設定 kernel stack 後進入 `kernel_main()`。目前主流程依序：

1. 清空 BSS 並設定 trap vector。
2. 初始化實體記憶體配置器。
3. 初始化 VirtIO block device。
4. 初始化 VirtIO network device。
5. 初始化 PLIC 並開啟 supervisor external interrupt。
6. 送出一個測試 Ethernet frame，以 interrupt 等待 TX completion。
7. 從 block device 載入 tar filesystem。
8. 初始化 process table。
9. 使用 `-DTEST` 時執行 VirtIO network TX/RX 與 block 測試。

目前 `create_process()`、`yield()` 與 user shell 啟動呼叫在 `kernel_main()` 中被註解，因此 kernel 完成初始化或測試後會停在 `PANIC("switched to idle process")`。

## VirtIO 架構

VirtIO 已依責任拆成三層：

### 共用 Transport

[`kernel/virtio.c`](kernel/virtio.c) 提供：

- MMIO register read/write
- device probing
- device status 初始化
- virtqueue 配置
- available ring enqueue
- queue notify
- used ring completion 判斷

共用資料結構定義在 [`include/virtio.h`](include/virtio.h)：

- `struct virtio_device`
- `struct virtq_desc`
- `struct virtq_avail`
- `struct virtq_used_elem`
- `struct virtq_used`
- `struct virtio_virtq`

### VirtIO Block

[`kernel/virtio_blk.c`](kernel/virtio_blk.c) 使用 queue 0 執行 sector I/O。

一個 block request 使用三個 chained descriptors：

1. request header：type、reserved、sector
2. 512-byte sector data
3. device status byte

`read_write_disk()` 目前以 busy waiting 等待 used ring completion。

### VirtIO Network

[`kernel/virtio_net.c`](kernel/virtio_net.c) 使用：

- queue 0：RX queue
- queue 1：TX queue
- `struct virtio_net_hdr`：每個 Ethernet frame 前的 VirtIO metadata
- RX buffer pool：預先交給 device 寫入
- TX buffer：driver 組好 frame 後交給 device 讀取

目前 block completion 與 network RX/TX completion 由 PLIC/VirtIO interrupt 處理。`virtio_net_poll()` 仍保留為除錯 fallback；尚未實作 feature negotiation、ARP 或 IP stack。

## Build 與執行

需要安裝：

- `clang`
- `lld`
- `llvm-objcopy`
- `qemu-system-riscv32`
- `make`

常用指令：

```sh
make shell
make kernel
make run
make test
make clean
```

`make run` 會建立 `disk.tar`，掛載 VirtIO block 與 VirtIO network device，然後啟動 QEMU。

### Network Backend

預設使用 QEMU user-mode network：

```sh
make run
make test
```

要從 host 注入或擷取原始 Ethernet frame，可使用 TAP：

```sh
sudo ip tuntap add dev tap0 mode tap
sudo ip link set tap0 up
make NETDEV=tap test
```

QEMU network device 使用固定 MAC：

```text
52:54:00:12:34:56
```

完整的 host 送包與 `tcpdump` 測試方式請參考 [`docs/virtio-net-rx-test.md`](docs/virtio-net-rx-test.md)。

## 測試

`make test` 透過 `CFLAGS_EXTRA=-DTEST` 啟用 kernel 內測試。目前 `kernel_main()` 執行：

- `test_virtio_net_tx()`：送出 ethertype `0x88b5` 的 broadcast frame，確認 TX used ring completion。
- `test_virtio_net_rx()`：等待 host 注入 ethertype `0x88b5` 的 frame，忽略 IPv6 等背景封包。
- `test_virtio()`：讀寫 VirtIO block sector 0。

`test_common()` 與 `test_process()` 目前在 `kernel_main()` 中被註解。

## 文件

- [`docs/virtio-network.md`](docs/virtio-network.md)：VirtIO network 初始化、header、RX/TX queue 與實作注意事項。
- [`docs/virtio-interrupts.md`](docs/virtio-interrupts.md)：PLIC、trap、VirtIO ACK 與 polling 改為 interrupt-driven 的實作指南。
- [`docs/virtio-net-rx-test.md`](docs/virtio-net-rx-test.md)：TAP、raw Ethernet frame、RX/TX 測試。
- [`docs/virtio-network-flow.html`](docs/virtio-network-flow.html)：VirtIO network 初始化與操作流程圖。
- [`docs/virtio-data-structures.html`](docs/virtio-data-structures.html)：VirtIO 資料結構互動圖。
- [`docs/plic-interrupt-flow.html`](docs/plic-interrupt-flow.html)：PLIC interrupt 註冊、claim/complete 與 Device/CPU 互動流程。

## 編譯輸出

`build/` 主要包含：

- `kernel.elf`
- `kernel.map`
- `shell.elf`
- `shell.map`
- `shell.bin`
- `shell.bin.o`
- `qemu.log`

`disk.tar` 由 `disk/*.txt` 以 ustar 格式建立。
