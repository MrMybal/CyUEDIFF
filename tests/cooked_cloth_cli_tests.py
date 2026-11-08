"""Stored cloth maps reach CLI, explicit native desktop pairing and protected outputs."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

cli, base, fixture = [str(Path(p).resolve()) for p in sys.argv[1:4]]
desktop = str(Path(sys.argv[4]).resolve()) if len(sys.argv) > 4 else None
env = {k.upper() if os.name == "nt" else k: v for k, v in os.environ.items()}
checks = 0
with tempfile.TemporaryDirectory(prefix=".test-tmp-cooked-cloth-", dir=Path.cwd()) as temp:
    root = Path(temp)
    for exe in (base, fixture):
        subprocess.run([exe, "--write-fixtures", str(root)], check=True, capture_output=True, env=env)
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

    before, after = root / "cloth.utoc", root / "cloth-changed.utoc"
    model = run("zen-skeletal", before, entry)
    cloth = model["lods"][0]["cloth"]
    assert not cloth["simulationMeshBoundsVerified"] and len(cloth["buffer"]) == 3
    assert cloth["sections"][0]["assetGuidWords"] == [1, 2, 3, 4] and cloth["indexMapping"] == [[0, 0, 3]]
    assert run("zen-skeletal-diff", before, entry, before, entry)["status"] == "identical"
    diff = run("zen-skeletal-diff", before, entry, after, entry, "--after-schema", root / "after-context.json", code=1)
    assert len(diff["changes"]) == 1 and diff["changes"][0]["path"] == "/cloth/sections/0/lodBiases/0/0/0/position/3"
    multi = run("zen-skeletal", root / "cloth-multi.utoc", entry)["lods"][0]["cloth"]
    assert len(multi["buffer"]) == 30 and multi["indexMapping"] == [[0, 0, 15]]
    run("zen-skeletal", root / "cloth-corrupt.utoc", entry, code=2)
    checks += 5
    if desktop:
        flags = ["--cooked-profile", "ue5.3", "--script-objects", root / "global.utoc", "--schema", root / "context.json", "--skeletal-profile", "ue5.3-windows", "--skeletal-keep-mobile-min-lod", "0", "--has-vertex-colors", "0", "--after-cooked-profile", "ue5.3", "--after-script-objects", root / "global.utoc", "--after-schema", root / "after-context.json", "--after-skeletal-profile", "ue5.3-windows", "--after-skeletal-keep-mobile-min-lod", "0", "--after-has-vertex-colors", "0"]
        state = {"container": {"mesh": "/Game/Texture.Texture LOD 0", "meshCloth": True, "meshClothView": {"section": 0, "lodBias": 0, "vertex": 0, "changedOnly": True, "properties": {"selected": "/influences/0/position/3"}}}}
        for locale in ("en", "fr"):
            report, workspace = root / (locale + ".json"), root / (locale + "-workspace.json")
            args = [desktop, "--smoke", "--no-preferences", "--no-restore-session", "--locale", locale, "--container", str(before), "--container-entry", entry, "--after-container", str(root / "cloth-renamed-changed.utoc"), "--after-container-entry", entry, "--container-tab", "meshes", "--report", str(report), "--save-workspace", str(workspace), *map(str, flags), "--document-action", "view-state:" + json.dumps(state, separators=(",", ":"))]
            p = subprocess.run(args, env=env, capture_output=True, timeout=45)
            assert p.returncode == 0, p.stderr.decode("utf-8", "replace")
            data = json.loads(report.read_text(encoding="utf-8"))
            views = data["containerView"]["meshViews"]
            assert data["locale"] == locale and all(v["comparisonKnown"] and v["clothAvailable"] and v["cloth"]["comparing"] and v["cloth"]["modified"] == 1 for v in views)
            assert views[0]["cloth"]["selected"] == 0 and views[0]["cloth"]["selectedVisible"]
            assert data["document"]["container"]["meshDifferences"][0]["pairing"] == "explicit-members-single-mesh-lod"
            saved = json.loads(workspace.read_text(encoding="utf-8"))["documents"][0]["viewState"]["container"]
            assert saved["meshCloth"] and saved["meshClothView"]["vertex"] == 0 and saved["meshClothView"]["lodBias"] == 0
            checks += 4
    for name in ("cloth.utoc", "cloth.ucas", "cloth-changed.utoc", "cloth-changed.ucas", "global.utoc", "global.ucas", "native.usmap", "after.usmap", "context.json", "after-context.json"):
        run("zen-skeletal-diff", before, entry, after, entry, "--after-schema", root / "after-context.json", "--out", root / name, code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == h for p, h in protected.items())
    checks += 1
print(f"{checks} cooked cloth CLI/desktop checks; inputs unchanged")
