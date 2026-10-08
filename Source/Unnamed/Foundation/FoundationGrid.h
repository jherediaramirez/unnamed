#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationProfile.h"

// The six faces of a cell, named by the project's Unreal axes (+X east, +Y south, +Z up).
// The order is stable: commands, tests and diagnostics refer to it.
enum class EFoundationFace : uint8
{
    West, // -X
    East, // +X
    North, // -Y
    South, // +Y
    Down, // -Z
    Up, // +Z
    None
};

// Integer block cells: one 100 cm conversion (UFoundationProfile::BlockCm) and floor division, so
// negative coordinates and a moved grid origin (world rebasing) resolve exactly like positive ones.
// Cells use Unreal axis order (X east, Y south, Z up); DescribeCell prints the reference x y z order.
namespace FoundationGrid
{
    constexpr int32 SectionCells = 16;

    inline int32 FloorDivide(int32 Value, int32 Divisor)
    {
        const int32 Quotient = Value / Divisor;
        return Quotient * Divisor > Value ? Quotient - 1 : Quotient;
    }

    inline int32 FloorToCell(double Cm)
    {
        return FMath::FloorToInt32(Cm / UFoundationProfile::BlockCm);
    }

    inline FIntVector CellAt(const FVector& WorldCm, const FVector& OriginCm)
    {
        const FVector Local = WorldCm - OriginCm;
        return {FloorToCell(Local.X), FloorToCell(Local.Y), FloorToCell(Local.Z)};
    }

    inline FVector CellMin(const FIntVector& Cell, const FVector& OriginCm)
    {
        return OriginCm + FVector(Cell) * UFoundationProfile::BlockCm;
    }

    inline FBox CellBox(const FIntVector& Cell, const FVector& OriginCm)
    {
        const FVector Min = CellMin(Cell, OriginCm);
        return FBox(Min, Min + FVector(UFoundationProfile::BlockCm));
    }

    inline FIntVector SectionOf(const FIntVector& Cell)
    {
        return {
            FloorDivide(Cell.X, SectionCells), FloorDivide(Cell.Y, SectionCells), FloorDivide(Cell.Z, SectionCells)};
    }

    inline FIntVector FaceOffset(EFoundationFace Face)
    {
        switch (Face)
        {
        case EFoundationFace::West:
            return {-1, 0, 0};
        case EFoundationFace::East:
            return {1, 0, 0};
        case EFoundationFace::North:
            return {0, -1, 0};
        case EFoundationFace::South:
            return {0, 1, 0};
        case EFoundationFace::Down:
            return {0, 0, -1};
        case EFoundationFace::Up:
            return {0, 0, 1};
        default:
            return FIntVector::ZeroValue;
        }
    }

    inline FVector FaceNormal(EFoundationFace Face)
    {
        return FVector(FaceOffset(Face));
    }

    // Axis 0..2 (X, Y, Z) and the side the face looks toward.
    inline EFoundationFace FaceFromAxis(int32 Axis, bool bPositive)
    {
        return EFoundationFace(Axis * 2 + (bPositive ? 1 : 0));
    }

    inline const TCHAR* FaceName(EFoundationFace Face)
    {
        static const TCHAR* Names[] = {
            TEXT("west"), TEXT("east"), TEXT("north"), TEXT("south"), TEXT("down"), TEXT("up"), TEXT("none")};
        return Names[FMath::Min(int32(Face), 6)];
    }

    // Reference order (x, y up, z south) so the text matches the F3 block coordinates.
    inline FString DescribeCell(const FIntVector& Cell)
    {
        return FString::Printf(TEXT("%d %d %d"), Cell.X, Cell.Z, Cell.Y);
    }
}
