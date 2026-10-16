#include "Foundation/FoundationBodyAnimInstance.h"
#include "UnnamedLog.h"
#include "AnimationRuntime.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include <atomic>

namespace
{
    // Evaluation cost per view mode (world 0, first person 1), from the animation worker (cycles).
    std::atomic<int64> BodyEvaluations[2] = {0, 0}, BodyEvaluationCycles[2] = {0, 0};

    // Blends the layers' clips into the output (the layers' weights sum to 1); false when a clip is missing.
    template <typename FLayers>
    bool SampleBodyLayers(const FLayers& Layers, const TArray<const UAnimSequence*>& Clips, FCompactPose& OutPose,
        FBlendedCurve& OutCurve, UE::Anim::FStackAttributeContainer& OutAttributes)
    {
        const int32 Count = Layers.Num();
        if (Count == 0) return false;
        TArray<FCompactPose, TInlineAllocator<12>> Poses;
        Poses.AddZeroed(Count);
        TArray<FBlendedCurve, TInlineAllocator<12>> Curves;
        Curves.AddZeroed(Count);
        TArray<UE::Anim::FStackAttributeContainer, TInlineAllocator<12>> Attributes;
        Attributes.AddZeroed(Count);
        TArray<float, TInlineAllocator<12>> Weights;
        Weights.AddZeroed(Count);
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const FFoundationBodyLayer& Layer = Layers[Index];
            const UAnimSequence* Clip = Clips.IsValidIndex(Layer.Clip) ? Clips[Layer.Clip] : nullptr;
            if (!Clip) return false;
            Poses[Index].SetBoneContainer(&OutPose.GetBoneContainer());
            Curves[Index].InitFrom(OutCurve);
            FAnimationPoseData PoseData(Poses[Index], Curves[Index], Attributes[Index]);
            Clip->GetAnimationPose(
                PoseData, FAnimExtractContext(double(FMath::Clamp(Layer.Time, 0.f, Clip->GetPlayLength()))));
            Weights[Index] = Layer.Weight;
        }
        FAnimationPoseData Out(OutPose, OutCurve, OutAttributes);
        FAnimationRuntime::BlendPosesTogether(TArrayView<const FCompactPose>(Poses),
            TArrayView<const FBlendedCurve>(Curves), TArrayView<const UE::Anim::FStackAttributeContainer>(Attributes),
            TArrayView<const float>(Weights), Out);
        OutPose.NormalizeRotations();
        return true;
    }

    // A compact pose as a parent-ordered bone array (FoundationBodyPose's form), and its bones by name.
    struct FBoneArrays
    {
        explicit FBoneArrays(const FCompactPose& Pose) : Container(Pose.GetBoneContainer())
        {
            const int32 Count = Container.GetCompactPoseNumBones();
            Local.SetNum(Count);
            Parents.SetNum(Count);
            for (int32 Index = 0; Index < Count; ++Index)
            {
                const FCompactPoseBoneIndex Bone(Index);
                Local[Index] = Pose[Bone];
                Parents[Index] = Container.GetParentBoneIndex(Bone).GetInt();
            }
        }
        int32 Find(const TCHAR* Name) const
        {
            const int32 MeshIndex = Container.GetPoseBoneIndexForBoneName(FName(Name));
            return MeshIndex == INDEX_NONE ? INDEX_NONE
                                           : Container.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex)).GetInt();
        }
        void WriteTo(FCompactPose& Pose) const
        {
            for (int32 Index = 0; Index < Local.Num(); ++Index)
                Pose[FCompactPoseBoneIndex(Index)] = Local[Index];
        }
        const FBoneContainer& Container;
        TArray<FTransform, TInlineAllocator<32>> Local;
        TArray<int32, TInlineAllocator<32>> Parents;
    };

    // PLAN 5's action mask: spine_01 half and spine_02 whole in component space, the neck, head, clavicles, arms and
    // hands whole; the pelvis and legs keep the gait.
    void BlendActionLayer(FCompactPose& Pose, const FCompactPose& Action, float Weight)
    {
        FBoneArrays Bones(Pose);
        const FBoneArrays Acting(Action);
        TArray<float, TInlineAllocator<32>> Weights;
        TArray<bool, TInlineAllocator<32>> MeshSpace;
        Weights.Init(0.f, Bones.Local.Num());
        MeshSpace.Init(false, Bones.Local.Num());
        auto Mask = [&](const TCHAR* Name, float Share, bool bMeshSpace)
        {
            const int32 Bone = Bones.Find(Name);
            if (Bone == INDEX_NONE) return;
            Weights[Bone] = Share * Weight;
            MeshSpace[Bone] = bMeshSpace;
        };
        Mask(TEXT("spine_01"), .5f, true);
        Mask(TEXT("spine_02"), 1, true);
        for (const TCHAR* Name :
            {TEXT("neck_01"), TEXT("head"), TEXT("clavicle_l"), TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"),
                TEXT("clavicle_r"), TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r")})
            Mask(Name, 1, false);
        FoundationBodyPose::BlendUpperBody(Bones.Local, Acting.Local, Bones.Parents, Weights, MeshSpace);
        Bones.WriteTo(Pose);
    }

    void ApplyChestAim(FCompactPose& Pose, double PitchDeg, double YawDeg)
    {
        FBoneArrays Bones(Pose);
        FoundationBodyPose::ApplyAim(Bones.Local, Bones.Parents, Bones.Find(TEXT("spine_02")), PitchDeg, YawDeg);
        Bones.WriteTo(Pose);
    }

    // The first-person corrections on a compact pose: its bones as a parent-ordered array, FoundationBodyPose::Apply,
    // then back.
    FFoundationFirstPersonResult ApplyFirstPerson(
        FCompactPose& Pose, const FFoundationFirstPersonView& View, const FFoundationFirstPersonTuning& Tuning)
    {
        FBoneArrays Arrays(Pose);
        TArray<FTransform, TInlineAllocator<32>>& Local = Arrays.Local;
        const TArray<int32, TInlineAllocator<32>>& Parents = Arrays.Parents;
        auto Find = [&Arrays](const TCHAR* Name) { return Arrays.Find(Name); };
        FoundationBodyPose::FBones Bones;
        Bones.Pelvis = Find(TEXT("pelvis"));
        Bones.Spine[0] = Find(TEXT("spine_01"));
        Bones.Spine[1] = Find(TEXT("spine_02"));
        Bones.Neck = Find(TEXT("neck_01"));
        const TCHAR* const Sides[2] = {TEXT("l"), TEXT("r")};
        for (int32 Side = 0; Side < 2; ++Side)
        {
            Bones.Thigh[Side] = Find(*(FString(TEXT("thigh_")) + Sides[Side]));
            Bones.Calf[Side] = Find(*(FString(TEXT("calf_")) + Sides[Side]));
            Bones.Foot[Side] = Find(*(FString(TEXT("foot_")) + Sides[Side]));
        }
        const FFoundationFirstPersonResult Result = FoundationBodyPose::Apply(Local, Parents, Bones, View, Tuning);
        Arrays.WriteTo(Pose);
        return Result;
    }
}

// Copies the game thread's pose and clips before each update; evaluates on the animation worker.
struct FFoundationBodyAnimProxy : public FAnimInstanceProxy
{
    explicit FFoundationBodyAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

    virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override
    {
        FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
        const auto* Instance = CastChecked<UFoundationBodyAnimInstance>(InAnimInstance);
        Pose = Instance->Pose;
        bFirstPerson = Instance->bFirstPerson;
        View = Instance->FirstPersonView;
        Tuning = Instance->FirstPersonTuning;
        Clips.Reset(Instance->Clips.Num());
        for (const TObjectPtr<UAnimSequence>& Clip : Instance->Clips)
            Clips.Add(Clip.Get());
    }

    virtual bool Evaluate(FPoseContext& Output) override
    {
        const uint64 Start = FPlatformTime::Cycles64();
        Output.ResetToRefPose();
        if (SampleBodyLayers(Pose.Base, Clips, Output.Pose, Output.Curve, Output.CustomAttributes))
        {
            // The upper body's action (the pickaxe hold and strikes) over the gait.
            const float Action = Pose.ActionWeight;
            if (Action > 0)
            {
                FPoseContext Acting(this);
                Acting.ResetToRefPose();
                if (SampleBodyLayers(Pose.Action, Clips, Acting.Pose, Acting.Curve, Acting.CustomAttributes))
                    BlendActionLayer(Output.Pose, Acting.Pose, Action);
            }
            if (bFirstPerson)
            {
                // The first-person copy: no head look (the head is hidden; its pitch would bend the spine); the aim,
                // then the corrections.
                ApplyChestAim(Output.Pose, Pose.AimPitchDeg * Action, Pose.AimYawDeg * Action);
                Result = ApplyFirstPerson(Output.Pose, View, Tuning);
            }
            else
            {
                FPoseContext Look(this, true);
                Look.ResetToAdditiveIdentity();
                if (SampleBodyLayers(Pose.Look, Clips, Look.Pose, Look.Curve, Look.CustomAttributes))
                {
                    // During an action the aim, not the look, turns the spine (PLAN 5: the two never stack).
                    if (Action > 0)
                        for (const TCHAR* Name : {TEXT("spine_01"), TEXT("spine_02")})
                        {
                            const int32 MeshIndex =
                                Look.Pose.GetBoneContainer().GetPoseBoneIndexForBoneName(FName(Name));
                            if (MeshIndex == INDEX_NONE) continue;
                            FTransform& Bone = Look.Pose[Look.Pose.GetBoneContainer().MakeCompactPoseIndex(
                                FMeshPoseBoneIndex(MeshIndex))];
                            Bone.SetRotation(FQuat::Slerp(FQuat::Identity, Bone.GetRotation(), 1 - Action));
                        }
                    FAnimationPoseData Base(Output);
                    const FAnimationPoseData Additive(Look);
                    FAnimationRuntime::AccumulateMeshSpaceRotationAdditiveToLocalPose(Base, Additive, 1.f);
                    Output.Pose.NormalizeRotations();
                }
                ApplyChestAim(Output.Pose, Pose.AimPitchDeg * Action, Pose.AimYawDeg * Action);
            }
        }
        const int32 Mode = bFirstPerson ? 1 : 0;
        BodyEvaluations[Mode].fetch_add(1, std::memory_order_relaxed);
        BodyEvaluationCycles[Mode].fetch_add(int64(FPlatformTime::Cycles64() - Start), std::memory_order_relaxed);
        return true;
    }

    virtual void PostEvaluate(UAnimInstance* InAnimInstance) override
    {
        FAnimInstanceProxy::PostEvaluate(InAnimInstance);
        auto* Instance = CastChecked<UFoundationBodyAnimInstance>(InAnimInstance);
        FScopeLock Lock(&Instance->ResultLock);
        Instance->FirstPersonResult = Result;
    }

    FFoundationBodyPose Pose;
    FFoundationFirstPersonView View;
    FFoundationFirstPersonTuning Tuning;
    FFoundationFirstPersonResult Result;
    TArray<const UAnimSequence*> Clips;
    bool bFirstPerson = false;
};

TArray<double> UFoundationBodyAnimInstance::GetClipLengths() const
{
    TArray<double> Lengths;
    for (int32 Clip = 0; Clip < FoundationBodyClip::Count; ++Clip)
        Lengths.Add(Clips.IsValidIndex(Clip) && Clips[Clip] ? Clips[Clip]->GetPlayLength()
                                                            : FoundationBodyClip::GetDefaultLength(Clip, Set));
    return Lengths;
}

void UFoundationBodyAnimInstance::SetFirstPerson(
    const FFoundationFirstPersonView& InView, const FFoundationFirstPersonTuning& InTuning)
{
    bFirstPerson = true;
    FirstPersonView = InView;
    FirstPersonTuning = InTuning;
}

FFoundationFirstPersonResult UFoundationBodyAnimInstance::GetFirstPersonResult() const
{
    FScopeLock Lock(&ResultLock);
    return FirstPersonResult;
}

void UFoundationBodyAnimInstance::GetEvaluationStats(int32 Mode, int64& OutCount, double& OutMilliseconds)
{
    OutCount = BodyEvaluations[Mode].load();
    OutMilliseconds = FPlatformTime::ToMilliseconds64(uint64(BodyEvaluationCycles[Mode].load()));
}

void UFoundationBodyAnimInstance::ResetEvaluationStats()
{
    for (int32 Mode = 0; Mode < 2; ++Mode)
    {
        BodyEvaluations[Mode] = 0;
        BodyEvaluationCycles[Mode] = 0;
    }
}

void UFoundationBodyAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    // The set is the mesh's: the Unnamed Player's mesh plays the Unnamed Player's clips.
    const USkeletalMeshComponent* Component = GetSkelMeshComponent();
    const USkeletalMesh* Mesh = Component ? Component->GetSkeletalMeshAsset() : nullptr;
    Set = Mesh && Mesh->GetName() == FoundationBodyClip::GetMeshName(EFoundationBodySet::UnnamedPlayer)
        ? EFoundationBodySet::UnnamedPlayer
        : EFoundationBodySet::BetterSteve;
    Clips.Reset();
    TArray<FString> Missing;
    for (int32 Clip = 0; Clip < FoundationBodyClip::Count; ++Clip)
    {
        const FString Name = FoundationBodyClip::GetName(Clip, Set);
        UAnimSequence* Sequence = nullptr;
        if (!Name.IsEmpty())
        {
            Sequence = Cast<UAnimSequence>(FoundationBodyClip::GetAssetPath(Set, Name).TryLoad());
            if (!Sequence) Missing.Add(Name);
        }
        Clips.Add(Sequence);
    }
    bHasClips = Missing.IsEmpty();
    if (!bHasClips)
        UE_LOG(LogUnnamedFoundation, Warning, TEXT("Player body: %d clips missing for %s: %s"), Missing.Num(),
            *FoundationBodyClip::GetMeshName(Set), *FString::Join(Missing, TEXT(", ")));
}

FAnimInstanceProxy* UFoundationBodyAnimInstance::CreateAnimInstanceProxy()
{
    return new FFoundationBodyAnimProxy(this);
}

void UFoundationBodyAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
    delete static_cast<FFoundationBodyAnimProxy*>(InProxy);
}
