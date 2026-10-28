#!/usr/bin/env python3
"""
Runs this plugin's integration scenarios the standard way, stopping at the first failure.

    set -a; . /path/to/.env; set +a            # real-service credentials, if any
    ./test/run_scenarios.py --image-tag dev [--scenario audius-send-only] [--wait-time N]

What it does (all of which are otherwise easy to forget):
  1. registers this plugin in raceboat/test/integration/plugin_registry.json (if missing);
  2. installs test/scenarios/*.json into raceboat/test/integration/scenarios/ (the orchestrator only
     reads scenarios from there);
  3. runs run_scenario.py per scenario in a fixed order (send, send-recv, client-connect, others);
  4. preserves every run's output and the generated docker logs under test/logs/<timestamp>-<id>/
     (pass or fail), because the orchestrator wipes generated/<id>/ on the next run. Credentials
     that race-cli --debug echoes into its logs are masked in these copies (the originals under
     raceboat/test/integration/generated/ are NOT scrubbed).

--image-tag must be the tag of the raceboat-runtime image you want to test with, and the plugin must
have been built (./build_artifacts_in_docker_image.sh) with the matching builder tag.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

PLUGIN_DIR = Path(__file__).resolve().parents[1]
MODE_ORDER = {"send": 0, "send-recv": 1, "client-connect": 2}


def find_integration_dir() -> Path:
    for parent in PLUGIN_DIR.parents:
        candidate = parent / "raceboat" / "test" / "integration"
        if candidate.is_dir():
            return candidate
    sys.exit("could not find raceboat/test/integration in any parent of " + str(PLUGIN_DIR))


def register_plugin(integration_dir: Path) -> None:
    registry_path = integration_dir / "plugin_registry.json"
    registry = json.loads(registry_path.read_text())
    relative = os.path.relpath(PLUGIN_DIR, integration_dir)
    if registry.get(PLUGIN_DIR.name) != relative:
        registry[PLUGIN_DIR.name] = relative
        registry_path.write_text(json.dumps(registry, indent=2) + "\n")
        print(f"registered {PLUGIN_DIR.name} -> {relative} in {registry_path}")


SECRET_PARAM = re.compile(rb"(Got parameter: '[^']*(?:token|secret|password|passwd|credential|api_?key)[^']*' = ')[^']*'",
                          re.IGNORECASE)


def scrub_secrets(directory: Path) -> None:
    """race-cli --debug logs every --param value. Mask credentials in the preserved copies, both by
    parameter name and by the literal value of any *TOKEN*/*SECRET*/*PASSWORD* environment variable."""
    literals = [value.encode() for name, value in os.environ.items()
                if value and len(value) >= 8 and re.search(r"TOKEN|SECRET|PASSWORD", name, re.I)]
    for path in directory.rglob("*"):
        if not path.is_file() or path.stat().st_size > 200_000_000:
            continue
        data = path.read_bytes()
        scrubbed = SECRET_PARAM.sub(rb"\1<redacted>'", data)
        for literal in literals:
            scrubbed = scrubbed.replace(literal, b"<redacted>")
        if scrubbed != data:
            path.write_bytes(scrubbed)


def load_scenarios(selected):
    scenarios = []
    for path in sorted((PLUGIN_DIR / "test" / "scenarios").glob("*.json")):
        scenario = json.loads(path.read_text())
        if selected and scenario["id"] not in selected:
            continue
        scenarios.append((MODE_ORDER.get(scenario["mode"], 99), scenario["id"], path))
    return [(sid, path) for _, sid, path in sorted(scenarios)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--image-tag", default=os.environ.get("RACEBOAT_IMAGE_TAG", "main"))
    parser.add_argument("--scenario", action="append", help="scenario id (repeatable); default all")
    parser.add_argument("--wait-time", type=int)
    args = parser.parse_args()

    integration_dir = find_integration_dir()
    register_plugin(integration_dir)
    scenarios = load_scenarios(set(args.scenario or []))
    if not scenarios:
        sys.exit("no matching scenarios in test/scenarios")

    for scenario_id, path in scenarios:
        shutil.copy(path, integration_dir / "scenarios" / path.name)
        command = [sys.executable, "run_scenario.py", "--scenario-id", scenario_id,
                   "--image-tag", args.image_tag]
        if args.wait_time is not None:
            command += ["--wait-time", str(args.wait_time)]

        saved = PLUGIN_DIR / "test" / "logs" / f"{time.strftime('%Y%m%d-%H%M%S')}-{scenario_id}"
        saved.mkdir(parents=True, exist_ok=True)
        print(f"\n=== {scenario_id}: {' '.join(command)}", flush=True)
        with open(saved / "run_scenario.out", "w") as out:
            process = subprocess.Popen(command, cwd=integration_dir, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, text=True)
            for line in process.stdout:
                sys.stdout.write(line)
                out.write(line)
            code = process.wait()

        generated = integration_dir / "generated" / scenario_id
        if generated.is_dir():
            shutil.copytree(generated / "logs", saved / "logs", dirs_exist_ok=True)
            shutil.copy(generated / "docker-compose.yml", saved / "docker-compose.yml")
        scrub_secrets(saved)
        print(f"=== {scenario_id}: {'PASSED' if code == 0 else 'FAILED'} (logs: {saved})")
        if code != 0:
            return code
    return 0


if __name__ == "__main__":
    sys.exit(main())
