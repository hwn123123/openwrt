# FM350 拨号策略（本地修改，2026-09-10）

本包下载 Photonicat 仓库的 `0e61a4abf35c423420310311f1b2b059deba0cac`。
原始 `src/fm350-mm.c` 已带有外网 ping、自动制式回退和定时恢复制式策略。
修改前，下载归档里的该文件与本地编译目录文件的 SHA-256 均为
`45e91ecf57488da0b5da0ec29f90b0fe6aada014a25ad0a2794aa9be25496dab`。

上游来源：

- [984e2d2](https://github.com/photonicat/rockchip_rk3568_pcat_manager/commit/984e2d2cf9056a3848652435505bdad341a3de72)：加入拨号失败后的 LTE 回退、自动恢复 5G 和外网探测；提交署名 c2h2 / Claude。
- [f652844](https://github.com/photonicat/rockchip_rk3568_pcat_manager/commit/f652844de29a0e04dd745e30118192035e7fdde4)：增加有地址但外网探测失败时的制式回退。
- [历史](https://github.com/photonicat/rockchip_rk3568_pcat_manager/commits/0e61a4abf35c423420310311f1b2b059deba0cac/src/fm350-mm.c)：后续缩短首次失败判断、跳过部分重复 CFUN 操作。

执行者是 `pcat-manager` 启动的 `/usr/bin/fm350-mm`。旧探测目标为
`223.5.5.5` / `119.29.29.29`，失败策略会写 `AT+GTACT` 并执行无线重注册。
它不会把用户偏好文件同步改成 LTE，因而保存的 AUTO 与模组实际锁定制式可能不同。

本地补丁 `020-fm350-respect-rat-without-internet-probes.patch` 按用户要求：

- 删除外网 ping，以及探测失败触发的重拨。
- 删除拨号激活失败后的自动制式回退、定时切回与空闲流量判断。
- 删除连续 12 次地址读取失败后强制重拨的计数。
- 拨号和重试始终使用 `/etc/pcat-modem-pref.json` 保存的用户制式。
- 保留 SIM、注册、PDP 激活、IP/DNS/路由和防火墙配置。
- 保留模组明确报告 DEACT/DETACH 时的正常重拨。地址读取失败时，只有
  `AT+CGACT?` 明确返回当前 PDP 为 inactive 才进入重拨；错误、缺字段或其他
  PDP 的 inactive 状态均不作断线判定。该查询只读本地模组状态，不发外网探测包。

拿到 IP 后直接配置主机网络，不需要先通过外网探测。本修改不保证有地址的
连接一定能够访问外网，也不修复模组或 RNDIS 驱动本身的数据传输故障。
网页的公网 IP 缓存展示属于另一处代码，本补丁未修改。

验证：

```sh
make package/ning/photonicat2-vendor/pcat-manager/compile -j2 V=s
python3 package/ning/photonicat2-vendor/pcat-manager/tests/test_fm350_normal_dial.py \
  build_dir/target-aarch64_generic_musl/pcat-manager-2.0.0/src/fm350-mm.c
```

6 项测试使用 socket 模拟 AT 模组，运行实际 C 拨号与监控函数，拦截主机配置
命令，避免修改测试主机网络。已通过 aarch64 OpenWrt 编译，包版本 `2.0.0-r3`。
此次只修改本地源码和生成安装包，没有替换设备程序、重启服务或做实机启动验证。
