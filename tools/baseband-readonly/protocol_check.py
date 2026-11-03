#!/usr/bin/env python3
"""Static, read-only QRTR/QMI header sanity check; no device access."""

import argparse
from pathlib import Path
import re
import sys


QRTR_HEADER = Path("include/uapi/linux/qrtr.h")
QMI_HEADER = Path("include/linux/soc/qcom/qmi.h")


def section(source: str, pattern: str, name: str) -> str:
    match = re.search(pattern, source, flags=re.S)
    if not match:
        raise ValueError(f"{name}: expected struct not found")
    return match.group(1)


def check(kernel_root: Path) -> None:
    qrtr = (kernel_root / QRTR_HEADER).read_text(encoding="utf-8")
    qmi = (kernel_root / QMI_HEADER).read_text(encoding="utf-8")
    for name, value in (("NEW_SERVER", "4"), ("NEW_LOOKUP", "10")):
        if not re.search(rf"\bQRTR_TYPE_{name}\s*=\s*{value}\b", qrtr):
            raise ValueError(f"QRTR_TYPE_{name}: value differs from reviewed header")

    ctrl = section(qrtr, r"struct\s+qrtr_ctrl_pkt\s*\{(.*?)\}\s*__packed\s*;", "QRTR")
    server = section(ctrl, r"struct\s*\{(.*?)\}\s*server\s*;", "QRTR server")
    fields = re.findall(r"__le32\s+(\w+)\s*;", server)
    if fields != ["service", "instance", "node", "port"]:
        raise ValueError("QRTR server field order differs from reviewed header")

    header = section(qmi, r"struct\s+qmi_header\s*\{(.*?)\}\s*__packed\s*;", "QMI")
    fields = re.findall(r"\b(u8|u16)\s+(\w+)\s*;", header)
    if fields != [("u8", "type"), ("u16", "txn_id"), ("u16", "msg_id"), ("u16", "msg_len")]:
        raise ValueError("QMI header differs from reviewed 7-byte QRTR wire format")
    print("OK: QRTR control values/field order and 7-byte QMI header match reviewed source")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel_root", type=Path, help="root of the local Xiaomi kernel source")
    args = parser.parse_args()
    try:
        check(args.kernel_root)
        return 0
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
