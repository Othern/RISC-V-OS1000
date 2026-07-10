# AGENTS.md

本文件提供給參與此專案的開發者與自動化代理，說明目前程式碼的實際架構、建置方式、修改原則與已知限制。若本文件與程式碼不一致，應以程式碼及 `Makefile` 為準，並在修改程式時同步更新文件。

## 專案概述

RISC-V OS1000 是一個以教學與實驗為目的的 32 位元 RISC-V 作業系統核心，目標平台為 QEMU `virt` machine。核心由 OpenSBI 載入，在 Supervisor mode 執行，使用 Sv32 分頁，並以自製的簡易使用者空間 shell 展示 process、syscall、檔案系統與 VirtIO 裝置操作。

目前主要功能如下：

- SBI console 字元輸入與輸出。
- Supervisor trap、system call 與外部中斷處理。
- Sv32 page table、頁面配置及簡易 process table。
- cooperative scheduling，process 透過 `yield()` 主動切換。
- 以 tar/ustar 映像為基礎的簡易檔案系統。
- 共用的 legacy VirtIO MMIO 與 split virtqueue 支援。
- VirtIO block sector I/O。
- VirtIO network Ethernet frame RX/TX。
- PLIC 中斷分派，以及 VirtIO block/network completion。
- 初步 ARP request、reply 與固定大小的 ARP cache。
- 初步 IPv4 packet parser、header checksum、固定本機 IP 與發包 helper。
- 內嵌於 kernel image 的使用者 shell。

本專案目前不是完整的通用作業系統，也沒有 preemptive scheduling、完整 POSIX API、TCP/IP stack、動態網路設定或完整的使用者指標驗證。

## 開發環境

建置與執行需要：

- `clang`
- `lld`
- `llvm-objcopy`
- `make`
- `tar`
- `qemu-system-riscv32`
- 可執行 Unix shell 指令的環境

`Makefile` 使用 `mkdir`、`rm`、`cd`、`tar` 等 Unix 指令。在 Windows 上建議使用 WSL、MSYS2、Git Bash，或其他提供相容工具鏈的環境，不應假設純 PowerShell 可直接完成全部 target。

OpenSBI 韌體檔名為 `opensbi-riscv32-generic-fw_dynamic.bin`。若檔案不存在，`Makefile` 會使用 `curl` 從 QEMU repository 下載。

## 常用指令

```sh
make shell
make kernel
make run
make test
make clean
```

- `make shell`：建置 user shell，產生 `build/shell.elf`、`shell.bin` 與可連結的 `shell.bin.o`。
- `make kernel`：建置 `build/kernel.elf`，並在需要時建立 `disk.tar`。
- `make run`：使用 QEMU 啟動 kernel、VirtIO block 與 VirtIO network。
- `make test`：先執行 `make clean`，再以 `CFLAGS_EXTRA=-DTEST` 重建並啟動。這是互動式 QEMU 測試，不是會自動結束的 host unit test。
- `make clean`：移除 `build/` 與 `disk.tar`。

一般網路後端使用 QEMU user networking：

```sh
make run
```

需要觀察原始 Ethernet frame 時，可先在 host 建立 `tap0`，再執行：

```sh
make NETDEV=tap run
make NETDEV=tap test
```

TAP 裝置的建立、IP 設定與權限由 host 負責，細節參考 `docs/virtio-net-rx-test.md` 與 `docs/arp.md`。

## 專案目錄

```text
include/    kernel 與 user 共用或核心模組的 header
kernel/     kernel、process、syscall、driver、filesystem 與 SBI 實作
user/       user runtime 與 shell
tests/      會被連結進 kernel 的測試程式
linker/     kernel 與 user linker script
disk/       建立 disk.tar 的文字檔來源
docs/       VirtIO、PLIC、ARP 與網路測試文件
scripts/    輔助執行腳本
build/      建置產物，不應提交
```

重要檔案：

- `kernel/kernel.c`：boot、trap entry、trap dispatch、核心初始化流程。
- `kernel/process.c`：process table、context switch、Sv32 mapping、scheduler。
- `kernel/syscall.c`：system call dispatch。
- `kernel/virtio.c`：共用 VirtIO MMIO 與 virtqueue helper。
- `kernel/virtio_blk.c`：VirtIO block driver。
- `kernel/virtio_net.c`：VirtIO network RX/TX 與 Ethernet frame 處理。
- `kernel/arp.c`：ARP parser、request/reply 與 cache。
- `kernel/ipv4.c`：IPv4 header 驗證、checksum、統計與發包 helper。
- `kernel/filesystem.c`：tar-based filesystem。
- `user/user.c`：user entry 與 syscall wrapper。
- `user/shell.c`：互動式 shell 指令。
- `Makefile`：實際 source list、編譯選項及 QEMU device 配置。

## 建置與記憶體配置

程式以 freestanding C11 編譯：

```text
--target=riscv32-unknown-elf
-ffreestanding
-nostdlib
-fno-stack-protector
```

kernel linker script `linker/kernel.ld`：

- kernel 起始位址：`0x80200000`
- kernel stack：128 KiB
- kernel 後方保留的 free RAM：64 MiB

user linker script `linker/user.ld`：

- user image 起始虛擬位址：`0x01000000`，即 `USER_BASE`
- user stack：64 KiB
- user image 上限必須低於 `0x01800000`

shell 先被轉成 raw binary，再由 `llvm-objcopy` 轉為 RISC-V object，最後連結進 kernel。kernel 啟動時使用 `_binary_shell_bin_start` 與 `_binary_shell_bin_size` 建立 shell process。

## Kernel 啟動流程

`boot()` 設定 kernel stack 後進入 `kernel_main()`。目前初始化順序為：

1. 清除 BSS。
2. 設定 `stvec` 與 `sscratch`。
3. 初始化實體頁面配置器。
4. 初始化 VirtIO block。
5. 初始化 VirtIO network。
6. 初始化 ARP。
7. 初始化 PLIC。
8. 開啟 supervisor external interrupt。
9. 傳送一個測試用 Ethernet frame。
10. 從 block device 載入 tar filesystem。
11. 初始化 process table 與 idle process。
12. 在 `TEST` build 中執行 VirtIO 測試。
13. 建立 shell process 並進入 scheduler。

變更初始化順序時要檢查依賴關係。例如 ARP 依賴 VirtIO network，filesystem 依賴 VirtIO block，中斷必須在 queue 與 driver 狀態準備完成後才開啟。

## Process、Trap 與 Syscall

- process 數量上限為 `PROCS_MAX`，目前是 8。
- scheduler 是 cooperative round-robin，沒有 timer preemption。
- PID 0 保留給 idle process。
- process 狀態包含 `PROC_UNUSED`、`PROC_RUNNABLE`、`PROC_EXITED`。
- 每個 process 有自己的 Sv32 page table，但 kernel RAM 與必要 MMIO page 會 identity-map 進該 page table。
- context switch 只保存 callee-saved registers；完整 register state 由 trap frame 處理。
- `kernel_entry()` 同時處理 user-mode trap 與 kernel-mode interrupt，修改 assembly 時必須保持 `sp`、`sscratch`、`sstatus.SPP` 的語意一致。

syscall number 定義於 `include/syscall.h`，kernel dispatch 位於 `kernel/syscall.c`，user wrapper 位於 `user/user.c` 與 `include/user.h`。新增 syscall 時必須同步修改這些位置，並視需要擴充 shell。

目前 syscall：

- console：`SYS_PUTCHAR`、`SYS_GETCHAR`
- process：`SYS_EXIT`
- filesystem：`SYS_READFILE`、`SYS_WRITEFILE`
- network：`SYS_SEND`
- ARP：`SYS_ARP_REQUEST`、`SYS_ARP_DUMP`
- IPv4：`SYS_IPV4_DUMP`

現有 syscall 直接使用 user 傳入的 pointer，尚未完整檢查位址範圍與存取權限。新增介面時不可誤認為已有安全的 `copy_from_user()` 或 `copy_to_user()`。

## VirtIO 與中斷

專案使用 legacy VirtIO MMIO：

- block MMIO base：`0x10001000`，PLIC IRQ 1
- network MMIO base：`0x10002000`，PLIC IRQ 2
- virtqueue entry 數量：16
- network RX queue：0
- network TX queue：1

`kernel/virtio.c` 負責 register access、device probing、device status、virtqueue 初始化、available ring enqueue、notify 與 used ring pop。裝置專屬 driver 不應重複實作這些共用操作。

外部中斷流程：

```text
VirtIO device
  -> PLIC
  -> supervisor external interrupt
  -> kernel_entry()
  -> handle_trap()
  -> plic_claim()
  -> virtio_blk_irq() 或 virtio_net_irq()
  -> VirtIO InterruptACK
  -> plic_complete()
```

block 與 network TX 目前都只有有限的 outstanding request 能力，並依賴 completion flag 或單一共用 buffer。若要支援 concurrent I/O，必須重新設計 descriptor 管理、buffer ownership 與等待者喚醒，不可只增加 queue entry。

IRQ handler 中應避免長時間 busy wait、等待另一個 IRQ、執行 filesystem I/O 或直接進行可能阻塞的工作。ARP reply 已採 deferred 方式，由 `arp_receive()` 記錄 pending reply，再由 `arp_poll()` 在非 IRQ context 傳送。

## Network、ARP 與 IPv4

固定 guest MAC：

```text
52:54:00:12:34:56
```

ARP 目前固定 guest IPv4：

```text
10.0.2.15
```

IPv4 目前也使用同一個固定本機位址，定義於 `include/ipv4.h` 的 `IPV4_LOCAL_ADDRESS`。若修改本機 IPv4，應以此常數為單一來源，並同步更新 ARP、文件與 QEMU/TAP 測試設定。

主要資料路徑：

```text
VirtIO RX used ring
  -> virtio_net_drain_rx()
  -> Ethernet EtherType dispatch
  -> arp_receive()，當 EtherType 為 0x0806
  -> ipv4_receive()，當 EtherType 為 0x0800

shell / syscall
  -> arp_request() 或 virtio_net_send_packet()
  -> virtio_net_send_ethernet()
  -> VirtIO TX queue
```

`send <message>` 使用實驗 EtherType `0x88b5`，以 broadcast Ethernet frame 傳送，不是 UDP 或 TCP。

ARP cache 大小為 8，尚無 timeout、aging、LRU、routing、DHCP 或多介面支援。pending ARP reply 目前也只有一筆。修改固定 IP、MAC 或 QEMU 網路配置時，必須同步檢查：

- `kernel/arp.c`
- `kernel/ipv4.c`
- `include/ipv4.h`
- `kernel/virtio_net.c`
- `Makefile`
- `docs/arp.md`
- `docs/ipv4.html`
- 網路相關測試與說明

IPv4 第一版支援 20-byte header 發包、收包 header 長度檢查、total length 檢查、header checksum 驗證、本機/broadcast 目的位址判斷與 protocol 統計。它尚未實作 fragmentation/reassembly、ICMP Echo reply、UDP handler、TCP state machine、route table、gateway、DHCP 或 DNS。收到 fragmented packet 時目前直接丟棄。新增 ICMP、UDP 或 TCP 時應接在 `kernel/ipv4.c` 的 protocol dispatch 後方，不應把 L4 parser 寫進 `kernel/virtio_net.c`。

## Filesystem

`disk/*.txt` 會被打包成 `disk.tar`，並掛載為 VirtIO block device。filesystem 在啟動時讀取 tar header，將內容載入固定大小的記憶體結構。

重要限制：

- `FILES_MAX` 目前是 2。
- 只適合目前的小型文字檔示範。
- `fs_flush()` 會將記憶體內容重寫到 block device。
- 不是一般用途的持久化 filesystem，沒有目錄、權限、journal 或並行控制。

新增 `disk/` 檔案時要同步評估 `FILES_MAX` 與 `DISK_MAX_SIZE`。由於 `disk.tar` 是生成檔且被 `.gitignore` 排除，修改來源檔後應重新建立映像。

## Shell 指令

目前 shell 支援：

- `hello`
- `exit`
- `readfile`
- `writefile`
- `send <message>`
- `ip`
- `arp`
- `arp <IPv4 address>`

shell command line buffer 為 128 bytes。新增指令時維持簡單、無 libc 依賴的解析方式，並注意所有 user code 都在 freestanding 環境中。

## 測試方式

`tests/` 內的測試會直接連結進 kernel。`make test` 定義 `TEST` 後，`kernel_main()` 目前會：

1. 測試 VirtIO network TX。
2. 進入互動式 VirtIO network RX frame monitor，按 `x` 停止。
3. 測試 VirtIO block sector 讀寫。

`test_common()` 與 `test_process()` 目前存在但在 `kernel_main()` 中被註解。不要把 `make test` 描述成完全自動或無破壞性的測試：block 測試會寫入 sector 0，而 RX 測試需要互動和外部封包。

修改後至少應執行：

```sh
make clean
make kernel
```

涉及 boot、trap、process、VirtIO、PLIC、filesystem 或 shell 行為時，還應在 QEMU 中執行對應的 `make run` 或 `make test` 流程。網路 RX/ARP 變更最好搭配 TAP、`tcpdump` 或 `arping` 驗證實際 frame。

## 修改原則

- 優先沿用現有 C11、header/API 與模組邊界，不引入 libc 或 host runtime 依賴。
- 新增 `.c` 檔時要加入 `Makefile` 的 `KERNEL_SRCS` 或 `USER_SRCS`。
- 新增或修改公開函式時同步更新 `include/` 中的宣告。
- 新增 syscall 時同步更新 syscall number、kernel dispatch、user wrapper 與必要文件。
- 新增 MMIO 裝置時同步處理 device probing、page mapping、PLIC IRQ 與 QEMU 參數。
- 修改 trap/context-switch assembly 前，先確認 user trap、kernel interrupt、`sscratch`、stack layout 與返回模式。
- 修改 virtqueue 時，清楚區分 descriptor index、available index、used index 與 buffer ownership。
- 網路多 byte 欄位使用 network byte order；避免直接依賴 packed struct 的未對齊 16/32-bit 存取。
- ISR 只做必要的 acknowledge、used-ring drain、狀態更新與工作排程。
- 不提交 `build/`、`disk.tar`、ELF、BIN、MAP、LOG、PCAP 或大多數生成的 HTML。
- 保留使用者工作區中與任務無關的既有修改，不要為了整理而重設或覆蓋它們。

## 已知風險與限制

- memory allocator、process region tracking 與固定大小陣列都有硬上限。
- kernel RAM 以寬鬆的 R/W/X 權限映射，隔離仍屬教學等級。
- user pointer 未完整驗證，錯誤 pointer 可能造成 kernel fault。
- scheduler 無 preemption、sleep queue 或一般化的 blocking I/O。
- VirtIO block 與 network TX 不支援一般化的多筆並行請求。
- ARP 狀態固定且容量有限，沒有 cache aging。
- `printf`、字串函式與 shell parser 是最小實作，不等同標準 C library。
- 測試多為整合測試和人工觀察，缺少可在 host 自動執行的完整 regression suite。

## 文件維護

網路與 VirtIO 行為改動後，優先更新：

- `README.md`
- `docs/virtio-network.md`
- `docs/virtio-interrupts.md`
- `docs/virtio-net-rx-test.md`
- `docs/arp.md`
- `docs/ipv4.html`

文件應描述目前已完成的行為，將尚未實作的內容明確標示為限制或後續工作。避免只根據設計文件推斷程式現況，應以 `Makefile`、header、driver 與 `kernel_main()` 的實際流程交叉確認。
