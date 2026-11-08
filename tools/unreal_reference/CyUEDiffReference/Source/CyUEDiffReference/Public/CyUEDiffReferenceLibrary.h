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
    // Read-only original source geometry plus the actual Nanite quantization setting.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static FString ExportNaniteSourceReference(UObject *Asset);
    // Samples the actual compressed codec, bypassing source-model/raw fallback.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static FString ExportCompressedAnimationReference(UObject *Asset);
    // Original curve buffers compressed/evaluated by the actual selected Engine codecs; no saves.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static FString ExportCompressedCurveSyntheticReference(UObject *CodecOwner);
    // Adds original curves and an explicit codec to an unsaved private copy; never saves a file.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static bool PreparePrivateAnimationCurves(UObject *Asset, int32 Codec);
    // Adds original skin-weight profiles only to an unsaved /Game/Corpus/Weight* copy.
    // No file is saved and no existing source asset is changed.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static bool PreparePrivateSkinWeightProfiles(UObject *Asset, int32 Variant);
    // Direct actual curve codec measurements, with complete compressed-buffer identity.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static FString ExportCompressedCurvesReference(UObject *Asset);
    // Bounded original wire fixtures, sampled by the actual per-track codec. No asset writes.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static FString ExportPerTrackSyntheticReference(UObject *CodecOwner);
    // Read-only engine API values, returned as JSON. Caller owns the output destination.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static FString ExportReference(UObject *Asset);
};
