import json
import subprocess
from flask import jsonify, request

OPTIONS = {"enabled", "provider", "ntfy_server", "ntfy_topic", "ntfy_token",
           "telegram_bot_token", "telegram_chat_id", "webhook_url",
           "event_boot", "event_wan", "wan_interfaces"}
BOOLS = {"enabled", "event_boot", "event_wan"}


def run(argv, timeout=22):
    try:
        p = subprocess.run(argv, text=True, capture_output=True, timeout=timeout, check=False)
        return p.returncode, p.stdout, p.stderr
    except (OSError, subprocess.TimeoutExpired) as exc:
        return 127, "", str(exc)


def config():
    values = {}
    for name in OPTIONS:
        rc, out, _ = run(["uci", "-q", "get", "push_center.main." + name], 5)
        values[name] = out.strip() if rc == 0 else ""
    return values


def helper(action):
    rc, out, err = run(["/usr/libexec/push-center", action], 22)
    try: result = json.loads(out)
    except ValueError: result = {"ok": rc == 0, "message": (err or out or "操作失败").strip()}
    return result, 200 if result.get("ok") else 500


def register(app, api_auth_required, manifest):
    @api_auth_required
    def api():
        if request.method == "GET":
            result, code = helper("status")
            result["config"] = config()
            return jsonify(result), code
        data = request.get_json(silent=True) or {}
        action = str(data.get("action") or "save")
        if action == "save":
            values = data.get("config") or {}
            for name in OPTIONS:
                if name not in values: continue
                value = str(values[name] or "").strip()
                if name in BOOLS: value = "1" if values[name] in {True, 1, "1", "true"} else "0"
                if len(value) > 512 or "\n" in value or "\r" in value:
                    return jsonify({"ok": False, "message": "配置内容不合法"}), 400
                run(["uci", "set", "push_center.main.%s=%s" % (name, value)], 5)
            run(["uci", "commit", "push_center"], 6)
            return jsonify({"ok": True, "message": "推送设置已保存"})
        if action == "test":
            result, code = helper("test")
            return jsonify(result), code
        return jsonify({"ok": False, "message": "不支持的操作"}), 400

    app.add_url_rule("/api/v1/extensions/push-center", "pcat_push_center_api", api, methods=["GET", "POST"])
