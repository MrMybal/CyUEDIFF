#include "CyUEDiffDefaultLibrary.h"
#include "CyUEDiffDefaultSha256.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Engine/UserDefinedStruct.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Internationalization/Text.h"
#include "Misc/EngineVersion.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "Hash/Blake3.h"
#include "Modules/ModuleManager.h"
#include "Serialization/CustomVersion.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchiveAdapters.h"
#include "UObject/NoExportTypes.h"
#include "UObject/StructOnScope.h"
#include "UObject/ObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/LinkerLoad.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "UObject/LazyObjectPtr.h"
#include <limits>
IMPLEMENT_MODULE(FDefaultModuleImpl, CyUEDiffDefaultExporter)
namespace {
using JObject = TSharedPtr<FJsonObject>;
using JValue = TSharedPtr<FJsonValue>;
JValue ObjectValue(JObject V) { return MakeShared<FJsonValueObject>(MoveTemp(V)); }
JValue StringValue(const FString &V) { return MakeShared<FJsonValueString>(V); }
JValue NumberValue(double V) { return MakeShared<FJsonValueNumber>(V); }
FString Hex64(uint64 V) { return FString::Printf(TEXT("%016llx"), static_cast<unsigned long long>(V)); }
FString HexBytes(const TArray<uint8> &Bytes) {
    static const TCHAR Digits[] = TEXT("0123456789abcdef");
    FString Text;
    Text.Reserve(Bytes.Num() * 2);
    for (uint8 Byte : Bytes) {
        Text.AppendChar(Digits[Byte >> 4]);
        Text.AppendChar(Digits[Byte & 15]);
    }
    return Text;
}
JObject NameValue(FName Name) {
    auto J = MakeShared<FJsonObject>();
    J->SetStringField(TEXT("base"), Name.GetPlainNameString());
    J->SetNumberField(TEXT("number"), Name.GetNumber());
    return J;
}
JValue Reference(UObject *Object) {
    if (!Object)
        return MakeShared<FJsonValueNull>();
    auto J = MakeShared<FJsonObject>();
    J->SetStringField(TEXT("path"), Object->GetPathName());
    J->SetStringField(TEXT("class"), Object->GetClass()->GetPathName());
    J->SetStringField(TEXT("flags"), Hex64(Object->GetFlags()));
    return ObjectValue(J);
}
class FValueWriter final : public FMemoryWriter {
    TArray<FString> Names;
    TMap<FString, int32> NameIndices;
    TArray<JValue> Objects;
    TMap<UObject *, int32> ObjectIndices;
    int64 CapturedText = 0;

  public:
    explicit FValueWriter(TArray<uint8> &Bytes, bool Filter) : FMemoryWriter(Bytes, true) {
        SetFilterEditorOnly(Filter);
        SetUseUnversionedPropertySerialization(true);
        SetForceUnicode(true);
        SetCustomVersions(FCurrentCustomVersions::GetAll());
    }
    void Serialize(void *Data, int64 Size) override {
        if (Size < 0 || Size > 4 * 1024 * 1024 - Tell()) {
            SetError();
            return;
        }
        FMemoryWriter::Serialize(Data, Size);
    }
    FArchive &operator<<(FName &Name) override {
        const FString Base = Name.GetPlainNameString();
        const auto *Found = NameIndices.Find(Base);
        int32 Index = Found ? *Found : INDEX_NONE;
        if (Index == INDEX_NONE) {
            const int64 TextSize = static_cast<int64>(Base.Len()) * 3;
            if (Names.Num() >= 100000 || Base.Len() > 65536 || TextSize > 2 * 1024 * 1024 - CapturedText) {
                SetError();
                return *this;
            }
            CapturedText += TextSize;
            Index = Names.Add(Base);
            NameIndices.Add(Base, Index);
        }
        int32 Number = Name.GetNumber();
        Serialize(&Index, 4);
        Serialize(&Number, 4);
        return *this;
    }
    FArchive &operator<<(UObject *&Object) override {
        int32 Index = 0;
        if (Object) {
            if (auto *Existing = ObjectIndices.Find(Object))
                Index = *Existing;
            else {
                const FString Path = Object->GetPathName(), Class = Object->GetClass()->GetPathName();
                const int64 TextSize = static_cast<int64>(Path.Len() + Class.Len()) * 3;
                if (Objects.Num() >= 100000 || TextSize > 2 * 1024 * 1024 - CapturedText) {
                    SetError();
                    return *this;
                }
                CapturedText += TextSize;
                Index = Objects.Add(Reference(Object)) + 1;
                ObjectIndices.Add(Object, Index);
            }
        }
        Serialize(&Index, 4);
        return *this;
    }
    FArchive &operator<<(FObjectPtr &Object) override {
        UObject *Raw = Object.Get();
        return *this << Raw;
    }
    int64 MetadataWork() {
        return 1 + static_cast<int64>(Names.Num()) + static_cast<int64>(Objects.Num()) * 4 +
               static_cast<int64>(GetCustomVersions().GetAllVersions().Num()) * 3;
    }
    int64 MetadataText() {
        return CapturedText + static_cast<int64>(Objects.Num()) * 16 +
               static_cast<int64>(GetCustomVersions().GetAllVersions().Num()) * 48;
    }
    JObject Description(const TArray<uint8> &Captured) {
        if (GetCustomVersions().GetAllVersions().Num() > 10000)
            SetError();
        auto J = MakeShared<FJsonObject>();
        J->SetBoolField(TEXT("available"), !IsError());
        if (IsError()) {
            J->SetStringField(TEXT("reason"), TEXT("wire-capture-limit-or-serializer-error"));
            return J;
        }
        J->SetStringField(TEXT("format"), TEXT("cyuediff.engine-field-wire/1"));
        J->SetBoolField(TEXT("unversioned"), true);
        J->SetBoolField(TEXT("filterEditorOnly"), IsFilterEditorOnly());
        J->SetBoolField(TEXT("isCooking"), IsCooking());
        J->SetBoolField(TEXT("forceUnicode"), IsForcingUnicode());
        J->SetStringField(TEXT("comparisonDefaults"), TEXT("none"));
        J->SetNumberField(TEXT("ue4"), UEVer().FileVersionUE4);
        J->SetNumberField(TEXT("ue5"), UEVer().FileVersionUE5);
        TArray<JValue> CustomVersionsJson;
        for (const FCustomVersion &Version : GetCustomVersions().GetAllVersions()) {
            auto Custom = MakeShared<FJsonObject>();
            Custom->SetStringField(TEXT("guid"), Version.Key.ToString(EGuidFormats::Digits).ToLower());
            Custom->SetNumberField(TEXT("version"), Version.Version);
            CustomVersionsJson.Add(ObjectValue(Custom));
        }
        J->SetArrayField(TEXT("customVersions"), MoveTemp(CustomVersionsJson));
        J->SetStringField(TEXT("hex"), HexBytes(Captured));
        TArray<JValue> NamesJson;
        for (const auto &Name : Names)
            NamesJson.Add(StringValue(Name));
        J->SetArrayField(TEXT("names"), MoveTemp(NamesJson));
        J->SetArrayField(TEXT("objects"), Objects);
        return J;
    }
};
struct FCapture {
    bool Filter = false, Failed = false;
    int64 Work = 0, Text = 0;
    JObject Structures = MakeShared<FJsonObject>();
    JObject Declarations = MakeShared<FJsonObject>(), Enumerations = MakeShared<FJsonObject>();
    bool Charge(int32 Depth = 0, int64 Bytes = 0, int64 Units = 1) {
        if (Failed)
            return false;
        if (Units < 0 || Units > 1000000 - Work || Depth > 48 || Bytes < 0 ||
            Bytes > 32 * 1024 * 1024 - Text) {
            Failed = true;
            return false;
        }
        Work += Units;
        Text += Bytes;
        return true;
    }
    JObject Environment(UObject *Root) {
        auto Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("format"), TEXT("cyuediff.engine-default-environment/1"));
        Result->SetStringField(TEXT("platform"), PLATFORM_WINDOWS ? TEXT("Windows") : TEXT("Other"));
        Result->SetStringField(TEXT("byteOrder"), PLATFORM_LITTLE_ENDIAN ? TEXT("little") : TEXT("big"));
        Result->SetBoolField(TEXT("configurationFileIOFrozen"),
                             GConfig && GConfig->AreFileOperationsDisabled());
        auto Files = MakeShared<FJsonObject>();
        uint64 FileBytes = 0;
        TArray<uint8> Buffer;
        Buffer.SetNumUninitialized(64 * 1024);
        auto Add = [&](const FString &Id, const FString &Role, const FString &Path) {
            if (!Charge(0, static_cast<int64>(Id.Len() + Role.Len() + Path.Len()) * 3))
                return;
            auto File = MakeShared<FJsonObject>();
            File->SetStringField(TEXT("role"), Role);
            const FString FullPath = FPaths::ConvertRelativePathToFull(Path);
            File->SetStringField(TEXT("file"), FullPath);
            const bool Present = FPaths::FileExists(FullPath);
            File->SetBoolField(TEXT("present"), Present);
            if (Present) {
                TUniquePtr<FArchive> Input(IFileManager::Get().CreateFileReader(*FullPath, FILEREAD_Silent));
                if (!Input || Input->TotalSize() < 0 ||
                    static_cast<uint64>(Input->TotalSize()) > 8ull * 1024 * 1024 * 1024 - FileBytes) {
                    Failed = true;
                    return;
                }
                const uint64 Size = static_cast<uint64>(Input->TotalSize());
                FileBytes += Size;
                cyuediff::detail::Sha256 Hash;
                for (uint64 Read = 0; Read < Size;) {
                    if (!Charge())
                        return;
                    const int64 Count = static_cast<int64>(FMath::Min<uint64>(Buffer.Num(), Size - Read));
                    Input->Serialize(Buffer.GetData(), Count);
                    if (Input->IsError()) {
                        Failed = true;
                        return;
                    }
                    Hash.update(Buffer.GetData(), static_cast<size_t>(Count));
                    Read += static_cast<uint64>(Count);
                }
                if (IFileManager::Get().FileSize(*FullPath) != static_cast<int64>(Size)) {
                    Failed = true;
                    return;
                }
                File->SetStringField(TEXT("bytes"),
                                     FString::Printf(TEXT("%llu"), static_cast<unsigned long long>(Size)));
                File->SetStringField(TEXT("sha256"), UTF8_TO_TCHAR(Hash.finish().c_str()));
            }
            Files->SetObjectField(Id, File);
        };
        TArray<FModuleStatus> Modules;
        FModuleManager::Get().QueryModules(Modules);
        for (const auto &Module : Modules)
            if (Module.bIsLoaded)
                Add(TEXT("module:") + Module.Name, TEXT("module"), Module.FilePath);
        TArray<FString> Configs;
        if (GConfig)
            GConfig->GetConfigFilenames(Configs);
        Configs.Sort();
        auto ConfigDigests = MakeShared<FJsonObject>();
        for (int32 I = 0; I < Configs.Num(); ++I) {
            const FString Id = FString::Printf(TEXT("config:%d"), I);
            Add(Id, TEXT("config"), Configs[I]);
            if (const FConfigFile *File = GConfig->FindConfigFile(Configs[I])) {
                for (const auto &Layer : File->SourceIniHierarchy)
                    Add(Id + FString::Printf(TEXT(":layer:%d"), Layer.Key), TEXT("config"), Layer.Value);
                FBlake3 Hash;
                auto HashText = [&](const FString &Text) {
                    const uint64 Size = static_cast<uint64>(Text.Len()) * sizeof(TCHAR);
                    if (!Charge(0, static_cast<int64>(Size)))
                        return;
                    Hash.Update(&Size, sizeof(Size));
                    Hash.Update(*Text, Size);
                };
                // Read the live sections directly: FConfigFile owns pointers and must not be shallow-copied.
                // Length-framed section/key/value text never leaves this process.
                const uint64 SectionCount = File->Num();
                Hash.Update(&SectionCount, sizeof(SectionCount));
                for (const auto &Section : *File) {
                    HashText(Section.Key);
                    const uint64 ValueCount = Section.Value.Num();
                    Hash.Update(&ValueCount, sizeof(ValueCount));
                    for (const auto &Pair : Section.Value) {
                        HashText(Pair.Key.ToString());
                        HashText(Pair.Value.GetValue());
                    }
                }
                ConfigDigests->SetStringField(Id, LexToString(Hash.Finalize()).ToLower());
            }
        }
        Result->SetObjectField(TEXT("mergedConfigBlake3"), ConfigDigests);
        Add(TEXT("executable"), TEXT("executable"), FPlatformProcess::ExecutablePath());
        Add(TEXT("engine-build"), TEXT("engine-build"), FPaths::EngineDir() / TEXT("Build/Build.version"));
        Add(TEXT("project"), TEXT("project"), FPaths::GetProjectFilePath());
        if (Root->GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint)) {
            auto Binding = MakeShared<FJsonObject>();
            Binding->SetStringField(TEXT("format"), TEXT("cyuediff.blueprint-default-dependencies/1"));
            TArray<JValue> Classes;
            TArray<FString> Pending;
            for (UClass *Class = Root->GetClass(); Class && !Failed; Class = Class->GetSuperClass()) {
                if (!Charge())
                    break;
                auto Row = MakeShared<FJsonObject>();
                Row->SetStringField(TEXT("class"), Str(Class->GetPathName()));
                Row->SetStringField(TEXT("super"), Class->GetSuperClass()
                                                       ? Str(Class->GetSuperClass()->GetPathName())
                                                       : TEXT(""));
                Row->SetField(TEXT("generatedBy"), ObjectReference(Class->ClassGeneratedBy));
                Row->SetField(TEXT("defaultObject"), ObjectReference(Class->GetDefaultObject(false)));
                const bool Blueprint = Class->HasAnyClassFlags(CLASS_CompiledFromBlueprint);
                Row->SetBoolField(TEXT("blueprint"), Blueprint);
                if (Blueprint) {
                    if (!Class->ClassGeneratedBy ||
                        Class->ClassGeneratedBy->GetOutermost() != Class->GetOutermost()) {
                        Failed = true;
                        break;
                    }
                    Pending.AddUnique(Class->GetOutermost()->GetName());
                }
                Classes.Add(ObjectValue(Row));
            }
            auto Packages = MakeShared<FJsonObject>();
            for (int32 I = 0; I < Pending.Num() && !Failed; ++I) {
                if (Pending.Num() > 256 || !Charge()) {
                    Failed = true;
                    break;
                }
                const FString PackageName = Pending[I];
                UPackage *Package = FindPackage(nullptr, *PackageName);
                FLinkerLoad *Linker = Package ? Package->GetLinker() : nullptr;
                FString File;
                if (!Linker || !FPackageName::DoesPackageExist(PackageName, &File)) {
                    Failed = true;
                    break;
                }
                auto Row = MakeShared<FJsonObject>();
                const FString Id = TEXT("package:") + PackageName;
                Add(Id, TEXT("asset-package"), File);
                Row->SetStringField(TEXT("fileId"), Id);
                TArray<JValue> Companions;
                for (const TCHAR *Extension : {TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl"), TEXT("m.ubulk"),
                                               TEXT("o.ubulk"), TEXT("upayload")}) {
                    const FString CompanionId = Id + TEXT(":") + Extension;
                    Add(CompanionId, TEXT("asset-companion"), FPaths::ChangeExtension(File, Extension));
                    Companions.Add(StringValue(CompanionId));
                }
                Row->SetArrayField(TEXT("companionIds"), MoveTemp(Companions));
                TArray<FString> Imports;
                for (const FObjectImport &Import : Linker->ImportMap) {
                    if (!Charge())
                        break;
                    if (Import.OuterIndex.IsNull() && Import.ClassName == TEXT("Package")) {
                        const FString Name = Import.ObjectName.ToString();
                        Imports.AddUnique(Name);
                        if (!Name.StartsWith(TEXT("/Script/")))
                            Pending.AddUnique(Name);
                    }
                }
                Imports.Sort();
                TArray<JValue> ImportValues;
                for (const FString &Import : Imports)
                    ImportValues.Add(StringValue(Str(Import)));
                Row->SetArrayField(TEXT("imports"), MoveTemp(ImportValues));
                Packages->SetObjectField(Str(PackageName), Row);
            }
            Binding->SetArrayField(TEXT("classes"), MoveTemp(Classes));
            Binding->SetObjectField(TEXT("packages"), Packages);
            Binding->SetBoolField(TEXT("complete"), !Failed);
            Result->SetObjectField(TEXT("blueprintDependencies"), Binding);
        }
        Result->SetObjectField(TEXT("files"), Files);
        return Result;
    }
    FString Str(const FString &S) {
        Charge(0, static_cast<int64>(S.Len()) * 3);
        return S;
    }
    JValue ObjectReference(UObject *Object) {
        if (Object)
            Charge(0,
                   static_cast<int64>(Object->GetPathName().Len() + Object->GetClass()->GetPathName().Len()) *
                           3 +
                       16,
                   4);
        return Reference(Object);
    }
    JObject Type(FProperty *P, int32 Depth) {
        auto J = MakeShared<FJsonObject>();
        if (!Charge(Depth))
            return J;
        J->SetStringField(TEXT("name"), Str(P->GetClass()->GetName()));
        TArray<JValue> Args;
        if (auto *StructProperty = CastField<FStructProperty>(P)) {
            J->SetStringField(TEXT("schema"), Str(StructProperty->Struct->GetPathName()));
            J->SetStringField(
                TEXT("structGuid"),
                StructProperty->Struct->GetCustomGuid().ToString(EGuidFormats::Digits).ToLower());
            J->SetBoolField(TEXT("nativeSerialization"), StructProperty->Struct->UseNativeSerialization());
            Declare(StructProperty->Struct, Depth + 1);
            const auto *Ops = StructProperty->Struct->GetCppStructOps();
            J->SetBoolField(TEXT("nativeIdentical"), Ops && Ops->HasIdentical());
        } else if (auto *ArrayProperty = CastField<FArrayProperty>(P))
            Args.Add(ObjectValue(Type(ArrayProperty->Inner, Depth + 1)));
        else if (auto *MapProperty = CastField<FMapProperty>(P)) {
            Args.Add(ObjectValue(Type(MapProperty->KeyProp, Depth + 1)));
            Args.Add(ObjectValue(Type(MapProperty->ValueProp, Depth + 1)));
        } else if (auto *SetProperty = CastField<FSetProperty>(P))
            Args.Add(ObjectValue(Type(SetProperty->ElementProp, Depth + 1)));
        else if (auto *EnumProperty = CastField<FEnumProperty>(P)) {
            J->SetStringField(TEXT("schema"), Str(EnumProperty->GetEnum()->GetPathName()));
            DeclareEnum(EnumProperty->GetEnum());
            Args.Add(ObjectValue(Type(EnumProperty->GetUnderlyingProperty(), Depth + 1)));
        } else if (auto *ByteProperty = CastField<FByteProperty>(P)) {
            if (ByteProperty->Enum) {
                J->SetStringField(TEXT("enum"), Str(ByteProperty->Enum->GetPathName()));
                DeclareEnum(ByteProperty->Enum);
            }
        } else if (auto *ObjectPropertyBase = CastField<FObjectPropertyBase>(P))
            J->SetStringField(TEXT("class"), Str(ObjectPropertyBase->PropertyClass->GetPathName()));
        if (Args.Num())
            J->SetArrayField(TEXT("arguments"), MoveTemp(Args));
        return J;
    }
    JObject Node(FProperty *P, const void *Value, int32 Depth, bool WithWire = false) {
        auto J = MakeShared<FJsonObject>();
        if (!Charge(Depth))
            return J;
        J->SetObjectField(TEXT("type"), Type(P, Depth));
        if (const auto *Owner = Cast<UBlueprintGeneratedClass>(P->GetOwnerStruct()))
            J->SetStringField(TEXT("propertyGuid"), Owner->FindBlueprintPropertyGuidFromName(P->GetFName())
                                                        .ToString(EGuidFormats::Digits)
                                                        .ToLower());
        J->SetBoolField(TEXT("available"), true);
        if (auto *BoolProperty = CastField<FBoolProperty>(P))
            J->SetBoolField(TEXT("value"), BoolProperty->GetPropertyValue(Value));
        else if (auto *EnumProperty = CastField<FEnumProperty>(P))
            J->SetObjectField(TEXT("value"), Node(EnumProperty->GetUnderlyingProperty(), Value, Depth + 1));
        else if (auto *NumericProperty = CastField<FNumericProperty>(P)) {
            if (NumericProperty->IsFloatingPoint()) {
                const double V = NumericProperty->GetFloatingPointPropertyValue(Value);
                if (FMath::IsFinite(V))
                    J->SetNumberField(TEXT("value"), V);
                else {
                    J->SetBoolField(TEXT("available"), false);
                    J->SetStringField(TEXT("reason"), TEXT("nonfinite-number"));
                }
            } else {
                const bool Unsigned = CastField<FByteProperty>(P) || CastField<FUInt16Property>(P) ||
                                      CastField<FUInt32Property>(P) || CastField<FUInt64Property>(P);
                J->SetStringField(
                    TEXT("value"),
                    Unsigned ? FString::Printf(TEXT("%llu"),
                                               static_cast<unsigned long long>(
                                                   NumericProperty->GetUnsignedIntPropertyValue(Value)))
                             : FString::Printf(TEXT("%lld"),
                                               static_cast<long long>(
                                                   NumericProperty->GetSignedIntPropertyValue(Value))));
            }
        } else if (auto *StrProperty = CastField<FStrProperty>(P))
            J->SetStringField(TEXT("value"), Str(StrProperty->GetPropertyValue(Value)));
        else if (auto *NameProperty = CastField<FNameProperty>(P)) {
            const FName CapturedName = NameProperty->GetPropertyValue(Value);
            Charge(Depth, static_cast<int64>(CapturedName.GetPlainNameString().Len()) * 3);
            J->SetObjectField(TEXT("value"), NameValue(CapturedName));
        } else if (auto *SoftObjectProperty = CastField<FSoftObjectProperty>(P))
            J->SetStringField(TEXT("value"),
                              Str(SoftObjectProperty->GetPropertyValue(Value).ToSoftObjectPath().ToString()));
        else if (auto *ObjectPropertyBase = CastField<FObjectPropertyBase>(P))
            J->SetField(TEXT("value"), ObjectReference(ObjectPropertyBase->GetObjectPropertyValue(Value)));
        else if (auto *ArrayProperty = CastField<FArrayProperty>(P)) {
            FScriptArrayHelper H(ArrayProperty, Value);
            TArray<JValue> Values;
            if (H.Num() > 1000000 - Work)
                Failed = true;
            for (int32 I = 0; I < H.Num() && !Failed; ++I)
                Values.Add(ObjectValue(Node(ArrayProperty->Inner, H.GetRawPtr(I), Depth + 1)));
            J->SetArrayField(TEXT("value"), MoveTemp(Values));
        } else if (auto *MapProperty = CastField<FMapProperty>(P)) {
            FScriptMapHelper H(MapProperty, Value);
            TArray<JValue> Values;
            if (H.GetMaxIndex() > 1000000 - Work)
                Failed = true;
            for (int32 I = 0; I < H.GetMaxIndex() && !Failed; ++I)
                if (H.IsValidIndex(I)) {
                    auto Pair = MakeShared<FJsonObject>();
                    Pair->SetObjectField(TEXT("key"), Node(MapProperty->KeyProp, H.GetKeyPtr(I), Depth + 1));
                    Pair->SetObjectField(TEXT("value"),
                                         Node(MapProperty->ValueProp, H.GetValuePtr(I), Depth + 1));
                    Values.Add(ObjectValue(Pair));
                }
            J->SetArrayField(TEXT("value"), MoveTemp(Values));
        } else if (auto *SetProperty = CastField<FSetProperty>(P)) {
            FScriptSetHelper H(SetProperty, Value);
            TArray<JValue> Values;
            if (H.GetMaxIndex() > 1000000 - Work)
                Failed = true;
            for (int32 I = 0; I < H.GetMaxIndex() && !Failed; ++I)
                if (H.IsValidIndex(I))
                    Values.Add(ObjectValue(Node(SetProperty->ElementProp, H.GetElementPtr(I), Depth + 1)));
            J->SetArrayField(TEXT("value"), MoveTemp(Values));
        } else if (auto *StructProperty = CastField<FStructProperty>(P)) {
            EnsureStructure(StructProperty->Struct, Depth + 1);
            J->SetObjectField(TEXT("value"), Fields(StructProperty->Struct, Value, Depth + 1));
            // These are complete reflected semantic fields, not a native wire-layout claim.
            J->SetStringField(TEXT("representation"), TEXT("reflected-fields"));
        } else {
            J->SetBoolField(TEXT("available"), false);
            J->SetStringField(TEXT("reason"), TEXT("reflection-value-unsupported"));
            if (CastField<FTextProperty>(P)) {
                FString TextValue;
                FTextStringHelper::WriteToBuffer(TextValue, *static_cast<const FText *>(Value));
                J->SetStringField(TEXT("diagnosticText"), Str(TextValue));
            }
        }
        if (WithWire && !Failed) {
            TArray<uint8> Bytes;
            FValueWriter Writer(Bytes, Filter);
            {
                void *Copy = FMemory::Malloc(P->GetSize(), P->GetMinAlignment());
                P->InitializeValue(Copy);
                P->CopySingleValue(Copy, Value);
                {
                    FStructuredArchiveFromArchive Archive(Writer);
                    P->SerializeItem(Archive.GetSlot(), Copy, nullptr);
                }
                P->DestroyValue(Copy);
                FMemory::Free(Copy);
            }
            Charge(Depth, static_cast<int64>(Bytes.Num()) * 2 + Writer.MetadataText(), Writer.MetadataWork());
            J->SetObjectField(TEXT("wire"), Writer.Description(Bytes));
        }
        return J;
    }
    JObject Fields(UStruct *Type, const void *Container, int32 Depth) {
        auto Fields = MakeShared<FJsonObject>();
        TArray<uint8> Empty;
        FValueWriter Policy(Empty, Filter);
        for (TFieldIterator<FProperty> It(Type); It && !Failed; ++It) {
            FProperty *P = *It;
            if (!Charge(Depth))
                break;
            if (!P->ShouldSerializeValue(Policy))
                continue;
            for (int32 I = 0; I < P->ArrayDim && !Failed; ++I) {
                auto J = Node(P, P->ContainerPtrToValuePtr<void>(Container, I), Depth + 1, true);
                J->SetStringField(TEXT("flags"), Hex64(P->GetPropertyFlags()));
                J->SetStringField(TEXT("owner"), Str(P->GetOwnerStruct()->GetPathName()));
                J->SetStringField(TEXT("name"), Str(P->GetName()));
                J->SetNumberField(TEXT("arrayIndex"), I);
                J->SetNumberField(TEXT("arrayDim"), P->ArrayDim);
                FString Key = P->GetOwnerStruct()->GetPathName() + TEXT(".") + P->GetName();
                if (P->ArrayDim > 1)
                    Key += FString::Printf(TEXT("[%d]"), I);
                if (Fields->HasField(Key)) {
                    Failed = true;
                    break;
                }
                Fields->SetObjectField(Str(Key), J);
            }
        }
        return Fields;
    }
    void DeclareEnum(UEnum *Enum) {
        const FString Path = Enum->GetPathName();
        if (Enumerations->HasField(Path) || !Charge())
            return;
        TArray<JValue> Values;
        for (int32 I = 0; I < Enum->NumEnums() && !Failed; ++I) {
            if (!Charge())
                break;
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("name"), Str(Enum->GetNameStringByIndex(I)));
            Row->SetStringField(TEXT("value"), FString::Printf(TEXT("%lld"), static_cast<long long>(
                                                                                 Enum->GetValueByIndex(I))));
            Values.Add(ObjectValue(Row));
        }
        Enumerations->SetArrayField(Str(Path), MoveTemp(Values));
    }
    void Declare(UStruct *Type, int32 Depth) {
        const FString Path = Type->GetPathName();
        if (Declarations->HasField(Path) || !Charge(Depth))
            return;
        auto J = MakeShared<FJsonObject>();
        Declarations->SetObjectField(Str(Path), J);
        J->SetStringField(TEXT("path"), Path);
        J->SetStringField(TEXT("super"),
                          Type->GetSuperStruct() ? Type->GetSuperStruct()->GetPathName() : TEXT(""));
        if (auto *Script = Cast<UScriptStruct>(Type)) {
            TArray<uint8> Unused;
            FValueWriter Policy(Unused, Filter);
            J->SetBoolField(TEXT("nativeSerialization"), Script->UseNativeSerialization());
            J->SetBoolField(TEXT("binarySerialization"), Script->UseBinarySerialization(Policy));
            J->SetStringField(TEXT("kind"),
                              Script->UseNativeSerialization() || Script->UseBinarySerialization(Policy)
                                  ? TEXT("native-struct")
                                  : TEXT("struct"));
            J->SetStringField(TEXT("structGuid"),
                              Script->GetCustomGuid().ToString(EGuidFormats::Digits).ToLower());
        } else
            J->SetStringField(TEXT("kind"), TEXT("class"));
        TArray<JValue> Properties;
        int32 Index = 0;
        for (TFieldIterator<FProperty> It(Type, EFieldIteratorFlags::ExcludeSuper); It && !Failed; ++It) {
            FProperty *P = *It;
            if (!Charge(Depth))
                break;
            if (Filter && P->HasAnyPropertyFlags(CPF_EditorOnly))
                continue;
            auto Field = MakeShared<FJsonObject>();
            Field->SetStringField(TEXT("name"), Str(P->GetName()));
            Field->SetNumberField(TEXT("index"), Index);
            Field->SetNumberField(TEXT("arrayDim"), P->ArrayDim);
            TArray<uint8> PolicyBytes;
            FValueWriter FieldPolicy(PolicyBytes, Filter);
            Field->SetBoolField(TEXT("serialize"), P->ShouldSerializeValue(FieldPolicy));
            Field->SetStringField(TEXT("flags"), Hex64(P->GetPropertyFlags()));
            Field->SetObjectField(TEXT("type"), this->Type(P, Depth + 1));
            if (const auto *Owner = Cast<UBlueprintGeneratedClass>(Type))
                Field->SetStringField(TEXT("propertyGuid"),
                                      Owner->FindBlueprintPropertyGuidFromName(P->GetFName())
                                          .ToString(EGuidFormats::Digits)
                                          .ToLower());
            Properties.Add(ObjectValue(Field));
            Index += P->ArrayDim;
        }
        J->SetNumberField(TEXT("slots"), Index);
        J->SetArrayField(TEXT("properties"), MoveTemp(Properties));
        if (Type->GetSuperStruct())
            Declare(Type->GetSuperStruct(), Depth + 1);
    }
    void EnsureStructure(UScriptStruct *Type, int32 Depth) {
        const FString Path = Type->GetPathName();
        if (Structures->HasField(Path) || !Charge(Depth))
            return;
        auto J = MakeShared<FJsonObject>();
        Structures->SetObjectField(Str(Path), J);
        J->SetStringField(TEXT("schema"), Path);
        J->SetBoolField(TEXT("postSerialize"), (Type->StructFlags & STRUCT_PostSerializeNative) != 0);
        J->SetStringField(TEXT("structGuid"), Type->GetCustomGuid().ToString(EGuidFormats::Digits).ToLower());
        FStructOnScope Initialized(Type);
        if (const auto *User = Cast<UUserDefinedStruct>(Type)) {
            if (!User->GetDefaultInstance()) {
                J->SetBoolField(TEXT("available"), false);
                J->SetStringField(TEXT("reason"), TEXT("user-defined-struct-default-instance-missing"));
                return;
            }
            Type->CopyScriptStruct(Initialized.GetStructMemory(), User->GetDefaultInstance());
        }
        J->SetObjectField(TEXT("fields"), Fields(Type, Initialized.GetStructMemory(), Depth + 1));
        J->SetBoolField(TEXT("available"), !Failed);
    }
};
FString Write(JObject J) {
    FString Text;
    FJsonSerializer::Serialize(J.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
    return Text;
}
} // namespace
FString UCyUEDiffDefaultLibrary::Capture(UObject *Object, bool FilterEditorOnly) {
    if (!Object)
        return TEXT("{}");
    FCapture Reader;
    Reader.Filter = FilterEditorOnly;
    auto J = MakeShared<FJsonObject>();
    J->SetStringField(TEXT("format"), TEXT("cyuediff.engine-default-capture/1"));
    J->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
    J->SetBoolField(TEXT("filterEditorOnly"), FilterEditorOnly);
    J->SetField(TEXT("object"), Reader.ObjectReference(Object));
    TArray<uint8> Empty;
    FValueWriter Policy(Empty, FilterEditorOnly);
    Reader.Charge(0, Policy.MetadataText(), Policy.MetadataWork());
    J->SetObjectField(TEXT("archive"), Policy.Description(Empty));
    J->SetField(TEXT("archetype"), Reader.ObjectReference(Object->GetArchetype()));
    J->SetBoolField(TEXT("classDefault"), Object->HasAnyFlags(RF_ClassDefaultObject));
    J->SetBoolField(TEXT("blueprintClass"),
                    Object->GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint));
    J->SetField(TEXT("classGeneratedBy"), Reader.ObjectReference(Object->GetClass()->ClassGeneratedBy));
    Reader.Declare(Object->GetClass(), 0);
    J->SetObjectField(TEXT("fields"), Reader.Fields(Object->GetClass(), Object, 0));
    J->SetObjectField(TEXT("structures"), Reader.Structures);
    J->SetObjectField(TEXT("declarations"), Reader.Declarations);
    J->SetObjectField(TEXT("enumerations"), Reader.Enumerations);
    J->SetObjectField(TEXT("environment"), Reader.Environment(Object));
    J->SetBoolField(TEXT("completeCapture"), !Reader.Failed);
    J->SetNumberField(TEXT("work"), Reader.Work);
    J->SetNumberField(TEXT("textBudgetUsed"), Reader.Text);
    if (Reader.Failed) {
        J->SetStringField(TEXT("error"), TEXT("capture-limit"));
        J->RemoveField(TEXT("fields"));
        J->RemoveField(TEXT("structures"));
        J->RemoveField(TEXT("declarations"));
        J->RemoveField(TEXT("enumerations"));
    }
    return Write(J);
}
FString UCyUEDiffDefaultLibrary::CaptureBlueprintFactory(UClass *Class, bool FilterEditorOnly) {
    if (!Class || !Class->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !Class->GetSuperClass() ||
        !Class->GetDefaultObject(false) || !Class->GetSuperClass()->GetDefaultObject(false))
        return TEXT("{}");
    UObject *OriginalDefault = Class->GetDefaultObject(false);
    UObject *ParentDefault = Class->GetSuperClass()->GetDefaultObject(false);
    FStaticConstructObjectParameters Params(Class);
    // Engine canonicalizes every CDO name; isolate the seed in its own transient package.
    UPackage *SeedPackage =
        CreatePackage(*(TEXT("/Temp/CyUEDiffFactory_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    SeedPackage->SetFlags(RF_Transient);
    Params.Outer = SeedPackage;
    Params.Name = MakeUniqueObjectName(Params.Outer, Class, TEXT("CyUEDiffFactorySeed"));
    Params.SetFlags = RF_ClassDefaultObject | RF_ArchetypeObject | RF_Transient;
    Params.Template = ParentDefault;
    Params.bAssumeTemplateIsArchetype = true;
    UObject *Seed = StaticConstructObject_Internal(Params);
    if (!Seed || Class->GetDefaultObject(false) != OriginalDefault || Seed->GetArchetype() != ParentDefault)
        return TEXT("{}");
    TSharedPtr<FJsonObject> J;
    const FString Captured = Capture(Seed, FilterEditorOnly);
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Captured), J) || !J)
        return TEXT("{}");
    auto Factory = MakeShared<FJsonObject>();
    Factory->SetStringField(TEXT("format"), TEXT("cyuediff.blueprint-cdo-factory/1"));
    Factory->SetStringField(TEXT("operation"), TEXT("construct-cdo-from-explicit-parent-template"));
    Factory->SetField(TEXT("class"), Reference(Class));
    Factory->SetField(TEXT("parentDefault"), Reference(ParentDefault));
    Factory->SetField(TEXT("targetDefault"), Reference(OriginalDefault));
    J->SetObjectField(TEXT("factory"), Factory);
    return Write(J);
}
FString UCyUEDiffDefaultLibrary::CapturePackageMetadata(UObject *Object) {
    UMetaData *Metadata = Cast<UMetaData>(Object);
    if (!Metadata || !Metadata->GetOutermost())
        return TEXT("{}");
    auto J = MakeShared<FJsonObject>();
    J->SetStringField(TEXT("format"), TEXT("cyuediff.engine-package-metadata/1"));
    J->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
    J->SetField(TEXT("object"), Reference(Metadata));
    J->SetStringField(TEXT("state"), TEXT("loaded-api-maps-after-engine-redirects"));
    const FUniqueObjectGuid ExistingGuid(Metadata);
    if (ExistingGuid.IsValid())
        J->SetStringField(TEXT("objectGuid"), ExistingGuid.GetGuid().ToString(EGuidFormats::Digits));
    uint32 Work = 0;
    int64 Text = 0;
    bool Failed = false;
    auto Charge = [&](const FString &Value) {
        Text += Value.Len() * 3ll;
        if (++Work > 100000 || Text > 32ll * 1024 * 1024)
            Failed = true;
    };
    auto Pairs = [&](const TMap<FName, FString> &Values) {
        TArray<JValue> Rows;
        for (const auto &Pair : Values) {
            Charge(Pair.Key.GetPlainNameString());
            Charge(Pair.Value);
            if (Failed)
                break;
            auto Row = MakeShared<FJsonObject>();
            Row->SetObjectField(TEXT("name"), NameValue(Pair.Key));
            Row->SetStringField(TEXT("value"), Pair.Value);
            Rows.Add(ObjectValue(Row));
        }
        return Rows;
    };
    J->SetArrayField(TEXT("rootValues"), Pairs(Metadata->RootMetaDataMap));
    TArray<JValue> Objects;
    for (const auto &Pair : Metadata->ObjectMetaDataMap) {
        Charge(FString());
        if (Failed)
            break;
        UObject *Target = Pair.Key.Get(true);
        auto Row = MakeShared<FJsonObject>();
        Row->SetField(TEXT("object"), Reference(Target));
        TArray<FString> Parts;
        UObject *Current = Target;
        uint32 Depth = 0;
        while (Current && Current != Metadata->GetOutermost()) {
            if (++Depth > 128) {
                Failed = true;
                break;
            }
            Charge(Current->GetName());
            if (Failed)
                break;
            Parts.Insert(Current->GetName(), 0);
            Current = Current->GetOuter();
        }
        Row->SetField(TEXT("reference"),
                      Target ? StringValue(FString::Join(Parts, TEXT("."))) : MakeShared<FJsonValueNull>());
        Row->SetArrayField(TEXT("values"), Pairs(Pair.Value));
        if (Failed)
            break;
        Objects.Add(ObjectValue(Row));
    }
    J->SetArrayField(TEXT("objectValues"), MoveTemp(Objects));
    J->SetBoolField(TEXT("completeCapture"), !Failed);
    if (Failed) {
        J->RemoveField(TEXT("rootValues"));
        J->RemoveField(TEXT("objectValues"));
    }
    return Write(J);
}
bool UCyUEDiffDefaultLibrary::BeginReadOnlyCommandletCapture() {
    if (!IsRunningCommandlet() || !GConfig)
        return false;
    GConfig->DisableFileOperations();
    return true;
}
FString UCyUEDiffDefaultLibrary::CaptureNameCaseMap() {
    auto J = MakeShared<FJsonObject>();
    J->SetStringField(TEXT("format"), TEXT("cyuediff.engine-name-case-map/1"));
    J->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
    TArray<JValue> Values;
    for (uint32 C = 0; C < 65536; ++C) {
        const auto Upper = static_cast<uint32>(static_cast<uint16>(FChar::ToUpper(static_cast<TCHAR>(C))));
        if (Upper != C)
            Values.Add(MakeShared<FJsonValueArray>(TArray<JValue>{NumberValue(C), NumberValue(Upper)}));
    }
    J->SetArrayField(TEXT("mapping"), MoveTemp(Values));
    return Write(J);
}
