# 基带分区只读备份与协议核查

此目录只包含只读备份工具和协议审查记录。工具只读取 `modemst1`、`modemst2`、`fsg`，不发送 QMI 写请求，也不提供恢复或刷写功能。

## 当前备份

2026-11-04 的备份位于项目的 `小米14pro/01-原机系统备份/当前基带只读备份-2026-11-04/`，在源码目录之外。三个镜像均为 8,388,608 字节；目录权限为 `0700`，镜像及清单权限为 `0600`。`manifest.json` 记录每个镜像的 SHA-256。原始镜像及清单不得提交 Git、发送到公开仓库或用于刷写。

本地复核：

```sh
python3 backup.py verify '/Users/ning/xiangmu3/小米14pro/01-原机系统备份/当前基带只读备份-2026-11-04'
```

如需在设备当前状态重新采集，先确保设备连接在 `192.168.66.1` 且 SSH 已建立可信主机密钥。必须指定一个尚不存在、且位于 Git 工作树之外的输出目录；工具不会覆盖旧备份：

```sh
python3 backup.py capture \
  --host root@192.168.66.1 \
  --output '/Users/ning/xiangmu3/小米14pro/01-原机系统备份/当前基带只读备份-新日期'
```

采集时先核对三个 `by-name` 路径都是 8 MiB 块设备，再分别只读复制；本地计算的 SHA-256 必须与设备端重新读取所得哈希一致。分区内容可能在读取期间改变，遇到不一致会删除本次不完整目录并报错。即便校验通过，也仅证明采集时的三个镜像一致，不能证明它们足以恢复全部基带状态。

## QRTR/QMI 核查结论

参照本地小米内核源码：

- `PixelOS-17-shennong/repos/android_kernel_xiaomi_sm8650/include/uapi/linux/qrtr.h`：`QRTR_TYPE_NEW_SERVER = 4`，`QRTR_TYPE_NEW_LOOKUP = 10`；服务器字段顺序是 `service`、`instance`、`node`、`port`。
- `PixelOS-17-shennong/repos/android_kernel_xiaomi_sm8650/include/linux/soc/qcom/qmi.h`：QRTR 上传输的 `qmi_header` 是 7 字节，即 `type:u8`、`txn_id:u16`、`msg_id:u16`、`msg_len:u16`。
- 现场只读检查曾发现 DMS 服务端口随运行状态变化；不能假定固定节点或端口。应通过正确的 QRTR 服务发现查询，而不能使用旧程序中的固定端口回退。

2026-11-04 在这台设备的 NWRT 系统中再次只读检查：`qrtr-lookup 2` 返回 DMS 的 node 0/port 90 和 node 1/port 16400；`qmicli -d qrtr://0 --dms-get-ids` 成功返回设备标识字段，值不记录在此。`--dms-get-supported-messages` 和 `--get-service-version-info` 均返回 `NotSupported`，因此没有从该设备取得完整的可用消息列表。当前没有 `/dev/diag` 或 `/dev/diag_mdm` 节点；DIAG 与 QRTR/QMI 是不同通道，`/dev/ttyHS*` 的存在也不能证明具备 DIAG 写权限。

官方 [libqmi DMS 消息表](https://github.com/linux-mobile-broadband/libqmi/blob/main/data/qmi-service-dms.json) 定义 `0x0025` 为只读 `Get IDs`，IMEI 是其响应 TLV `0x11`；`0x0026` 为只读 `Get Power State`。[libqmi NAS 消息表](https://github.com/linux-mobile-broadband/libqmi/blob/main/data/qmi-service-nas.json) 定义 NAS `0x0024` 为只读 `Get Serving System`。因此 Antigravity 文档声称的 NAS `0x0024 = NV_WRITE` 和旧 C 程序声称的 DMS `0x0026 = SetSerialNumbers` 均与协议表冲突。这两个消息 ID 不能作为本机的串号写入路径。

可用静态检查脚本复核上述头文件常量和结构；它只读本地源码，不连接手机：

```sh
python3 protocol_check.py '/Users/ning/xiangmu3/小米14pro/PixelOS-17-shennong/repos/android_kernel_xiaomi_sm8650'
```

Antigravity 留下的 `/tmp/qmi_imei_write.c` 使用了错误的 QRTR 控制包类型及字段顺序，还把 QMUX/CTL 客户端分配帧用于 QRTR DMS 套接字。它没有通过协议核查，且包含实际写请求，**不能运行或纳入固件**。Antigravity 会话仅包含用户要求自定义显示和构造原始报文，未提供明确的新目标值；也没有成功写入后回读的证据。当前仅完成只读备份及协议核对，设备标识未修改。任何未来写入方案都需要先找到本机实际可用的协议、授权条件和明确目标值。
