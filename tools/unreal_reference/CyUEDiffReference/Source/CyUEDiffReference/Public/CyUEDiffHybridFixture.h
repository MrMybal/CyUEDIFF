#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CyUEDiffHybridFixture.generated.h"
// Original validation types. Their serializers are intentionally explicit; no Engine code is copied.
USTRUCT()
struct CYUEDIFFREFERENCE_API FCyUEDiffReferenceHybridInner {
    GENERATED_BODY()
    UPROPERTY() int32 Marker = 0;
    int64 NativeStamp = 0;
    FString NativeLabel;
    bool Serialize(FArchive &Ar);
};
template <>
struct TStructOpsTypeTraits<FCyUEDiffReferenceHybridInner>
    : TStructOpsTypeTraitsBase2<FCyUEDiffReferenceHybridInner> {
    enum { WithSerializer = true };
};
USTRUCT()
struct CYUEDIFFREFERENCE_API FCyUEDiffReferenceHybridOuter {
    GENERATED_BODY()
    UPROPERTY() int32 Mode = 0;
    UPROPERTY() FString Label;
    UPROPERTY() FCyUEDiffReferenceHybridInner Nested;
    UPROPERTY() TArray<FCyUEDiffReferenceHybridInner> Records;
    UPROPERTY() TMap<FName, FString> Labels;
    TArray<float> NativeSamples;
    FName NativeName;
    bool NativeEnabled = false;
    bool Serialize(FArchive &Ar);
};
template <>
struct TStructOpsTypeTraits<FCyUEDiffReferenceHybridOuter>
    : TStructOpsTypeTraitsBase2<FCyUEDiffReferenceHybridOuter> {
    enum { WithSerializer = true };
};
UCLASS()
class CYUEDIFFREFERENCE_API UCyUEDiffReferenceHybridAsset : public UDataAsset {
    GENERATED_BODY()
  public:
    UPROPERTY() FCyUEDiffReferenceHybridOuter Primary;
    UPROPERTY() TArray<FCyUEDiffReferenceHybridOuter> Repeated;
    UPROPERTY() int32 AfterHybrid = 0;
};
