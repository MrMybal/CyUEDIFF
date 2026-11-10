#include "CyUEDiffReferenceLibrary.h"
#include "Dom/JsonObject.h"
#include "Engine/UserDefinedStruct.h"
#include "Misc/EngineVersion.h"
#include "Serialization/CustomVersion.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchiveAdapters.h"
#include "UObject/NoExportTypes.h"
#include "UObject/ObjectVersion.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
namespace {
class FIdentityWriter final : public FMemoryWriter {
    TArray<FString> &Names;

  public:
    FIdentityWriter(TArray<uint8> &Data, TArray<FString> &InNames)
        : FMemoryWriter(Data, true), Names(InNames) {
        SetUEVer(FPackageFileVersion(522, static_cast<EUnrealEngineObjectUE5Version>(1009)));
        SetCustomVersions(FCurrentCustomVersions::GetAll());
    }
    FArchive &operator<<(FName &Name) override {
        const FString Plain = Name.GetPlainNameString();
        int32 Index = Names.IndexOfByKey(Plain);
        if (Index == INDEX_NONE)
            Index = Names.Add(Plain);
        int32 Number = Name.GetNumber();
        Serialize(&Index, sizeof(Index));
        Serialize(&Number, sizeof(Number));
        return *this;
    }
};
TSharedPtr<FJsonObject> StructCase(UScriptStruct *Type, const TCHAR *Label, bool Nonzero) {
    TArray<FString> Names = {
        TEXT("None"),          TEXT("TestAsset"),      TEXT("DataAsset"),    TEXT("/Script/CoreUObject"),
        TEXT("Class"),         TEXT("Value"),          TEXT("IntProperty"),  TEXT("Enabled"),
        TEXT("BoolProperty"),  TEXT("Text"),           TEXT("StrProperty"),  TEXT("Items"),
        TEXT("ArrayProperty"), TEXT("StructProperty"), TEXT("ExampleStruct")};
    TStrongObjectPtr<UScriptStruct> Holder(NewObject<UScriptStruct>(GetTransientPackage()));
    auto *Property = new FStructProperty(Holder.Get(), FName(TEXT("Identity")), RF_NoFlags);
    Property->Struct = Type;
    Holder->AddCppProperty(Property);
    Holder->Bind();
    Holder->PrepareCppStructOps();
    Holder->StaticLink();
    FStructOnScope Container(Holder.Get());
    uint8 *Value = Property->ContainerPtrToValuePtr<uint8>(Container.GetStructMemory());
    if (const auto *User = Cast<UUserDefinedStruct>(Type)) {
        if (!User->GetDefaultInstance())
            return nullptr;
        Type->CopyScriptStruct(Value, User->GetDefaultInstance());
    }
    if (Type == TBaseStructure<FIntPoint>::Get())
        *reinterpret_cast<FIntPoint *>(Value) = Nonzero ? FIntPoint(9, -4) : FIntPoint(0, 0);
    TArray<uint8> Payload, Stream;
    FIdentityWriter Body(Payload, Names);
    const bool Native = Type->UseNativeSerialization(), Binary = Type->UseBinarySerialization(Body);
    {
        FStructuredArchiveFromArchive Structured(Body);
        Property->SerializeItem(Structured.GetSlot(), Value, Value);
    }
    if (Body.IsError() || Payload.Num() > 2 * 1024 * 1024)
        return nullptr;
    FIdentityWriter Full(Stream, Names);
    // Null outer defaults force this field to be written. UUserDefinedStruct supplies its
    // own defaults for the inner delta, producing an actual empty stored-property stream.
    Holder->SerializeTaggedProperties(Full, Container.GetStructMemory(), nullptr, nullptr);
    if (Full.IsError() || Stream.Num() < Payload.Num() + 8)
        return nullptr;
    auto Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("label"), Label);
    int32 FieldCount = 0;
    for (TFieldIterator<FProperty> Field(Type); Field; ++Field)
        ++FieldCount;
    Row->SetNumberField(TEXT("reflectedFieldCount"), FieldCount);
    Row->SetStringField(TEXT("structPath"), Type->GetPathName());
    Row->SetStringField(TEXT("classPath"), Type->GetClass()->GetPathName());
    Row->SetStringField(TEXT("structGuid"), Type->GetCustomGuid().ToString(EGuidFormats::Digits).ToLower());
    Row->SetStringField(TEXT("tagStructGuid"),
                        Type->GetCustomGuid().ToString(EGuidFormats::Digits).ToLower());
    Row->SetStringField(TEXT("serialization"), Native || Binary ? TEXT("native") : TEXT("tagged"));
    Row->SetBoolField(TEXT("useNativeSerialization"), Native);
    Row->SetBoolField(TEXT("useBinarySerialization"), Binary);
    Row->SetNumberField(TEXT("payloadBytes"), Payload.Num());
    Row->SetNumberField(TEXT("headerBytes"), Stream.Num() - Payload.Num() - 8);
    Row->SetStringField(TEXT("payloadHex"), BytesToHex(Payload.GetData(), Payload.Num()).ToLower());
    Row->SetStringField(TEXT("streamHex"), BytesToHex(Stream.GetData(), Stream.Num()).ToLower());
    TArray<TSharedPtr<FJsonValue>> Table;
    for (const FString &Name : Names)
        Table.Add(MakeShared<FJsonValueString>(Name));
    Row->SetArrayField(TEXT("names"), Table);
    return Row;
}
} // namespace
FString UCyUEDiffReferenceLibrary::ExportStructIdentityReference(UObject *Asset) {
    auto *User = Cast<UUserDefinedStruct>(Asset);
    if (!User || User->GetClass() != UUserDefinedStruct::StaticClass() ||
        !User->GetPathName().StartsWith(TEXT("/Game/Corpus/StructIdentity192/")) ||
        User->GetFName() != FName(TEXT("IntPoint")) || !User->GetCustomGuid().IsValid() ||
        User->GetStructureSize() <= 0 || User->GetStructureSize() > 65536)
        return {};
    auto Document = MakeShared<FJsonObject>();
    Document->SetStringField(TEXT("schema"), TEXT("cyuediff.struct-identity-reference/1"));
    Document->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
    Document->SetNumberField(TEXT("ue4Version"), 522);
    Document->SetNumberField(TEXT("ue5Version"), 1009);
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (auto Pair :
         {TPair<UScriptStruct *, const TCHAR *>(User, TEXT("user-defined-empty")),
          TPair<UScriptStruct *, const TCHAR *>(TBaseStructure<FIntPoint>::Get(), TEXT("core-zero")),
          TPair<UScriptStruct *, const TCHAR *>(TBaseStructure<FIntPoint>::Get(), TEXT("core-nonzero"))}) {
        auto Row = StructCase(Pair.Key, Pair.Value, FString(Pair.Value) == TEXT("core-nonzero"));
        if (!Row.IsValid())
            return {};
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    Document->SetArrayField(TEXT("cases"), Rows);
    FString Result;
    return FJsonSerializer::Serialize(Document, TJsonWriterFactory<>::Create(&Result)) ? Result : FString{};
}
