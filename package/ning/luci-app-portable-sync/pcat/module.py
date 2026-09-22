import re
import subprocess
from flask import jsonify, request

OPTIONS = {"enabled", "gui_address", "home", "user", "group", "memlimit"}


def run(argv, timeout=25):
    try:
        p = subprocess.run(argv, text=True, capture_output=True, timeout=timeout, check=False)
        return p.returncode, p.stdout, p.stderr
    except (OSError, subprocess.TimeoutExpired) as exc:
        return 127, "", str(exc)


def get(option, default=""):
    rc, out, _ = run(["uci", "-q", "get", "syncthing.syncthing." + option], 5)
    return out.strip() if rc == 0 else default


def snapshot():
    running = run(["pidof", "syncthing"], 5)[0] == 0
    defaults = {"enabled": "0", "gui_address": "http://0.0.0.0:8384",
                "home": "/etc/syncthing", "user": "syncthing",
                "group": "syncthing", "memlimit": "0"}
    return {"ok": True, "running": running,
            "config": {name: get(name, defaults[name]) for name in OPTIONS}}


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
                if name not in cfg: continue
                value = str(cfg[name] or "").strip()
                if name == "enabled": value = "1" if cfg[name] in {True, 1, "1", "true"} else "0"
                if name in {"home"} and value and not value.startswith("/"):
                    return jsonify({"ok": False, "message": "目录必须使用绝对路径"}), 400
                if name == "gui_address" and not re.fullmatch(r"https?://[^\s:]+:\d{1,5}", value):
                    return jsonify({"ok": False, "message": "管理地址格式应为 http://地址:端口"}), 400
                if name == "memlimit" and value and not value.isdigit():
                    return jsonify({"ok": False, "message": "内存上限必须是整数"}), 400
                if len(value) > 256 or "\n" in value:
                    return jsonify({"ok": False, "message": "配置内容不合法"}), 400
                run(["uci", "set", "syncthing.syncthing.%s=%s" % (name, value)], 5)
            run(["uci", "commit", "syncthing"], 6)
            action = "restart"
        if action not in {"restart", "stop"}:
            return jsonify({"ok": False, "message": "不支持的操作"}), 400
        rc, out, err = run(["/etc/init.d/syncthing", action], 30)
        return jsonify({"ok": rc == 0, "message": "同步服务已" + ("重启" if action == "restart" else "停止"), "detail": (err or out).strip()}), 200 if rc == 0 else 500

    app.add_url_rule("/api/v1/extensions/portable-sync", "pcat_portable_sync_api", api, methods=["GET", "POST"])
