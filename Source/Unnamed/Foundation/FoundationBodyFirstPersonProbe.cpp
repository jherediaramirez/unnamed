// The Unnamed Player's first-person body, measured (-FoundationSuite=body): the M1a acceptance rows of PLAN.md section
// 7. Clearance and toe visibility are geometry on the CPU-skinned body (the mesh's imported model, editor builds); the
// head's pixels are counted afterwards in the "-body-fp-head-*" shots, where the head draws magenta.
#include "Foundation/FoundationRuntimeProbe.h"
#include "AnimationRuntime.h"
#include "Foundation/FoundationBodyAnimInstance.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationProfile.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITORONLY_DATA
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    const FName FirstPersonHeadMaterial(TEXT("M_UP_Head"));

    int32 SetFirstPersonProbeCVar(const TCHAR* Name, int32 Value)
    {
        IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
        if (!Variable) return 0;
        const int32 Previous = Variable->GetInt();
        Variable->Set(Value, ECVF_SetByCode);
        return Previous;
    }

    // A body's surface in world space for this frame's pose: CPU-skinned from the mesh's imported model (the render
    // data keeps no CPU copy), the head's section left out on request.
    struct FBodySurface
    {
        TArray<FVector> Vertices;
        TArray<int32> Indices;
        // Each vertex's most weighted bone (mesh bone index).
        TArray<int32> Bone;
    };

    bool GatherBodySurface(const USkeletalMeshComponent* Component, bool bSkipHead, FBodySurface& Out)
    {
        Out = FBodySurface();
#if WITH_EDITORONLY_DATA
        USkeletalMesh* Mesh = Component ? Component->GetSkeletalMeshAsset() : nullptr;
        const FSkeletalMeshModel* Model = Mesh ? Mesh->GetImportedModel() : nullptr;
        if (!Model || Model->LODModels.IsEmpty()) return false;
        const FSkeletalMeshLODModel& Lod = Model->LODModels[0];
        const int32 Head = Mesh->GetMaterials().IndexOfByPredicate(
            [](const FSkeletalMaterial& Material) { return Material.MaterialSlotName == FirstPersonHeadMaterial; });
        const TArray<FTransform>& Pose = Component->GetComponentSpaceTransforms();
        const TArray<FMatrix44f>& RefInverse = Mesh->GetRefBasesInvMatrix();
        const FTransform ToWorld = Component->GetComponentTransform();
        TArray<FMatrix> Skin;
        Skin.SetNum(Pose.Num());
        for (int32 Bone = 0; Bone < Pose.Num(); ++Bone)
            Skin[Bone] = FMatrix(RefInverse[Bone]) * Pose[Bone].ToMatrixWithScale();
        for (const FSkelMeshSection& Section : Lod.Sections)
        {
            if (bSkipHead && Section.MaterialIndex == Head) continue;
            const int32 Base = Out.Vertices.Num();
            for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
            {
                FVector Position = FVector::ZeroVector;
                int32 Strongest = 0;
                uint16 Most = 0;
                for (int32 Influence = 0; Influence < MAX_TOTAL_INFLUENCES; ++Influence)
                {
                    const uint16 Weight = Vertex.InfluenceWeights[Influence];
                    if (!Weight) continue;
                    const int32 Bone = Section.BoneMap[Vertex.InfluenceBones[Influence]];
                    Position += Skin[Bone].TransformPosition(FVector(Vertex.Position)) * (Weight / 65535.);
                    if (Weight > Most)
                    {
                        Most = Weight;
                        Strongest = Bone;
                    }
                }
                Out.Vertices.Add(ToWorld.TransformPosition(Position));
                Out.Bone.Add(Strongest);
            }
            for (uint32 Index = 0; Index < Section.NumTriangles * 3; ++Index)
                Out.Indices.Add(
                    int32(Lod.IndexBuffer[Section.BaseIndex + Index]) - int32(Section.BaseVertexIndex) + Base);
        }
        return Out.Indices.Num() > 0;
#else
        return false;
#endif
    }

    // The nearest surface point to Point: its distance and the bone of its triangle's first vertex.
    double NearestSurface(const FBodySurface& Surface, const FVector& Point, int32& OutBone)
    {
        double Best = TNumericLimits<double>::Max();
        OutBone = INDEX_NONE;
        for (int32 Index = 0; Index + 2 < Surface.Indices.Num(); Index += 3)
        {
            const FVector& A = Surface.Vertices[Surface.Indices[Index]];
            const FVector Closest = FMath::ClosestPointOnTriangleToPoint(
                Point, A, Surface.Vertices[Surface.Indices[Index + 1]], Surface.Vertices[Surface.Indices[Index + 2]]);
            const double Distance = FVector::Dist(Point, Closest);
            if (Distance < Best)
            {
                Best = Distance;
                OutBone = Surface.Bone[Surface.Indices[Index]];
            }
        }
        return Best;
    }

    // How many of Bone's vertices the eye sees: inside a view of VerticalFovDeg at Aspect, nothing of the surface in
    // between (the segment stops 0.5 cm short of the vertex, so its own faces don't count).
    int32 CountVisible(const FBodySurface& Surface, int32 Bone, const FVector& Eye, const FRotator& View,
        double VerticalFovDeg, double Aspect)
    {
        const double HalfV = FMath::DegreesToRadians(VerticalFovDeg * .5);
        const double HalfH = FMath::Atan(FMath::Tan(HalfV) * Aspect);
        const FMatrix Axes = FRotationMatrix(View);
        const FVector Forward = Axes.GetScaledAxis(EAxis::X), Right = Axes.GetScaledAxis(EAxis::Y),
                      Up = Axes.GetScaledAxis(EAxis::Z);
        int32 Visible = 0;
        for (int32 Vertex = 0; Vertex < Surface.Vertices.Num(); ++Vertex)
        {
            if (Surface.Bone[Vertex] != Bone) continue;
            const FVector To = Surface.Vertices[Vertex] - Eye;
            const double X = FVector::DotProduct(To, Forward);
            if (X <= 10) continue;
            if (FMath::Abs(FMath::Atan2(FVector::DotProduct(To, Right), X)) > HalfH ||
                FMath::Abs(FMath::Atan2(FVector::DotProduct(To, Up), X)) > HalfV)
                continue;
            const FVector End = Surface.Vertices[Vertex] - To.GetSafeNormal() * .5;
            bool bBlocked = false;
            for (int32 Index = 0; Index + 2 < Surface.Indices.Num() && !bBlocked; Index += 3)
            {
                FVector Hit, Normal;
                bBlocked = FMath::SegmentTriangleIntersection(Eye, End, Surface.Vertices[Surface.Indices[Index]],
                    Surface.Vertices[Surface.Indices[Index + 1]], Surface.Vertices[Surface.Indices[Index + 2]], Hit,
                    Normal);
            }
            Visible += bBlocked ? 0 : 1;
        }
        return Visible;
    }

    // The largest distance of any bone from its reference pose (cm, component space): ~0 means a reference-pose frame.
    double DistanceFromReferencePose(const USkeletalMeshComponent* Component)
    {
        const USkeletalMesh* Mesh = Component->GetSkeletalMeshAsset();
        const TArray<FTransform>& Pose = Component->GetComponentSpaceTransforms();
        const FReferenceSkeleton& Reference = Mesh->GetRefSkeleton();
        TArray<FTransform> Rest;
        FAnimationRuntime::FillUpComponentSpaceTransforms(Reference, Reference.GetRefBonePose(), Rest);
        double Most = 0;
        for (int32 Bone = 0; Bone < FMath::Min(Pose.Num(), Rest.Num()); ++Bone)
            Most = FMath::Max(Most, FVector::Dist(Pose[Bone].GetLocation(), Rest[Bone].GetLocation()));
        return Most;
    }

    // One scripted reference step's results, for the bit-identical check.
    struct FStepRecord
    {
        FVector Feet, Velocity;
        double EyeCm;
        bool operator==(const FStepRecord& Other) const
        {
            return Feet == Other.Feet && Velocity == Other.Velocity && EyeCm == Other.EyeCm;
        }
    };

    // The scripted 30 s path (600 reference steps): walk, sprint, jump, strafe, crouch-walk, back up, turn, stand.
    void ScriptStep(int64 Step, FFoundationIntent& Intent, double& ViewYaw)
    {
        Intent = FFoundationIntent();
        ViewYaw = Step >= 460 && Step < 520 ? (Step - 460) * 1.5 : Step >= 520 ? 90 : 0;
        if (Step < 100)
            Intent.Move = FVector2D(1, 0);
        else if (Step < 160)
        {
            Intent.Move = FVector2D(1, 0);
            Intent.bSprint = true;
        }
        else if (Step < 200)
        {
            Intent.Move = FVector2D(1, 0);
            Intent.bJump = true;
        }
        else if (Step < 260)
            Intent.Move = FVector2D(0, 1);
        else if (Step < 340)
        {
            Intent.Move = FVector2D(1, 0);
            Intent.bCrouch = true;
        }
        else if (Step < 400)
            Intent.Move = FVector2D(-1, 0);
        else if (Step < 520)
            Intent.Move = FVector2D(1, 0);
        else
            Intent.bJump = Step >= 540 && Step < 545;
    }
}
#endif

void UFoundationRuntimeProbe::AddFirstPersonBodyCases()
{
#if WITH_DEV_AUTOMATION_TESTS
    // An open stretch of the dev grid (FoundationBodyProbe's spot, 20 m further south), facing east (+X).
    const FVector Spot(20000, 22000, 0);
    // The Playground's pool (FoundationSwimProbe): water 0-200 cm.
    const FVector Pool(1030, 550, 0);
    auto Key = [this](const TCHAR* Id) { return Controller->GetInputSubsystem()->GetCurrentKey(Id); };
    auto Shot = [this](const FString& Name)
    {
        FScreenshotRequest::RequestScreenshot(
            FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-body-") + Name + TEXT(".png"), true, false);
    };
    auto ReleaseAll = [this]
    {
        auto Keys = DownKeys;
        for (FKey Down : Keys)
            PressKey(Down, false);
    };
    auto Stand = [this](FVector Where, int32 View, FRotator Look)
    {
        Pawn->SetSimulationEnabled(true);
        ResetPlayer(Where);
        Pawn->SetPerspective(View);
        Controller->SetControlRotation(Look);
        Pawn->ResetBody();
    };
    auto SaveText = [this](const TCHAR* Suffix, const FString& Text)
    { FFileHelper::SaveStringToFile(Text, *(FPaths::ChangeExtension(Output, TEXT("")) + Suffix)); };

    // Visibility: in first person the copy shows (no head), the world body is the owner's shadow only.
    AddCase(
        TEXT("body fp visible"), .5, [Stand, Spot] { Stand(Spot, 0, FRotator(-80, 0, 0)); },
        [this]
        {
            USkeletalMeshComponent* Copy = Pawn->GetFirstPersonBodyMesh();
            const int32 Head = Copy ? Copy->GetMaterialIndex(FirstPersonHeadMaterial) : INDEX_NONE;
            Record(TEXT("body fp: the first-person copy shows"), Pawn->IsFirstPersonBodyShown(), TEXT(""));
            Record(TEXT("body fp: the world body casts the owner's shadow only"),
                Pawn->IsBodyShadowOnly() && Pawn->GetBodyMesh()->CastShadow, TEXT("world-space representation"));
            Record(TEXT("body fp: the copy's head section is off"),
                Copy && Head != INDEX_NONE && !Copy->IsMaterialSectionShown(Head, 0),
                FString::Printf(TEXT("head material %d"), Head));
            Record(TEXT("body fp: the copy is owner-only, shadowless, out of captures"),
                Copy && Copy->bOnlyOwnerSee && !Copy->CastShadow && Copy->bHiddenInSceneCapture &&
                    !Copy->bVisibleInReflectionCaptures,
                TEXT(""));
        });

    // Clearance (PLAN 7: >= 15 cm from the eye to any first-person body surface) over {stand, crouch, low} x pitch x
    // the view's yaw off the body; the simulation is paused so each sample holds its pose. Standing also counts the
    // toe caps' visible vertices looking down (PLAN 7: visible at pitch <= -75, vFOV 70 and 110).
    TSharedRef<FString> Clearance =
        MakeShared<FString>(TEXT("stance,pitch,yaw_offset,eye_z,min_cm,nearest_bone,neck_behind_cm,neck_below_cm,"
                                 "blend,pelvis_back_cm,pelvis_drop_cm,bend_01,bend_02,foot_error_cm\n"));
    TSharedRef<TMap<FString, double>> Worst = MakeShared<TMap<FString, double>>();
    TSharedRef<FString> Toes = MakeShared<FString>(TEXT("pitch,vfov,left_visible,right_visible\n"));
    const double Pitches[] = {-89.9, -60, -30, 0, 30, 60, 89.9};
    const double Offsets[] = {-60, 0, 60};
    struct FStance
    {
        const TCHAR* Name;
        TArray<const TCHAR*> Keys;
        bool bPool;
        double SettleSeconds;
    };
    const FStance Stances[] = {{TEXT("stand"), {}, false, .6}, {TEXT("crouch"), {TEXT("key.sneak")}, false, .8},
        {TEXT("low"), {TEXT("key.forward"), TEXT("key.sprint")}, true, .5}};
    for (const FStance& Stance : Stances)
    {
        const FString Name = Stance.Name;
        AddCase(
            TEXT("body fp settle ") + Name, Stance.SettleSeconds,
            [this, Stand, Key, Stance, Spot, Pool]
            {
                Stand(Stance.bPool ? Pool : Spot, 0, FRotator(-10, 0, 0));
                for (const TCHAR* Id : Stance.Keys)
                    PressKey(Key(Id), true);
            },
            [this, Name]
            {
                // Hold the pose: the eyes and stance stay as they are while the samples turn the view.
                Pawn->SetSimulationEnabled(false);
                Record(TEXT("body fp clearance: ") + Name + TEXT(" pose reached"),
                    Name != TEXT("low") || Pawn->Simulation.bLowPose,
                    FString::Printf(TEXT("eye %.1f cm, low %d"), Pawn->Simulation.EyeCm, Pawn->Simulation.bLowPose));
            });
        TSharedRef<int32> Sample = MakeShared<int32>(0), Wait = MakeShared<int32>(0);
        AddCase(
            TEXT("body fp clearance ") + Name, .1,
            [this, Name, Sample, Wait, Pitches, Offsets, Clearance, Worst, Toes]
            {
                *Sample = 0;
                *Wait = 0;
                bHoldCase = true;
                FrameHook = [this, Name, Sample, Wait, Pitches, Offsets, Clearance, Worst, Toes]
                {
                    const int32 Count = UE_ARRAY_COUNT(Pitches) * UE_ARRAY_COUNT(Offsets);
                    if (*Sample >= Count)
                    {
                        bHoldCase = false;
                        return;
                    }
                    const double Pitch = Pitches[*Sample / UE_ARRAY_COUNT(Offsets)];
                    const double Offset = Offsets[*Sample % UE_ARRAY_COUNT(Offsets)];
                    // Set the view, then let three frames pose and draw it.
                    if ((*Wait)++ == 0)
                    {
                        Controller->SetControlRotation(FRotator(Pitch, 0, 0));
                        Pawn->BodyTurn.Yaw = Pawn->BodyTurn.PreviousYaw = -Offset;
                        return;
                    }
                    if (*Wait < 4) return;
                    *Wait = 0;
                    ++*Sample;
                    const FVector Eye = Pawn->GetCamera()->GetComponentLocation();
                    FBodySurface Surface;
                    if (!GatherBodySurface(Pawn->GetFirstPersonBodyMesh(), true, Surface)) return;
                    int32 Bone = INDEX_NONE;
                    const double Distance = NearestSurface(Surface, Eye, Bone);
                    const auto* Anim =
                        Cast<UFoundationBodyAnimInstance>(Pawn->GetFirstPersonBodyMesh()->GetAnimInstance());
                    const FFoundationFirstPersonResult R =
                        Anim ? Anim->GetFirstPersonResult() : FFoundationFirstPersonResult();
                    *Clearance += FString::Printf(
                        TEXT("%s,%.1f,%.0f,%.2f,%.3f,%s,%.2f,%.2f,%.3f,%.2f,%.2f,%.2f,%.2f,%.4f\n"), *Name, Pitch,
                        Offset, Eye.Z, Distance, *Pawn->GetFirstPersonBodyMesh()->GetBoneName(Bone).ToString(),
                        R.NeckBehindEyeCm, R.NeckBelowEyeCm, R.Blend, R.PelvisBackCm, R.PelvisDropCm, R.SpineBendDeg[0],
                        R.SpineBendDeg[1], R.FootErrorCm);
                    double& Least = Worst->FindOrAdd(Name, TNumericLimits<double>::Max());
                    Least = FMath::Min(Least, Distance);
                    if (Name == TEXT("stand") && Offset == 0 && Pitch <= -60)
                    {
                        const int32 Left = Pawn->GetFirstPersonBodyMesh()->GetBoneIndex(TEXT("ball_l")),
                                    Right = Pawn->GetFirstPersonBodyMesh()->GetBoneIndex(TEXT("ball_r"));
                        int32 Width = 16, Height = 9;
                        Controller->GetViewportSize(Width, Height);
                        const FRotator View = Pawn->GetCamera()->GetComponentRotation();
                        for (const double Fov : {70., 110.})
                            *Toes += FString::Printf(TEXT("%.1f,%.0f,%d,%d\n"), Pitch, Fov,
                                CountVisible(Surface, Left, Eye, View, Fov, double(Width) / FMath::Max(Height, 1)),
                                CountVisible(Surface, Right, Eye, View, Fov, double(Width) / FMath::Max(Height, 1)));
                    }
                };
            },
            [this, Name, Worst, Shot, Clearance, Toes, SaveText]
            {
                const double* Least = Worst->Find(Name);
                Record(TEXT("body fp clearance: ") + Name + TEXT(" >= 15 cm from the eye"), Least && *Least >= 15,
                    Least ? FString::Printf(
                                TEXT("min %.2f cm over 21 views (pitch -89.9..89.9, yaw off -60/0/60)"), *Least)
                          : FString(TEXT("not measured (needs an editor build's imported model)")));
                SaveText(TEXT("-body-fp-clearance.csv"), *Clearance);
                if (Name == TEXT("stand")) SaveText(TEXT("-body-fp-toes.csv"), *Toes);
            });
        // Shots of the pose looking down and ahead, for Jesus.
        for (const double Pitch : {-89.9, -60., 0.})
        {
            const FString ShotName = FString::Printf(TEXT("fp-%s-pitch%d"), *Name, int32(Pitch));
            AddCase(
                TEXT("body fp shot ") + ShotName, .3,
                [this, Pitch]
                {
                    Controller->SetControlRotation(FRotator(Pitch, 0, 0));
                    Pawn->BodyTurn.Yaw = Pawn->BodyTurn.PreviousYaw = 0;
                },
                [Shot, ShotName] { Shot(ShotName); });
        }
        AddCase(
            TEXT("body fp release ") + Name, .3,
            [this, ReleaseAll]
            {
                ReleaseAll();
                Pawn->SetSimulationEnabled(true);
            },
            [] {});
    }
    AddCase(
        TEXT("body fp toes"), .1, [] {},
        [this, Toes]
        {
            // Every row of the toe table at pitch <= -75 (-89.9 here; -60 is reported for scale) needs both feet.
            TArray<FString> Lines;
            Toes->ParseIntoArrayLines(Lines);
            bool bAll = Lines.Num() > 1;
            for (int32 Line = 1; Line < Lines.Num(); ++Line)
            {
                TArray<FString> Cells;
                Lines[Line].ParseIntoArray(Cells, TEXT(","));
                if (Cells.Num() == 4 && FCString::Atod(*Cells[0]) <= -75)
                    bAll &= FCString::Atoi(*Cells[2]) > 0 && FCString::Atoi(*Cells[3]) > 0;
            }
            Record(TEXT("body fp: toe caps visible looking down, vFOV 70 and 110"), bAll,
                Toes->Replace(TEXT("\n"), TEXT("; ")));
        });

    // The head never shows to the owner: both bodies' head material turns magenta for these shots, counted afterwards
    // (Scripts: count the magenta pixels in -body-fp-head-*.png; the front-view control must have some).
    TSharedRef<TArray<TPair<int32, TObjectPtr<UMaterialInterface>>>> Saved =
        MakeShared<TArray<TPair<int32, TObjectPtr<UMaterialInterface>>>>();
    AddCase(
        TEXT("body fp head magenta"), .4,
        [this, Stand, Spot, Saved]
        {
            Stand(Spot, 0, FRotator(0, 0, 0));
            UTexture2D* Magenta = UTexture2D::CreateTransient(1, 1, PF_B8G8R8A8);
            FColor* Texel = static_cast<FColor*>(Magenta->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
            *Texel = FColor(255, 0, 255, 255);
            Magenta->GetPlatformData()->Mips[0].BulkData.Unlock();
            Magenta->Filter = TF_Nearest;
            Magenta->UpdateResource();
            Saved->Reset();
            for (USkeletalMeshComponent* Body : {Pawn->GetBodyMesh(), Pawn->GetFirstPersonBodyMesh()})
            {
                const int32 Head = Body->GetMaterialIndex(FirstPersonHeadMaterial);
                Saved->Add({Head, Body->GetMaterial(Head)});
                UMaterialInstanceDynamic* Dynamic = Body->CreateDynamicMaterialInstance(Head);
                TArray<FMaterialParameterInfo> Infos;
                TArray<FGuid> Ids;
                Dynamic->GetAllTextureParameterInfo(Infos, Ids);
                for (const FMaterialParameterInfo& Info : Infos)
                    Dynamic->SetTextureParameterValueByInfo(Info, Magenta);
            }
        },
        [] {});
    for (const TCHAR* Stance : {TEXT("stand"), TEXT("crouch")})
        for (const double Pitch : {-89.9, -60., -30., 0., 30., 60., 89.9})
        {
            const FString ShotName = FString::Printf(TEXT("fp-head-%s-pitch%d"), Stance, int32(Pitch));
            const bool bCrouch = FString(Stance) == TEXT("crouch");
            AddCase(
                TEXT("body ") + ShotName, bCrouch && Pitch == -89.9 ? .8 : .3,
                [this, Key, Pitch, bCrouch]
                {
                    if (bCrouch && !DownKeys.Contains(Key(TEXT("key.sneak")))) PressKey(Key(TEXT("key.sneak")), true);
                    Controller->SetControlRotation(FRotator(Pitch, 0, 0));
                },
                [Shot, ShotName] { Shot(ShotName); });
        }
    AddCase(
        TEXT("body fp head control"), .4,
        [this, ReleaseAll]
        {
            ReleaseAll();
            Pawn->SetPerspective(2);
            Controller->SetControlRotation(FRotator(0, 0, 0));
        },
        [Shot] { Shot(TEXT("fp-head-control-front")); });
    AddCase(
        TEXT("body fp head restore"), .3,
        [this, Saved]
        {
            USkeletalMeshComponent* Bodies[] = {Pawn->GetBodyMesh(), Pawn->GetFirstPersonBodyMesh()};
            for (int32 Index = 0; Index < Saved->Num(); ++Index)
                Bodies[Index]->SetMaterial((*Saved)[Index].Key, (*Saved)[Index].Value);
            Pawn->SetPerspective(0);
        },
        [] {});

    // The shadow: in first person at a low sun behind the player, the ground shows one body shadow, head included.
    TSharedRef<FRotator> SunBefore = MakeShared<FRotator>();
    TSharedRef<TWeakObjectPtr<ADirectionalLight>> Sun = MakeShared<TWeakObjectPtr<ADirectionalLight>>();
    AddCase(
        TEXT("body fp shadow"), .8,
        [this, Stand, Spot, SunBefore, Sun]
        {
            Stand(Spot, 0, FRotator(-35, 0, 0));
            for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
            {
                *Sun = *It;
                *SunBefore = It->GetActorRotation();
                // 15 degrees up, shining east (from behind the player): the shadow falls ahead, about 7 body
                // heights long.
                It->SetActorRotation(FRotator(-15, 0, 0));
                break;
            }
        },
        [this, Shot, Sun]
        {
            Shot(TEXT("fp-shadow-low-sun"));
            Record(TEXT("body fp shadow: shot at a low sun (check the head's shadow by eye)"),
                Sun->IsValid() && Pawn->IsBodyShadowOnly() && Pawn->IsFirstPersonBodyShown(),
                Sun->IsValid() ? TEXT("directional light at pitch -15, yaw 0") : TEXT("no directional light"));
        });
    AddCase(
        TEXT("body fp shadow restore"), .2,
        [SunBefore, Sun]
        {
            if (Sun->IsValid()) (*Sun)->SetActorRotation(*SunBefore);
        },
        [] {});

    // View switching: 20 F5 cycles (one switch a frame) while jumping: every frame exactly one body is drawn for the
    // owner, and it is posed (never the reference pose).
    struct FSwitching
    {
        int32 Frames = 0, Bad = 0, Airborne = 0, Toggles = 0;
        double LeastPosed = TNumericLimits<double>::Max();
        FString FirstBad;
    };
    TSharedRef<FSwitching> Switching = MakeShared<FSwitching>();
    AddCase(
        TEXT("body fp switching"), .1,
        [this, Stand, Key, Spot, Switching]
        {
            Stand(Spot, 0, FRotator(-30, 0, 0));
            *Switching = FSwitching();
            PressKey(Key(TEXT("key.jump")), true);
            bHoldCase = true;
            FrameHook = [this, Switching]
            {
                // Settle into the first jump before switching.
                if (++Switching->Frames < 6) return;
                const int32 View = Pawn->GetPerspective();
                const bool bCopy = Pawn->IsFirstPersonBodyShown(), bWorld = Pawn->IsBodyShown();
                const bool bRight = View == 0 ? bCopy && !bWorld && Pawn->IsBodyShadowOnly() : bWorld && !bCopy;
                const double Posed =
                    DistanceFromReferencePose(View == 0 ? Pawn->GetFirstPersonBodyMesh() : Pawn->GetBodyMesh());
                Switching->LeastPosed = FMath::Min(Switching->LeastPosed, Posed);
                if (!bRight || Posed < 1)
                {
                    if (!Switching->Bad++)
                        Switching->FirstBad =
                            FString::Printf(TEXT("view %d copy %d world %d posed %.2f cm"), View, bCopy, bWorld, Posed);
                }
                Switching->Airborne += Pawn->Simulation.bGrounded ? 0 : 1;
                if (Switching->Toggles == 40)
                {
                    bHoldCase = false;
                    return;
                }
                Pawn->SetPerspective(View == 0 ? 1 : 0);
                ++Switching->Toggles;
            };
        },
        [this, ReleaseAll, Switching]
        {
            ReleaseAll();
            Pawn->SetPerspective(0);
            Record(TEXT("body fp switching: one body drawn every frame, never the reference pose (20 F5 cycles)"),
                Switching->Toggles == 40 && Switching->Bad == 0,
                FString::Printf(TEXT("%d switches over %d frames, %d of them airborne; least posed %.1f cm from the "
                                     "reference pose; %d bad%s"),
                    Switching->Toggles, Switching->Frames - 5, Switching->Airborne, Switching->LeastPosed,
                    Switching->Bad, Switching->Bad ? *(TEXT(": ") + Switching->FirstBad) : TEXT("")));
        });

    // Planted feet in first person (the corrections' leg re-solve included): walking east looking 60 degrees down,
    // at the run's frame rate, then with the frame time jittered between 16.7 and 33 ms; and the world body jittered.
    struct FFeetRun
    {
        TArray<FTransform> Component;
        TArray<FVector> Ball[2];
        double MinMs = 1e9, MaxMs = 0;
    };
    const struct
    {
        const TCHAR* Name;
        int32 View;
        bool bJitter;
    } FeetRuns[] = {{TEXT("fp"), 0, false}, {TEXT("fp-jitter"), 0, true}, {TEXT("back-jitter"), 1, true}};
    for (const auto& FeetCase : FeetRuns)
    {
        const FString Name = FeetCase.Name;
        const int32 View = FeetCase.View;
        const bool bJitter = FeetCase.bJitter;
        TSharedRef<FFeetRun> Run = MakeShared<FFeetRun>();
        TSharedRef<TPair<int32, int32>> Parallel = MakeShared<TPair<int32, int32>>(1, 1);
        AddCase(
            TEXT("body feet ") + Name, 3,
            [this, Stand, Key, Spot, Run, Parallel, View, bJitter]
            {
                Stand(Spot, View, FRotator(-60, 0, 0));
                PressKey(Key(TEXT("key.forward")), true);
                *Run = FFeetRun();
                USkeletalMeshComponent* Mesh = View == 0 ? Pawn->GetFirstPersonBodyMesh() : Pawn->GetBodyMesh();
                AddTickPrerequisiteComponent(Mesh);
                Parallel->Key = SetFirstPersonProbeCVar(TEXT("a.ParallelAnimEvaluation"), 0);
                Parallel->Value = SetFirstPersonProbeCVar(TEXT("a.ParallelAnimUpdate"), 0);
                FrameHook = [this, Run, Mesh, bJitter]
                {
                    // A fixed sequence of caps between 30 and 60 fps: 16.7-33 ms frames.
                    static const int32 Caps[] = {60, 30, 45, 60, 35, 50, 30, 60, 40, 55};
                    if (bJitter) SetFirstPersonProbeCVar(TEXT("t.MaxFPS"), Caps[Frames % UE_ARRAY_COUNT(Caps)]);
                    if (FPlatformTime::Seconds() - PhaseStarted < 1) return;
                    const double Ms = GetWorld()->GetDeltaSeconds() * 1000.;
                    Run->MinMs = FMath::Min(Run->MinMs, Ms);
                    Run->MaxMs = FMath::Max(Run->MaxMs, Ms);
                    Run->Component.Add(Mesh->GetComponentTransform());
                    Run->Ball[0].Add(
                        Mesh->GetBoneTransform(Mesh->GetBoneIndex(TEXT("ball_l")), FTransform::Identity).GetLocation());
                    Run->Ball[1].Add(
                        Mesh->GetBoneTransform(Mesh->GetBoneIndex(TEXT("ball_r")), FTransform::Identity).GetLocation());
                };
            },
            [this, ReleaseAll, Run, Parallel, Name, View, SaveText]
            {
                ReleaseAll();
                SetFirstPersonProbeCVar(TEXT("t.MaxFPS"), RequestedFPS);
                RemoveTickPrerequisiteComponent(View == 0 ? Pawn->GetFirstPersonBodyMesh() : Pawn->GetBodyMesh());
                SetFirstPersonProbeCVar(TEXT("a.ParallelAnimEvaluation"), Parallel->Key);
                SetFirstPersonProbeCVar(TEXT("a.ParallelAnimUpdate"), Parallel->Value);
                // A plant: consecutive frames with the ball within 0.5 cm of its lowest (FoundationBodyProbe's rule).
                double Worst = 0;
                int32 Runs = 0;
                FString Csv = TEXT("frame,ball_l_x,ball_l_y,ball_l_z,ball_r_x,ball_r_y,ball_r_z\n");
                TArray<FVector> World[2];
                for (int32 Foot = 0; Foot < 2; ++Foot)
                    for (int32 Index = 0; Index < Run->Component.Num(); ++Index)
                        World[Foot].Add(Run->Component[Index].TransformPosition(Run->Ball[Foot][Index]));
                for (int32 Index = 0; Index < Run->Component.Num(); ++Index)
                    Csv += FString::Printf(TEXT("%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n"), Index, World[0][Index].X,
                        World[0][Index].Y, World[0][Index].Z, World[1][Index].X, World[1][Index].Y, World[1][Index].Z);
                for (int32 Foot = 0; Foot < 2; ++Foot)
                {
                    double Lowest = TNumericLimits<double>::Max();
                    for (const FVector& Point : World[Foot])
                        Lowest = FMath::Min(Lowest, Point.Z);
                    int32 Start = INDEX_NONE;
                    for (int32 Index = 0; Index <= World[Foot].Num(); ++Index)
                    {
                        const bool bPlanted = Index < World[Foot].Num() && World[Foot][Index].Z < Lowest + .5;
                        if (bPlanted && Start == INDEX_NONE) Start = Index;
                        if (!bPlanted && Start != INDEX_NONE)
                        {
                            if (Index - Start >= 2)
                            {
                                ++Runs;
                                for (int32 Other = Start; Other < Index; ++Other)
                                    Worst = FMath::Max(Worst, FVector::Dist2D(World[Foot][Start], World[Foot][Other]));
                            }
                            Start = INDEX_NONE;
                        }
                    }
                }
                SaveText(*(TEXT("-body-feet-") + Name + TEXT(".csv")), Csv);
                Record(TEXT("body feet ") + Name + TEXT(": planted drift <= 1 cm (target 0.5)"),
                    Worst <= 1 && Runs >= 3,
                    FString::Printf(TEXT("%d frames (%.1f-%.1f ms); max planted drift %.3f cm over %d plants"),
                        Run->Component.Num(), Run->MinMs, Run->MaxMs, Worst, Runs));
            });
    }

    // The simulation does not see the body: the same scripted 30 s path (600 steps, the view switching every 50
    // steps) with Better Steve and with the Unnamed Player gives bit-identical feet, eyes and velocity at every step.
    TSharedRef<TArray<FStepRecord>> Records[2] = {MakeShared<TArray<FStepRecord>>(), MakeShared<TArray<FStepRecord>>()};
    TSharedRef<int32> MobsBefore = MakeShared<int32>(1);
    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        TSharedRef<TArray<FStepRecord>> Steps = Records[Pass];
        AddCase(
            FString::Printf(TEXT("body sim path set %d"), Pass), .1,
            [this, Stand, Spot, Steps, Pass, MobsBefore]
            {
                if (Pass == 0) *MobsBefore = SetFirstPersonProbeCVar(TEXT("foundation.Mobs"), 0);
                SetFirstPersonProbeCVar(TEXT("foundation.Body.Set"), Pass);
                Stand(Spot, 0, FRotator(-20, 0, 0));
                Steps->Reset();
                TSharedRef<int64> First = MakeShared<int64>(-1);
                Pawn->ScriptedStep = [this, Steps, First](int64 Step, FFoundationIntent& Intent, double& ViewYaw)
                {
                    if (*First < 0) *First = Step;
                    const int64 Index = Step - *First;
                    // The state the previous step left.
                    if (Index > 0)
                        Steps->Add({Pawn->Simulation.Feet, Pawn->Simulation.VelocityPerTick, Pawn->Simulation.EyeCm});
                    ScriptStep(Index, Intent, ViewYaw);
                    Pawn->SetPerspective(int32(Index / 50) % 3);
                };
                bHoldCase = true;
                FrameHook = [this, Steps]
                {
                    if (Steps->Num() >= 600) bHoldCase = false;
                };
            },
            [] {});
        // MaxHoldSeconds (10 s) is shorter than the path: wait out the rest in further cases, then stop the script.
        for (int32 Wait = 0; Wait < 3; ++Wait)
            AddCase(
                FString::Printf(TEXT("body sim path set %d wait %d"), Pass, Wait), .1,
                [this, Steps]
                {
                    if (Steps->Num() >= 600) return;
                    bHoldCase = true;
                    FrameHook = [this, Steps]
                    {
                        if (Steps->Num() >= 600) bHoldCase = false;
                    };
                },
                [] {});
        AddCase(
            FString::Printf(TEXT("body sim path set %d done"), Pass), .1,
            [this]
            {
                Pawn->ScriptedStep = nullptr;
                Pawn->SetPerspective(0);
            },
            [this, Steps, Pass]
            {
                Record(FString::Printf(TEXT("body sim path set %d: 600 steps recorded"), Pass), Steps->Num() >= 600,
                    FString::Printf(TEXT("%d steps; last feet %s"), Steps->Num(),
                        Steps->Num() ? *Steps->Last().Feet.ToCompactString() : TEXT("-")));
            });
    }
    AddCase(
        TEXT("body sim path compare"), .1,
        [this, MobsBefore]
        {
            Pawn->ScriptedStep = nullptr;
            SetFirstPersonProbeCVar(TEXT("foundation.Mobs"), *MobsBefore);
            SetFirstPersonProbeCVar(TEXT("foundation.Body.Set"), 1);
        },
        [this, Records]
        {
            const TArray<FStepRecord>& A = *Records[0];
            const TArray<FStepRecord>& B = *Records[1];
            const int32 Count = FMath::Min3(A.Num(), B.Num(), 600);
            int32 FirstDiff = INDEX_NONE;
            for (int32 Index = 0; Index < Count && FirstDiff == INDEX_NONE; ++Index)
                if (!(A[Index] == B[Index])) FirstDiff = Index;
            Record(TEXT("body sim path: bit-identical with Better Steve and the Unnamed Player"),
                Count == 600 && FirstDiff == INDEX_NONE,
                FirstDiff == INDEX_NONE
                    ? FString::Printf(TEXT("%d steps equal; end %s"), Count, *A[Count - 1].Feet.ToCompactString())
                    : FString::Printf(TEXT("step %d differs: %s vs %s"), FirstDiff,
                          *A[FirstDiff].Feet.ToCompactString(), *B[FirstDiff].Feet.ToCompactString()));
        });

    // Cost: three alternating 3 s walks in first person with each set, uncapped (their phase rates hold the game,
    // render and GPU times); the evaluation time per body from the anim instances.
    TSharedRef<FString> Cost = MakeShared<FString>(TEXT("run,set,world_evals,world_ms_each,fp_evals,fp_ms_each\n"));
    for (int32 Run = 0; Run < 6; ++Run)
    {
        const int32 Set = Run % 2;
        AddCase(
            FString::Printf(TEXT("body perf set %d run %d"), Set, Run / 2), 3,
            [this, Stand, Key, Spot, Set]
            {
                SetFirstPersonProbeCVar(TEXT("foundation.Body.Set"), Set);
                Stand(Spot, 0, FRotator(-45, 0, 0));
                SetFirstPersonProbeCVar(TEXT("t.MaxFPS"), 0);
                PressKey(Key(TEXT("key.forward")), true);
                UFoundationBodyAnimInstance::ResetEvaluationStats();
            },
            [this, ReleaseAll, Cost, Run, Set]
            {
                ReleaseAll();
                SetFirstPersonProbeCVar(TEXT("t.MaxFPS"), RequestedFPS);
                int64 Counts[2];
                double Ms[2];
                for (int32 Mode = 0; Mode < 2; ++Mode)
                    UFoundationBodyAnimInstance::GetEvaluationStats(Mode, Counts[Mode], Ms[Mode]);
                *Cost += FString::Printf(TEXT("%d,%d,%lld,%.4f,%lld,%.4f\n"), Run / 2, Set, Counts[0],
                    Counts[0] ? Ms[0] / Counts[0] : 0., Counts[1], Counts[1] ? Ms[1] / Counts[1] : 0.);
            });
    }
    AddCase(
        TEXT("body perf report"), .1, [this] { SetFirstPersonProbeCVar(TEXT("foundation.Body.Set"), 1); },
        [this, Cost, SaveText]
        {
            SaveText(TEXT("-body-perf.csv"), *Cost);
            // The per-body evaluation row (PLAN 7: <= 0.10 ms); the frame delta is read from the phase rates.
            TArray<FString> Lines;
            Cost->ParseIntoArrayLines(Lines);
            double Most = 0;
            for (int32 Line = 1; Line < Lines.Num(); ++Line)
            {
                TArray<FString> Cells;
                Lines[Line].ParseIntoArray(Cells, TEXT(","));
                if (Cells.Num() == 6 && Cells[1] == TEXT("1"))
                    Most = FMath::Max3(Most, FCString::Atod(*Cells[3]), FCString::Atod(*Cells[5]));
            }
            Record(TEXT("body perf: <= 0.10 ms per body evaluation"), Most > 0 && Most <= .1,
                FString::Printf(TEXT("worst run's mean %.4f ms (world and first-person bodies)"), Most));
        });

    // Better Steve stays the fallback: foundation.Body.Set 0 hides the body in first person and plays its clips.
    AddCase(
        TEXT("body set 0"), 1,
        [this, Stand, Spot]
        {
            SetFirstPersonProbeCVar(TEXT("foundation.Body.Set"), 0);
            Stand(Spot, 1, FRotator(-10, 0, 0));
        },
        [this, Shot]
        {
            Shot(TEXT("set0-back-idle"));
            Record(TEXT("body set 0: Better Steve in the back view, no first-person copy"),
                Pawn->GetBodySet() == EFoundationBodySet::BetterSteve && Pawn->IsBodyShown() &&
                    !Pawn->GetFirstPersonBodyMesh() &&
                    Pawn->BodyAnimator.Pose.GetWeight(FoundationBodyClip::Idle) > .99f,
                FoundationBodyClip::GetMeshName(Pawn->GetBodySet()));
        });
    AddCase(
        TEXT("body set 0 first person"), .4, [this] { Pawn->SetPerspective(0); },
        [this]
        {
            Record(TEXT("body set 0: nothing in first person"), !Pawn->IsBodyShown() && !Pawn->IsBodyShadowOnly(),
                TEXT(""));
            SetFirstPersonProbeCVar(TEXT("foundation.Body.Set"), 1);
        });
    AddCase(
        TEXT("body set 1 back"), .5, [this] { Pawn->SetPerspective(0); },
        [this]
        {
            Record(TEXT("body set 1: the Unnamed Player again"),
                Pawn->GetBodySet() == EFoundationBodySet::UnnamedPlayer && Pawn->IsFirstPersonBodyShown(), TEXT(""));
        });
#endif
}
