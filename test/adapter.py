#!/usr/bin/env python3
"""
Integration-test adapter for the transport stub (copy and adapt for a new transport).

Contract (see raceboat/test/integration/adapter_types.py and TESTING_NEW_PLUGINS.md):
  * kit_dir(role)                     -> where this plugin's built kit lives
  * generate_node_contribution(req)   -> race-cli params/flags for ONE node (+direction)

Addressing model used here (LD_CREATOR_TO_LOADER, "each node creates its own SEND container"):
  * every node owns one container it posts into:  S(role) = <prefix>-<scenario>-<role>-send
  * a node receives by loading its PEER's container: listener loads S(connector) and vice versa
  * names are deterministic, so no address has to be passed between nodes (no peer_context)

Scenario modes handled (race-cli flags in parentheses):
  send            connector sends, listener receives            (--send / --recv)
  send-recv       connector request, listener reply             (--send-recv / --recv-reply)
  client-connect  persistent socket proxy, both directions      (--client-connect / --server-connect)
bootstrap-connect needs a bootstrap-capable channel and is not supported by this template.
"""

import json
import os
import platform
import sys
from pathlib import Path

try:
    # generate_scenario.py puts raceboat/test/integration on sys.path before loading adapters.
    from adapter_types import NodeContribution, NodeRequest
except ImportError:
    # Standalone use: find a raceboat checkout in any parent directory of this plugin (the depth
    # differs between racegen/plugins/<name> and racegen/<name>).
    _integration_dir = next(
        (p / "raceboat" / "test" / "integration" for p in Path(__file__).resolve().parents
         if (p / "raceboat" / "test" / "integration").is_dir()), None)
    if _integration_dir is None:
        raise ImportError("adapter: could not find raceboat/test/integration") from None
    sys.path.insert(0, str(_integration_dir))
    from adapter_types import NodeContribution, NodeRequest  # noqa: E402

# Must match source/manifest.json ("compositions[].id") and source/CMakeLists.txt (PLUGIN_NAME).
DEFAULT_COMPOSITION = "audiusComposition"
KIT_NAME = "PluginAudius"

# Host environment variables holding the service credentials. When set they are referenced as
# ${VAR} in the generated docker-compose.yml, so compose substitutes them at `up` time and nothing
# secret is ever written to a file. Rename for your service.
SERVER_ENV = "AUDIUS_SERVER"
TOKEN_ENV = "AUDIUS_TOKEN"
# Container-name prefix. Services often restrict names, so keep it [a-z0-9-_].
CONTAINER_PREFIX = "transport"

# With no AUDIUS_SERVER set the scenarios run against test/mock_service.py in a sidecar container, so
# the template works with no credentials. For a real service delete the mock and always use the
# credential branch of _credential_params(). Sidecars are extra docker-compose services keyed by
# name (see decomposed-exemplars/test/adapter.py for another example).
MOCK_HOST = "audius-mock-service"
MOCK_SERVER = f"http://{MOCK_HOST}:8080"
SIDECAR_SERVICES = {
    MOCK_HOST: {
        "image": "python:3.12-slim",
        "hostname": MOCK_HOST,
        "entrypoint": ["python3", "/mock_service.py"],
        "volumes": [f"{Path(__file__).resolve().parent / 'mock_service.py'}:/mock_service.py:ro"],
        "healthcheck": {
            "test": ["CMD", "python3", "-c",
                     "import socket; socket.create_connection(('127.0.0.1', 8080), 2).close()"],
            "interval": "2s",
            "timeout": "2s",
            "retries": 15,
        },
    }
}


def _detect_host_architecture() -> str:
    machine = platform.machine().lower()
    if machine in {"x86_64", "amd64"}:
        return "x86_64"
    if machine in {"arm64", "aarch64"}:
        return "arm64-v8a"
    raise ValueError(f"Unsupported host architecture: {platform.machine()}")


def kit_dir(role: str) -> Path:
    """Called with no scenario context, only to read this kit's manifest.json."""
    side = "client" if role == "connector" else "server"
    plugin_root = Path(__file__).resolve().parents[1]
    return (plugin_root / "kit" / "artifacts"
            / f"linux-{_detect_host_architecture()}-{side}" / KIT_NAME)


def container_name(scenario_id: str, role: str) -> str:
    """The container `role` posts into. Both nodes compute every name, so nothing is exchanged."""
    safe = scenario_id.replace(".", "-")
    return f"{CONTAINER_PREFIX}-{safe}-{role}-send"


def container_address(name: str) -> str:
    """JSON text of a link address; must parse with source/transport/LinkAddress.cpp."""
    return json.dumps({"container": name})


def action_file(mode: str, role: str) -> str:
    """Absolute path (inside the container) of the user-model schedule for this node.

    The file-based user model resolves a RELATIVE path against ITS OWN plugin directory, not this
    plugin's, so always pass an absolute /kits/<KIT_NAME>/... path.
    """
    if mode == "send":
        name = "actions-send-only.json" if role == "connector" else "actions.json"
    else:
        name = "actions-combined.json"
    return f"/kits/{KIT_NAME}/config/{name}"


def _credential_params(use_mock: bool):
    """(server, token) values for --param. Real credentials are never inlined: they are ${VAR}
    placeholders that docker compose fills from the host environment (e.g. `set -a; . .env`)."""
    if use_mock:
        return MOCK_SERVER, "mock-token"
    if not os.environ.get(TOKEN_ENV):
        raise ValueError(f"{TOKEN_ENV} must be set when {SERVER_ENV} is set")
    return "${" + SERVER_ENV + "}", "${" + TOKEN_ENV + "}"


def generate_node_contribution(request: NodeRequest) -> NodeContribution:
    mode = request.scenario_mode
    if mode not in ("send", "send-recv", "client-connect"):
        raise ValueError(f"stub adapter does not support scenario mode '{mode}'")
    scenario_id = request.scenario_id or "audius-scenario"
    composition = request.composition_name or DEFAULT_COMPOSITION
    peer_role = "connector" if request.role == "listener" else "listener"

    # Which directions this node participates in for this mode.
    wants_send = mode != "send" or request.role == "connector"
    wants_recv = mode != "send" or request.role == "listener"
    # request.direction is None when ONE contribution serves both directions, or "send"/"recv"
    # when the scenario split the slot across two channels; then fill only that direction.
    wants_send &= request.direction in (None, "send")
    wants_recv &= request.direction in (None, "recv")

    use_mock = not os.environ.get(SERVER_ENV)
    server, token = _credential_params(use_mock)
    contribution = NodeContribution(
        params={
            # --param keys are "<composition id>.<key>"; <key> is what the component passes to
            # sdk->requestPluginUserInput(). The plugin/library name is NOT the prefix.
            f"{composition}.server": server,
            f"{composition}.token": token,
            f"{composition}.actionFile": action_file(mode, request.role),
        },
        channel_name=composition,
        kit_dir=kit_dir(request.role),
        sidecar_services=dict(SIDECAR_SERVICES) if use_mock else {},
    )

    if wants_send:
        contribution.cli_flags["send-address"] = container_address(
            container_name(scenario_id, request.role))
    if wants_recv:
        contribution.cli_flags["recv-address"] = container_address(
            container_name(scenario_id, peer_role))
    return contribution
