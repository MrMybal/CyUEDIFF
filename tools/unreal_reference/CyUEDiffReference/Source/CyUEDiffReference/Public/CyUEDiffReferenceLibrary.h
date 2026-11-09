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
    // Creates original cloth mapping data only on caller-prepared /Game/Corpus/Cloth* copies.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static bool PreparePrivateCloth(UObject *Asset, int32 Variant);
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static FString ExportClothReference(UObject *Asset);
    // Original two-LOD physical data on an unsaved /Game/Corpus/ClothPhysical* copy only.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static bool PreparePrivatePhysicalCloth(UObject *Asset, int32 Variant);
    // Read-only actual physical mesh, config and native tether/transition references.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static FString ExportPhysicalClothReference(UObject *Asset);
    // Direct physical geometry/transition fields and actual Chaos 5.3 CPU wrap results; never saves.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static FString ExportPhysicalClothGeometryReference(UObject *Asset);
    // Private source UV variant and read-only native overlap reference; caller owns saving.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static bool PreparePrivateOverlaps(UObject *Asset, int32 Variant);
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static FString ExportOverlapsReference(UObject *Asset);
    // Original scalar vertex channels on private copies; never saves a file.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static bool PreparePrivateVertexAttributes(UObject *Asset, int32 Variant);
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static FString ExportVertexAttributesReference(UObject *Asset);
    // Original CPU-only channels encoded by the actual Engine, including vectors.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static FString ExportVertexAttributesSyntheticReference();
    // Adds original morph deltas only to caller-prepared /Game/Corpus/Morph* copies; never saves.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static bool PreparePrivateMorphs(UObject *Asset, int32 Variant);
    // Serializes a fresh actual native compressed buffer and measures source/API quantization.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static FString ExportCompressedMorphReference(UObject *Asset);
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
