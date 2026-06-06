# VirtIO Network Device 整理

這份文件整理在本專案加入 VirtIO network device 時需要掌握的背景、規格重點、QEMU 設定、driver 架構與實作順序。專案目前已實作 `virtio-blk`，並開始加入 `virtio-net` 的最小 skeleton：device probing、RX/TX queue 初始化、RX buffer pool，以及一個 TX 測試 frame。

參考資料：

- VirtIO 1.1 規格，Network Device 章節：https://docs.oasis-open.org/virtio/virtio/v1.1/cs01/virtio-v1.1-cs01.html
- QEMU VirtIO devices 文件：https://www.qemu.org/docs/master/system/devices/virtio/index.html
- 從 polling 改為 interrupt-driven：[`virtio-interrupts.md`](virtio-interrupts.md)

## 目前專案狀態

相關檔案：

- `include/virtio.h`：定義 VirtIO MMIO register、status bit、virtqueue descriptor / avail / used ring，以及 block request。
- `kernel/virtio.c`：提供可指定 MMIO base 的 VirtIO helper；`virtio-blk` 使用 queue 0 做 sector read/write，`virtio-net` 掃描 MMIO slot 並初始化 RX queue 0 / TX queue 1。
- `Makefile`：目前 QEMU 只掛一個 block device：

```sh
-drive id=drive0,file=$(DISK),format=raw,if=none
-device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0
```

`virt.dts` 顯示 QEMU `virt` machine 提供多個 VirtIO MMIO slot：

| MMIO base | interrupt | 用途 |
| --- | --- | --- |
| `0x10001000` | 1 | 目前被 `virtio-blk` 使用 |
| `0x10002000` ~ `0x10008000` | 2 ~ 8 | 可掛其他 virtio-mmio device，例如 `virtio-net-device` |

現有 `virtio.c` 已將 register helper 改成可傳入 MMIO base 的形式，因此 block 與 network 可以各自持有自己的 `struct virtio_device`。

## VirtIO Network 基本資料

VirtIO network device 是一張虛擬 Ethernet NIC。

常用識別：

- VirtIO device id：`1`
- QEMU device：`virtio-net-device`
- Transport：本專案使用 QEMU `virt` machine 的 VirtIO MMIO transport，不是 PCI。
- 最小 queue 組合：
  - queue 0：receive queue
  - queue 1：transmit queue
  - queue 2：control queue，只有 negotiation 啟用 `VIRTIO_NET_F_CTRL_VQ` 時才需要

目前專案已加入 PLIC 與 VirtIO interrupt-driven completion；`virtio_net_poll()` 僅保留為除錯 fallback。詳細流程請參考 [`virtio-interrupts.md`](virtio-interrupts.md)。

## QEMU 啟動設定

最簡單的 user-mode network 設定：

```sh
-netdev user,id=net0
-device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.1
```

若保留目前 block device 在 `virtio-mmio-bus.0`，network 可放在 `virtio-mmio-bus.1`，通常會對應到下一個 MMIO slot，例如 `0x10002000`。實作時仍建議透過 device probing 檢查 `DEVICE_ID == 1`，不要只靠假設。

若需要固定 MAC，可加：

```sh
-device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.1,mac=52:54:00:12:34:56
```

## MMIO 初始化流程

VirtIO MMIO v1 初始化順序可沿用目前 block driver 的核心流程：

1. 讀 `VIRTIO_REG_MAGIC`，應為 `0x74726976`。
2. 讀 `VIRTIO_REG_VERSION`，本專案目前檢查版本 `1`。
3. 讀 `VIRTIO_REG_DEVICE_ID`，network 應為 `1`。
4. 將 `VIRTIO_REG_DEVICE_STATUS` 寫 `0` 重設裝置。
5. 設定 `ACKNOWLEDGE`。
6. 設定 `DRIVER`。
7. 設定 `VIRTIO_REG_PAGE_SIZE = PAGE_SIZE`。
8. 選擇並初始化 queue 0 和 queue 1。
9. 完成 feature negotiation 後設定 `DRIVER_OK`。

現有 header 已有基本 status bit：

```c
#define VIRTIO_STATUS_ACK       1
#define VIRTIO_STATUS_DRIVER    2
#define VIRTIO_STATUS_DRIVER_OK 4
```

若要完整處理 feature negotiation，還需要加入 `FEATURES_OK` 與 `FAILED` 等 status bit，以及 device/driver feature register。

## Network Packet 格式

每個送收封包前面都要有 VirtIO network header。最小 header 可先定義如下：

```c
struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed));
```

`virtio_net_hdr` 欄位說明：

| Member | 大小 | 說明 |
| --- | --- | --- |
| `flags` | 8-bit | 封包狀態旗標。主要和 checksum offload 有關，例如 device 告訴 driver 這包需要補 checksum。若目前沒有啟用 checksum offload，通常為 `0`。 |
| `gso_type` | 8-bit | Generic Segmentation Offload 類型。用來表示是否為需要分段的大封包，例如 TCP segmentation offload。未啟用 GSO/TSO/UFO 時通常為 `0`。 |
| `hdr_len` | 16-bit | GSO 使用的 header 長度，表示封包前面不可被分段的 header 範圍。未啟用 GSO 時通常為 `0`。 |
| `gso_size` | 16-bit | GSO 每個 segment 的目標大小。未啟用 GSO 時通常為 `0`。 |
| `csum_start` | 16-bit | checksum 計算起點，通常是 L4 header 在封包中的 offset。未啟用 checksum offload 時通常為 `0`。 |
| `csum_offset` | 16-bit | checksum 欄位相對於 `csum_start` 的 offset。未啟用 checksum offload 時通常為 `0`。 |

目前專案沒有啟用 checksum/GSO 相關 feature，因此 TX 測試封包會將整個 `virtio_net_hdr` 清成 `0`；真正的 Ethernet frame 從這個 header 後方開始。

若 negotiation 啟用 mergeable RX buffers，header 會多 `num_buffers` 欄位。入門實作建議先不要啟用 `VIRTIO_NET_F_MRG_RXBUF`，讓 header 維持簡單版本。

一個完整 Ethernet frame buffer 可以用：

```c
#define ETH_MAX_FRAME_SIZE 1514
#define VIRTIO_NET_HDR_SIZE sizeof(struct virtio_net_hdr)
```

RX/TX buffer 大小建議至少配置：

```c
VIRTIO_NET_HDR_SIZE + ETH_MAX_FRAME_SIZE
```

## RX Queue

RX queue 的 driver 責任是先提供可寫入的 packet buffers 給 device。流程：

1. 配置多個 RX packet buffer。
2. 每個 descriptor 指向一個 buffer，長度為 `virtio_net_hdr + ethernet frame`。
3. RX descriptor 必須設定 `VIRTQ_DESC_F_WRITE`，表示 device 會寫入資料。
4. 把 descriptor head 放入 available ring。
5. kick RX queue。
6. device 更新 used ring 並觸發 IRQ 2；`virtio_net_irq()` drain RX used ring。
7. 解析 buffer：前面是 `virtio_net_hdr`，後面是 Ethernet frame。
8. 處理完後，將同一個 buffer 重新放回 RX queue。

RX queue 不是送一次 request 等一次 response；它更像一個長期維持的 buffer pool。driver 啟動後應持續補 RX descriptor。

## TX Queue

TX queue 負責送出 Ethernet frame。最小流程：

1. 準備一個 buffer，內容為 `virtio_net_hdr + ethernet frame`。
2. descriptor 指向該 buffer，不設定 `VIRTQ_DESC_F_WRITE`。
3. 把 descriptor head 放入 available ring。
4. kick TX queue。
5. device 更新 used ring 並觸發 IRQ 2；`virtio_net_irq()` drain TX completion 後釋放或重用 buffer。

TX 可以先用單一 outstanding packet 實作，等功能通了再加入 descriptor free list 和多封包佇列。

## Feature Negotiation 建議

第一版 driver 建議採取保守策略：

- 不要求 checksum offload。
- 不啟用 mergeable RX buffers。
- 不啟用 multiqueue。
- 若 device 提供 MAC feature，可讀取 config 中的 MAC；否則使用 driver 內建 MAC。

常見 feature：

| Feature | 說明 | 第一版建議 |
| --- | --- | --- |
| `VIRTIO_NET_F_MAC` | config space 內有 MAC address | 可接受 |
| `VIRTIO_NET_F_CSUM` | device 可處理 checksum | 先不啟用 |
| `VIRTIO_NET_F_GUEST_CSUM` | guest 可接收 checksum 未完成封包 | 先不啟用 |
| `VIRTIO_NET_F_MRG_RXBUF` | RX packet 可跨多個 buffer | 先不啟用 |
| `VIRTIO_NET_F_CTRL_VQ` | 啟用 control queue | 先不啟用 |
| `VIRTIO_NET_F_MQ` | multiqueue | 先不啟用 |

## 目前檔案拆分

目前已將共用 VirtIO transport / virtqueue helper 與 device-specific driver 拆開：

```text
include/virtio.h        # 共用 VirtIO MMIO / virtqueue 定義
include/virtio_blk.h    # block device API 與 request struct
include/virtio_net.h    # network device API 與 network header
kernel/virtio.c         # 共用 MMIO / virtqueue helper
kernel/virtio_blk.c     # block driver
kernel/virtio_net.c     # network driver
tests/test_virtio_net.c # network driver test
```

共用 helper 避免寫死 `VIRTIO_BLK_PADDR`，目前使用：

```c
struct virtio_device {
    paddr_t base;
    uint32_t device_id;
};

uint32_t virtio_reg_read32(struct virtio_device *dev, unsigned offset);
void virtio_reg_write32(struct virtio_device *dev, unsigned offset, uint32_t value);
struct virtio_virtq *virtq_init(struct virtio_device *dev, unsigned index);
void virtq_kick(struct virtio_device *dev, struct virtio_virtq *vq, int desc_index);
```

## 實作順序

建議按下面順序做，降低一次改太多造成的除錯成本：

1. 重構 VirtIO helper，讓 register access 可指定 MMIO base。
2. 保持 `virtio-blk` 功能不變，先確認現有磁碟 I/O 沒壞。
3. 在 `Makefile` 加入 `virtio-net-device` 與 network backend。
4. 掃描 `0x10001000` 到 `0x10008000`，確認 network device id 為 `1`。
5. 新增 `virtio_net_init()`，初始化 RX queue 0 和 TX queue 1。
6. 建立 RX buffer pool，先印出收到的 Ethernet frame type。
7. 建立 TX path，先送出固定 Ethernet broadcast frame。
8. 補 ARP request/reply。
9. 再往上做 IPv4、ICMP ping、UDP 或 TCP。

## 常見陷阱

- `virtio-blk` 和 `virtio-net` 不能共用寫死的 MMIO base helper。
- RX descriptor 必須先放進 queue，device 才有地方寫入封包。
- RX descriptor 要有 `VIRTQ_DESC_F_WRITE`；TX descriptor 不要有。
- `avail.index` / `used.index` 是遞增 counter，ring slot 才用 `% VIRTQ_ENTRY_NUM`。
- kick queue 前要做 memory barrier，例如現有 `__sync_synchronize()`。
- 第一版不要啟用複雜 features，否則 packet header 長度與 checksum 行為會變複雜。
- QEMU user-mode network 不等於 guest 自動有 TCP/IP stack；virtio-net driver 只處理 L2 Ethernet frame，上層 ARP/IP/ICMP/UDP/TCP 仍要自己實作。
