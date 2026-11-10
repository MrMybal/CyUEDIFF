#include "CyUEDiffDefaultLibrary.h"
#include "Dom/JsonObject.h"
#include "Engine/UserDefinedStruct.h"
#include "Internationalization/Text.h"
#include "Misc/EngineVersion.h"
#include "Modules/ModuleManager.h"
#include "Serialization/CustomVersion.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchiveAdapters.h"
#include "UObject/NoExportTypes.h"
#include "UObject/StructOnScope.h"
#include "UObject/ObjectPtr.h"
#include "UObject/UnrealType.h"
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
