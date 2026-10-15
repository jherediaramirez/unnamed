#include "Foundation/FoundationMobPath.h"
#include "Algo/Reverse.h"

namespace
{
    constexpr double MobPathEpsilon = 1e-6;

    struct FMobPathNode
    {
        FIntVector Cell;
        double Feet = 0;
        double Cost = 0;
        int32 Parent = INDEX_NONE;
        bool bClosed = false;
    };

    // An open-list entry; the heap is a min-heap on operator<, ties broken by insertion order.
    struct FMobPathOpen
    {
        double Score = 0;
        int32 Sequence = 0;
        int32 Node = 0;

        bool operator<(const FMobPathOpen& Other) const
        {
            return Score != Other.Score ? Score < Other.Score : Sequence < Other.Sequence;
        }
    };

    // Every cell of the column from FromZ up to Top (blocks) is empty and not avoided.
    bool MobPathHasRoom(FoundationMobPath::FQuery Query, int32 X, int32 Y, int32 FromZ, double Top)
    {
        for (int32 Z = FromZ; Z < Top - MobPathEpsilon; ++Z)
        {
            const FFoundationPathCell Cell = Query(FIntVector(X, Y, Z));
            if (Cell.Solid > 0 || Cell.bAvoid)
            {
                return false;
            }
        }
        return true;
    }

    double MobPathDistance(const FIntVector& A, double FeetA, const FIntVector& B, double FeetB)
    {
        return FVector(A.X - B.X, A.Y - B.Y, FeetA - FeetB).Size();
    }
}

namespace FoundationMobPath
{
    bool GetStand(FQuery Query, const FIntVector& Cell, double HeightCells, double& OutFeet)
    {
        const FFoundationPathCell Here = Query(Cell);
        if (Here.bAvoid)
        {
            return false;
        }
        double Feet = Cell.Z;
        int32 FirstFree = Cell.Z;
        if (Here.Solid > 0)
        {
            if (Here.Solid >= 1)
            {
                return false;
            }
            Feet = Cell.Z + Here.Solid;
            FirstFree = Cell.Z + 1;
        }
        else
        {
            const FFoundationPathCell Below = Query(FIntVector(Cell.X, Cell.Y, Cell.Z - 1));
            if (Below.bAvoid || Below.Solid < MinFloor || Below.Solid > 1)
            {
                return false;
            }
        }
        if (!MobPathHasRoom(Query, Cell.X, Cell.Y, FirstFree, Feet + HeightCells))
        {
            return false;
        }
        OutFeet = Feet;
        return true;
    }

    bool FindPath(FQuery Query, const FIntVector& Start, const FIntVector& Goal, double HeightCells,
        TArray<FIntVector>& OutPath, int32* OutExpanded)
    {
        OutPath.Reset();
        TArray<FMobPathNode> Nodes;
        TMap<FIntVector, int32> Best;
        TArray<FMobPathOpen> Open;
        int32 Sequence = 0, Expanded = 0, Closest = 0;

        double StartFeet = Start.Z;
        GetStand(Query, Start, HeightCells, StartFeet);
        FMobPathNode& First = Nodes.AddDefaulted_GetRef();
        First.Cell = Start;
        First.Feet = StartFeet;
        Best.Add(Start, 0);
        Open.HeapPush(FMobPathOpen{MobPathDistance(Start, StartFeet, Goal, Goal.Z), Sequence++, 0});

        auto Distance = [&Goal](const FIntVector& Cell)
        {
            return FIntPoint(
                FMath::Square(Cell.X - Goal.X) + FMath::Square(Cell.Y - Goal.Y), FMath::Abs(Cell.Z - Goal.Z));
        };
        auto IsCloser = [&Distance](const FIntVector& A, const FIntVector& B)
        {
            const FIntPoint DA = Distance(A), DB = Distance(B);
            return DA.X != DB.X ? DA.X < DB.X : DA.Y < DB.Y;
        };

        bool bReached = false;
        int32 Reached = 0;
        while (Open.Num() > 0 && Expanded < MaxNodes)
        {
            FMobPathOpen Top;
            Open.HeapPop(Top, EAllowShrinking::No);
            const int32 Index = Top.Node;
            if (Nodes[Index].bClosed || Best[Nodes[Index].Cell] != Index)
            {
                continue;
            }
            Nodes[Index].bClosed = true;
            ++Expanded;
            const FIntVector Cell = Nodes[Index].Cell;
            const double Feet = Nodes[Index].Feet, Cost = Nodes[Index].Cost;
            if (IsCloser(Cell, Nodes[Closest].Cell))
            {
                Closest = Index;
            }
            if (Cell.X == Goal.X && Cell.Y == Goal.Y && FMath::Abs(Cell.Z - Goal.Z) <= 1)
            {
                bReached = true;
                Reached = Index;
                break;
            }

            for (int32 DX = -1; DX <= 1; ++DX)
            {
                for (int32 DY = -1; DY <= 1; ++DY)
                {
                    const bool bDiagonal = DX != 0 && DY != 0;
                    double Ignored = 0;
                    if (DX == 0 && DY == 0)
                    {
                        continue;
                    }
                    // No corner cutting: both cells beside the corner must be standable at this height too.
                    if (bDiagonal &&
                        (!GetStand(Query, FIntVector(Cell.X + DX, Cell.Y, Cell.Z), HeightCells, Ignored) ||
                            !GetStand(Query, FIntVector(Cell.X, Cell.Y + DY, Cell.Z), HeightCells, Ignored)))
                    {
                        continue;
                    }
                    // The neighbour column from a jump up to the longest drop; a block ends the scan (no falling
                    // through it). A diagonal only goes to the same height.
                    for (int32 DZ = bDiagonal ? 0 : 1; DZ >= (bDiagonal ? 0 : -MaxDropCells); --DZ)
                    {
                        const FIntVector Next(Cell.X + DX, Cell.Y + DY, Cell.Z + DZ);
                        double NextFeet = 0;
                        if (GetStand(Query, Next, HeightCells, NextFeet))
                        {
                            const double Rise = NextFeet - Feet;
                            const bool bRoom = Rise <= StepRise ||
                                MobPathHasRoom(
                                    Query, Cell.X, Cell.Y, FMath::FloorToInt(Feet + 1), Feet + 1 + HeightCells);
                            if (Rise <= JumpRise && bRoom)
                            {
                                const double NextCost = Cost + MobPathDistance(Cell, Feet, Next, NextFeet);
                                const int32* Known = Best.Find(Next);
                                if (!Known || NextCost < Nodes[*Known].Cost - MobPathEpsilon)
                                {
                                    if (!Known || !Nodes[*Known].bClosed)
                                    {
                                        const int32 NewIndex = Nodes.Num();
                                        FMobPathNode& Node = Nodes.AddDefaulted_GetRef();
                                        Node.Cell = Next;
                                        Node.Feet = NextFeet;
                                        Node.Cost = NextCost;
                                        Node.Parent = Index;
                                        Best.Add(Next, NewIndex);
                                        Open.HeapPush(
                                            FMobPathOpen{NextCost + MobPathDistance(Next, NextFeet, Goal, Goal.Z),
                                                Sequence++, NewIndex});
                                    }
                                }
                            }
                            break;
                        }
                        if (DZ <= 0 && Query(Next).Solid > 0)
                        {
                            break;
                        }
                    }
                }
            }
        }

        for (int32 Index = bReached ? Reached : Closest; Index > 0; Index = Nodes[Index].Parent)
        {
            OutPath.Add(Nodes[Index].Cell);
        }
        Algo::Reverse(OutPath);
        if (OutExpanded)
        {
            *OutExpanded = Expanded;
        }
        return bReached;
    }
}
