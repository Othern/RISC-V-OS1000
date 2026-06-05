# VirtIO-Net RX/TX 測試

這個測試用來確認 virtio-net RX/TX queue 是否能處理 Ethernet frame。它不需要 ARP/IP/UDP stack，只測 L2 frame 是否能透過 virtqueue 進出 guest。

## Kernel 端 RX 測試

`tests/test_virtio_net.c` 會：

1. 記錄目前 `virtio_net_rx_packets()`。
2. 記錄目前 `virtio_net_rx_test_packets()`。
3. 進入一段 polling window，重複呼叫 `virtio_net_poll()`。
4. 只有收到 ethertype `0x88b5` 的測試封包才印出 PASS。
5. 如果只收到其他背景封包，例如 IPv6 `0x86dd`，不算 PASS。

測試會在 `#ifdef TEST` 區塊中先於 `test_common()` 執行，因為目前 `test_common()` 後面有 `unimp`。

## Kernel 端 TX 測試

`tests/test_virtio_net.c` 也包含 `test_virtio_net_tx()`，它會：

1. 記錄目前 `virtio_net_tx_packets()`。
2. 呼叫 `virtio_net_send_test_packet()` 送出 ethertype `0x88b5` 的 broadcast frame。
3. 等 TX queue completion；driver 看到 used ring 回收 descriptor 後，TX counter 會增加。
4. 如果 counter 增加，印出 PASS。

這個 PASS 代表 virtio-net device 已完成 TX descriptor，不等於 host 一定有應用程式處理該封包。若要確認 frame 真的出現在 TAP，可以在 host 端抓包：

```sh
sudo tcpdump -i tap0 -e -XX ether proto 0x88b5
```

預期會看到 destination MAC `ff:ff:ff:ff:ff:ff`、source MAC `52:54:00:12:34:56`、ethertype `0x88b5` 的 frame。

## QEMU 使用 TAP backend

先建立 TAP：

```sh
sudo ip tuntap add dev tap0 mode tap
sudo ip link set tap0 up
```

用 TAP backend 啟動：

```sh
make NETDEV=tap test
```

`Makefile` 會使用：

```sh
-netdev tap,id=net0,ifname=tap0,script=no,downscript=no
-device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.1,mac=52:54:00:12:34:56
```

## Host 注入封包

當 kernel 印出：

```text
test_virtio_net_rx: waiting for host-injected ethernet frames
test_virtio_net_rx: send frames to dst mac 52:54:00:12:34:56
```

在 host 端送 Ethernet frame：

```sh
sudo python3 - <<'PY'
from scapy.all import Ether, Raw, sendp

pkt = (
    Ether(dst="52:54:00:12:34:56",
          src="02:00:00:00:00:01",
          type=0x88b5)
    / Raw(b"hello virtio-net rx")
)

sendp(pkt, iface="tap0", count=5, inter=1)
PY
```

預期 kernel 看到：

```text
virtio-net: rx len=...
virtio-net: rx ethertype=0x000088b5
test_virtio_net_rx: PASS packets=... last_len=... last_ethertype=0x000088b5
```

如果還沒送測試封包就看到：

```text
virtio-net: rx len=... ethertype=0x000086dd
```

這通常是 TAP 介面或 host network stack 自己產生的 IPv6 背景封包。它表示 RX path 有收到資料，但不是指定的 `0x88b5` 測試封包。

如果看到 `WARN no rx packets observed`，優先檢查：

- QEMU 是否用 `NETDEV=tap` 啟動。
- `tap0` 是否已建立且 `UP`。
- Scapy 是否對 `tap0` 送出封包。
- `virtio-net-device` MAC 是否仍是 `52:54:00:12:34:56`。
