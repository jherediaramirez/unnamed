#include "Foundation/FoundationBodyAnimInstance.h"
#include "UnnamedLog.h"
#include "UnnamedProjectSettings.h"
#include "AnimationRuntime.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"

namespace
{
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
        Clips.Reset(Instance->Clips.Num());
        for (const TObjectPtr<UAnimSequence>& Clip : Instance->Clips)
            Clips.Add(Clip.Get());
    }

    virtual bool Evaluate(FPoseContext& Output) override
    {
        Output.ResetToRefPose();
        if (!SampleBodyLayers(Pose.Base, Clips, Output.Pose, Output.Curve, Output.CustomAttributes)) return true;
        FPoseContext Look(this, true);
        Look.ResetToAdditiveIdentity();
        if (SampleBodyLayers(Pose.Look, Clips, Look.Pose, Look.Curve, Look.CustomAttributes))
        {
            FAnimationPoseData Base(Output);
            const FAnimationPoseData Additive(Look);
            FAnimationRuntime::AccumulateMeshSpaceRotationAdditiveToLocalPose(Base, Additive, 1.f);
            Output.Pose.NormalizeRotations();
        }
        return true;
    }

    FFoundationBodyPose Pose;
    TArray<const UAnimSequence*> Clips;
};

TArray<double> UFoundationBodyAnimInstance::GetClipLengths() const
{
    TArray<double> Lengths;
    for (int32 Clip = 0; Clip < FoundationBodyClip::Count; ++Clip)
        Lengths.Add(Clips.IsValidIndex(Clip) && Clips[Clip] ? Clips[Clip]->GetPlayLength()
                                                            : FoundationBodyClip::GetDefaultLength(Clip));
    return Lengths;
}

void UFoundationBodyAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    Clips.Reset();
    TArray<FString> Missing;
    for (int32 Clip = 0; Clip < FoundationBodyClip::Count; ++Clip)
    {
        const FString Name = FoundationBodyClip::GetName(Clip);
        auto* Sequence = Cast<UAnimSequence>(UUnnamedProjectSettings::GetPlayerBodyAssetPath(Name).TryLoad());
        if (!Sequence) Missing.Add(Name);
        Clips.Add(Sequence);
    }
    bHasClips = Missing.IsEmpty();
    if (!bHasClips)
        UE_LOG(LogUnnamedFoundation, Warning, TEXT("Player body: %d clips missing under %s: %s"), Missing.Num(),
            *UUnnamedProjectSettings::Get().PlayerBodyRoot, *FString::Join(Missing, TEXT(", ")));
}

FAnimInstanceProxy* UFoundationBodyAnimInstance::CreateAnimInstanceProxy()
{
    return new FFoundationBodyAnimProxy(this);
}

void UFoundationBodyAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
    delete static_cast<FFoundationBodyAnimProxy*>(InProxy);
}
