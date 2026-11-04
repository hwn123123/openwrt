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

## A 槽 Android 的 DIAG 只读探测

`diag_probe.py` 在 Mac 上临时把 A 槽 USB 切到 `diag,adb`，仅发送 Qualcomm DIAG 版本查询 `0x00` 和 NV 项 550 读取 `0x26`。它会恢复原来的 `adb` / configfs 2 配置，并在设备端安排自动恢复定时任务，以防切换时 ADB 断开。脚本不会输出读到的标识值，也没有写入命令。

本机依赖可以装进独立虚拟环境，不需要 Homebrew：

```sh
python3 -m venv /tmp/nwrt-diag-venv
/tmp/nwrt-diag-venv/bin/python -m pip install pyusb pyserial colorama
git clone --depth 1 https://github.com/bkerler/edl.git /tmp/nwrt-edl-readonly
/tmp/nwrt-diag-venv/bin/python -u diag_probe.py \
  --edl /tmp/nwrt-edl-readonly \
  --libusb /Library/PrivilegedHelperTools/libusb-1.0.0.dylib
```

2026-11-04 实机结果：系统的 configfs 1 模式能够把 `05c6:901d` DIAG USB 组合暴露给 Mac；`diag-router` 进程已启动，USB 接口可以被工具占用，但版本查询及 NV 550 读取均未收到响应。故目前不能确认 NV 550 可读，更不能推断存在可用的写入权限。脚本执行后确认 ADB、configfs 2 和 A 槽均恢复。`bkerler/edl` 是独立 GPLv3 项目，此目录没有复制其代码。

## 其他公开路径的实机核查

2026-11-04 从 [catxeon/qcom-efs-browser](https://github.com/catxeon/qcom-efs-browser) 源码构建 Android arm64 helper（检出版本 `b7fa178`），在 A 槽以默认只读模式请求 `open`。设备返回：`no DIAG service (4097) published on the QRTR bus; /dev/diag does not exist`。这说明该项目已验证的 QRTR DIAG 和 `diagchar` 两条通道在当前 PixelOS 内核/基带组合下均不可用。测试后 helper、ADB 端口转发及临时文件已清理；未尝试写入。

同日切回 B 槽 NWRT 后，`qrtr-lookup 4097` 只输出表头（没有 DIAG 服务），且 `/dev/diag` 不存在；因此 NWRT 当前也没有这条直接访问基带 EFS 的路径。NWRT 热点处于 AP 模式，蜂窝链路上行可访问网页。

## 原厂 OS3 诊断组件的只读核对

2026-11-04 使用本地 OS3.0.309.0 全量只读备份中的 `super.img` 和对应 `lpdump.txt` 定位 `vendor_a`；提取后由 `dump.erofs` 识别并列出文件。原厂 vendor 确实包含 `diag-router`、`ATFWD-daemon`、`diag_mdlog`、`libdiag.so`、诊断 HAL、工厂服务和相关 init/VINTF 文件。`vendor.qti.diag_userdebug.rc` 在 `early-boot` 启动 `/vendor/bin/diag-router -d 7`。原厂 `diag-router` 与 A 槽 PixelOS source dump 中的文件 SHA-256 完全相同，排除了该程序被 PixelOS 替换这一原因。

对原厂工厂服务、AT 转发服务、诊断路由及相关库做了只读静态检查，没有找到可证明 IMEI/NV/EFS 写入能力的专用接口。工厂服务提供的通用文件和进程方法不能等同于基带写权限。原厂用户态组件存在，与当前 A 槽只读 DIAG 查询无响应、B 槽缺少可用 DIAG/EFS 通道的实机结果并不矛盾；仍需区分服务运行状态、基带是否发布服务、路由和权限。

B 槽 `/sys/module/diag` 对应的是 Linux TIPC socket 诊断模块，已通过 `/proc/kallsyms` 中的 `tipc_sock_diag_handler` 等符号核对；它不是 Qualcomm 基带 DIAG。B 槽没有 `/dev/diag` 或 `/dev/diag_mdm`，也没有运行中的 `diag-router`。原厂镜像和本机备份均保持只读，未刷机、未修改设备标识。

[iamromulan/qfenix](https://github.com/iamromulan/qfenix) 支持常见 USB DIAG 串口，但 macOS 版需要其 `qcseriald` 串口桥；目前未在本机取得 NV 读取结果，不能把它列为已验证方案。第三方 [AndroidWinTool 的机型页](https://androidwintool.com/supported-models/xiaomi/xiaomi-14-pro-shennong) 宣称支持 `shennong` 的双卡 IMEI 维修，[版本记录](https://androidwintool.com/download)又列出 Magisk 30+ root 的条件；这是厂商声明，未在这台 PixelOS/NWRT 设备上验证，也没有公开实现细节。不要把其他设备的 QCN 或 modemst 镜像刷到本机。
