# Xiaomi 14 Pro 原生 LuCI 移动网络

本包是原生 LuCI 应用：不开独立 HTTP 端口、不用 iframe、不依赖
旧的 Quectel RGMII 服务。组件布局和视觉体系移植自
`quectel-rgmii-toolkit-Rust` 的 SimpleAdmin 前端，但所有数据和操作均使用
Xiaomi 14 Pro 的 nwrt 本地后端。未引入参考前端的主题、语言、登录或侧边栏实现；
页面只读取 nwrt 总主题变量和 LuCI 语言。

LuCI 一级菜单为 `移动网络`，二级页面按用途合并为四项：

- 概览：CPU、内存、基带温度、信号、流量和 CPE 状态；
- 蜂窝网络与 SIM：真实数据链路、动态运营商/APN、飞行模式、双卡分工、信号、服务小区、频段与载波聚合；
- 通话与短信：WMS 路由、实时通话查询、IMS 与 SIM 能力；
- CPE 与设备：Wi-Fi 热点、USB 网络、NAT 转发、设备后端、QMI 服务和诊断日志。

安卓参考日志用于核对拨号、数据开关、SIM 热插拔和换槽、飞行模式、
IMS、短信和电话流程。当前手机固件会拒绝通用 QMI WMS 存储列表查询，
因此 UI 明确显示“手机基带固件已拒绝”，不生成短信数量。语音的只读实时通话
查询已接通；在可写语音/短信执行层完成前，不提供伪造的拨号、发送或卡槽切换按钮。

运行时严禁伪造：不为缺失的信号、SIM、APN、短信、电话、小区或连接状态
提供样例值。没有真实 nwrt 执行通道的控件必须保持禁用。

概览页通过原厂 DIAG 服务和只读 NV 550 查询分别获取 IMEI 1 与 IMEI 2，
结果仅缓存到本次启动的 `/var/run/xm14pro/nv-identities.json`。当前 QMI
标识单独显示，便于核对活动卡槽；若插入 SIM，还通过 UIM 读取对应
ICCID，并明确标注为 SIM 卡标识。页面提供手动重新读取按钮；若诊断服务
不可用，明确显示第二项“暂不可读取”，不使用备份或常量冒充实时数据。
查询适配层使用 qcom-efs-browser 的 BSD-3-Clause DIAG/NV 读取代码，
其许可证随软件包安装。

`xm14display.main.cell_custom_imei` 是独立的“页面显示测试值”，只用于
检查 LuCI 显示逻辑。它不会覆盖真实标识行，也不写入 NV/EFS，不改变
ModemManager、SIM 或运营商读取的身份。
