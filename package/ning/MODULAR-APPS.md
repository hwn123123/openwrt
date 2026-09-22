# Modular LuCI and Photonicat applications

The applications below are normal target-independent OpenWrt packages:

- `luci-app-remote-access`
- `luci-app-latency-control`
- `luci-app-push-center`
- `luci-app-portable-sync`

Each package owns its backend dependency, UCI configuration and LuCI page. It
can therefore be selected and used on an OpenWrt device that does not install
the Photonicat web interface.

The same package also installs a dormant Photonicat extension in:

```
/usr/share/pcat-manager-web/extensions/<application-id>/
```

An extension contains `manifest.json`, `module.py` and `page.html`. The
`pcat-manager-web` loader scans this directory once when it starts. An installed
manifest creates an entry in the Photonicat application center; an absent
package creates no menu or route. The feature package deliberately has no
dependency on `pcat-manager-web`, so these extra files have no effect on other
OpenWrt devices.

LuCI and Photonicat are two frontends over the same UCI configuration and
service. Neither frontend copies settings to the other one.
