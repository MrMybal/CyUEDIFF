# Raw reference skeletons

The reader retains stored raw bone hierarchy and local reference transforms without launching Unreal. No engine-owned source or asset data is redistributed.

## Bounded native hierarchy and local reference pose - 0.83

Classic Skeleton exports with file versions UE4 522 / UE5 1013 expose a bounded raw reference-skeleton prefix: bone FNames, parent indices, optional editor export names, double-precision local transforms and the stored name/index map. Counts, parent order, duplicate identities, pose/map consistency, finite values, byte/text/work limits and cancellation are checked. Unsupported profiles and failures retain the entire native suffix opaque; successful reads keep all later retarget/SmartName/marker payloads opaque too. Native skeleton decoding can be disabled, with a separate cache profile. Full-package fixtures and every truncated native prefix are covered; the filtered prefix is additionally tested through the bounded archive adapter. No skinning, animation, virtual-bone rebuilding or engine defaults are evaluated.
