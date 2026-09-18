"""FM350 eSIM controls integrated with the vendor Web modem client.

Status commands use the already-open serial connection.  lpac uses the same
AT port under the shared flock used by the Web client and fm350-mm, so APDU
responses cannot be consumed by a different modem worker.
"""

import json
import os
import re
import subprocess
import threading
import time
import uuid

from flask import jsonify, request

import pcat_modem_usb


_LOCK_PATH = "/tmp/pcat-fm350-at.lock"
_LPAC = "/usr/bin/lpac-fm350"
_JOB_LOCK = threading.Lock()
_JOB = None
_REGISTERED = False


class EsimError(Exception):
    def __init__(self, message, status=400):
        super().__init__(message)
        self.status = status


def _client(app_module):
    client = getattr(app_module, "modem_client", None)
    if client is None:
        raise EsimError("蜂窝模组客户端尚未就绪，请稍后重试", 503)
    model = str(getattr(client, "basic", {}).get("model", "")).strip()
    # The vendor poller fills basic["model"] asynchronously after Web starts.
    # An empty value is therefore not evidence of an unsupported modem.  The
    # FM350-only GTDUALSIM/SIMTYPE commands in _status() provide the capability
    # check; keep rejecting a positively identified, different modem.
    if model and "FM350" not in model.upper():
        raise EsimError("只支持已识别的 FM350 模组", 409)
    if not app_module.is_wwan_powered():
        raise EsimError("蜂窝模组电源已关闭", 409)
    return client


def _at_locked(client, command, timeout=8):
    serial_obj = client.serial_obj
    if serial_obj is None or not serial_obj.is_open:
        raise EsimError("AT 端口尚未就绪，请稍后重试", 503)
    serial_obj.write((command + "\r").encode("ascii"))
    serial_obj.flush()
    lines = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = serial_obj.readline()
        line = raw.decode("ascii", errors="replace").strip()
        if not line or line == command:
            continue
        if client._handle_unsolicited_line(line):
            continue
        if line == "OK":
            return lines
        if line == "ERROR" or line.startswith("+CME ERROR"):
            raise EsimError("模组拒绝 {}：{}".format(command.split("=")[0], line), 409)
        lines.append(line)
    raise EsimError("模组响应超时，请稍后重试", 504)


def _status(app_module):
    client = _client(app_module)
    with client.serial_lock:
        slot_lines = _at_locked(client, "AT+GTDUALSIM?")
        slots_lines = _at_locked(client, "AT+GTDUALSIM=?")
        type_lines = _at_locked(client, "AT+SIMTYPE?")
        slot_match = re.search(r"\+GTDUALSIM\s*:\s*([01])", " ".join(slot_lines))
        slots_match = re.search(r"\+GTDUALSIM\s*:\s*\(?([0-9\- ,]+)\)?", " ".join(slots_lines))
        type_match = re.search(r"\+SIMTYPE\s*:\s*([01])", " ".join(type_lines))
        slot = int(slot_match.group(1)) if slot_match else None
        eid = ""
        if slot == 1:
            try:
                eid_lines = _at_locked(client, "AT+EID?")
                eid_match = re.search(r"\+EID\s*:\s*\"?(\d{32})\"?", " ".join(eid_lines))
                eid = eid_match.group(1) if eid_match else ""
            except EsimError:
                pass
    return {
        "status": "ok", "model": str(client.basic.get("model", "")).strip() or "FM350-GL",
        "slot": slot, "sim_type": int(type_match.group(1)) if type_match else None,
        "sim2_available": bool(slots_match and "1" in slots_match.group(1)),
        "eid": eid, "euicc_ready": bool(eid),
        "lpac_available": os.path.isfile(_LPAC) and os.access(_LPAC, os.X_OK),
        "message": ("已检测到 eUICC" if eid else
                    "当前为实体卡 SIM1；切换至 SIM2 后才能检测 EID" if slot == 0 else
                    "SIM2 未返回 EID，可能未搭载或未初始化 eUICC"),
    }


def _switch_slot(app_module, slot):
    if slot not in (0, 1):
        raise EsimError("卡槽必须是 SIM1 或 SIM2")
    client = _client(app_module)
    with client.serial_lock:
        supported = _at_locked(client, "AT+GTDUALSIM=?")
        if slot == 1 and not re.search(r"\b0\s*-\s*1\b|\b1\b", " ".join(supported)):
            raise EsimError("模组未报告 SIM2 可用", 409)
        _at_locked(client, "AT+GTDUALSIM={}".format(slot), timeout=20)
    client.cmd_queue_append("AT+CPIN?")
    client.cmd_queue_append("AT+CCID")
    client.cmd_queue_append("AT+CIMI")
    return {"status": "ok", "message": "已切换到 SIM{}；蜂窝数据连接可能需要重新驻网".format(slot + 1)}


def _lpac(args, timeout=45):
    if not os.path.isfile(_LPAC) or not os.access(_LPAC, os.X_OK):
        raise EsimError("固件中未安装 lpac-fm350，无法管理 eSIM 套餐", 503)
    env = os.environ.copy()
    env.update({
        "LPAC_APDU": "at",
        "LPAC_APDU_AT_DEVICE": pcat_modem_usb.resolve_primary_at_port(),
        "LPAC_HTTP": "curl", "LPAC_APDU_AT_DEBUG": "false",
        "LPAC_HTTP_DEBUG": "false",
    })
    try:
        result = subprocess.run(
            ["/usr/bin/flock", "-x", _LOCK_PATH, _LPAC] + args,
            capture_output=True, text=True, env=env, timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired:
        raise EsimError("eSIM 操作超时，结果可能仍需回读确认", 504)
    except OSError:
        raise EsimError("无法启动 eSIM 管理程序", 503)
    try:
        payload = json.loads(result.stdout)
        body = payload["payload"]
    except (ValueError, TypeError, KeyError):
        raise EsimError("eSIM 管理程序无有效响应；请检查 SIM2/EID 与 AT 通道", 502)
    if result.returncode or body.get("code") != 0:
        raise EsimError("eSIM 操作失败：{}".format(str(body.get("message", "未知错误"))[:180]), 409)
    return body.get("data")


def _require_euicc(app_module):
    status = _status(app_module)
    if status["slot"] != 1:
        raise EsimError("请先切换至 eSIM 所在的 SIM2；切换会中断当前蜂窝连接", 409)
    if not status["euicc_ready"]:
        raise EsimError("SIM2 未返回有效 EID，不能执行套餐操作", 409)
    return status


def _string(payload, key, maximum=256, pattern=None, required=True):
    value = str(payload.get(key, "")).strip()
    if (required and not value) or len(value) > maximum or any(ord(c) < 32 for c in value):
        raise EsimError("{} 格式不正确".format(key))
    if pattern and value and not re.fullmatch(pattern, value):
        raise EsimError("{} 格式不正确".format(key))
    return value


def _action_args(action, payload, eid):
    if action in ("enable", "disable", "delete", "nickname"):
        iccid = _string(payload, "iccid", 40, r"[0-9A-Fa-f]{10,40}")
        if action == "nickname":
            nickname = _string(payload, "nickname", 64)
            return ["profile", "nickname", iccid, nickname], 60
        return ["profile", action, iccid], 90
    if action == "download":
        activation = _string(payload, "activation_code", 1024, r"LPA:1\$[^\s]{1,1018}")
        args = ["profile", "download", "-a", activation]
        code = _string(payload, "confirmation_code", 256, required=False)
        if code:
            args += ["-c", code]
        return args, 600
    if action == "discovery":
        server = _string(payload, "server", 253, r"[A-Za-z0-9.-]+", required=False)
        return (["profile", "discovery", "-s", server] if server else
                ["profile", "discovery"]), 180
    if action == "default_smdp":
        address = _string(payload, "address", 253, r"[A-Za-z0-9.-]+")
        return ["chip", "defaultsmdp", address], 60
    if action in ("process_notification", "remove_notification"):
        sequence = _string(payload, "sequence", 10, r"[0-9]{1,10}")
        verb = "process" if action == "process_notification" else "remove"
        return ["notification", verb, sequence], 180
    if action == "purge":
        if _string(payload, "confirm_eid", 32, r"[0-9]{32}") != eid:
            raise EsimError("请输入完整 EID 确认清空 eUICC")
        return ["chip", "purge"], 180
    raise EsimError("不支持的 eSIM 操作")


def _job_worker(app_module, job_id, action, args, timeout):
    global _JOB
    try:
        _require_euicc(app_module)
        data = _lpac(args, timeout=timeout)
        result = {"status": "ok", "data": data,
                  "message": "{} 已完成，请刷新状态确认".format(action)}
        state = "done"
    except EsimError as error:
        result = {"status": "error", "message": str(error)}
        state = "error"
    with _JOB_LOCK:
        if _JOB and _JOB["id"] == job_id:
            _JOB.update({"state": state, "result": result, "finished": int(time.time())})


def register_esim(flask_app, api_auth_required, app_module):
    global _REGISTERED, _JOB
    if _REGISTERED:
        return
    _REGISTERED = True

    @flask_app.route("/api/v1/modem/esim/status.json", methods=["GET"])
    @api_auth_required
    def pcat_esim_status():
        try:
            response = jsonify(_status(app_module))
            response.headers["Cache-Control"] = "no-store"
            return response
        except EsimError as error:
            return jsonify({"status": "error", "message": str(error)}), error.status

    @flask_app.route("/api/v1/modem/esim/slot.json", methods=["POST"])
    @api_auth_required
    def pcat_esim_switch_slot():
        try:
            payload = request.get_json(silent=True) or {}
            return jsonify(_switch_slot(app_module, payload.get("slot")))
        except EsimError as error:
            return jsonify({"status": "error", "message": str(error)}), error.status

    @flask_app.route("/api/v1/modem/esim/read.json", methods=["GET"])
    @api_auth_required
    def pcat_esim_read():
        try:
            _require_euicc(app_module)
            kind = request.args.get("kind", "")
            commands = {"chip": ["chip", "info"],
                        "profiles": ["profile", "list"],
                        "notifications": ["notification", "list"]}
            if kind not in commands:
                raise EsimError("无效的读取项目")
            data = _lpac(commands[kind], timeout=60)
            if kind == "profiles" and isinstance(data, list):
                data = [{key: value for key, value in item.items() if key != "icon"}
                        for item in data if isinstance(item, dict)]
            response = jsonify({"status": "ok", "data": data})
            response.headers["Cache-Control"] = "no-store"
            return response
        except EsimError as error:
            return jsonify({"status": "error", "message": str(error)}), error.status

    @flask_app.route("/api/v1/modem/esim/action.json", methods=["POST"])
    @api_auth_required
    def pcat_esim_action():
        global _JOB
        try:
            payload = request.get_json(silent=True) or {}
            action = _string(payload, "action", 32, r"[a-z_]+")
            status = _require_euicc(app_module)
            args, timeout = _action_args(action, payload, status["eid"])
            with _JOB_LOCK:
                if _JOB and _JOB["state"] == "running":
                    raise EsimError("已有 eSIM 操作正在进行", 409)
                job_id = uuid.uuid4().hex
                _JOB = {"id": job_id, "state": "running", "action": action,
                        "started": int(time.time()), "result": None}
            threading.Thread(target=_job_worker,
                             args=(app_module, job_id, action, args, timeout),
                             name="pcat-esim-operation", daemon=True).start()
            return jsonify({"status": "ok", "job_id": job_id}), 202
        except EsimError as error:
            return jsonify({"status": "error", "message": str(error)}), error.status

    @flask_app.route("/api/v1/modem/esim/job.json", methods=["GET"])
    @api_auth_required
    def pcat_esim_job():
        with _JOB_LOCK:
            job = dict(_JOB) if _JOB else None
        if not job or job["id"] != request.args.get("id"):
            return jsonify({"status": "error", "message": "操作记录不存在"}), 404
        response = jsonify({"status": "ok", **job})
        response.headers["Cache-Control"] = "no-store"
        return response
