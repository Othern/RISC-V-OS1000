# VirtIO 從 Polling 改成 Interrupt-Driven

這份文件說明本專案把 VirtIO block/network 從 polling 改成 interrupt-driven 所需的模組、register、trap 處理與同步機制。

目前已實作：

- PLIC priority、enable、claim/complete。
- supervisor external interrupt trap dispatch。
- kernel/user trap stack 分流。
- VirtIO `InterruptStatus` / `InterruptACK`。
- block completion interrupt。
- network RX/TX used-ring drain。
- PLIC MMIO page mapping。

目前仍保留：

- block/TX 同步 API 使用 completion flag + `wait_for_interrupt()` 等待。
- `virtio_net_poll()` 作為除錯 fallback。
- ISR 內仍直接解析 RX frame 並輸出訊息，尚未拆成 deferred work。

## Interrupt 路徑

在 QEMU `virt` machine 上，VirtIO MMIO interrupt 的路徑是：

```text
VirtIO device
    │ queue completion / config change
    ▼
VirtIO MMIO InterruptStatus
    │
    ▼
PLIC interrupt source
    │ priority + enable + threshold
    ▼
RISC-V supervisor external interrupt
    │ scause interrupt=1, code=9
    ▼
kernel_entry()
    ▼
handle_trap()
    ▼
PLIC claim
    ▼
virtio_blk_irq() / virtio_net_irq()
    ▼
VirtIO InterruptACK
    ▼
PLIC complete
```

兩個不同層級都要 acknowledge：

1. 寫 VirtIO `InterruptACK`，清除 device 的 interrupt reason。
2. 寫 PLIC claim/complete register，通知 PLIC 該 IRQ 已處理完畢。

只做其中一個通常會造成 IRQ 不再出現或持續重複觸發。

## 本專案的 IRQ Mapping

`virt.dts` 中的 VirtIO MMIO slot：

| MMIO base | PLIC IRQ | 目前用途 |
| --- | ---: | --- |
| `0x10001000` | 1 | VirtIO block |
| `0x10002000` | 2 | VirtIO network |
| `0x10003000` | 3 | 未使用 |
| `0x10004000` | 4 | 未使用 |
| `0x10005000` | 5 | 未使用 |
| `0x10006000` | 6 | 未使用 |
| `0x10007000` | 7 | 未使用 |
| `0x10008000` | 8 | 未使用 |

建議先定義：

```c
#define VIRTIO_BLK_IRQ 1
#define VIRTIO_NET_IRQ 2
```

目前 QEMU 參數把 block 掛在 `virtio-mmio-bus.0`、network 掛在 `virtio-mmio-bus.1`，因此與上表相符。不過更完整的 OS 應解析 device tree，而不是永久寫死 IRQ。

## 1. 補齊 VirtIO MMIO Interrupt Registers

Legacy VirtIO MMIO 使用：

```c
#define VIRTIO_REG_INTERRUPT_STATUS 0x60
#define VIRTIO_REG_INTERRUPT_ACK    0x64

#define VIRTIO_INT_USED_BUFFER  1
#define VIRTIO_INT_CONFIG_CHANGE 2
```

讀取與清除方式：

```c
uint32_t virtio_irq_status(struct virtio_device *dev) {
    return virtio_reg_read32(dev, VIRTIO_REG_INTERRUPT_STATUS);
}

void virtio_irq_ack(struct virtio_device *dev, uint32_t status) {
    virtio_reg_write32(dev, VIRTIO_REG_INTERRUPT_ACK, status);
}
```

`InterruptStatus` bit：

| Bit | 名稱 | 意義 |
| ---: | --- | --- |
| 0 | used buffer notification | Device 已經更新至少一個 used ring |
| 1 | configuration change | Device config generation/內容有變動 |

同一次 interrupt 可能同時包含兩個 bit，因此 handler 應保留完整 status 並一次 acknowledge：

```c
uint32_t status = virtio_irq_status(dev);
if (status == 0)
    return;

virtio_irq_ack(dev, status);

if (status & VIRTIO_INT_USED_BUFFER)
    handle_used_queues();

if (status & VIRTIO_INT_CONFIG_CHANGE)
    handle_config_change();
```

## 2. 新增 PLIC Driver

建議新增：

```text
include/plic.h
kernel/plic.c
```

QEMU `virt` 的 PLIC base 是 `0x0c000000`。單核心 supervisor context 常用 register：

```c
#define PLIC_BASE              0x0c000000
#define PLIC_PRIORITY(irq)     (PLIC_BASE + (irq) * 4)
#define PLIC_SENABLE           (PLIC_BASE + 0x2080)
#define PLIC_STHRESHOLD        (PLIC_BASE + 0x201000)
#define PLIC_SCLAIM            (PLIC_BASE + 0x201004)
```

這些 offset 對應 QEMU `virt` 單 hart 的 supervisor context。若未來加入多 hart，enable/context register 必須依 hart/context 計算。

最小 API：

```c
void plic_init(void);
void plic_enable(unsigned irq);
uint32_t plic_claim(void);
void plic_complete(uint32_t irq);
```

初始化範例：

```c
void plic_enable(unsigned irq) {
    volatile uint32_t *priority =
        (volatile uint32_t *) PLIC_PRIORITY(irq);
    volatile uint32_t *enable =
        (volatile uint32_t *) PLIC_SENABLE;

    *priority = 1;
    *enable |= 1u << irq;
}

void plic_init(void) {
    *(volatile uint32_t *) PLIC_STHRESHOLD = 0;
    plic_enable(VIRTIO_BLK_IRQ);
    plic_enable(VIRTIO_NET_IRQ);
}

uint32_t plic_claim(void) {
    return *(volatile uint32_t *) PLIC_SCLAIM;
}

void plic_complete(uint32_t irq) {
    *(volatile uint32_t *) PLIC_SCLAIM = irq;
}
```

目前只使用 IRQ 1/2，所以單一 32-bit enable register 足夠。若要支援 IRQ >= 32，需要依 `irq / 32` 選擇 enable word。

## 3. 開啟 RISC-V Supervisor Interrupt

需要開啟兩層 CSR bit：

```c
#define SSTATUS_SIE (1u << 1)
#define SIE_SEIE    (1u << 9)

WRITE_CSR(sie, READ_CSR(sie) | SIE_SEIE);
WRITE_CSR(sstatus, READ_CSR(sstatus) | SSTATUS_SIE);
```

- `sie.SEIE`：允許 supervisor external interrupt。
- `sstatus.SIE`：全域允許 supervisor interrupt。

建議初始化順序：

1. 設定 `stvec`。
2. 初始化 allocator。
3. 初始化 VirtIO queues 與 RX buffers。
4. 初始化 PLIC priority/enable/threshold。
5. 清除可能殘留的 VirtIO interrupt status。
6. 設定 `sie.SEIE`。
7. 最後設定 `sstatus.SIE`。

全域 interrupt 應最後才開，避免 handler 在 queue/device 尚未完成初始化時被呼叫。

OpenSBI/QEMU 通常已把 external interrupt delegation 到 S-mode；如果永遠收不到 supervisor external interrupt，需檢查 firmware delegation 與 `mideleg`。

## 4. 先讓 Trap Entry 支援 Kernel-Mode Interrupt

目前 `kernel_entry()` 一開始無條件執行：

```asm
csrrw sp, sscratch, sp
```

這個設計假設 trap 一定來自 user mode：

- user `sp` 被換進 `sscratch`
- `sscratch` 中預先保存的 kernel stack 被換到 `sp`

但 external interrupt 可能在 kernel 執行期間發生。此時 `sp` 已經是 kernel stack，仍然執行交換會換到錯誤 stack，甚至在 `sscratch` 尚未初始化時跳到 address 0。

開啟 interrupt 前，trap entry 必須區分 trap 來源：

- `sstatus.SPP == 0`：trap 來自 user mode，需要切到 kernel stack。
- `sstatus.SPP == 1`：trap 來自 supervisor mode，沿用目前 kernel stack。

概念上的 assembly 分流：

```asm
csrr t0, sstatus
andi t0, t0, SSTATUS_SPP
bnez t0, trap_from_kernel

trap_from_user:
    csrrw sp, sscratch, sp
    # 在 kernel stack 建立 trap frame
    j save_registers

trap_from_kernel:
    # 不交換 sp，直接在目前 kernel stack 建立 trap frame

save_registers:
    ...
```

返回時也要依 trap 來源決定是否把 user `sp` 從 `sscratch` 換回來。

另一種短期方案是只在一個明確的 idle loop 開 interrupt，並保證該時刻 `sscratch`/kernel stack 已準備好；但這仍無法支援 kernel I/O 等待期間的 interrupt，因此不建議當成長期架構。

需要補：

```c
#define SSTATUS_SPP (1u << 8)
```

## 5. 修改 Trap Handler

RV32 的 `scause`：

- bit 31：是否為 interrupt
- 低位 cause code `9`：Supervisor external interrupt

建議定義：

```c
#define SCAUSE_INTERRUPT        (1u << 31)
#define SCAUSE_CODE_MASK        0x7fffffffu
#define SCAUSE_SUPERVISOR_EXT   9
```

trap dispatch：

```c
void handle_trap(struct trap_frame *f) {
    uint32_t scause = READ_CSR(scause);
    uint32_t code = scause & SCAUSE_CODE_MASK;

    if (scause & SCAUSE_INTERRUPT) {
        if (code == SCAUSE_SUPERVISOR_EXT) {
            handle_external_interrupt();
            return;
        }

        PANIC("unexpected interrupt scause=%x", scause);
    }

    if (code == SCAUSE_ECALL) {
        handle_syscall(f);
        WRITE_CSR(sepc, READ_CSR(sepc) + 4);
        return;
    }

    PANIC("unexpected exception scause=%x", scause);
}
```

Interrupt 不應把 `sepc` 加 4。只有同步的 user `ecall` 需要跳過產生 exception 的 instruction。

PLIC dispatch：

```c
void handle_external_interrupt(void) {
    uint32_t irq = plic_claim();

    switch (irq) {
    case VIRTIO_BLK_IRQ:
        virtio_blk_irq();
        break;
    case VIRTIO_NET_IRQ:
        virtio_net_irq();
        break;
    case 0:
        return;
    default:
        printf("unexpected external irq=%d\n", irq);
        break;
    }

    plic_complete(irq);
}
```

Claim 回傳 `0` 代表當下沒有可 claim 的 IRQ，不應 complete IRQ 0。

## 6. VirtIO Network Interrupt

Network RX/TX 的 used ring 處理已抽成共用 drain helper：

```c
static void virtio_net_drain_rx(void);
static void virtio_net_drain_tx(void);

void virtio_net_poll(void) {
    virtio_net_drain_rx();
    virtio_net_drain_tx();
}

void virtio_net_irq(void) {
    uint32_t status = virtio_irq_status(&net_dev);
    virtio_irq_ack(&net_dev, status);

    if (status & VIRTIO_INT_USED_BUFFER) {
        virtio_net_drain_rx();
        virtio_net_drain_tx();
    }

    if (status & VIRTIO_INT_CONFIG_CHANGE) {
        // 之後可重新讀 link status / MAC / config。
    }
}
```

### 為什麼 IRQ handler 要 drain 整個 used ring

一次 interrupt 不一定只代表一個 packet。Device 可以：

- 合併多個 completion 再送一次 interrupt。
- 在 handler 執行時繼續增加 `used.index`。
- 同一 IRQ 同時包含 RX 與 TX completion。

所以不能只處理一個 used entry。應使用：

```c
struct virtq_used_elem used;
while (virtq_pop_used(vq, &used)) {
    // consume the completed descriptor identified by used.id
}
```

RX 處理完後仍要把 descriptor 放回 available ring，否則 RX buffer pool 最終會耗盡。

### ISR 與 Deferred Work

第一版可以直接在 ISR 中解析封包，但不要在 ISR：

- 長時間 `printf`
- busy wait
- 執行 filesystem I/O
- 執行可能 `yield()` 的程式
- 處理完整 TCP/IP protocol stack

較好的架構：

```text
top half (IRQ)
    acknowledge interrupt
    drain used ring / 收集 descriptor id
    將 packet 放入 software RX queue
    設定 work_pending

bottom half / kernel loop
    解析 Ethernet / ARP / IP
    呼叫較重的 protocol handler
```

目前 OS 還沒有 kernel worker/thread，可以先讓 ISR 更新 `net_rx_pending = true`，在 idle loop 或 scheduler 回到 kernel 時呼叫 deferred handler。

## 7. VirtIO Block Interrupt

block driver 目前只有一個全域 request buffer，且一次只允許一個 outstanding request，因此可先用簡單 completion flag：

```c
static volatile bool blk_done;

void read_write_disk(...) {
    blk_done = false;
    virtq_kick(&blk_dev, blk_request_vq, 0);

    wait_for_interrupt(&blk_done);

    // 檢查 blk_req->status。
}

void virtio_blk_irq(void) {
    uint32_t status = virtio_irq_status(&blk_dev);
    virtio_irq_ack(&blk_dev, status);

    if (status & VIRTIO_INT_USED_BUFFER) {
        struct virtq_used_elem used;
        while (virtq_pop_used(blk_request_vq, &used)) {
        }
        blk_done = true;
    }
}
```

這比原本不斷讀 used ring 的 busy loop省 CPU，但仍是同步 API。完整做法應讓目前 process 進入 blocked state，IRQ completion 再把 process 設回 runnable。

### Queue Index 語意

共用 virtqueue 已將提交數量與 used ring 消費位置拆成兩個欄位：

```c
uint16_t submitted_index;
uint16_t consumed_used_index;
```

`virtq_kick()` 增加 `submitted_index`；block、network RX 與 network TX 都透過 `virtq_pop_used()` 增加 `consumed_used_index`，不再維護 device-specific used index。

## 8. 避免 Lost Wakeup

使用 interrupt 等待 completion 時，不能直接：

```c
if (!done)
    wfi();
```

因為 completion 可能剛好發生在檢查 `done` 與執行 `wfi` 之間。

單核心初版可採：

```c
wait_for_interrupt(&done);
```

RISC-V 並沒有單一 C API 自動保證這段原子性，因此需要仔細設計 CSR 操作。對目前專案，更簡單穩定的過渡方案是：

1. IRQ handler 設定 completion flag。
2. 主迴圈仍定期檢查 flag，但不再掃 used ring。
3. 等 scheduler 有 blocked/wakeup 機制後，再改成真正睡眠等待。

如果使用 `wfi`，要確認 QEMU/CPU 在 interrupt 已 pending 時不會永久睡眠；規格允許 `wfi` 被視為 hint，但同步程式仍應以條件 flag 為準。

## 9. Interrupt Suppression Flags

Split virtqueue 有兩個常見 suppression flag：

- `avail.flags & VIRTQ_AVAIL_F_NO_INTERRUPT`
- `used.flags & VIRTQ_USED_F_NO_NOTIFY`

要接收 used-buffer interrupt，不要設定：

```c
vq->avail.flags = VIRTQ_AVAIL_F_NO_INTERRUPT;
```

目前 allocator 會把 queue memory 清成 0，因此 `avail.flags == 0`，device 預設可以送 completion interrupt。

建議補：

```c
#define VIRTQ_USED_F_NO_NOTIFY 1
```

這個 flag 是 device 告訴 driver 是否可省略 notify；第一版可以先忽略 optimization，每次 enqueue 後照常 notify。

## 10. Page Table 與 MMIO Mapping

PLIC 位於 `0x0c000000`，不在一般 RAM identity mapping 範圍內。

目前 `create_process()` 會額外映射：

```c
map_page(proc, VIRTIO_BLK_PADDR, VIRTIO_BLK_PADDR, PAGE_R | PAGE_W);
map_page(proc, VIRTIO_NET_PADDR, VIRTIO_NET_PADDR, PAGE_R | PAGE_W);
```

當 CPU 使用 process page table，而 VirtIO interrupt 進入 kernel handler 時，handler 還是使用該 page table。若沒有映射 PLIC register，`plic_claim()` 會 page fault。

至少需要映射 PLIC 用到的頁面：

```c
map_page(proc, 0x0c000000, 0x0c000000, PAGE_R | PAGE_W); // priority
map_page(proc, 0x0c002000, 0x0c002000, PAGE_R | PAGE_W); // enable
map_page(proc, 0x0c201000, 0x0c201000, PAGE_R | PAGE_W); // threshold/claim
```

較好的做法是建立統一的 kernel MMIO mapping helper，讓每個 process page table 都包含 UART、PLIC、VirtIO 等 kernel 必須存取的 MMIO range。

如果未來改成 trap 時切換到獨立 kernel page table，就可以把 MMIO mapping 集中在 kernel page table，不必複製到每個 process。

## 11. 建議檔案拆分

```text
include/kernel.h          # scause / sstatus / sie interrupt bits
include/plic.h            # PLIC register 與 API
include/virtio.h          # InterruptStatus / InterruptACK 共用 API
include/virtio_blk.h      # virtio_blk_irq()
include/virtio_net.h      # virtio_net_irq()

kernel/kernel.c           # trap dispatch / external interrupt dispatch
kernel/plic.c             # PLIC init、enable、claim、complete
kernel/virtio.c           # VirtIO interrupt status/ack helper
kernel/virtio_blk.c       # block completion handler
kernel/virtio_net.c       # RX/TX used ring drain
```

## 12. 實作內容

1. 修改 trap entry，正確區分 user-mode 與 kernel-mode trap stack。
2. 新增 PLIC driver，先只準備 VirtIO network IRQ 2。
3. 將 PLIC MMIO pages 加入 kernel/process page table。
4. 新增 `InterruptStatus` / `InterruptACK` register。
5. 修改 trap handler，先能 claim、印出並 complete IRQ。
6. 把 `virtio_net_poll()` 的 RX used-ring 處理抽成 `virtio_net_drain_rx()`。
7. 在 `virtio_net_irq()` 中呼叫 drain，確認 host 注入 frame 時會進 ISR。
8. 移除 kernel boot/test 中反覆呼叫 RX poll。
9. 加入 TX completion drain 與 TX completion counter。
10. 再啟用 VirtIO block IRQ 1。
11. 將 block busy loop 改為 completion flag 或 process sleep/wakeup。
12. 最後加入 deferred packet processing，縮短 ISR 執行時間。

先從 network RX 開始最容易驗證：TAP 注入 frame 後，應看到 supervisor external interrupt、PLIC claim IRQ 2、VirtIO used-buffer status，以及 RX used ring 前進。

## 13. 除錯檢查

如果完全沒有進 interrupt：

- `sstatus.SIE` 是否為 1。
- `sie.SEIE` 是否為 1。
- PLIC IRQ priority 是否大於 threshold。
- PLIC supervisor enable 是否包含 IRQ 1/2。
- `stvec` 是否在開 interrupt 前設定。
- trap 若來自 S-mode，是否仍錯誤交換 `sp`/`sscratch`。
- 目前 page table 是否映射 PLIC priority/enable/claim register。
- `avail.flags` 是否意外設成 `VIRTQ_AVAIL_F_NO_INTERRUPT`。
- QEMU device 是否真的掛在預期 MMIO bus。
- OpenSBI 是否將 external interrupt delegation 到 S-mode。

如果只進一次 interrupt：

- 是否寫了 VirtIO `InterruptACK`。
- 是否呼叫 `plic_complete(irq)`。
- 是否 drain 完所有 used entries。
- RX descriptor 是否重新放回 available ring。

如果 interrupt storm：

- VirtIO ACK 是否寫回完整的 `InterruptStatus` bit mask。
- PLIC complete 是否使用 claim 回傳的同一個 IRQ。
- Handler 是否在 acknowledge 前就提前 return。
- Config-change bit 是否一直未處理/未清除。

如果 block request 卡住：

- completion flag 是否為 `volatile`。
- IRQ handler 是否更新正確 queue 的 consumed index。
- 是否在 device/PLIC 初始化完成前開啟 global interrupt。
- 是否有 lost wakeup 或錯誤的 `wfi` 等待順序。
