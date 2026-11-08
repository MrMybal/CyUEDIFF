#include "CyUEDiffReferenceLibrary.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimBoneCompressionCodec.h"
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
    else if (auto *StaticMesh = Cast<UStaticMesh>(Asset))
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
