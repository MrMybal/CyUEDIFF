# Binary block deltas

Standalone byte comparisons and verified reconstruction, independent of Unreal/CyUI/Git.

## Bounded rolling block matching — 0.158

The Core indexes aligned base blocks and scans rolling target windows, preserving copy matches after inserted/deleted bytes. Candidate hashes are lookup hints; every accepted byte is compared against the original base. Operations coalesce adjacent literals and contiguous original-base copy ranges. Input/index/operation/literal/candidate-work limits, cancellation and progress are explicit. Exact byte identity is separate from Unreal property or visual differences. The algorithm is deterministic for the same inputs/options and does not promise a minimal patch or compression.
