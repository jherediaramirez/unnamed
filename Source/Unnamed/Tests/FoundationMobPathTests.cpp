#include "Misc/AutomationTest.h"
#include "Foundation/FoundationMobPath.h"

#if WITH_DEV_AUTOMATION_TESTS
// Survival T6 20b: the mob path search (scout facts.md section 6): hand-built block worlds, a floor of full blocks at
// z -1, one pig-sized (0.9) and one cow-sized (1.4) mob. Mutations: MaxDropCells 4; diagonal corner cutting; ignoring
// headroom.
namespace MobPathTestsLocal
{
    struct FMobPathWorld
    {
        TMap<FIntVector, FFoundationPathCell> Cells;
        bool bInfiniteFloor = false;

        FMobPathWorld()
        {
            for (int32 X = -10; X <= 10; ++X)
            {
                for (int32 Y = -10; Y <= 10; ++Y)
                {
                    Set(X, Y, -1, 1);
                }
            }
        }

        void Set(int32 X, int32 Y, int32 Z, float Solid, bool bAvoid = false)
        {
            FFoundationPathCell& Cell = Cells.FindOrAdd(FIntVector(X, Y, Z));
            Cell.Solid = Solid;
            Cell.bAvoid = bAvoid;
        }

        void Box(FIntVector Min, FIntVector Max, float Solid, bool bAvoid = false)
        {
            for (int32 X = Min.X; X <= Max.X; ++X)
            {
                for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
                {
                    for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
                    {
                        Set(X, Y, Z, Solid, bAvoid);
                    }
                }
            }
        }

        FFoundationPathCell Get(const FIntVector& Cell) const
        {
            if (const FFoundationPathCell* Found = Cells.Find(Cell))
            {
                return *Found;
            }
            FFoundationPathCell Result;
            Result.Solid = bInfiniteFloor && Cell.Z == -1 ? 1.f : 0.f;
            return Result;
        }

        bool Find(const FIntVector& Start, const FIntVector& Goal, double Height, TArray<FIntVector>& Path,
            int32* Expanded = nullptr) const
        {
            return FoundationMobPath::FindPath(
                [this](const FIntVector& Cell) { return Get(Cell); }, Start, Goal, Height, Path, Expanded);
        }

        // Every diagonal step has both cells beside its corner empty.
        bool NoCornerCut(const FIntVector& Start, const TArray<FIntVector>& Path) const
        {
            FIntVector Prev = Start;
            for (const FIntVector& Cell : Path)
            {
                if (Cell.X != Prev.X && Cell.Y != Prev.Y &&
                    (Get(FIntVector(Cell.X, Prev.Y, Prev.Z)).Solid > 0 ||
                        Get(FIntVector(Prev.X, Cell.Y, Prev.Z)).Solid > 0))
                {
                    return false;
                }
                Prev = Cell;
            }
            return true;
        }

        bool Touches(const TArray<FIntVector>& Path, FIntVector Min, FIntVector Max) const
        {
            return Path.ContainsByPredicate(
                [&](const FIntVector& Cell)
                {
                    return Cell.X >= Min.X && Cell.X <= Max.X && Cell.Y >= Min.Y && Cell.Y <= Max.Y &&
                        Cell.Z >= Min.Z && Cell.Z <= Max.Z;
                });
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobPathTest, "Unnamed.Foundation.Mobs.Path",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobPathTest::RunTest(const FString&)
{
    using namespace MobPathTestsLocal;
    const double Heights[] = {.9, 1.4};
    for (const double H : Heights)
    {
        const FString Tag = FString::Printf(TEXT("height %.1f: "), H);
        TArray<FIntVector> Path;
        int32 Expanded = 0;
        const FIntVector Origin(0, 0, 0);

        {
            // 1. Open floor.
            FMobPathWorld W;
            const bool bFound = W.Find(Origin, FIntVector(5, 0, 0), H, Path);
            TestTrue(Tag + TEXT("open floor reaches the goal"), bFound);
            TestTrue(
                Tag + TEXT("open floor path ends at the goal"), Path.Num() > 0 && Path.Last() == FIntVector(5, 0, 0));
            TestTrue(Tag + TEXT("open floor path is at most 6 cells"), Path.Num() <= 6);
        }
        {
            // 2. Around a wall.
            FMobPathWorld W;
            W.Box(FIntVector(2, -3, 0), FIntVector(2, 3, 2), 1);
            const bool bFound = W.Find(Origin, FIntVector(5, 0, 0), H, Path);
            TestTrue(Tag + TEXT("wall: reaches the goal around it"), bFound);
            TestFalse(Tag + TEXT("wall: no path cell inside the wall"),
                W.Touches(Path, FIntVector(2, -3, 0), FIntVector(2, 3, 2)));
            TestTrue(Tag + TEXT("wall: the detour is longer than the straight line"), Path.Num() > 5);
            TestTrue(Tag + TEXT("wall: no corner cutting"), W.NoCornerCut(Origin, Path));
        }
        {
            // 3. Up one full block.
            FMobPathWorld W;
            W.Box(FIntVector(3, 0, 0), FIntVector(6, 0, 0), 1);
            const bool bFound = W.Find(Origin, FIntVector(5, 0, 1), H, Path);
            TestTrue(Tag + TEXT("step up: reaches the top of the blocks"), bFound);
            TestTrue(Tag + TEXT("step up: ends at the goal"), Path.Num() > 0 && Path.Last() == FIntVector(5, 0, 1));
            TestTrue(
                Tag + TEXT("step up: climbs to z 1"), W.Touches(Path, FIntVector(-10, -10, 1), FIntVector(10, 10, 1)));
        }
        {
            // 4. A 2-block step is refused.
            FMobPathWorld W;
            W.Box(FIntVector(3, -10, 0), FIntVector(3, 10, 1), 1);
            W.Box(FIntVector(5, -1, 0), FIntVector(6, 1, 1), 1);
            const bool bFound = W.Find(Origin, FIntVector(5, 0, 2), H, Path);
            TestFalse(Tag + TEXT("2-high wall: the platform behind it is not reached"), bFound);
            TestFalse(Tag + TEXT("2-high wall: no path cell at z 2 or above"),
                W.Touches(Path, FIntVector(-99, -99, 2), FIntVector(99, 99, 99)));
            TestFalse(Tag + TEXT("2-high wall: no path cell past the wall"),
                W.Touches(Path, FIntVector(3, -99, -99), FIntVector(99, 99, 99)));
        }
        {
            // 5. Drops: 3 cells allowed, 4 refused.
            FMobPathWorld W;
            W.Box(FIntVector(-10, -10, 0), FIntVector(0, 10, 2), 1);
            const bool bDrop3 = W.Find(FIntVector(-1, 0, 3), FIntVector(4, 0, 0), H, Path);
            TestTrue(Tag + TEXT("drop 3: walks off the ledge to the floor"), bDrop3);
            TestTrue(Tag + TEXT("drop 3: ends on the floor"), Path.Num() > 0 && Path.Last() == FIntVector(4, 0, 0));

            FMobPathWorld W4;
            W4.Box(FIntVector(-10, -10, 0), FIntVector(0, 10, 3), 1);
            const bool bDrop4 = W4.Find(FIntVector(-1, 0, 4), FIntVector(4, 0, 0), H, Path);
            TestFalse(Tag + TEXT("drop 4: refused without stairs"), bDrop4);
            TestFalse(Tag + TEXT("drop 4: no path cell on the floor"),
                W4.Touches(Path, FIntVector(-99, -99, 0), FIntVector(99, 99, 0)));
        }
        {
            // 6. No corner cutting: two blocks two high (one high is climbed over).
            FMobPathWorld W;
            W.Set(1, 0, 0, 1);
            W.Set(0, 1, 0, 1);
            W.Set(1, 0, 1, 1);
            W.Set(0, 1, 1, 1);
            const bool bFound = W.Find(Origin, FIntVector(1, 1, 0), H, Path);
            TestTrue(Tag + TEXT("corner: no diagonal between the two blocks"), W.NoCornerCut(Origin, Path));
            TestTrue(Tag + TEXT("corner: first step is not the corner-cutting diagonal"),
                Path.Num() == 0 || Path[0] != FIntVector(1, 1, 0));
            TestTrue(Tag + TEXT("corner: goes around instead"), bFound && Path.Num() > 2);
        }
        {
            // 7. Water forces a detour.
            FMobPathWorld W;
            W.Box(FIntVector(2, -5, -1), FIntVector(3, 5, -1), 1, true);
            W.Box(FIntVector(2, -5, 0), FIntVector(3, 5, 0), 0, true);
            const bool bFound = W.Find(Origin, FIntVector(6, 0, 0), H, Path);
            TestTrue(Tag + TEXT("water: reaches the goal"), bFound);
            TestFalse(Tag + TEXT("water: no path cell in the pool"),
                W.Touches(Path, FIntVector(2, -5, -1), FIntVector(3, 5, 1)));
            TestTrue(Tag + TEXT("water: the detour is longer than the straight line"), Path.Num() > 6);
        }
        {
            // 8. A bottom slab is the only route.
            FMobPathWorld W;
            W.Box(FIntVector(2, -10, 0), FIntVector(2, 10, 1), 1);
            W.Set(2, 0, 0, .5f);
            W.Set(2, 0, 1, 0);
            const bool bFound = W.Find(Origin, FIntVector(5, 0, 0), H, Path);
            TestTrue(Tag + TEXT("slab: crosses the wall over the slab"), bFound);
            TestTrue(Tag + TEXT("slab: passes through the slab cell"), Path.Contains(FIntVector(2, 0, 0)));
            double Feet = 0;
            const bool bStand = FoundationMobPath::GetStand(
                [&W](const FIntVector& Cell) { return W.Get(Cell); }, FIntVector(2, 0, 0), H, Feet);
            TestTrue(Tag + TEXT("slab: the slab cell is standable"), bStand);
            TestEqual(Tag + TEXT("slab: feet stand on the slab's top"), Feet, 0.5);
        }
        {
            // 9. The node cap.
            FMobPathWorld W;
            W.bInfiniteFloor = true;
            const bool bFound = W.Find(Origin, FIntVector(400, 0, 0), H, Path, &Expanded);
            TestFalse(Tag + TEXT("cap: a goal 400 cells away is not reached"), bFound);
            TestTrue(Tag + TEXT("cap: expands at most MaxNodes"), Expanded <= FoundationMobPath::MaxNodes);
            TestTrue(Tag + TEXT("cap: a partial path toward the goal"), Path.Num() > 0 && Path.Last().X > 0);
        }
    }
    {
        // 10. Headroom: a ceiling at z 1 across the corridor passes 0.9, not 1.4.
        FMobPathWorld W;
        W.Box(FIntVector(2, -10, 1), FIntVector(3, 10, 1), 1);
        TArray<FIntVector> Path;
        TestTrue(
            TEXT("ceiling: a 0.9 mob passes under it"), W.Find(FIntVector(0, 0, 0), FIntVector(5, 0, 0), .9, Path));
        TestFalse(TEXT("ceiling: a 1.4 mob does not"), W.Find(FIntVector(0, 0, 0), FIntVector(5, 0, 0), 1.4, Path));
        TestFalse(TEXT("ceiling: the 1.4 mob's partial path stops before it"),
            W.Touches(Path, FIntVector(2, -99, -99), FIntVector(99, 99, 99)));
    }
    return true;
}
#endif
