#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Show actionable LuCI logs by default while retaining a raw-log switch."""

from pathlib import Path
import sys


TOPDIR = Path(__file__).resolve().parents[3]
DEFAULT = TOPDIR / (
    "feeds/luci/modules/luci-mod-status/htdocs/luci-static/resources/view/status"
)
directory = Path(sys.argv[1]) if len(sys.argv) == 2 else DEFAULT
syslog = directory / "syslog.js"
dmesg = directory / "dmesg.js"

stock_syslog = """'use strict';
'require tools.views as views';

return views.LogreadBox(null, _('System Log'));"""

device_syslog = """'use strict';
'require tools.views as views';

const page = views.LogreadBox(null, _('System Log'));
const originalRetrieve = page.retrieveLog;
const originalRender = page.render;

page.onlyImportant = true;
page.retrieveLog = async function() {
    const result = await originalRetrieve.call(this);
    if (!this.onlyImportant || !result.value)
        return result;

    const rows = result.value.split('\\n').filter(line => {
        if (!/\\.(?:emerg|alert|crit|err|warn):/.test(line))
            return false;
        return !/(?:__wlan_hdd_cfg80211_dump_station|dp_txrx_set_vdev_param_be|wlan_twt_responder_enable|osif_twt_responder_enable|kauditd_printk_skb|xm_charger_debug_info_print_work|xm_batt_update_work)/.test(line)
            && !/audit: type=1400.*permissive=1/.test(line);
    });
    return { value: rows.join('\\n'), rows: rows.length + 1 };
};

page.render = function(loglines) {
    const showAll = E('input', { type: 'checkbox', class: 'cbi-input-checkbox' });
    showAll.addEventListener('change', () => {
        this.onlyImportant = !showAll.checked;
        this.pollLog();
    });
    return E('div', {}, [
        E('div', { class: 'cbi-section', style: 'padding:10px' }, [
            showAll, E('label', { style: 'margin-left:8px' }, '显示全部原始日志')
        ]),
        originalRender.call(this, loglines)
    ]);
};

return page;
"""

source = syslog.read_text(encoding="utf-8")
if source.strip() == device_syslog.strip():
    print(f"LuCI system-log compatibility already installed: {syslog}")
elif source.strip() == stock_syslog:
    syslog.write_text(device_syslog, encoding="utf-8")
    print(f"installed LuCI system-log compatibility: {syslog}")
else:
    raise SystemExit(f"refusing to replace changed LuCI system-log view: {syslog}")

source = dmesg.read_text(encoding="utf-8")
marker = "\tonlyImportant: true,"
if marker in source:
    if "\t\tpoll.add(this.pollLog.bind(this));" in source:
        source = source.replace(
            "\t\tpoll.add(this.pollLog.bind(this));",
            "\t\tpoll.add(this.pollLog.bind(this), 30);",
            1,
        )
        dmesg.write_text(source, encoding="utf-8")
    elif "\t\tpoll.add(this.pollLog.bind(this), 30);" not in source:
        raise SystemExit(f"refusing to patch changed LuCI kernel-log polling: {dmesg}")
    print(f"LuCI kernel-log compatibility already installed: {dmesg}")
else:
    replacements = [
        (
            "\tsortLogsDescending: false,",
            "\tsortLogsDescending: false,\n\tonlyImportant: true,",
        ),
        (
            "\t\t\t// Sort by time\n\t\t\tif (this.sortLogsDescending) loglines.reverse();",
            "\t\t\t// Keep the original records available through the raw-log switch.\n"
            "\t\t\tif (this.onlyImportant)\n"
            "\t\t\t\tloglines = loglines.filter(entry => entry.severity <= 4\n"
            "\t\t\t\t\t&& !/(?:__wlan_hdd_cfg80211_dump_station|dp_txrx_set_vdev_param_be|wlan_twt_responder_enable|osif_twt_responder_enable|kauditd_printk_skb|xm_charger_debug_info_print_work|xm_batt_update_work)/.test(entry.text)\n"
            "\t\t\t\t\t&& !/audit: type=1400.*permissive=1/.test(entry.text));\n\n"
            "\t\t\t// Sort by time\n\t\t\tif (this.sortLogsDescending) loglines.reverse();",
        ),
        (
            "\t\tconst self = this;",
            "\t\tconst self = this;\n"
            "\t\tconst showAll = E('input', { type: 'checkbox', class: 'cbi-input-checkbox' });\n"
            "\t\tshowAll.addEventListener('change', () => {\n"
            "\t\t\tthis.onlyImportant = !showAll.checked;\n"
            "\t\t\tthis.pollLog();\n"
            "\t\t});",
        ),
        (
            "\t\tpoll.add(this.pollLog.bind(this));",
            "\t\tpoll.add(this.pollLog.bind(this), 30);",
        ),
        (
            "\t\t\tE('h2', {}, [ _('Kernel Log') ]),",
            "\t\t\tE('h2', {}, [ _('Kernel Log') ]),\n"
            "\t\t\tE('div', { class: 'cbi-section', style: 'padding:10px' }, [\n"
            "\t\t\t\tshowAll, E('label', { style: 'margin-left:8px' }, '显示全部原始日志')\n"
            "\t\t\t]),",
        ),
    ]
    for old, new in replacements:
        if source.count(old) != 1:
            raise SystemExit(f"refusing to patch changed LuCI kernel-log view near {old[:60]!r}: {dmesg}")
        source = source.replace(old, new, 1)
    dmesg.write_text(source, encoding="utf-8")
    print(f"installed LuCI kernel-log compatibility: {dmesg}")
