#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Foundation/FoundationBody.h"
#include "FoundationBodyAnimInstance.generated.h"

class UAnimSequence;

// The player body's animation in native code, no Animation Blueprint: it samples the clips FFoundationBodyAnimator
// picked (blended by weight), then adds the head look (mesh-space additive look poses). The clips load from
// UUnnamedProjectSettings::PlayerBodyRoot when the instance initializes.
UCLASS(Transient, NotBlueprintable)
class UNNAMED_API UFoundationBodyAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    // True when every clip loaded (missing ones are logged).
    bool HasClips() const { return bHasClips; }
    // Each clip's play length (s), for FFoundationBodyAnimator::SetLengths.
    TArray<double> GetClipLengths() const;
    UAnimSequence* GetClip(int32 Clip) const { return Clips.IsValidIndex(Clip) ? Clips[Clip].Get() : nullptr; }
    void SetPose(const FFoundationBodyPose& InPose) { Pose = InPose; }

protected:
    virtual void NativeInitializeAnimation() override;
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
    friend struct FFoundationBodyAnimProxy;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UAnimSequence>> Clips;
    FFoundationBodyPose Pose;
    bool bHasClips = false;
};
