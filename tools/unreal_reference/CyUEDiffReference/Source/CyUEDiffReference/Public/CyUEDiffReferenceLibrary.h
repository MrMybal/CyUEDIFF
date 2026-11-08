#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CyUEDiffReferenceLibrary.generated.h"
class USkeleton;
class UStaticMesh;
class UPhysicsAsset;
UCLASS()
class CYUEDIFFREFERENCE_API UCyUEDiffReferenceLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
  public:
    // Original bounded procedural test geometry; fixed private package, never overwrites an asset.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static UStaticMesh *CreatePrivateGrid(int32 Cells);
    // Writes only caller-prepared /Game/Corpus assets; no file is saved here.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static bool BindPrivateSkeleton(UObject *Asset, USkeleton *Skeleton, UPhysicsAsset *Physics);
    // Samples the actual compressed codec, bypassing source-model/raw fallback.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static FString ExportCompressedAnimationReference(UObject *Asset);
    // Bounded original wire fixtures, sampled by the actual per-track codec. No asset writes.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static FString ExportPerTrackSyntheticReference(UObject *CodecOwner);
    // Read-only engine API values, returned as JSON. Caller owns the output destination.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static FString ExportReference(UObject *Asset);
};
