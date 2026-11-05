# Binary block deltas

Standalone byte comparisons and verified reconstruction, independent of Unreal/CyUI/Git.

## Bounded rolling block matching — 0.158

The Core indexes aligned base blocks and scans rolling target windows, preserving copy matches after inserted/deleted bytes. Candidate hashes are lookup hints; every accepted byte is compared against the original base. Operations coalesce adjacent literals and contiguous original-base copy ranges. Input/index/operation/literal/candidate-work limits, cancellation and progress are explicit. Exact byte identity is separate from Unreal property or visual differences. The algorithm is deterministic for the same inputs/options and does not promise a minimal patch or compression.

## Verified reconstruction and portable manifests — 0.159

BinaryDelta serializes as cyuediff.binary-delta/1: endpoint sizes/SHA-256, block size and ordered original-base copy ranges or lowercase hex literals. Strict parsing rejects unknown/duplicate fields, future formats, invalid numbers/hex/ranges, nested or oversized input and aggregate budgets. Reconstruction validates the entire plan and base identity before allocation, then verifies the target SHA-256 before returning any result. File APIs are bounded readers only. Candidate hash collision tests include a mathematically constructed rolling-hash collision and adversarial repeated candidates; copy acceptance remains byte-exact and work-bounded. The manifest contains new literal bytes, can exceed the target size, is not compressed or authenticated, and summary identities are claims until the base/target reconstruction is verified.
