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
    // Read-only engine API values, returned as JSON. Caller owns the output destination.
    UFUNCTION(BlueprintCallable, Category = "CyUEDiff|Validation")
    static FString ExportReference(UObject *Asset);
};
