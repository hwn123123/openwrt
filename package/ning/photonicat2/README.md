# Photonicat 2 原生 LuCI 模块

这里不是厂家主题，也不启动 8001/8080/80 之外的新 Web 服务。公共包
`photonicat2-native-core` 只负责通过 pcat-manager、内核 sysfs 和
pcat2-display-mini 提供硬件后端；LuCI 页面均由当前 OpenWrt 主题渲染。

编译菜单使用一个总包入口：

* `luci-app-photonicat2`：在 `make menuconfig` 中选择后，会展开
  `Photonicat2 modules` 二级菜单。11 个模块选项默认全部为 `y`，可以按需
  关闭任意页面；关闭后对应的 LuCI 子菜单和页面文件不会进入固件。

二级菜单中的模块包：

* `photonicat2-native-core`：公共硬件后端（11 个选项中的后端项，页面会自动依赖）
* `luci-app-photonicat2-base`：设备总览
* `luci-app-photonicat2-wireless`：内置无线
* `luci-app-photonicat2-led`：电源指示灯
* `luci-app-photonicat2-buzzer`：蜂鸣器
* `luci-app-photonicat2-cpu`：CPU 节能
* `luci-app-photonicat2-fan`：智能风扇
* `luci-app-photonicat2-charge`：充电限制
* `luci-app-photonicat2-power`：开关机策略
* `luci-app-photonicat2-screen`：迷你屏幕设置、可视布局、JSON、预览和短信页
* `luci-app-photonicat2-battery`：电池实时数据、曲线、健康度和循环统计

公共 `photonicat2-native-core` 会由总包自动带入，提供硬件守护进程、
RPCD 权限和公共 CSS/JS；它不单独显示为页面。当前 `.config` 应选择新的
`luci-app-photonicat2` 总包，并取消旧的厂家 Web 包；旧厂家 Web 包源码已从
`package/ning` 移除，不会与新总包冲突。厂家主题
`luci-theme-design` 和旧的迷你屏幕 LuCI 包可以继续保持取消；`pcat-manager`、
`pcat2-display-mini`、`kmod-photonicat-pm` 作为硬件后端保留。默认的 11 个
二级选项为“公共硬件后端”加上 10 个 LuCI 页面包；页面包被勾选时会自动
保留后端，后端不能在仍有页面使用时被真正取消。

编译完成后，当前 OpenWrt 主题中会出现一级菜单 `photonicat2功能`，下面按
编译时勾选的页面显示子菜单。所有页面共用同一个原生 LuCI 后端，不启动厂家
80/8080/8001 Web 服务，也不接管原生 QModem/mqodem 拨号。
