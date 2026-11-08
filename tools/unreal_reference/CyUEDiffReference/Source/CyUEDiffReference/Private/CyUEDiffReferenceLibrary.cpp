#include "CyUEDiffReferenceLibrary.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimBoneCompressionCodec.h"
#include "Animation/AnimCurveCompressionCodec_CompressedRichCurve.h"
#include "Animation/AnimCurveCompressionCodec_UniformIndexable.h"
#include "Animation/AnimCurveCompressionCodec_UniformlySampled.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimSequenceDecompressionContext.h"
#include "IO/IoHash.h"
#include "AnimationCompression.h"
#include "Serialization/MemoryWriter.h"
#include "Containers/UnrealString.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "AssetCompilingManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "StaticMeshResources.h"
#include "Rendering/NaniteResources.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Modules/ModuleManager.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#if ENGINE_MAJOR_VERSION != 5 || ENGINE_MINOR_VERSION != 3
#error This validation reference adapter targets Unreal Engine 5.3 explicitly.
#endif
IMPLEMENT_MODULE(FDefaultModuleImpl, CyUEDiffReference)
namespace {
using Values = TArray<TSharedPtr<FJsonValue>>;
using Object = TSharedPtr<FJsonObject>;
TSharedPtr<FJsonValue> Number(double V) {
    return MakeShared<FJsonValueNumber>(V);
}
TSharedPtr<FJsonValue> Array(Values V) {
    return MakeShared<FJsonValueArray>(MoveTemp(V));
}
TSharedPtr<FJsonValue> Obj(Object V) {
    return MakeShared<FJsonValueObject>(MoveTemp(V));
}
template <class V> TSharedPtr<FJsonValue> Vec3(const V &V3) {
    return Array({Number(V3.X), Number(V3.Y), Number(V3.Z)});
}
template <class V> TSharedPtr<FJsonValue> Vec4(const V &V4) {
    return Array({Number(V4.X), Number(V4.Y), Number(V4.Z), Number(V4.W)});
}
template <class C> Values Integers(const C &Items) {
    Values Out;
    for (auto V : Items)
        Out.Add(Number(V));
    return Out;
}
Object Transform(const FTransform &T) {
    auto O = MakeShared<FJsonObject>();
    O->SetField(TEXT("translation"), Vec3(T.GetTranslation()));
    O->SetField(TEXT("rotation"), Vec4(T.GetRotation()));
    O->SetField(TEXT("scale"), Vec3(T.GetScale3D()));
    return O;
}
Values Bones(const FReferenceSkeleton &S) {
    Values Out;
    const auto &Info = S.GetRawRefBoneInfo();
    const auto &Pose = S.GetRawRefBonePose();
    for (int32 I = 0; I < Info.Num(); ++I) {
        auto O = Transform(Pose[I]);
        O->SetStringField(TEXT("name"), Info[I].Name.ToString());
        O->SetNumberField(TEXT("parent"), Info[I].ParentIndex);
        Out.Add(Obj(O));
    }
    return Out;
}
Object Skeletal(USkeletalMesh *Mesh) {
    auto O = MakeShared<FJsonObject>();
    O->SetArrayField(TEXT("bones"), Bones(Mesh->GetRefSkeleton()));
    O->SetBoolField(TEXT("hasVertexColors"), Mesh->GetHasVertexColors());
    Values Lods;
    const auto *Render = Mesh->GetResourceForRendering();
    if (!Render)
        return O;
    for (const auto &LOD : Render->LODRenderData) {
        auto L = MakeShared<FJsonObject>();
        const auto &VB = LOD.StaticVertexBuffers;
        const auto &Weights = LOD.SkinWeightVertexBuffer;
        L->SetNumberField(TEXT("indexBytes"), LOD.MultiSizeIndexContainer.GetDataTypeSize());
        L->SetNumberField(TEXT("boneIndexBytes"), Weights.GetBoneIndexByteSize());
        L->SetNumberField(TEXT("boneWeightBytes"), Weights.GetBoneWeightByteSize());
        L->SetBoolField(TEXT("variableInfluences"), Weights.GetVariableBonesPerVertex());
        L->SetNumberField(TEXT("maxInfluences"), Weights.GetMaxBoneInfluences());
        Values Vertices;
        for (uint32 I = 0; I < VB.PositionVertexBuffer.GetNumVertices(); ++I) {
            auto V = MakeShared<FJsonObject>();
            V->SetField(TEXT("position"), Vec3(VB.PositionVertexBuffer.VertexPosition(I)));
            V->SetField(TEXT("tangentX"), Vec4(VB.StaticMeshVertexBuffer.VertexTangentX(I)));
            V->SetField(TEXT("tangentZ"), Vec4(VB.StaticMeshVertexBuffer.VertexTangentZ(I)));
            Values UVs;
            for (uint32 U = 0; U < VB.StaticMeshVertexBuffer.GetNumTexCoords(); ++U) {
                const auto UV = VB.StaticMeshVertexBuffer.GetVertexUV(I, U);
                UVs.Add(Array({Number(UV.X), Number(UV.Y)}));
            }
            V->SetArrayField(TEXT("uvs"), UVs);
            Values Influences;
            uint32 Offset = 0, Count = 0;
            Weights.GetVertexInfluenceOffsetCount(I, Offset, Count);
            for (uint32 J = 0; J < Count; ++J) {
                Influences.Add(
                    Array({Number(Weights.GetBoneIndex(I, J)), Number(Weights.GetBoneWeight(I, J))}));
            }
            V->SetArrayField(TEXT("influences"), Influences);
            if (VB.ColorVertexBuffer.GetNumVertices()) {
                const auto C = VB.ColorVertexBuffer.VertexColor(I);
                V->SetField(TEXT("color"), Array({Number(C.R), Number(C.G), Number(C.B), Number(C.A)}));
            }
            Vertices.Add(Obj(V));
        }
        L->SetArrayField(TEXT("vertices"), Vertices);
        Values Indices;
        const auto *IB = LOD.MultiSizeIndexContainer.GetIndexBuffer();
        for (int32 I = 0; I < IB->Num(); ++I)
            Indices.Add(Number(IB->Get(I)));
        L->SetArrayField(TEXT("indices"), Indices);
        Values Sections;
        for (const auto &S : LOD.RenderSections) {
            auto R = MakeShared<FJsonObject>();
            R->SetNumberField(TEXT("material"), S.MaterialIndex);
            R->SetNumberField(TEXT("firstIndex"), S.BaseIndex);
            R->SetNumberField(TEXT("triangles"), S.NumTriangles);
            R->SetNumberField(TEXT("firstVertex"), S.BaseVertexIndex);
            R->SetNumberField(TEXT("vertices"), S.NumVertices);
            R->SetArrayField(TEXT("boneMap"), Integers(S.BoneMap));
            Sections.Add(Obj(R));
        }
        L->SetArrayField(TEXT("sections"), Sections);
        L->SetArrayField(TEXT("requiredBones"), Integers(LOD.RequiredBones));
        L->SetArrayField(TEXT("activeBones"), Integers(LOD.ActiveBoneIndices));
        Lods.Add(Obj(L));
    }
    O->SetArrayField(TEXT("lods"), Lods);
    return O;
}
Object Animation(UAnimSequence *Anim) {
    auto O = MakeShared<FJsonObject>();
    O->SetNumberField(TEXT("duration"), Anim->GetPlayLength());
    O->SetNumberField(TEXT("sampledKeys"), Anim->GetNumberOfSampledKeys());
    if (auto *Skeleton = Anim->GetSkeleton())
        O->SetArrayField(TEXT("bones"), Bones(Skeleton->GetReferenceSkeleton()));
    const auto *Model = Anim->GetDataModel();
    if (!Model)
        return O;
    O->SetNumberField(TEXT("frameRateNumerator"), Model->GetFrameRate().Numerator);
    O->SetNumberField(TEXT("frameRateDenominator"), Model->GetFrameRate().Denominator);
    Values Tracks;
    TArray<FName> Names;
    Model->GetBoneTrackNames(Names);
    O->SetStringField(TEXT("sampling"), TEXT("source model integer frames"));
    for (const auto Name : Names) {
        auto T = MakeShared<FJsonObject>();
        T->SetStringField(TEXT("name"), Name.ToString());
        TArray<FTransform> Keys;
        Model->GetBoneTrackTransforms(Name, Keys);
        Values Pos, Rot, Scale;
        for (const auto &Key : Keys) {
            Pos.Add(Vec3(Key.GetTranslation()));
            Rot.Add(Vec4(Key.GetRotation()));
            Scale.Add(Vec3(Key.GetScale3D()));
        }
        T->SetArrayField(TEXT("positions"), Pos);
        T->SetArrayField(TEXT("rotations"), Rot);
        T->SetArrayField(TEXT("scales"), Scale);
        Tracks.Add(Obj(T));
    }
    O->SetArrayField(TEXT("tracks"), Tracks);
    return O;
}
Object Static(UStaticMesh *Mesh) {
    auto O = MakeShared<FJsonObject>();
    const auto *Render = Mesh->GetRenderData();
    if (!Render)
        return O;
    if (const auto *N = Render->NaniteResourcesPtr.Get()) {
        auto V = MakeShared<FJsonObject>();
        V->SetNumberField(TEXT("rootBytes"), N->RootData.Num());
        V->SetNumberField(TEXT("residentStreamingBulkBytes"), N->StreamablePages.GetBulkDataSize());
        V->SetNumberField(TEXT("pages"), N->PageStreamingStates.Num());
        V->SetNumberField(TEXT("rootPages"), N->NumRootPages);
        Values States;
        uint64 NonRootStoredPageBytes = 0;
        for (int32 I = 0; I < N->PageStreamingStates.Num(); ++I) {
            const auto &P = N->PageStreamingStates[I];
            auto State = MakeShared<FJsonObject>();
            State->SetNumberField(TEXT("bulkOffset"), P.BulkOffset);
            State->SetNumberField(TEXT("bulkBytes"), P.BulkSize);
            State->SetNumberField(TEXT("pageBytes"), P.PageSize);
            State->SetNumberField(TEXT("dependenciesStart"), P.DependenciesStart);
            State->SetNumberField(TEXT("dependenciesCount"), P.DependenciesNum);
            State->SetNumberField(TEXT("maxHierarchyDepth"), P.MaxHierarchyDepth);
            State->SetNumberField(TEXT("flags"), P.Flags);
            States.Add(Obj(State));
            if (I >= static_cast<int32>(N->NumRootPages))
                NonRootStoredPageBytes += P.BulkSize;
        }
        V->SetArrayField(TEXT("pageStates"), States);
        V->SetNumberField(TEXT("nonRootStoredPageBytes"), static_cast<double>(NonRootStoredPageBytes));

        V->SetNumberField(TEXT("clusters"), N->NumClusters);
        V->SetNumberField(TEXT("inputTriangles"), N->NumInputTriangles);
        V->SetNumberField(TEXT("inputVertices"), N->NumInputVertices);
        V->SetNumberField(TEXT("positionPrecision"), N->PositionPrecision);
        V->SetNumberField(TEXT("normalPrecision"), N->NormalPrecision);
        V->SetNumberField(TEXT("tangentPrecision"), N->TangentPrecision);
        O->SetObjectField(TEXT("nanite"), V);
    }
    return O;
}
} // namespace
UStaticMesh *UCyUEDiffReferenceLibrary::CreatePrivateGrid(int32 Cells) {
    if (Cells < 2 || Cells > 256)
        return nullptr;
    const FString PackageName = TEXT("/Game/Corpus/NaniteGrid");
    if (FPackageName::DoesPackageExist(PackageName))
        return nullptr;
    UPackage *Package = CreatePackage(*PackageName);
    if (FindObject<UStaticMesh>(Package, TEXT("NaniteGrid")))
        return nullptr;
    UStaticMesh *Mesh = NewObject<UStaticMesh>(Package, TEXT("NaniteGrid"), RF_Public | RF_Standalone);
    FMeshDescription Description;
    FStaticMeshAttributes Attributes(Description);
    Attributes.Register();
    auto Positions = Attributes.GetVertexPositions();
    auto UVs = Attributes.GetVertexInstanceUVs();
    UVs.SetNumChannels(1);
    const auto Group = Description.CreatePolygonGroup();
    Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Surface");
    TArray<FVertexID> Vertices;
    for (int32 Y = 0; Y <= Cells; ++Y)
        for (int32 X = 0; X <= Cells; ++X) {
            const auto ID = Description.CreateVertex();
            Vertices.Add(ID);
            Positions[ID] = FVector3f(X * 4.f, Y * 4.f, 12.f * FMath::Sin(X * .17f) * FMath::Cos(Y * .11f));
        }
    auto Triangle = [&](int32 A, int32 B, int32 C) {
        TArray<FVertexInstanceID> Corners;
        for (int32 I : {A, B, C}) {
            const auto ID = Description.CreateVertexInstance(Vertices[I]);
            Corners.Add(ID);
            UVs.Set(ID, 0, FVector2f(float(I % (Cells + 1)) / Cells, float(I / (Cells + 1)) / Cells));
        }
        Description.CreateTriangle(Group, Corners);
    };
    for (int32 Y = 0; Y < Cells; ++Y)
        for (int32 X = 0; X < Cells; ++X) {
            const int32 A = Y * (Cells + 1) + X, B = A + 1, C = A + Cells + 1, D = C + 1;
            Triangle(A, B, D);
            Triangle(A, D, C);
        }
    auto &Source = Mesh->AddSourceModel();
    Source.BuildSettings.bRecomputeNormals = true;
    Source.BuildSettings.bRecomputeTangents = true;
    Mesh->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface)));
    Mesh->CreateMeshDescription(0, MoveTemp(Description));
    Mesh->CommitMeshDescription(0);
    Mesh->NaniteSettings.bEnabled = true;
    Mesh->Build(false);
    Mesh->MarkPackageDirty();
    return Mesh;
}
bool UCyUEDiffReferenceLibrary::BindPrivateSkeleton(UObject *Asset, USkeleton *Skeleton,
                                                    UPhysicsAsset *Physics) {
    if (!Asset || !Asset->GetOutermost()->GetName().StartsWith(TEXT("/Game/Corpus/")) ||
        (Skeleton && !Skeleton->GetOutermost()->GetName().StartsWith(TEXT("/Game/Corpus/"))) ||
        (Physics && !Physics->GetOutermost()->GetName().StartsWith(TEXT("/Game/Corpus/"))))
        return false;
    if (auto *Mesh = Cast<USkeletalMesh>(Asset)) {
        Mesh->SetSkeleton(Skeleton);
        if (Physics)
            Mesh->SetPhysicsAsset(Physics);
    } else if (auto *Anim = Cast<UAnimSequence>(Asset))
        Anim->SetSkeleton(Skeleton);
    else
        return false;
    Asset->MarkPackageDirty();
    Asset->PostEditChange();
    return true;
}
FString UCyUEDiffReferenceLibrary::ExportReference(UObject *Asset) {
    if (!Asset)
        return FString();
    FAssetCompilingManager::Get().FinishAllCompilation();
    Object Data;
    if (auto *Mesh = Cast<USkeletalMesh>(Asset))
        Data = Skeletal(Mesh);
    else if (auto *Anim = Cast<UAnimSequence>(Asset))
        Data = Animation(Anim);
    else if (auto *Skeleton = Cast<USkeleton>(Asset)) {
        Data = MakeShared<FJsonObject>();
        Data->SetArrayField(TEXT("bones"), Bones(Skeleton->GetReferenceSkeleton()));
        Data->SetStringField(TEXT("skeletonGuid"), Skeleton->GetGuid().ToString(EGuidFormats::Digits));
    } else if (auto *StaticMesh = Cast<UStaticMesh>(Asset))
        Data = Static(StaticMesh);
    else
        Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("schema"), TEXT("cyuediff.unreal-reference/1"));
    Data->SetStringField(TEXT("referenceSource"), TEXT("editor API; not a cooked decode"));
    Data->SetStringField(TEXT("object"), Asset->GetPathName());
    Data->SetStringField(TEXT("class"), Asset->GetClass()->GetPathName());
    Data->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
    FString Output;
    auto Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Data.ToSharedRef(), Writer);
    return Output;
}

FString UCyUEDiffReferenceLibrary::ExportCompressedAnimationReference(UObject *Asset) {
    auto *Anim = Cast<UAnimSequence>(Asset);
    if (!Anim || !Anim->GetSkeleton())
        return FString();
    FAssetCompilingManager::Get().FinishAllCompilation();
    const auto &D = Anim->CompressedData;
    if (!D.BoneCompressionCodec || !D.CompressedDataStructure)
        return FString();
    const int32 Keys = D.CompressedDataStructure->CompressedNumberOfKeys;
    const int32 Count = D.CompressedTrackToSkeletonMapTable.Num();
    if (Keys < 1 || Keys > 10000 || Count < 1 || int64(Count) * (2 * Keys - 1) > 500000)
        return FString();
    auto O = MakeShared<FJsonObject>();
    O->SetStringField(TEXT("schema"), TEXT("cyuediff.unreal-compressed-animation-reference/1"));
    O->SetStringField(TEXT("object"), Anim->GetPathName());
    O->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
    O->SetStringField(TEXT("boneCodecClass"), D.BoneCompressionCodec->GetClass()->GetPathName());
    O->SetStringField(TEXT("boneCodecHandle"), D.BoneCompressionCodec->GetCodecDDCHandle());
    O->SetStringField(
        TEXT("compressedBufferIoHash"),
        LexToString(FIoHash::HashBuffer(D.CompressedByteStream.GetData(), D.CompressedByteStream.Num())));
    O->SetNumberField(TEXT("compressedBytes"), D.CompressedByteStream.Num());
    O->SetNumberField(TEXT("keys"), Keys);
    O->SetNumberField(TEXT("interpolation"), static_cast<int32>(Anim->Interpolation));
    const auto Rate = Anim->GetSamplingFrameRate();
    O->SetNumberField(TEXT("samplingRateNumerator"), Rate.Numerator);
    O->SetNumberField(TEXT("samplingRateDenominator"), Rate.Denominator);
    const auto &Ref = Anim->GetSkeleton()->GetReferenceSkeleton();
    Values Frames;
    for (int32 I = 0; I < 2 * Keys - 1; ++I)
        Frames.Add(Number(I * .5));
    O->SetArrayField(TEXT("sampleFrames"), Frames);
    Values Tracks;
    for (int32 Track = 0; Track < Count; ++Track) {
        const auto Bone = D.CompressedTrackToSkeletonMapTable[Track].BoneTreeIndex;
        if (Bone < 0 || Bone >= Ref.GetNum())
            return FString();
        auto T = MakeShared<FJsonObject>();
        T->SetNumberField(TEXT("skeletonIndex"), Bone);
        T->SetStringField(TEXT("bone"), Ref.GetBoneName(Bone).ToString());
        FAnimSequenceDecompressionContext Context(Rate, Keys - 1, Anim->Interpolation, Anim->GetFName(),
                                                  *D.CompressedDataStructure, Ref.GetRefBonePose(),
                                                  D.CompressedTrackToSkeletonMapTable, Anim->GetSkeleton(),
                                                  Anim->IsValidAdditive());
        Values Translations, Rotations, Scales;
        for (int32 I = 0; I < 2 * Keys - 1; ++I) {
            Context.Seek(Rate.AsSeconds(FFrameTime::FromDecimal(I * .5)));
            FTransform Pose;
            D.BoneCompressionCodec->DecompressBone(Context, Track, Pose);
            Translations.Add(Vec3(Pose.GetTranslation()));
            Rotations.Add(Vec4(Pose.GetRotation()));
            Scales.Add(Vec3(Pose.GetScale3D()));
        }
        T->SetArrayField(TEXT("translations"), Translations);
        T->SetArrayField(TEXT("rotations"), Rotations);
        T->SetArrayField(TEXT("scales"), Scales);
        Tracks.Add(Obj(T));
    }
    O->SetArrayField(TEXT("tracks"), Tracks);
    FString Output;
    auto Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(O, Writer);
    return Output;
}

FString UCyUEDiffReferenceLibrary::ExportPerTrackSyntheticReference(UObject *CodecOwner) {
    auto *Anim = Cast<UAnimSequence>(CodecOwner);
    if (!Anim || !Anim->GetSkeleton())
        return FString();
    FAssetCompilingManager::Get().FinishAllCompilation();
    const auto &Owner = Anim->CompressedData;
    if (!Owner.BoneCompressionCodec || !Owner.CompressedDataStructure ||
        Owner.BoneCompressionCodec->GetClass()->GetPathName() !=
            TEXT("/Script/Engine.AnimCompress_PerTrackCompression"))
        return FString();
    const auto &OwnerData = static_cast<const FUECompressedAnimData &>(*Owner.CompressedDataStructure);
    auto Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("schema"), TEXT("cyuediff.unreal-per-track-synthetic-reference/1"));
    Root->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
    Root->SetStringField(TEXT("boneCodecClass"), Owner.BoneCompressionCodec->GetClass()->GetPathName());
    Values Cases;
    for (int32 Kind = 0; Kind < 3; ++Kind)
        for (int32 Format = 1; Format <= 6; ++Format)
            for (int32 Mask = 0; Mask < 8; ++Mask)
                for (int32 Timed = 0; Timed < 2; ++Timed)
                    for (int32 Width = 0; Width < 2; ++Width) {
                        if ((Kind != 1 && (Format == 4 || Format == 5)) ||
                            (Kind == 1 && Format == 1 && Mask != 0 && Mask != 7))
                            continue;
                        const int32 Frames = Width ? 256 : 5;
                        TArray<uint8> Keys;
                        FMemoryWriter Writer(Keys);
                        uint32 Header = (uint32(Format) << 28) | (uint32(Mask | (Timed ? 8 : 0)) << 24) | 3u;
                        Writer << Header;
                        if (Format == 3) {
                            for (int32 Axis = 0; Axis < 3; ++Axis)
                                if (!Mask || (Mask & (1 << Axis))) {
                                    float Min = .1f * Axis, Range = .5f / (Axis + 1);
                                    Writer << Min << Range;
                                }
                        }
                        for (int32 Key = 0; Key < 3; ++Key) {
                            if (Format == 1) {
                                for (int32 Axis = 0; Axis < 3; ++Axis)
                                    if (!Mask || (Mask & (1 << Axis))) {
                                        float V =
                                            (Kind == 1 ? .05f : 1.f) * (Key + 1) * (Axis % 2 ? -1.f : 1.f);
                                        Writer << V;
                                    }
                            } else if (Format == 2) {
                                for (int32 Axis = 0; Axis < 3; ++Axis)
                                    if (!Mask || (Mask & (1 << Axis))) {
                                        uint16 V = Kind == 1 ? uint16(32767 + (Key + 1) * (Axis + 1) *
                                                                                  (Axis % 2 ? -1000 : 1000))
                                                             : uint16(255 + Key + Axis + 1);
                                        Writer << V;
                                    }
                            } else if (Format == 3 || Format == 4) {
                                uint32 V = Kind == 1
                                               ? ((uint32(1033 + Key * 45) << 21) |
                                                  (uint32(900 + Key * 12) << 10) | uint32(500 + Key * 10))
                                               : ((uint32(1060 + Key * 15) << 21) |
                                                  (uint32(900 + Key * 12) << 10) | uint32(520 + Key * 10));
                                Writer << V;
                            } else if (Format == 5) {
                                FQuat4f Q(.1f * (Key + 1), -.05f * (Key + 1), .125f * (Key + 1), 1.f);
                                Q.Normalize();
                                FQuatFloat32NoW Packed(Q);
                                Writer << Packed;
                            }
                        }
                        if (Timed) {
                            while (Writer.Tell() % 4) {
                                uint8 Pad = 0x55;
                                Writer << Pad;
                            }
                            for (int32 F : {0, Frames / 2, Frames - 1}) {
                                if (Width) {
                                    uint16 V = uint16(F);
                                    Writer << V;
                                } else {
                                    uint8 V = uint8(F);
                                    Writer << V;
                                }
                            }
                        }
                        Writer.Close();
                        TArray<int32> Offsets{Kind == 0 ? 0 : -1, Kind == 1 ? 0 : -1}, Scale;
                        if (Kind == 2)
                            Scale.Add(0);
                        FUECompressedAnimData Data;
                        Data.CompressedNumberOfKeys = Frames;
                        Data.KeyEncodingFormat = AKF_PerTrackCompression;
                        Data.TranslationCompressionFormat = Data.RotationCompressionFormat =
                            Data.ScaleCompressionFormat = ACF_Identity;
                        Data.TranslationCodec = OwnerData.TranslationCodec;
                        Data.RotationCodec = OwnerData.RotationCodec;
                        Data.ScaleCodec = OwnerData.ScaleCodec;
                        Data.CompressedTrackOffsets = MakeArrayView(Offsets);
                        Data.CompressedScaleOffsets.OffsetData = MakeArrayView(Scale);
                        Data.CompressedScaleOffsets.StripSize = Kind == 2 ? 1 : 2;
                        Data.CompressedByteStream = MakeArrayView(Keys);
                        TArray<uint8> Buffer;
                        FMemoryWriter BufferWriter(Buffer);
                        for (auto V : Offsets)
                            BufferWriter << V;
                        for (auto V : Scale)
                            BufferWriter << V;
                        BufferWriter.Serialize(Keys.GetData(), Keys.Num());
                        BufferWriter.Close();
                        auto Case = MakeShared<FJsonObject>();
                        Case->SetStringField(
                            TEXT("name"), FString::Printf(TEXT("channel%d-format%d-mask%d-time%d-frames%d"),
                                                          Kind, Format, Mask, Timed, Frames));
                        Case->SetStringField(TEXT("bufferHex"), BytesToHex(Buffer.GetData(), Buffer.Num()));
                        Case->SetStringField(TEXT("bufferIoHash"), LexToString(FIoHash::HashBuffer(
                                                                       Buffer.GetData(), Buffer.Num())));
                        auto Input = MakeShared<FJsonObject>();
                        Input->SetNumberField(TEXT("frames"), Frames);
                        Input->SetNumberField(TEXT("trackOffsetCount"), 2);
                        Input->SetNumberField(TEXT("scaleOffsetCount"), Scale.Num());
                        Input->SetNumberField(TEXT("scaleStripSize"), Kind == 2 ? 1 : 2);
                        Input->SetNumberField(TEXT("keyBytes"), Keys.Num());
                        Input->SetArrayField(TEXT("trackToSkeleton"), {Number(0)});
                        Values SampleFrames;
                        const TArray<double> Times{0.,
                                                   .5,
                                                   (Frames - 1) * .25,
                                                   Frames / 2.,
                                                   double(Frames / 2),
                                                   Frames - 1.5,
                                                   double(Frames - 1)};
                        for (auto T : Times)
                            SampleFrames.Add(Number(T));
                        Input->SetArrayField(TEXT("sampleFrames"), SampleFrames);
                        Case->SetObjectField(TEXT("input"), Input);
                        TArray<FTrackToSkeletonMap> Map;
                        Map.AddDefaulted();
                        Map[0].BoneTreeIndex = 0;
                        const auto &Ref = Anim->GetSkeleton()->GetReferenceSkeleton();
                        const FFrameRate Rate(30, 1);
                        for (int32 Step = 0; Step < 2; ++Step) {
                            const auto Mode =
                                Step ? EAnimInterpolationType::Step : EAnimInterpolationType::Linear;
                            FAnimSequenceDecompressionContext Context(
                                Rate, Frames - 1, Mode, Anim->GetFName(), Data, Ref.GetRefBonePose(), Map,
                                Anim->GetSkeleton(), false);
                            Values Samples;
                            for (auto T : Times) {
                                Context.Seek(Rate.AsSeconds(FFrameTime::FromDecimal(T)));
                                FTransform Pose;
                                Owner.BoneCompressionCodec->DecompressBone(Context, 0, Pose);
                                auto Sample = Transform(Pose);
                                Sample->SetNumberField(TEXT("frame"), T);
                                Samples.Add(Obj(Sample));
                            }
                            Case->SetArrayField(Step ? TEXT("step") : TEXT("linear"), Samples);
                        }
                        Cases.Add(Obj(Case));
                    }
    Root->SetArrayField(TEXT("cases"), Cases);
    FString Output;
    auto Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Root, Writer);
    return Output;
}

namespace {
FFloatCurve OriginalCurve(const FName Name, int32 Kind, int32 Extrapolation) {
    FFloatCurve C(Name, 4);
    C.FloatCurve.DefaultValue = Kind == 0 ? MAX_flt : 2.75f;
    C.FloatCurve.PreInfinityExtrap = static_cast<ERichCurveExtrapolation>(Extrapolation);
    C.FloatCurve.PostInfinityExtrap = static_cast<ERichCurveExtrapolation>(Extrapolation);
    if (Kind == 0)
        return C;
    const float Times[] = {0.f, .1f, .2f, .4f, .875f, 1.1f, 1.4f, 1.75f, 2.f};
    const float MixedTimes[] = {0.f, .2f, .875f, 1.4f, 2.f};
    for (int32 I = 0; I < (Kind == 4 ? 5 : 9); ++I) {
        const auto Handle = C.FloatCurve.AddKey(Kind == 4 ? MixedTimes[I] : Times[I],
                                                Kind == 1 ? 3.25f : float((I * I + 3 * I) % 11) - 2.5f);
        auto &Key = C.FloatCurve.GetKey(Handle);
        Key.InterpMode = Kind <= 2   ? RCIM_Linear
                         : Kind == 4 ? static_cast<ERichCurveInterpMode>(I % 3)
                                     : RCIM_Cubic;
        Key.TangentMode = RCTM_Break;
        Key.ArriveTangent = float(I) * .37f - .45f;
        Key.LeaveTangent = .75f - float(I) * .23f;
        if (Kind == 5) {
            Key.TangentWeightMode = static_cast<ERichCurveTangentWeightMode>(I % 4);
            Key.ArriveTangentWeight = .11f + float(I) * .07f;
            Key.LeaveTangentWeight = .15f + float(I) * .09f;
        }
    }
    return C;
}
UAnimCurveCompressionCodec *CurveCodec(UObject *Owner, int32 Kind) {
    if (Kind == 0)
        return NewObject<UAnimCurveCompressionCodec_CompressedRichCurve>(Owner);
    if (Kind == 1)
        return NewObject<UAnimCurveCompressionCodec_UniformIndexable>(Owner);
    if (Kind == 2)
        return NewObject<UAnimCurveCompressionCodec_UniformlySampled>(Owner);
    return nullptr;
}
Object CurveMeasurements(const FCompressedAnimSequence &D, const TArray<float> &Times) {
    FMemMark Mark(FMemStack::Get());
    auto O = MakeShared<FJsonObject>();
    O->SetStringField(TEXT("codecClass"), D.CurveCompressionCodec->GetClass()->GetPathName());
    O->SetStringField(TEXT("codecPath"), D.CurveCompressionCodec->GetPathName());
    O->SetStringField(TEXT("bufferIoHash"),
                      LexToString(FIoHash::HashBuffer(D.CompressedCurveByteStream.GetData(),
                                                      D.CompressedCurveByteStream.Num())));
    O->SetNumberField(TEXT("bufferBytes"), D.CompressedCurveByteStream.Num());
    O->SetArrayField(TEXT("times"), Integers(Times));
    Values Names, Permutation, Rows;
    for (const auto &N : D.IndexedCurveNames) {
        Names.Add(MakeShared<FJsonValueString>(N.CurveName.ToString()));
        Permutation.Add(Number(N.CurveIndex));
    }
    O->SetArrayField(TEXT("names"), Names);
    O->SetArrayField(TEXT("runtimeNamePermutation"), Permutation);
    for (float Time : Times) {
        FBlendedCurve Batch;
        D.CurveCompressionCodec->DecompressCurves(D, Batch, Time);
        Values ValuesAtTime;
        for (const auto &N : D.IndexedCurveNames) {
            auto V = MakeShared<FJsonObject>();
            V->SetStringField(TEXT("name"), N.CurveName.ToString());
            bool Present = false;
            const float Value = Batch.Get(N.CurveName, Present);
            V->SetBoolField(TEXT("batchPresent"), Present);
            V->SetNumberField(TEXT("batch"), Value);
            // UE 5.3 UniformlySampled single-curve lookup can use an animated array index past its row.
            // This adapter never invokes that unsafe path. Batch measurements remain authoritative.
            if (!D.CurveCompressionCodec->IsA<UAnimCurveCompressionCodec_UniformlySampled>())
                V->SetNumberField(TEXT("single"),
                                  D.CurveCompressionCodec->DecompressCurve(D, N.CurveName, Time));
            ValuesAtTime.Add(Obj(V));
        }
        Rows.Add(Array(ValuesAtTime));
    }
    O->SetArrayField(TEXT("samples"), Rows);
    return O;
}
FString JsonText(Object O) {
    FString Text;
    auto Writer = TJsonWriterFactory<>::Create(&Text);
    FJsonSerializer::Serialize(O.ToSharedRef(), Writer);
    return Text;
}
} // namespace
FString UCyUEDiffReferenceLibrary::ExportCompressedCurveSyntheticReference(UObject *CodecOwner) {
    if (!CodecOwner || !CodecOwner->GetPathName().StartsWith(TEXT("/Game/Corpus/")))
        return FString();
    auto O = MakeShared<FJsonObject>();
    O->SetStringField(TEXT("schema"), TEXT("cyuediff.unreal-compressed-curve-synthetic-reference/1"));
    O->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
    Values Cases;
    for (int32 Codec = 0; Codec < 3; ++Codec)
        for (int32 Kind = 0; Kind < (Codec == 0 ? 6 : 3); ++Kind)
            for (int32 Extrap = 0; Extrap < (Codec == 0 ? 6 : 1); ++Extrap)
                for (int32 Precision = 0; Precision < (Codec == 0 ? 2 : 1); ++Precision) {
                    auto *NativeCodec = CurveCodec(CodecOwner, Codec);
                    if (auto *Rich = Cast<UAnimCurveCompressionCodec_CompressedRichCurve>(NativeCodec))
                        Rich->MaxCurveError = Precision ? .002f : 0.f;
                    FCompressibleAnimData Input;
                    Input.SequenceLength = 2.;
                    Input.NumberOfKeys = 61;
                    Input.SampledFrameRate = FFrameRate(30, 1);
                    const int32 Count = Codec == 0 ? 1 : Kind == 0 ? 1 : Kind == 1 ? 5 : 37;
                    for (int32 I = 0; I < Count; ++I)
                        Input.RawFloatCurves.Add(
                            OriginalCurve(FName(*FString::Printf(TEXT("CyNumericCurve_%d"), I)),
                                          Codec == 0 ? Kind : (I % 3 == 0 ? 1 : 2 + I % 4), Extrap));
                    FAnimCurveCompressionResult Compressed;
                    if (!NativeCodec->Compress(Input, Compressed))
                        return FString();
                    // CompressCurve leaves alignment padding unspecified. Normalize only those gaps,
                    // then measure the actual codec on the normalized original numerical input.
                    if (Codec == 0) {
                        auto &B = Compressed.CompressedBytes;
                        int32 N = 0, Offset = 0;
                        FMemory::Memcpy(&N, B.GetData() + 4, 4);
                        FMemory::Memcpy(&Offset, B.GetData() + 8, 4);
                        const uint8 Format = B[0], TimeFormat = B[1];
                        if (Format >= 2 && N > 0) {
                            const int32 Modes = Format == 4 ? N : Format == 5 ? 2 * N : 0;
                            const int32 TimeAt = Align(Modes, TimeFormat == 0 ? 2 : 4);
                            for (int32 I = Modes; I < TimeAt; ++I)
                                B[Offset + I] = 0;
                            if (TimeFormat == 0) {
                                const int32 End = TimeAt + 2 * N;
                                for (int32 I = End; I < Align(End, 4); ++I)
                                    B[Offset + I] = 0;
                            }
                        }
                    }
                    FCompressedAnimSequence D;
                    D.CompressedCurveByteStream = MoveTemp(Compressed.CompressedBytes);
                    D.CurveCompressionCodec = NativeCodec;
                    for (const auto &Curve : Input.RawFloatCurves) {
                        FAnimCompressedCurveIndexedName N;
                        N.CurveName = Curve.GetName();
                        D.IndexedCurveNames.Add(N);
                    }
                    D.RebuildCurveIndexTable();
                    TArray<float> Times = {-2.f, -.5f, -.01f, 0.f,  .0001f, .2f, .875f,
                                           1.4f, 2.f,  2.01f, 2.5f, 4.f,    6.f};
                    for (int32 I = 0; I <= 32; ++I)
                        Times.Add(float(I) / 16.f);
                    auto Case = CurveMeasurements(D, Times);
                    Case->SetNumberField(TEXT("codec"), Codec);
                    Case->SetNumberField(TEXT("kind"), Kind);
                    Case->SetNumberField(TEXT("extrapolation"), Extrap);
                    Case->SetNumberField(TEXT("precision"), Precision);
                    Case->SetArrayField(TEXT("buffer"), Integers(D.CompressedCurveByteStream));
                    Cases.Add(Obj(Case));
                }
    O->SetArrayField(TEXT("cases"), Cases);
    return JsonText(O);
}
bool UCyUEDiffReferenceLibrary::PreparePrivateAnimationCurves(UObject *Asset, int32 Codec) {
    auto *Anim = Cast<UAnimSequence>(Asset);
    if (!Anim || !Anim->GetPathName().StartsWith(TEXT("/Game/Corpus/Curve")) || Codec < 0 || Codec > 2 ||
        Anim->GetDataModel()->GetFloatCurves().Num() != 0)
        return false;
    const FString PackageName = Anim->GetOutermost()->GetName() + TEXT("Settings");
    if (FindPackage(nullptr, *PackageName) || FPackageName::DoesPackageExist(PackageName))
        return false;
    auto *Package = CreatePackage(*PackageName);
    auto *Settings = NewObject<UAnimCurveCompressionSettings>(
        Package, FName(*FPackageName::GetShortName(PackageName)), RF_Public | RF_Standalone);
    Settings->Codec = CurveCodec(Settings, Codec);
    if (!Settings->Codec)
        return false;
    Anim->CurveCompressionSettings = Settings;
    auto &Controller = Anim->GetController();
    Controller.OpenBracket(FText::FromString(TEXT("Prepare original private curve corpus")), false);
    bool Success = true;
    for (int32 Kind = 0; Kind < 6; ++Kind) {
        const FName Name(*FString::Printf(TEXT("CyValidationCurve_%d"), Kind));
        const auto Curve = OriginalCurve(Name, Kind, Kind % 6);
        const FAnimationCurveIdentifier Id(Name, ERawCurveTrackTypes::RCT_Float);
        Success = Controller.AddCurve(Id, 4, false) && Success;
        Success = Controller.SetCurveKeys(Id, Curve.FloatCurve.GetConstRefOfKeys(), false) && Success;
    }
    Controller.CloseBracket(false);
    Settings->MarkPackageDirty();
    Anim->MarkPackageDirty();
    return Success;
}
FString UCyUEDiffReferenceLibrary::ExportCompressedCurvesReference(UObject *Asset) {
    auto *Anim = Cast<UAnimSequence>(Asset);
    if (!Anim || !Anim->GetPathName().StartsWith(TEXT("/Game/Corpus/")))
        return FString();
    FAssetCompilingManager::Get().FinishAllCompilation();
    const auto &D = Anim->CompressedData;
    if (!D.CurveCompressionCodec || D.IndexedCurveNames.Num() > 10000 ||
        D.CompressedCurveByteStream.Num() > 16 * 1024 * 1024)
        return FString();
    TArray<float> Times;
    const float Length = Anim->GetPlayLength();
    for (int32 I = 0; I <= 256; ++I)
        Times.Add(Length * (float(I) / 256.f));
    auto O = CurveMeasurements(D, Times);
    O->SetStringField(TEXT("schema"), TEXT("cyuediff.unreal-compressed-curves-reference/1"));
    O->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
    O->SetStringField(TEXT("object"), Anim->GetPathName());
    O->SetNumberField(TEXT("duration"), Length);
    O->SetArrayField(TEXT("buffer"), Integers(D.CompressedCurveByteStream));
    return JsonText(O);
}

FString UCyUEDiffReferenceLibrary::ExportNaniteSourceReference(UObject *Asset) {
    auto *Mesh = Cast<UStaticMesh>(Asset);
    if (!Mesh || !Mesh->GetOutermost()->GetName().StartsWith(TEXT("/Game/Corpus/")))
        return FString();
    FAssetCompilingManager::Get().FinishAllCompilation();
    const auto *Render = Mesh->GetRenderData();
    const auto *Resource = Render ? Render->NaniteResourcesPtr.Get() : nullptr;
    const auto *Description = Mesh->GetMeshDescription(0);
    if (!Resource || !Resource->NumClusters || !Description || Description->Vertices().Num() > 1000000 ||
        Description->Triangles().Num() > 2000000)
        return FString();
    const auto Attributes = FStaticMeshConstAttributes(*Description);
    const auto Positions = Attributes.GetVertexPositions();
    auto Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("schema"), TEXT("cyuediff.unreal-nanite-source-reference/1"));
    Result->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
    Result->SetStringField(TEXT("object"), Mesh->GetPathName());
    Result->SetNumberField(TEXT("positionPrecision"), Resource->PositionPrecision);
    const float Scale = FMath::Exp2(float(Resource->PositionPrecision));
    Values Vertices;
    for (const auto ID : Description->Vertices().GetElementIDs()) {
        const auto Position = Positions[ID];
        const float X = FMath::RoundToFloat(Position.X * Scale), Y = FMath::RoundToFloat(Position.Y * Scale),
                    Z = FMath::RoundToFloat(Position.Z * Scale);
        if (!FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z) ||
            FMath::Abs(double(X)) > 2147483647. || FMath::Abs(double(Y)) > 2147483647. ||
            FMath::Abs(double(Z)) > 2147483647.)
            return FString();
        auto V = MakeShared<FJsonObject>();
        V->SetNumberField(TEXT("id"), ID.GetValue());
        V->SetField(TEXT("position"), Vec3(Position));
        V->SetField(TEXT("quantizedPosition"), Vec3(FIntVector(int32(X), int32(Y), int32(Z))));
        Vertices.Add(Obj(V));
    }
    Values Triangles;
    for (const auto ID : Description->Triangles().GetElementIDs()) {
        Values VertexIDs;
        for (const auto Instance : Description->GetTriangleVertexInstances(ID))
            VertexIDs.Add(Number(Description->GetVertexInstanceVertex(Instance).GetValue()));
        Triangles.Add(Array(MoveTemp(VertexIDs)));
    }
    Result->SetArrayField(TEXT("vertices"), MoveTemp(Vertices));
    Result->SetArrayField(TEXT("triangles"), MoveTemp(Triangles));
    Result->SetNumberField(TEXT("naniteClusters"), Resource->NumClusters);
    Result->SetNumberField(TEXT("nanitePages"), Resource->PageStreamingStates.Num());
    FString Output;
    const auto Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Result, Writer);
    return Output;
}
