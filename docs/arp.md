# ARP 實作

本專案加入最小可用的 IPv4 ARP（Address Resolution Protocol）：

- 發送 ARP Request，查詢 IPv4 對應的 Ethernet MAC。
- 接收 ARP Request 與 Reply。
- 學習 sender IPv4/MAC mapping。
- 對本機 IPv4 的 Request 回覆 ARP Reply。
- 提供固定大小的 ARP cache。
- shell 可發送 Request 並顯示 cache。

目前本機網路設定：

```text
MAC  52:54:00:12:34:56
IPv4 10.0.2.15
```

`10.0.2.15` 是目前專案針對 QEMU user networking 選用的靜態位址。使用 TAP 時，應讓 host TAP 與 guest 位於相同 subnet，或修改 `include/ipv4.h` 的 `IPV4_LOCAL_ADDRESS`。

## 模組分工

```text
kernel/virtio_net.c
    VirtIO RX/TX queue
    Ethernet header
    EtherType dispatch
            |
            | EtherType 0x0806
            v
kernel/arp.c
    ARP parser
    ARP cache
    Request / Reply
            |
            v
user/shell.c
    arp <IPv4>
    arp
```

VirtIO-net driver 不負責解釋 IPv4 address。它只收送 Ethernet frame，看到 EtherType `0x0806` 時將 frame 交給 `arp_receive()`。

## ARP Packet 格式

ARP packet 放在 Ethernet payload 中：

```text
Ethernet header (14 bytes)
├── Destination MAC  6 bytes
├── Source MAC       6 bytes
└── EtherType        2 bytes = 0x0806

ARP payload (28 bytes for Ethernet/IPv4)
├── Hardware type    2 bytes = 1 (Ethernet)
├── Protocol type    2 bytes = 0x0800 (IPv4)
├── Hardware length  1 byte  = 6
├── Protocol length  1 byte  = 4
├── Operation        2 bytes = 1 Request / 2 Reply
├── Sender MAC       6 bytes
├── Sender IPv4      4 bytes
├── Target MAC       6 bytes
└── Target IPv4      4 bytes
```

RFC 826 規定 ARP 的 16-bit 欄位以 high byte first 傳送。本專案使用 byte array 與 `read_be16()` / `write_be16()`，避免 RV32 對 packed struct 未對齊 16-bit 存取造成 fault。

## 發送 ARP Request

Shell：

```text
> arp 10.0.2.2
arp: request sent
```

流程：

1. shell 將 dotted IPv4 轉成 `uint32_t`。
2. `SYS_ARP_REQUEST` 呼叫 `arp_request()`。
3. 建立 operation 1 的 ARP payload。
4. Ethernet destination 設為 `ff:ff:ff:ff:ff:ff`。
5. Target MAC 設為全 0，Target IPv4 設為查詢位址。
6. `virtio_net_send_ethernet()` 加上 Ethernet header 並提交 TX queue。
7. TX interrupt drain used ring，解除同步等待。

Request 的關鍵欄位：

```text
Ethernet dst = ff:ff:ff:ff:ff:ff
EtherType    = 0x0806
Operation    = 1
Sender IP    = 10.0.2.15
Target IP    = shell 指定的 IPv4
```

## 接收與 Cache 更新

RX interrupt drain used ring 後：

```c
if (eth_type == ARP_ETHERTYPE)
    arp_receive(frame, frame_len);
```

`arp_receive()` 會先驗證：

- frame 足以容納 Ethernet + ARP header。
- Hardware type 是 Ethernet。
- Protocol type 是 IPv4。
- Hardware length 是 6。
- Protocol length 是 4。

驗證成功後，無論是 Request 或 Reply，都先把 sender IPv4/MAC 寫入 cache。這符合 RFC 826 建議的接收演算法：先合併 sender mapping，再判斷 packet 是否以本機為 target。

目前 cache 有 8 格：

```c
struct arp_cache_entry {
    bool valid;
    uint32_t ip;
    uint8_t mac[6];
};
```

相同 IPv4 會更新 MAC；cache 滿時目前覆蓋第 0 格。尚未加入 aging、LRU 或 timeout。

查看 cache：

```text
> arp
ARP cache:
  10.0.2.2 -> 52:55:0a:00:02:02
```

## 回覆 ARP Request

若收到：

```text
Operation = Request
Target IP = 10.0.2.15
```

driver 需要向 sender MAC 單播 Reply。

不能直接在 RX interrupt handler 中呼叫同步 TX：

```text
RX IRQ
  -> arp_receive()
  -> TX
  -> wait_for_interrupt(TX completion)  // 不應在 ISR 內等待
```

因此目前採 deferred reply：

1. `arp_receive()` 在 IRQ context 只保存 sender MAC/IP，並設定 `pending_reply`。
2. shell 等待輸入時，`SYS_GETCHAR` 迴圈呼叫 `arp_poll()`。
3. `arp_poll()` 取出 pending reply。
4. 在 syscall/kernel context 呼叫同步 `virtio_net_send_ethernet()`。

Reply：

```text
Ethernet dst = Request sender MAC
Operation    = 2
Sender MAC   = 52:54:00:12:34:56
Sender IP    = 10.0.2.15
Target MAC   = Request sender MAC
Target IP    = Request sender IP
```

目前 pending reply 只有一格；短時間大量 ARP Request 可能讓後來的 request 覆蓋前一筆。後續可改為 software queue 或讓 TX 支援多個 outstanding descriptors。

## 測試

### QEMU User Networking

啟動：

```sh
make run
```

shell：

```text
> arp 10.0.2.2
arp: request sent
> arp
```

若 QEMU backend 回覆，cache 應出現 `10.0.2.2` 的 MAC。

### TAP

先讓 TAP 與 guest 位於同一 subnet，例如：

```sh
sudo ip tuntap add dev tap0 mode tap
sudo ip addr add 10.0.2.2/24 dev tap0
sudo ip link set tap0 up
make NETDEV=tap run
```

觀察 ARP：

```sh
sudo tcpdump -i tap0 -n -e arp
```

guest：

```text
> arp 10.0.2.2
```

也可以從 host 主動查 guest：

```sh
sudo arping -I tap0 10.0.2.15
```

guest 應輸出：

```text
arp: replied to 10.0.2.2
```

## 已知限制

- IPv4 是靜態 `10.0.2.15`，尚未實作 DHCP。
- cache 沒有 aging、timeout 或 LRU。
- pending reply 只有一格。
- 沒有 route table；ARP 目前只處理同一 Ethernet segment。
- 已有初步 IPv4 packet parser 與 header checksum；尚未實作 ICMP、UDP 或 TCP。
- TX queue 仍是單一同步 buffer，不支援多個 concurrent packet。
- syscall 尚未做完整 user pointer validation。

## 引用資源

1. [RFC 826: An Ethernet Address Resolution Protocol](https://www.rfc-editor.org/rfc/rfc826.html)  
   ARP packet 格式、Request/Reply opcode、sender mapping merge 與回覆流程。
2. [OASIS VirtIO 1.1, Network Device](https://docs.oasis-open.org/virtio/virtio/v1.1/cs01/virtio-v1.1-cs01.html#x1-2040001)  
   VirtIO network TX/RX buffer、`virtio_net_hdr` 與 receive queue 行為。
3. [QEMU Network Emulation](https://www.qemu.org/docs/master/system/devices/net.html)  
   QEMU user/TAP network backend 的角色與 TAP Ethernet 連接方式。
4. [QEMU Invocation: Network Options](https://www.qemu.org/docs/master/system/invocation.html#network-options)  
   `-netdev user`、`-netdev tap` 等 backend 參數。
