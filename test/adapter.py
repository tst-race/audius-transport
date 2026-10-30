#!/usr/bin/env python3
"""
Integration-test adapter for the Audius transport.
"""

import json
import os
import platform
import sys
from pathlib import Path

try:
    from adapter_types import NodeContribution, NodeRequest
except ImportError:
    _integration_dir = next(
        (p / "raceboat" / "test" / "integration" for p in Path(__file__).resolve().parents
         if (p / "raceboat" / "test" / "integration").is_dir()), None)
    if _integration_dir is None:
        raise ImportError("adapter: could not find raceboat/test/integration") from None
    sys.path.insert(0, str(_integration_dir))
    from adapter_types import NodeContribution, NodeRequest  # noqa: E402

DEFAULT_COMPOSITION = "audiusComposition"
KIT_NAME = "PluginAudius"

CONTAINER_PREFIX = "audius"

API_KEY_ENV = "AUDIUS_API_KEY"
API_SECRET_ENV = "AUDIUS_API_SECRET"
USER_ID_ENV = "AUDIUS_USER_ID"
BEARER_ENV = "AUDIUS_BEARER_TOKEN"
CONTENT_NODE_ENV = "AUDIUS_CONTENT_NODE"


def _detect_host_architecture() -> str:
    machine = platform.machine().lower()
    if machine in {"x86_64", "amd64"}:
        return "x86_64"
    if machine in {"arm64", "aarch64"}:
        return "arm64-v8a"
    raise ValueError(f"Unsupported host architecture: {platform.machine()}")


def kit_dir(role: str) -> Path:
    side = "client" if role == "connector" else "server"
    plugin_root = Path(__file__).resolve().parents[1]
    return (plugin_root / "kit" / "artifacts"
            / f"linux-{_detect_host_architecture()}-{side}" / KIT_NAME)


def container_name(scenario_id: str, role: str) -> str:
    safe = scenario_id.replace(".", "-")
    return f"{CONTAINER_PREFIX}-{safe}-{role}-send"


def container_address(name: str) -> str:
    # Owner is embedded so a peer can resolve the logical name to the creator's track.
    return json.dumps({"container": "${" + USER_ID_ENV + "}:" + name})


def action_file(mode: str, role: str) -> str:
    if mode == "send":
        name = "actions-send-only.json" if role == "connector" else "actions.json"
    else:
        name = "actions-combined.json"
    return f"/kits/{KIT_NAME}/config/{name}"


def _required_env(name: str) -> str:
    if not os.environ.get(name):
        raise ValueError(f"{name} must be set")
    return "${" + name + "}"


def _optional_env(name: str) -> str:
    if os.environ.get(name):
        return "${" + name + "}"
    return ""



def _scenario_id(request) -> str:
    nid = request.node_id
    for suffix in ("-listener", "-connector"):
        if nid.endswith(suffix):
            return nid[:-len(suffix)]
    return nid


def _scenario_mode(scenario_id: str) -> str:
    plugin_root = Path(__file__).resolve().parents[1]
    for base in (plugin_root / "test" / "scenarios",):
        pth = base / f"{scenario_id}.json"
        if pth.exists():
            try:
                return json.loads(pth.read_text()).get("mode", "client-connect")
            except Exception:
                pass
    # fallback inference
    sid = scenario_id.lower()
    if "send-only" in sid or "send_only" in sid:
        return "send"
    if "send-recv" in sid or "send_recv" in sid:
        return "send-recv"
    return "client-connect"


def generate_node_contribution(request: NodeRequest) -> NodeContribution:
    scenario_id = _scenario_id(request)
    mode = _scenario_mode(scenario_id)
    if mode not in ("send", "send-recv", "client-connect"):
        raise ValueError(f"audius adapter does not support scenario mode '{mode}'")
    composition = request.composition_name or DEFAULT_COMPOSITION
    peer_role = "connector" if request.role == "listener" else "listener"

    wants_send = mode != "send" or request.role == "connector"
    wants_recv = mode != "send" or request.role == "listener"
    wants_send &= request.direction in (None, "send")
    wants_recv &= request.direction in (None, "recv")

    api_key = _required_env(API_KEY_ENV)
    api_secret = _required_env(API_SECRET_ENV)
    user_id = _required_env(USER_ID_ENV)
    bearer = _optional_env(BEARER_ENV)
    content_node = _optional_env(CONTENT_NODE_ENV)

    params = {
        f"{composition}.apiKey": api_key,
        f"{composition}.apiSecret": api_secret,
        f"{composition}.userId": user_id,
        f"{composition}.actionFile": action_file(mode, request.role),
    }
    if bearer:
        params[f"{composition}.bearerToken"] = bearer
    if content_node:
        params[f"{composition}.contentNode"] = content_node

    contribution = NodeContribution(
        params=params,
        channel_name=composition,
        kit_dir=kit_dir(request.role),
    )

    if wants_send:
        contribution.cli_flags["send-address"] = container_address(
            container_name(scenario_id, request.role))
    if wants_recv:
        contribution.cli_flags["recv-address"] = container_address(
            container_name(scenario_id, peer_role))
    return contribution
