# Workspace files

The optional desktop host has a data-only `cyuediff.workspace/1` JSON format. It records one to eight documents in order, the active document index, source and project/engine/schema contexts, preview options and initial asset/container view. It does not serialize asset bytes, loaded views, GPU resources, caches, callbacks, closed-document history, appearance preferences or codec modules. A workspace file does not certify that its source files or schemas still match a build.

Local filesystem paths are saved as absolute paths. Relative paths in a file being opened resolve against that file's parent directory. Git asset/project paths remain repository-relative, and mounted names such as `/Game/Example` remain mounted when a project context is present. Git revision expressions are retained as entered, so a later opening can resolve a moving branch differently. No checkout, network fetch or source modification is performed.

The reader limits a file to 1 MiB, eight documents, 32,768 UTF-8 bytes per string and JSON nesting depth 12. Unknown versions/fields, duplicate fields, malformed UTF-8, control characters, invalid types/ranges and ambiguous drive-relative paths are rejected. It reads context data without opening packages, mappings or codecs. Missing files produce an error instead of creating a workspace.

Saving validates the complete document first, reserves a unique file in the destination directory, flushes it and atomically replaces the destination. Existing files must already be valid workspace files; unrelated, malformed and future-version files are preserved. The native host additionally checks output paths against source packages, companions, container partitions, mappings, Git/LFS storage and engine metadata.
