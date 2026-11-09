#include "CyUEDiffHybridFixture.h"
#include "CyUEDiffReferenceLibrary.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Misc/EngineVersion.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
bool FCyUEDiffReferenceHybridInner::Serialize(FArchive &Ar) {
    StaticStruct()->SerializeTaggedProperties(Ar, reinterpret_cast<uint8 *>(this), StaticStruct(), nullptr);
    Ar << NativeStamp;
    Ar << NativeLabel;
    return true;
}
bool FCyUEDiffReferenceHybridOuter::Serialize(FArchive &Ar) {
    StaticStruct()->SerializeTaggedProperties(Ar, reinterpret_cast<uint8 *>(this), StaticStruct(), nullptr);
    Ar << NativeSamples;
    Ar << NativeName;
    uint32 Enabled = NativeEnabled ? 1 : 0;
    Ar << Enabled;
    if (Ar.IsLoading()) {
        if (Enabled > 1)
            Ar.SetError();
        NativeEnabled = Enabled == 1;
    }
    return true;
}
namespace {
FCyUEDiffReferenceHybridInner Inner(int32 Marker, int32 Variant) {
    FCyUEDiffReferenceHybridInner V;
    V.Marker = Marker;
    V.NativeStamp = 9007199254740993ll + Marker + Variant * 3;
    V.NativeLabel = Variant ? TEXT("original native after") : TEXT("original native before");
    return V;
}
FCyUEDiffReferenceHybridOuter Outer(int32 Id, int32 Variant) {
    FCyUEDiffReferenceHybridOuter V;
    V.Mode = Id + Variant;
    V.Label = Variant ? TEXT("original tagged after") : TEXT("original tagged before");
    V.Nested = Inner(Id * 10, Variant);
    V.Records = {Inner(Id * 10 + 1, 0), Inner(Id * 10 + 2, Variant)};
    V.Labels.Add(TEXT("North"), TEXT("original"));
    V.Labels.Add(TEXT("South"), Variant ? TEXT("changed") : TEXT("stored"));
    V.NativeSamples = {.25f, Variant ? 1.5f : .5f, .75f};
    V.NativeName = Variant ? FName(TEXT("OriginalAfter")) : FName(TEXT("OriginalBefore"));
    V.NativeEnabled = Variant == 0;
    return V;
}
TSharedPtr<FJsonObject> InnerJson(const FCyUEDiffReferenceHybridInner &V) {
    auto J = MakeShared<FJsonObject>();
    J->SetNumberField(TEXT("marker"), V.Marker);
    // JSON's double-number reference representation cannot carry the exact int64 value.
    J->SetStringField(TEXT("stampDecimal"), FString::Printf(TEXT("%lld"), V.NativeStamp));
    J->SetStringField(TEXT("nativeLabel"), V.NativeLabel);
    return J;
}
TSharedPtr<FJsonObject> OuterJson(const FCyUEDiffReferenceHybridOuter &V) {
    auto J = MakeShared<FJsonObject>();
    J->SetNumberField(TEXT("mode"), V.Mode);
    J->SetStringField(TEXT("label"), V.Label);
    J->SetObjectField(TEXT("nested"), InnerJson(V.Nested));
    TArray<TSharedPtr<FJsonValue>> Records, Samples;
    for (const auto &R : V.Records)
        Records.Add(MakeShared<FJsonValueObject>(InnerJson(R)));
    for (float F : V.NativeSamples)
        Samples.Add(MakeShared<FJsonValueNumber>(F));
    J->SetArrayField(TEXT("records"), Records);
    J->SetArrayField(TEXT("samples"), Samples);
    auto Labels = MakeShared<FJsonObject>();
    for (const auto &L : V.Labels)
        Labels->SetStringField(L.Key.ToString(), L.Value);
    J->SetObjectField(TEXT("labels"), Labels);
    J->SetStringField(TEXT("name"), V.NativeName.ToString());
    J->SetBoolField(TEXT("enabled"), V.NativeEnabled);
    return J;
}
} // namespace
UObject *UCyUEDiffReferenceLibrary::CreatePrivateHybridSource(const FString &PackageName, int32 Variant) {
    if (!PackageName.StartsWith(TEXT("/Game/Corpus/SourceHybrid185/")) ||
        !FPackageName::IsValidLongPackageName(PackageName) || (Variant != 0 && Variant != 1) ||
        FPackageName::DoesPackageExist(PackageName) || FindPackage(nullptr, *PackageName))
        return nullptr;
    auto *Package = CreatePackage(*PackageName);
    auto *Asset = NewObject<UCyUEDiffReferenceHybridAsset>(
        Package, *FPackageName::GetLongPackageAssetName(PackageName), RF_Public | RF_Standalone);
    Asset->Primary = Outer(7, Variant);
    Asset->Repeated = {Outer(11, 0), Outer(13, Variant)};
    Asset->AfterHybrid = 987 + Variant;
    Package->MarkPackageDirty();
    return Asset;
}
FString UCyUEDiffReferenceLibrary::ExportTaggedHybridReference(UObject *Object) {
    const auto *Asset = Cast<UCyUEDiffReferenceHybridAsset>(Object);
    if (!Asset)
        return {};
    auto J = MakeShared<FJsonObject>();
    J->SetStringField(TEXT("schema"), TEXT("cyuediff.original-tagged-hybrid-reference/1"));
    J->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
    J->SetStringField(TEXT("object"), Asset->GetPathName());
    J->SetObjectField(TEXT("primary"), OuterJson(Asset->Primary));
    TArray<TSharedPtr<FJsonValue>> Repeated;
    for (const auto &V : Asset->Repeated)
        Repeated.Add(MakeShared<FJsonValueObject>(OuterJson(V)));
    J->SetArrayField(TEXT("repeated"), Repeated);
    J->SetNumberField(TEXT("afterHybrid"), Asset->AfterHybrid);
    FString Result;
    auto Writer = TJsonWriterFactory<>::Create(&Result);
    FJsonSerializer::Serialize(J, Writer);
    return Result;
}
