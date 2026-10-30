#!/usr/bin/env python3
import argparse, json, os, re, shutil, subprocess, sys, time
from pathlib import Path
PLUGIN_DIR = Path(__file__).resolve().parents[1]
MODE_ORDER = {"send": 0, "send-recv": 1, "client-connect": 2}
def find_integration_dir():
    for parent in PLUGIN_DIR.parents:
        c = parent / "raceboat" / "test" / "integration"
        if c.is_dir(): return c
    sys.exit("no integration dir")
def register_plugin(integration_dir):
    rp = integration_dir / "plugin_registry.json"
    registry = json.loads(rp.read_text())
    relative = os.path.relpath(PLUGIN_DIR, integration_dir)
    if registry.get(PLUGIN_DIR.name) != relative:
        registry[PLUGIN_DIR.name] = relative
        rp.write_text(json.dumps(registry, indent=2) + "\n")
def load_scenarios(selected):
    out = []
    for path in sorted((PLUGIN_DIR / "test" / "scenarios").glob("*.json")):
        sc = json.loads(path.read_text())
        if selected and sc["id"] not in selected: continue
        out.append((MODE_ORDER.get(sc["mode"], 99), sc["id"], path, sc))
    return [(sid, p, sc) for _, sid, p, sc in sorted(out)]
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image-tag", default=os.environ.get("RACEBOAT_IMAGE_TAG", "main"))
    ap.add_argument("--scenario", action="append")
    ap.add_argument("--wait-time", type=int)
    ap.add_argument("--exit-timeout", type=int, default=120)
    ap.add_argument("--stub-timeout", type=int, default=180)
    args = ap.parse_args()
    integration_dir = find_integration_dir()
    register_plugin(integration_dir)
    scenarios = load_scenarios(set(args.scenario or []))
    if not scenarios: sys.exit("no scenarios")
    stubs_dir = PLUGIN_DIR / "test" / "stubs"
    for scenario_id, path, scenario in scenarios:
        shutil.copy(path, integration_dir / "scenarios" / path.name)
        mode = scenario.get("mode", "client-connect")
        wait_time = args.wait_time if args.wait_time is not None else scenario.get("wait_time", 60)
        stub_timeout = args.stub_timeout if args.stub_timeout is not None else scenario.get("stub_timeout", 180)
        subprocess.run([sys.executable, "generate_scenario.py", "--scenario-id", scenario_id, "--image-tag", args.image_tag], cwd=integration_dir, check=True)
        generated = integration_dir / "generated" / scenario_id
        compose_file = generated / "docker-compose.yml"
        if mode in ("send", "send-recv"):
            cmd = [sys.executable, "run-send-recv-test.py", "--compose-file", str(compose_file), "--wait-time", str(wait_time), "--exit-timeout", str(args.exit_timeout), "--name", scenario_id, "--listener-container", f"{scenario_id}-listener", "--connector-container", f"{scenario_id}-connector", "--mode", "send-recv" if mode == "send-recv" else "send", "--send-message", scenario.get("send_message", "Hello"), "--reply-message", scenario.get("reply_message", "Reply")]
        else:
            cmd = [sys.executable, "run-integration-test.py", "--compose-file", str(compose_file), "--wait-time", str(wait_time), "--name", scenario_id, "--server-container", f"{scenario_id}-listener", "--client-container", f"{scenario_id}-connector", "--client-stub-path", str(stubs_dir / "tcp-stub-client.py"), "--server-stub-path", str(stubs_dir / "tcp-stub-server.py"), "--stub-timeout", str(stub_timeout)]
        saved = PLUGIN_DIR / "test" / "logs" / f"{time.strftime('%Y%m%d-%H%M%S')}-{scenario_id}"
        saved.mkdir(parents=True, exist_ok=True)
        print(f"\n=== {scenario_id}: {' '.join(cmd)}", flush=True)
        with open(saved / "run_scenario.out", "w") as out:
            proc = subprocess.Popen(cmd, cwd=integration_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            for line in proc.stdout:
                sys.stdout.write(line); out.write(line)
            code = proc.wait()
        if generated.is_dir():
            if (generated / "logs").is_dir():
                shutil.copytree(generated / "logs", saved / "logs", dirs_exist_ok=True)
            if compose_file.is_file():
                shutil.copy(compose_file, saved / "docker-compose.yml")
        print(f"=== {scenario_id}: {'PASSED' if code == 0 else 'FAILED'} (logs: {saved})")
        if code != 0: return code
    return 0
if __name__ == "__main__":
    sys.exit(main())
