"""Native named render morphs: original cooked files, CLI, desktop and protected inputs."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

cli, fixture = [str(Path(p).resolve()) for p in sys.argv[1:3]]
desktop = str(Path(sys.argv[3]).resolve()) if len(sys.argv) > 3 else None
env = {k.upper() if os.name == "nt" else k: v for k, v in os.environ.items()}
checks = 0
with tempfile.TemporaryDirectory(prefix=".test-tmp-cooked-morphs-", dir=Path.cwd()) as temp:
    root = Path(temp)
    subprocess.run([fixture, "--write-fixtures", str(root)], check=True, capture_output=True, env=env)
    context = json.loads((root / "context.json").read_text(encoding="utf-8"))
    context["usmap"] = "after.usmap"
    (root / "after.usmap").write_bytes((root / "native.usmap").read_bytes())
    (root / "after-context.json").write_text(json.dumps(context), encoding="utf-8")
    protected = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    entry = "000000000000000000000001"
    common = ["--cooked-profile", "ue5.3", "--script-objects", root / "global.utoc", "--schema", root / "context.json", "--skeletal-profile", "ue5.3-windows", "--keep-mobile-min-lod", "0", "--has-vertex-colors", "0"]

    def run(command, *args, code=0):
        global checks
        result = subprocess.run([cli, command, *map(str, common), *map(str, args)], env=env, capture_output=True, timeout=30)
        assert result.returncode == code, (result.returncode, result.stderr.decode("utf-8", "replace"))
        checks += 1
        return json.loads(result.stdout) if result.stdout else None

    before, after = root / "morphs.utoc", root / "changed.utoc"
    model = run("zen-skeletal", before, entry)
    assert model["lods"][0]["morphs"]["ready"] and model["morphBinding"]["names"] == ["Morph/0", "Morph/1"]
    assert model["lods"][0]["morphs"]["targets"][1]["storedDeltas"] == 0
    assert run("zen-skeletal-diff", before, entry, before, entry)["status"] == "identical"
    diff = run("zen-skeletal-diff", before, entry, after, entry, "--after-schema", root / "after-context.json", code=1)
    assert len(diff["changes"]) == 1 and diff["changes"][0]["path"].endswith("/Morph~10/vertices/0/0/position/0")
    unknown = run("zen-skeletal-diff", before, entry, root / "unbound.utoc", entry, code=2)
    assert unknown["status"] == "incomplete" and not unknown["complete"] and not unknown["changes"]
    stream = run("zen-skeletal", root / "streamed.utoc", entry)
    assert stream["lods"][0]["morphs"]["buffer"] == model["lods"][0]["morphs"]["buffer"]
    assert run("zen-skeletal-diff", before, entry, root / "absent.utoc", entry, code=1)["status"] == "changed"
    run("zen-skeletal", root / "corrupt.utoc", entry, code=2)
    checks += 6
    if desktop:
        flags = ["--cooked-profile", "ue5.3", "--script-objects", root / "global.utoc", "--schema", root / "context.json", "--skeletal-profile", "ue5.3-windows", "--skeletal-keep-mobile-min-lod", "0", "--has-vertex-colors", "0", "--after-cooked-profile", "ue5.3", "--after-script-objects", root / "global.utoc", "--after-schema", root / "after-context.json", "--after-skeletal-profile", "ue5.3-windows", "--after-skeletal-keep-mobile-min-lod", "0", "--after-has-vertex-colors", "0"]
        state = {"container": {"mesh": "/Game/Texture.Texture LOD 0", "meshRenderMorphs": True, "meshRenderMorphView": {"target": "Morph/0", "vertex": 0, "changedOnly": True, "properties": {"selected": "/0/position/0"}}}}
        for language in ("en", "fr"):
            report, workspace = root / (language + ".json"), root / (language + "-workspace.json")
            result = subprocess.run([desktop, "--smoke", "--no-preferences", "--no-restore-session", "--locale", language, "--container", str(before), "--container-entry", entry, "--after-container", str(after), "--after-container-entry", entry, "--container-tab", "meshes", "--report", str(report), "--save-workspace", str(workspace), *map(str, flags), "--document-action", "view-state:" + json.dumps(state, separators=(",", ":"))], capture_output=True, env=env, timeout=45)
            assert result.returncode == 0, result.stderr.decode("utf-8", "replace")
            data = json.loads(report.read_text(encoding="utf-8"))
            views = data["containerView"]["meshViews"]
            assert data["locale"] == language and all(v["morphsAvailable"] and v["morphs"]["ready"] and v["morphs"]["modified"] == 1 and v["morphs"]["added"] == v["morphs"]["removed"] == 0 for v in views)
            assert views[0]["morphs"]["selected"] == 0 and views[0]["morphs"]["selectedVisible"]
            saved = json.loads(workspace.read_text(encoding="utf-8"))
            assert saved["documents"][0]["viewState"]["container"]["meshRenderMorphs"]
            assert saved["documents"][0]["viewState"]["container"]["meshRenderMorphView"]["target"] == "Morph/0"
            checks += 4
    for name in ("morphs.utoc", "morphs.ucas", "changed.utoc", "changed.ucas", "global.utoc", "global.ucas", "native.usmap", "after.usmap", "context.json", "after-context.json"):
        run("zen-skeletal-diff", before, entry, after, entry, "--after-schema", root / "after-context.json", "--out", root / name, code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == digest for p, digest in protected.items())
    checks += 1
print(f"{checks} cooked render morph CLI/desktop checks; inputs unchanged")
