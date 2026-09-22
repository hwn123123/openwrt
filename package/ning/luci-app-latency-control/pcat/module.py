import re
import subprocess
from flask import jsonify, request

OPTIONS = {"enabled", "interface", "download", "upload", "latency_preset"}


def run(argv, timeout=20):
    try:
        p = subprocess.run(argv, text=True, capture_output=True, timeout=timeout, check=False)
        return p.returncode, p.stdout, p.stderr
    except (OSError, subprocess.TimeoutExpired) as exc:
        return 127, "", str(exc)


def get(option, default=""):
    rc, out, _ = run(["uci", "-q", "get", "sqm.@queue[0]." + option], 5)
    return out.strip() if rc == 0 else default


def snapshot():
    interface = get("interface")
    if not interface:
        _, routes, _ = run(["ip", "-4", "route", "show", "default"], 5)
        match = re.search(r"(?:^|\s)dev\s+([A-Za-z0-9_.:@-]+)", routes)
        interface = match.group(1) if match else ""
    rc, out, err = run(["tc", "-s", "qdisc", "show", "dev", interface], 8) if re.fullmatch(r"[A-Za-z0-9_.:@-]+", interface or "") else (1, "", "")
    defaults = {"enabled": "0", "interface": "", "download": "100000",
                "upload": "30000", "latency_preset": "balanced"}
    cfg = {name: get(name, defaults[name]) for name in OPTIONS}
    if not cfg.get("interface"):
        cfg["interface"] = interface
    return {"ok": True, "config": cfg,
            "active": "cake" in out, "qdisc": out.strip(), "error": err.strip()}


def register(app, api_auth_required, manifest):
    @api_auth_required
    def api():
        if request.method == "GET":
            return jsonify(snapshot())
        data = request.get_json(silent=True) or {}
        action = str(data.get("action") or "save")
        if action == "save":
            cfg = data.get("config") or {}
            for name in OPTIONS:
                if name not in cfg:
                    continue
                value = str(cfg[name] or "").strip()
                if name == "enabled": value = "1" if cfg[name] in {True, 1, "1", "true"} else "0"
                if name in {"download", "upload"} and not re.fullmatch(r"[0-9]{1,9}", value):
                    return jsonify({"ok": False, "message": "带宽必须是 kbit/s 整数"}), 400
                if len(value) > 128 or "\n" in value:
                    return jsonify({"ok": False, "message": "配置内容不合法"}), 400
                run(["uci", "set", "sqm.@queue[0].%s=%s" % (name, value)], 5)
            preset = str(cfg.get("latency_preset") or "balanced")
            ingress = "besteffort nat dual-dsthost ingress" if preset == "streaming" else "diffserv4 nat dual-dsthost ingress"
            egress = "besteffort nat dual-srchost ack-filter" if preset == "streaming" else "diffserv4 nat dual-srchost ack-filter"
            for name, value in {"qdisc":"cake", "script":"layer_cake.qos", "qdisc_advanced":"1", "iqdisc_opts":ingress, "eqdisc_opts":egress}.items():
                run(["uci", "set", "sqm.@queue[0].%s=%s" % (name, value)], 5)
            run(["uci", "commit", "sqm"], 6)
            action = "restart"
        if action not in {"restart", "stop"}:
            return jsonify({"ok": False, "message": "不支持的操作"}), 400
        rc, out, err = run(["/etc/init.d/sqm", action], 25)
        return jsonify({"ok": rc == 0, "message": "低延迟整形已" + ("应用" if action == "restart" else "停止"), "detail": (err or out).strip()}), 200 if rc == 0 else 500

    app.add_url_rule("/api/v1/extensions/latency-control", "pcat_latency_control_api", api, methods=["GET", "POST"])
