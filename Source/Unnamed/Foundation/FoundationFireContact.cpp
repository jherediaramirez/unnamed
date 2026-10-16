#include "Foundation/FoundationFireContact.h"

#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationGrid.h"
#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarFireBlocks(TEXT("foundation.FireBlocks"), 1,
        TEXT("Fire blocks (step 16c): 1 standing in fire hurts 1 (soul fire 2) at ticks 1, 11, 21 ... and sets the "
             "player burning after 20 ticks, a lava cauldron's lava hurts as lava (needs foundation.Fire 1); 0 fire "
             "blocks do nothing, as in step 16a."),
        ECVF_Default);
}

bool FoundationFireContact::IsOn()
{
    return CVarFireBlocks.GetValueOnAnyThread() != 0;
}

FoundationFireContact::FContact FoundationFireContact::Get(const FFoundationBlockWorld& World, const FBox& Body)
{
    FContact Contact;
    const FFoundationBlockType* Fire = World.FindBlock(TEXT("fire"));
    const FFoundationBlockType* SoulFire = World.FindBlock(TEXT("soul_fire"));
    const FFoundationBlockType* LavaCauldron = World.FindBlock(TEXT("lava_cauldron"));
    if (!Fire && !SoulFire && !LavaCauldron) return Contact;
    const FBox Inner = Body.ExpandBy(-.1);
    const FIntVector Low = FoundationGrid::CellAt(Inner.Min, World.OriginCm),
                     High = FoundationGrid::CellAt(Inner.Max, World.OriginCm);
    for (int32 Z = Low.Z; Z <= High.Z; ++Z)
        for (int32 Y = Low.Y; Y <= High.Y; ++Y)
            for (int32 X = Low.X; X <= High.X; ++X)
            {
                const FIntVector Cell(X, Y, Z);
                if (!World.IsKnown(Cell)) continue;
                const FFoundationBlockType* Block = &World.GetBlock(World.Get(Cell).State);
                if (Block == SoulFire)
                    Contact.FireDamage = 2;
                else if (Block == Fire)
                    Contact.FireDamage = FMath::Max(Contact.FireDamage, 1);
                else if (Block == LavaCauldron)
                {
                    const FVector Min = FoundationGrid::CellMin(Cell, World.OriginCm);
                    const double Sixteenth = UFoundationProfile::BlockCm / 16.;
                    const FBox Lava(Min + FVector(2, 2, 4) * Sixteenth, Min + FVector(14, 14, 15) * Sixteenth);
                    if (Lava.Intersect(Inner)) Contact.bLavaCauldron = true;
                }
            }
    return Contact;
}
