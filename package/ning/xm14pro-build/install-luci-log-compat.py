#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Default LuCI logs to actionable lines while retaining a one-click full view."""

from pathlib import Path


top = Path(__file__).resolve().parents[3]
base = top / "feeds/luci/modules/luci-base/htdocs/luci-static/resources/tools/views.js"
syslog = top / "feeds/luci/modules/luci-mod-status/htdocs/luci-static/resources/view/status/syslog.js"
dmesg = top / "feeds/luci/modules/luci-mod-status/htdocs/luci-static/resources/view/status/dmesg.js"


def patch(path, old, new):
    source = path.read_text(encoding="utf-8")
    if new in source:
        return
    if source.count(old) != 1:
        raise SystemExit(f"refusing to patch changed LuCI log source: {path}")
    path.write_text(source.replace(old, new, 1), encoding="utf-8")


patch(base, "var CBILogreadBox = function(logtag, name) {", "var CBILogreadBox = function(logtag, name, focusErrors) {")
patch(base, "\t\tlogName: name ? name : _('System Log'),", "\t\tlogName: name ? name : _('System Log'),\n\t\tfocusErrors: !!focusErrors,")
patch(base,
      "\t\t\t\treturn {\n\t\t\t\t\tvalue: loglines?.join('\\n'),",
      """\t\t\t\t// Keep raw logs intact. Only the default display hides routine
\t\t\t\t// messages; known battery telemetry is mislabelled kern.err.
\t\t\t\tif (this.focusErrors)
\t\t\t\t\tloglines = loglines.filter(line =>
\t\t\t\t\t\t(/\\.(?:emerg|alert|crit|err|warn):/.test(line) || /\\b(?:error|failed|denied|timeout)\\b/i.test(line)) &&
\t\t\t\t\t\t!(/BATTERY_CHG: (?:handle_message:.*fg_update_status|quick_charge_type_show: wls_present)/.test(line) &&
\t\t\t\t\t\t  !/\\b(?:error|failed|denied|timeout)\\b/i.test(line)));

\t\t\t\treturn {
\t\t\t\t\tvalue: loglines?.join('\\n'),""")
patch(base,
      "\t\t\tconst self = this;\n\n\t\t\t// Create facility invert checkbox",
      """\t\t\tconst self = this;
\t\t\tconst focusToggle = E('input', {
\t\t\t\t'type': 'checkbox',
\t\t\t\t'checked': this.focusErrors ? 'checked' : null
\t\t\t});
\t\t\tfocusToggle.addEventListener('change', () => {
\t\t\t\tself.focusErrors = focusToggle.checked;
\t\t\t\tself.pollLog();
\t\t\t});

\t\t\t// Create facility invert checkbox""")
patch(base,
      "\t\t\t\tE('div', { 'id': 'content_syslog', 'class': 'cbi-section' }, [\n",
      "\t\t\t\tE('div', { 'id': 'content_syslog', 'class': 'cbi-section' }, [\n\t\t\t\t\tE('div', { 'style': 'margin-bottom:10px' }, [ focusToggle, E('span', {}, ' 只显示重点报错与警告（取消勾选可看完整日志）') ]),\n")
patch(syslog,
      "return views.LogreadBox(null, _('System Log'));",
      "return views.LogreadBox(null, _('System Log'), true);")
patch(dmesg,
      "\tsortLogsDescending: false,",
      "\tsortLogsDescending: false,\n\tfocusErrors: true,")
patch(dmesg,
      "\t\t\t// Sort by time\n\t\t\tif (this.sortLogsDescending) loglines.reverse();",
      """\t\t\t// Do not hide any real kernel warning or error. The battery
\t\t\t// telemetry below is informational despite its kern.err priority.
\t\t\tif (this.focusErrors)
\t\t\t\tloglines = loglines.filter(entry =>
\t\t\t\t\t(entry.severity <= 4 || /\\b(?:error|failed|denied|timeout)\\b/i.test(entry.text)) &&
\t\t\t\t\t!(/BATTERY_CHG: (?:handle_message:.*fg_update_status|quick_charge_type_show: wls_present)/.test(entry.text) &&
\t\t\t\t\t  !/\\b(?:error|failed|denied|timeout)\\b/i.test(entry.text)));

\t\t\t// Sort by time
\t\t\tif (this.sortLogsDescending) loglines.reverse();""")
patch(dmesg,
      "\t\tconst self = this;\n\n\n\t\t// Create range invert checkbox",
      """\t\tconst self = this;
\t\tconst focusToggle = E('input', {
\t\t\t'type': 'checkbox',
\t\t\t'checked': this.focusErrors ? 'checked' : null
\t\t});
\t\tfocusToggle.addEventListener('change', () => {
\t\t\tself.focusErrors = focusToggle.checked;
\t\t\tself.pollLog();
\t\t});

\t\t// Create range invert checkbox""")
patch(dmesg,
      "\t\t\tE('div', { 'id': 'content_syslog', 'class': 'cbi-section' }, [\n",
      "\t\t\tE('div', { 'id': 'content_syslog', 'class': 'cbi-section' }, [\n\t\t\t\tE('div', { 'style': 'margin-bottom:10px' }, [ focusToggle, E('span', {}, ' 只显示重点报错与警告（取消勾选可看完整日志）') ]),\n")
print("installed LuCI focused-log display; raw logs remain available")
