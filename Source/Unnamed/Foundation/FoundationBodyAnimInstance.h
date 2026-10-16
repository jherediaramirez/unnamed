#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Foundation/FoundationBody.h"
#include "Foundation/FoundationBodyPose.h"
#include "FoundationBodyAnimInstance.generated.h"

class UAnimSequence;

// The player body's animation in native code, no Animation Blueprint: it samples the clips FFoundationBodyAnimator
// picked (blended by weight), then adds the head look (mesh-space additive look poses). The clips load from the root of
// the set its mesh belongs to (FoundationBodyClip::GetAssetPath) when the instance initializes. The first-person copy
// (SetFirstPerson) drops the look and runs FoundationBodyPose's corrections instead.
UCLASS(Transient, NotBlueprintable)
class UNNAMED_API UFoundationBodyAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    // True when every clip of the set loaded (missing ones are logged).
    bool HasClips() const { return bHasClips; }
    EFoundationBodySet GetSet() const { return Set; }
    // Each clip's play length (s), for FFoundationBodyAnimator::SetLengths; 0 for a clip the set lacks.
    TArray<double> GetClipLengths() const;
    UAnimSequence* GetClip(int32 Clip) const { return Clips.IsValidIndex(Clip) ? Clips[Clip].Get() : nullptr; }
    void SetPose(const FFoundationBodyPose& InPose) { Pose = InPose; }
    // The first-person copy: this frame's view in component space (the corrections' input).
    void SetFirstPerson(const FFoundationFirstPersonView& InView, const FFoundationFirstPersonTuning& InTuning);
    bool IsFirstPerson() const { return bFirstPerson; }
    // The last evaluated frame's corrections (first-person copy only).
    FFoundationFirstPersonResult GetFirstPersonResult() const;

    // Evaluation cost per body (the probe's perf row): the world bodies (0) and first-person bodies (1) since the last
    // reset, from the animation worker.
    static void GetEvaluationStats(int32 Mode, int64& OutCount, double& OutMilliseconds);
    static void ResetEvaluationStats();

protected:
    virtual void NativeInitializeAnimation() override;
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
    friend struct FFoundationBodyAnimProxy;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UAnimSequence>> Clips;
    FFoundationBodyPose Pose;
    FFoundationFirstPersonView FirstPersonView;
    FFoundationFirstPersonTuning FirstPersonTuning;
    FFoundationFirstPersonResult FirstPersonResult;
    mutable FCriticalSection ResultLock;
    EFoundationBodySet Set = EFoundationBodySet::BetterSteve;
    bool bHasClips = false, bFirstPerson = false;
};
