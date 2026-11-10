# CyUEDiff default exporter

Optional original MIT editor module, tested with Unreal Engine 5.3.2. It prepares explicit default snapshots for the standalone CyUEDiff Core. Unreal and its plugins are needed only during this preparation step. Their own licenses remain applicable.

Copy this directory into a **separate owned validation project's** `Plugins/CyUEDiffDefaultExporter` directory, enable the plugin and build it with the matching Engine. Load the class/plugin whose defaults you need. From that editor's Python environment, explicitly select a class and output path:

```python
import unreal
from pathlib import Path

cls = unreal.load_class(None, "/Script/YourModule.YourAssetClass")
if cls is None:
    raise RuntimeError("The explicitly selected class is not loaded")
cdo = unreal.get_default_object(cls)
captured = unreal.CyUEDiffDefaultLibrary.capture(cdo, False)
# Set an owned output destination. The exporter itself performs no file writes.
Path("D:/OwnedValidation/class-default.json").write_text(captured, encoding="utf-8")
```

Use `True` for the separately selected editor-only filter, and use matching reader options. The output includes eligibility, declarations, initialized struct fields, reflected diagnostic values, actual per-field serialization, local names/object references and the archive's custom-version inventory. Integers in reflected JSON use decimal strings to preserve their exact values. FText diagnostics do not stand in for a decoded value; the Core reads its actual wire.

Only explicitly selected objects are inspected. There is no asset discovery, automatic save, upload or Core runtime dependency on this module. The Engine tool may capture ordinary objects for validation, but `DefaultSnapshot` accepts class-default captures only. Its snapshots remain unbound to a target asset or Blueprint/module dependency until a later explicit reconstruction step.

Serialization operates on property copies. Arbitrary constructors/native serializers can execute plugin code and cannot be given a universal execution-time bound. Capture limits restrict output/reflection expansion; they do not make unknown serializers discoverable. The field memory archive is independent of source/cooked package serialization.

See [the complete reader contract](../../docs/DEFAULT_SNAPSHOTS.md) and [the installed Core consumer](../../examples/default_snapshot_consumer/README.md).
