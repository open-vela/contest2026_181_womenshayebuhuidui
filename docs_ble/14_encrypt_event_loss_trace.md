# 14. R52 加密事件丢失定位（EncryptChange 到不了 zblue）

日期：2026-08-15 深夜（R52 → R53）
目标：BR/EDR PAN 的 encryption gate——让 zblue 看到 hci_encrypt_change 被调用、
bt_br_update_sec_level 把 sec_level 提到 L2，然后 BNEP Setup 才能成功。

## 现象（R52, pan17.out）

LCPU 持续上报加密事件（合并 buffer，每个都非标准 5 参数）：

    04 13 05 01 81 00 01 00   <- status=0x01 失败格式（实为 handle=0x0081 enc=1）
    sf32lb52 bth4: EncryptChange status 0x01 -> 0 (handle 0x0081 enc 1)   <- 归一化生效（4 次）

但 zblue 侧：
- [zblue] encrypt_change syslog（hci_encrypt_change 内，无条件）一次都没打
- "Unhandled event 0x1b len 3: 810005" —— Remote Name Complete 到了 zblue（handle_event 打印），
  说明合并 buffer 里其它帧（0x07 Remote Name Complete 等）能到 zblue（手机名 "Ren40s" 也出现了）
- 唯独 0x13 事件消失：没有 handler 日志，也没有 Unhandled 日志

结论：EncryptChange 帧在 驱动 -> zblue 之间的某处被静默丢弃。

## 数据通路梳理

    LCPU 控制器
      -> sf32lb52 rx_ind (IPC 环形 DMA)
          -> sf32lb52_bt_recv_cb()  [vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c]
              - 逐帧拆分 (h4_packet_len, 多帧合并 buffer)
              - EncryptChange 状态字节归一化 (04 13 05 -> status=0)
              -> sf32lb52_bt_forward_packet()
                  -> bt_netdev_receive -> drv.receive = uart_bth4_receive()
                      -> [nuttx/drivers/serial/uart_bth4.c]
                          -> circbuf_write(&circbuf, ...)   <- 空间不足时 ret=-ENOMEM，无日志！
                              -> poll_notify(POLLIN)
                                  -> hci_h4.c [apps/.../service/stacks/zephyr/hci_h4.c]
                                      -> read() -> 拆帧 -> get_rx() -> bt_buf_get_evt()
                                          -> bt_buf_get_rx(BT_BUF_EVT, K_FOREVER)  <- pool 满会阻塞/失败
                                              -> h4->recv() = bt_hci_recv -> bt_recv_unsafe
                                                  -> rx_queue_put -> rx_work (sysworkq)
                                                      -> hci_event -> handle_event
                                                          -> hci_encrypt_change()   <- 目标

## 两个静默丢弃点（都已加日志，R53）

1. uart_bth4_receive：circbuf_space < buflen + 4 时 ret = -ENOMEM
   —— 帧被静默丢弃（无任何日志）。合并包一次写入多帧时，
   如果 zblue 读得慢（service_loop poll 延迟 / get_rx 阻塞），circbuf 满 -> 丢帧。
   已加：uart_bth4 rx dropped: circbuf full (type=.. len=.. space=..)

2. hci_h4.c bt_sal_hci_transport_recv：get_rx() 返回 NULL（evt pool 耗尽等）
   —— BT_LOGD 是 no-op（CONFIG_BLUETOOTH_SERVICE_LOG_LEVEL 未启用），静默 continue 丢帧。
   已加：每帧 [h4] frame type=.. evt=.. len=.. + DROP 时 [h4] DROP frame ...

3. zblue rx_work_handler：入口加 [zblue] rx_work enter（确认事件确实进入 hci_event 路径）。

## 其它观察（R52 日志）

- 04 0f 04 00 06 1c 04：CC for 0x041c（Write Default Link Policy）status=0x04——LCPU 报错；
  zblue send_sync 拿到失败但继续（非致命）。
- 04 23 0d ...（0x23 Encryption Key Refresh）LCPU 发 13 参数（标准 3）——又一个非标准事件。
- 04 07 ff 00 a4 d1 fe b3 cc a4 "Ren40s"...：Remote Name Complete 是标准格式（255B）-> zblue 正常处理。
- [zblue] send_sync 0x0411 ... ncmd=0：Authentication Requested 发出时 ncmd=0（前一个命令 CC 未消费），
  之后 [pan] worker: timeout, L2CAP anyway 才走 L2CAP。

## R53 预期

- 若 uart_bth4 rx dropped 出现 -> circbuf 溢出是丢帧根因 -> 增大 CONFIG_UART_BTH4_RXBUFSIZE
  或加快 zblue 消费（get_rx 阻塞点排查：hci_rx_pool 是否耗尽）。
- 若 [h4] DROP 出现 -> evt pool 问题 -> 检查 CONFIG_BT_BUF_* 配置。
- 若 [h4] frame evt=0x13 出现但无 [zblue] encrypt_change -> 问题在 bt_recv->handle_event 之间
  （CONFIG_BT_CLASSIC / CONFIG_BT_SMP 是否裁剪了 0x13 handler——检查 build 的 config.h）。
