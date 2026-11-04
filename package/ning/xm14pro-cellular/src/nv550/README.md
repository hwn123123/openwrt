# 双卡 IMEI 只读查询适配层

`reader.c` 只调用 `nv_read_sub()` 读取 Qualcomm NV 550 的索引 0 和 1。
它不包含写入调用，不修改基带、SIM 或运营商侧的设备标识。
`diag.c`、`efs2.c`、`util.c` 及其头文件来自
[catxeon/qcom-efs-browser](https://github.com/catxeon/qcom-efs-browser)，
按同目录 BSD-3-Clause `LICENSE` 使用。

本软件包的 `files/android-root` 下五个运行时文件来自这台 Xiaomi 14 Pro
的原厂系统备份，保持二进制原样，供 Android 兼容根中的诊断服务使用。
诊断服务仅在查询时短暂运行，结果只保存在本次启动的临时目录。
