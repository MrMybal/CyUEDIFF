# Optional UHT metadata exporter

This original MIT-licensed exporter is built against the owner's UnrealHeaderTool assemblies. It reads the header parser's typed model, preserving nested struct/enum references that the standard JSON export omits. It exports metadata only. Runtime CyUEDiff Core reads the resulting cached mappings independently.

The supplied driver is validated on Windows with UE 5.3.2. It requires engine source headers, the existing matching project UHT manifest and XML configuration cache, Python 3.11+, and a compatible .NET SDK. Engine/UHT assemblies and generated project descriptions are not redistributed.

From the source checkout:

```powershell
python scripts/export_uht_metadata.py --engine-root C:/UE/UE_5.3 --manifest C:/Project/Intermediate/Build/Win64/ProjectEditor/Development/ProjectEditor.uhtmanifest --project C:/Project/Project.uproject --xml-cache C:/Local/XmlConfigCache.bin --output-dir build-native/uht-metadata
python scripts/build_uht_mappings.py --metadata build-native/uht-metadata/modules --output-dir build-native/uht-mappings
```

Use the manifest/cache from a completed build of the project and its selected engine. The driver copies the manifest/cache, redirects every output, adds an empty exporter module, disables standard code generation and refuses external UHT plugins. It hashes original headers and selected project/engine/tool inputs before and after. Generation reports, logs and source fingerprints remain under the ignored build directory. Failed runs retain diagnostics; no source tree is cleaned or modified.

`build_uht_mappings.py` produces `native.usmap` and `summary.json`. It filters editor-only fields for cooked serialization while retaining transient/deprecated slots, inheritance, literal static-array dimensions and qualified module identities. Unknown nested types retain unmapped ordinals. Unknown static dimensions exclude the type and its descendants. No count, native serializer, default value or reflected struct wire format is inferred.

Loading the mapping uses the existing explicit Zen schema context, with per-class offsets and explicit reflected/native struct bindings. The required mapping fingerprint identifies bytes; it does not certify every asset from a build. See [the full contract](../../docs/UHT_SCHEMAS.md).
