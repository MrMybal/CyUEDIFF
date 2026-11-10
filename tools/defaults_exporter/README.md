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

Only explicitly selected objects and the saved import closure of their already loaded Blueprint class packages are inspected. There is no project-wide discovery, automatic save, upload or Core runtime dependency on this module. The Engine tool may capture ordinary objects for validation, but `DefaultSnapshot` accepts class-default captures only. Its snapshots remain unbound to a target asset or Blueprint/module dependency until a later explicit reconstruction step.

Serialization operates on property copies. Arbitrary constructors/native serializers can execute plugin code and cannot be given a universal execution-time bound. Capture limits restrict output/reflection expansion; they do not make unknown serializers discoverable. The field memory archive is independent of source/cooked package serialization.

See [the complete reader contract](../../docs/DEFAULT_SNAPSHOTS.md) and [the installed Core consumer](../../examples/default_snapshot_consumer/README.md).

## Effective source contexts

Version0.198 adds capture-time SHA256 file identities, bounded recorded environment inputs, merged configuration digests and a commandlet-only `BeginReadOnlyCommandletCapture` helper. Warm the selected CDO/reflected initializers with normal configuration IO first; the helper freezes that owned commandlet's configuration file IO for final capture/shutdown and returns false in an interactive editor. It does not save assets. See [preparation, pinned local inputs and Core/CLI resolution](../../docs/EFFECTIVE_DEFAULTS.md). Captures remain data supplied by the explicitly invoked trusted producer; input consistency is not producer authentication or arbitrary runtime emulation. The private SHA256 header is the original MIT Core implementation; independent validation also uses Python hashlib.

## Blueprint preparation (0.200)

`CaptureBlueprintFactory` captures a fresh transient class-default initialization from an explicit real parent template; `Capture` on the real CDO prepares instance defaults. Neither operation saves an asset or replaces the class CDO. Blueprint captures also record qualified ancestry, actual property GUIDs and capture-time package/import/companion identities. Missing already loaded dependency linkers fail capture. Use the appropriate capture with the Blueprint package or instance in `prepare_default_context.py`. See [the complete Blueprint preparation contract](../../docs/EFFECTIVE_DEFAULTS.md#blueprint-class-defaults-and-instances), including the distinction between initial factory values and final target CDO values. Soft/runtime dependencies and arbitrary subobject rebasing remain outside this profile.

## Package metadata validation

`CapturePackageMetadata(Object)` inspects an explicitly selected, already loaded UMetaData object. It reads the native root/object maps and existing GUID without loading, creating or saving metadata. The output records API state after Engine metadata-key redirects, bounded complete/incomplete capture and actual FName components. It is an independent validation reference for selected stored-map fixtures, not a replacement for the standalone reader. See [the stored metadata contract](../../docs/PACKAGE_METADATA.md).
