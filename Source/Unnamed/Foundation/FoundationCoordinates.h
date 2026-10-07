#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationProfile.h"

// Converts between Unreal centimeters (X east, Y south, Z up) and reference blocks (X, Y up, Z).
namespace FoundationCoordinates
{
    inline FVector ToMinecraft(FVector LocalFeetCm, FVector OriginCm = {}, FIntVector RebaseCm = {})
    {
        const FVector P = (LocalFeetCm + FVector(RebaseCm) - OriginCm) / UFoundationProfile::BlockCm;
        return {P.X, P.Z, P.Y};
    }

    inline FVector ToUnreal(FVector MC, FVector OriginCm = {}, FIntVector RebaseCm = {})
    {
        return FVector(MC.X, MC.Z, MC.Y) * UFoundationProfile::BlockCm + OriginCm - FVector(RebaseCm);
    }

    inline FInt64Vector3 Blocks(FVector MC)
    {
        return {FMath::FloorToInt64(MC.X), FMath::FloorToInt64(MC.Y), FMath::FloorToInt64(MC.Z)};
    }

    inline double Yaw(double UEYaw)
    {
        return FRotator::NormalizeAxis(UEYaw - 90.);
    }

    inline double Pitch(double UEPitch)
    {
        return -UEPitch;
    }

    inline double ToUnrealYaw(double MCYaw)
    {
        return FRotator::NormalizeAxis(MCYaw + 90.);
    }

    inline double ToUnrealPitch(double MCPitch)
    {
        return -MCPitch;
    }

    inline double HorizontalFov(double VerticalDegrees, double Aspect)
    {
        return FMath::RadiansToDegrees(
            2. * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(VerticalDegrees) * .5) * Aspect));
    }

    inline FString Facing(double UEYaw)
    {
        const int32 Index = (FMath::RoundToInt(FRotator::ClampAxis(UEYaw) / 90.) % 4);
        return TArray<FString>{TEXT("east (+X)"), TEXT("south (+Z)"), TEXT("west (-X)"), TEXT("north (-Z)")}[Index];
    }
}
