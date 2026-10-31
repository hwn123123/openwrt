#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Use hostapd's station cache on the Xiaomi AP instead of cfg80211 dumps."""

from pathlib import Path
import sys


TOPDIR = Path(__file__).resolve().parents[3]
DEFAULT = TOPDIR / (
    "feeds/luci/modules/luci-mod-status/htdocs/luci-static/resources/"
    "view/status/include/60_wifi.js"
)
target = Path(sys.argv[1]) if len(sys.argv) == 2 else DEFAULT

old = """\t\t\ttasks.push(L.resolveDefault(radios_networks_hints[i].getAssocList(), []).then(L.bind((net, list) => {
\t\t\t\tnet.assoclist = list.sort((a, b) => a.mac.localeCompare(b.mac));
\t\t\t}, this, radios_networks_hints[i])));"""
new = """\t\t\tconst net = radios_networks_hints[i];
\t\t\tif (net.getIfname() == 'nwrt-ap') {
\t\t\t\t// Xiaomi's cfg80211 dump walks beyond the final peer and floods dmesg.
\t\t\t\t// hostapd already owns the association table and its cached metrics.
\t\t\t\tconst getClients = rpc.declare({ object: 'hostapd.nwrt-ap', method: 'get_clients' });
\t\t\t\tconst width = parseInt((uci.get('wireless', net.getWifiDeviceName(), 'htmode') || '').replace(/\\D/g, ''), 10) || 20;
\t\t\t\ttasks.push(L.resolveDefault(getClients(), { clients: {} }).then(data => {
\t\t\t\t\tnet.assoclist = Object.entries(data.clients || {}).filter(([mac, client]) => client.authorized).map(([mac, client]) => ({
\t\t\t\t\t\tmac: mac.toUpperCase(),
\t\t\t\t\t\tsignal: Number(client.signal) || -110,
\t\t\t\t\t\tnoise: 0,
\t\t\t\t\t\trx: { rate: (Number(client.rate?.rx) || 0) / 100, mhz: width },
\t\t\t\t\t\ttx: { rate: (Number(client.rate?.tx) || 0) / 100, mhz: width }
\t\t\t\t\t})).sort((a, b) => a.mac.localeCompare(b.mac));
\t\t\t\t}));
\t\t\t}
\t\t\telse {
\t\t\t\ttasks.push(L.resolveDefault(net.getAssocList(), []).then(list => {
\t\t\t\t\tnet.assoclist = list.sort((a, b) => a.mac.localeCompare(b.mac));
\t\t\t\t}));
\t\t\t}"""

old = "\n".join("\t" + line for line in old.split("\n"))
new = "\n".join("\t" + line for line in new.split("\n"))
source = target.read_text(encoding="utf-8")
if new in source:
    print(f"LuCI Wi-Fi compatibility already installed: {target}")
elif source.count(old) == 1:
    target.write_text(source.replace(old, new, 1), encoding="utf-8")
    print(f"installed LuCI Wi-Fi compatibility: {target}")
else:
    raise SystemExit(f"refusing to patch changed LuCI Wi-Fi source: {target}")
