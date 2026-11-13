# Asset families and testing

CyUEDiff opens loose `.uasset` and `.umap` packages through one reader and inspector. There is no whitelist of asset classes: native, Blueprint-generated and plugin-defined export classes are retained. Format/version support still determines which bytes can be decoded. A successful package open is not a claim of complete native serialization or a specialized preview.

## Try your own project

Run `cyuediff_desktop.exe`, choose **Open project...** (`Ctrl+P`), then double-click a package in the content browser. Project and plugin content are included. **Include engine content** also lists the selected engine and its content plugins. `Ctrl+B` returns to the catalogue. **Open asset...** / `Ctrl+O` opens an individual file. English is the default; **View → Language → Français** selects French.

```powershell
cyuediff_desktop.exe D:/Projects/Game/Game.uproject
cyuediff_desktop.exe --project D:/Projects/Game/Game.uproject --engine-root D:/Engines/UE_5.3
cyuediff_desktop.exe D:/Projects/Game/Content/AnyAsset.uasset
```

The project/engine settings remain available in the sidebar. The engine root is the directory containing `Engine`, not the `Engine` directory itself. Matching engine discovery supplies mounts/context; it does not run Unreal or make arbitrary custom native serialization self-describing. Input packages and their companions are read only.

## What to inspect

| Asset family | General inspection | Specialized review and limits |
| --- | --- | --- |
| Blueprint, editor utility and widget Blueprint | Stored properties, objects, declarations, dependencies and opaque ranges | Supported stored graph nodes/pins; UE 5.3 editor native `EdGraphPinType` fields in variable declarations. No Blueprint execution or widget rendering. |
| Texture2D and texture variants | Stored properties and available source/payload metadata | Supported Texture2D LDR/HDR source previews and explicit cooked profiles. Cube, array, light-profile and volume classes can open without a specialized image preview. |
| Static/skeletal mesh, skeleton and physics assets | Stored properties, references and available native records | Supported geometry wireframes, skeleton/weights/morph/collision inspectors and explicit cooked profiles. Rendering and format limits remain documented separately. |
| Material, instance and function | Stored parameters, references and editor objects | Supported graphs/instance parameter views. No shader evaluation. |
| Data Asset, Data Table, curves, user enum/struct and plugin-defined objects | Generic tagged properties, class identity and opaque data | Supported curves have a curve view. Data table native row bytes and custom native tails require matching supported layouts. Inherited constructor defaults require explicit supported default context. |
| Maps, worlds and external actors | Export objects, readable actor/component properties and dependencies | No level viewport or world simulation. Only mounted content is catalogued; `Saved` backups are not project mounts. |
| Animation, blend space and rig assets | Stored properties and readable native records | Supported source keys, skeletons/curves and selected cooked animation profiles. No animation playback, runtime blending or Control Rig evaluation. |
| Sound Wave, cue, mix, class, submix and effects | Stored properties, references and opaque native payloads | No audio playback or waveform decoder is supplied by this change. |
| Niagara, particle system, geometry cache, fonts and other plugin classes | The same generic export/property inspector, with explicit unavailable data | No Niagara simulation, geometry-cache playback or font preview is supplied by this change. |

Unversioned cooked properties require a matching schema and cooked profile. Encrypted/compressed containers, virtualized sources and custom serialization retain their documented context/codec requirements. Unread values stay opaque with their byte ranges/fingerprints where available; they must not be interpreted as empty values. A failed read limit remains explicit and prevents a misleading semantic comparison.

## Reproducible corpus audit

The audit resolves the project's mounted catalogue once, then inspects its absolute package paths with optional additional loose files. It avoids rediscovering the same engine/plugins for every package. It retains per-package JSON reports, actual stored export classes, property/native coverage, diagnostics, input availability and read-limit failures. It hashes inputs and companion files before/after. Use a new output directory outside the input project/engine.

```powershell
python scripts/validate_asset_types.py --cli build-native/Release/cyuediff.exe `
  --project D:/Projects/Game/Game.uproject --engine-root D:/Engines/UE_5.3 `
  --output-dir build-audit/project-run
# Add --include-engine to audit the entire engine catalogue as well.
# Or use repeated --asset FILE arguments for a separately selected corpus.
```

Exit zero means every selected package opened with available inputs, no failed read limits and unchanged input hashes. It does not assert complete property/native coverage, playable media or a visual preview. Diagnostics and opaque coverage remain in `summary.json` and individual reports. Private asset contents/reports are local validation artifacts, not distributed examples.
