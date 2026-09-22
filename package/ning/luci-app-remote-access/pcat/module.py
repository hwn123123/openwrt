import json
import re
import subprocess

from flask import jsonify, request

OPTIONS = {
    "enabled", "hostname", "login_server", "accept_dns", "accept_routes",
    "advertise_exit_node", "advertise_routes", "exit_node",
    "exit_node_allow_lan_access", "tailscale_ssh",
}
BOOLS = {"enabled", "accept_dns", "accept_routes", "advertise_exit_node",
         "exit_node_allow_lan_access", "tailscale_ssh"}


def run(argv, timeout=25):
    try:
        result = subprocess.run(argv, text=True, capture_output=True, timeout=timeout, check=False)
        return result.returncode, result.stdout, result.stderr
    except (OSError, subprocess.TimeoutExpired) as exc:
        return 127, "", str(exc)


def config():
    values = {}
    for name in OPTIONS:
        _, out, _ = run(["uci", "-q", "get", "remote_access.main." + name], 5)
        values[name] = out.strip()
    return values


def status():
    rc, out, err = run(["/usr/libexec/remote-access", "status"], 8)
    try:
        value = json.loads(out)
    except ValueError:
        value = {"ok": False, "message": (err or out or "状态读取失败").strip()}
    value["config"] = config()
    return value, 200 if rc == 0 else 500


def register(app, api_auth_required, manifest):
    endpoint = "pcat_remote_access_api"

    @api_auth_required
    def api():
        if request.method == "GET":
            value, code = status()
            return jsonify(value), code
        data = request.get_json(silent=True) or {}
        action = str(data.get("action") or "save")
        if action == "save":
            submitted = data.get("config") or {}
            for name in OPTIONS:
                if name not in submitted:
                    continue
                value = str(submitted[name] or "").strip()
                if name in BOOLS:
                    value = "1" if value in {"1", "true", "True"} or submitted[name] is True else "0"
                if len(value) > 256 or "\n" in value or "\r" in value:
                    return jsonify({"ok": False, "message": "配置内容不合法"}), 400
                run(["uci", "set", "remote_access.main.%s=%s" % (name, value)], 5)
            run(["uci", "commit", "remote_access"], 8)
            rc, out, err = run(["/usr/libexec/remote-access", "apply"], 30)
        elif action in {"connect", "apply", "down", "logout"}:
            rc, out, err = run(["/usr/libexec/remote-access", action], 30)
        else:
            return jsonify({"ok": False, "message": "不支持的操作"}), 400
        try:
            result = json.loads(out)
        except ValueError:
            result = {"ok": rc == 0, "message": (err or out or "操作完成").strip()}
        return jsonify(result), 200 if result.get("ok") else 400

    app.add_url_rule("/api/v1/extensions/remote-access", endpoint, api,
                     methods=["GET", "POST"])
