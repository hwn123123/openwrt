#!/usr/bin/env python3
"""Load optional feature packages into the Photonicat management UI."""

from __future__ import annotations

import importlib.util
import json
import re
import sys
from pathlib import Path

from flask import jsonify, redirect, render_template, session
from jinja2 import ChoiceLoader, FileSystemLoader


EXTENSION_ROOT = Path("/usr/share/pcat-manager-web/extensions")
ID_RE = re.compile(r"^[a-z][a-z0-9-]{1,47}$")


def _load_manifest(directory):
    try:
        raw = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    except (OSError, ValueError, TypeError):
        return None
    ident = str(raw.get("id") or "")
    if not ID_RE.fullmatch(ident) or ident != directory.name or not (directory / "page.html").is_file():
        return None
    try:
        order = int(raw.get("order") or 100)
    except (TypeError, ValueError):
        order = 100
    return {
        "id": ident,
        "title": str(raw.get("title") or ident),
        "title_zh": str(raw.get("title_zh") or raw.get("title") or ident),
        "description": str(raw.get("description") or ""),
        "description_zh": str(raw.get("description_zh") or raw.get("description") or ""),
        "icon": str(raw.get("icon") or "app"),
        "order": order,
        "path": "/apps/" + ident,
    }


def _discover():
    try:
        directories = sorted(item for item in EXTENSION_ROOT.iterdir() if item.is_dir())
    except OSError:
        directories = []
    found = []
    for directory in directories:
        manifest = _load_manifest(directory)
        if manifest:
            found.append((directory, manifest))
    return sorted(found, key=lambda item: (item[1]["order"], item[1]["id"]))


def register_extensions(flask_app, api_auth_required, base_locales):
    discovered = _discover()
    manifests = [manifest for _, manifest in discovered]
    by_id = {manifest["id"]: (directory, manifest) for directory, manifest in discovered}

    flask_app.jinja_loader = ChoiceLoader([
        flask_app.jinja_loader,
        FileSystemLoader(str(EXTENSION_ROOT)),
    ])

    for directory, manifest in discovered:
        module_file = directory / "module.py"
        if not module_file.is_file():
            continue
        module_name = "pcat_extension_" + manifest["id"].replace("-", "_")
        try:
            spec = importlib.util.spec_from_file_location(module_name, module_file)
            module = importlib.util.module_from_spec(spec)
            sys.modules[module_name] = module
            spec.loader.exec_module(module)
            module.register(flask_app, api_auth_required, manifest)
        except Exception:
            # A broken optional package must not take down the main web UI.
            flask_app.logger.exception("failed to load Photonicat extension %s", manifest["id"])

    @flask_app.route("/apps")
    def pcat_extensions_page():
        if "username" not in session:
            return redirect("/login?next=/apps")
        return render_template("extensions.html", locales=base_locales(), extensions=manifests)

    @flask_app.route("/apps/<extension_id>")
    def pcat_extension_page(extension_id):
        if "username" not in session:
            return redirect("/login?next=/apps/" + extension_id)
        item = by_id.get(extension_id)
        if item is None:
            return "Extension not found", 404
        _, manifest = item
        return render_template(extension_id + "/page.html", locales=base_locales(),
                               extension=manifest, extensions=manifests)

    @flask_app.route("/api/v1/extensions.json")
    @api_auth_required
    def pcat_extensions_api():
        return jsonify({"status": "ok", "extensions": manifests})
