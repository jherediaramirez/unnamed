#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockFamilies.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationProfile.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Voxel/VoxelBlocks.h"

// Survival B3a (track T5): doors, trapdoors and fence gates. Placement (two halves, the hinge, the trapdoor's half and
// facing), the hand use (survival.r9.switches, F 5071-5135), the use sounds, door survival
// (Reference/minecraft-blocks-26.3/facts/family_survival.json) and the hinge against the black-box probe
// (Tests/Probes/door-hinge-26.3.json).
#if WITH_DEV_AUTOMATION_TESTS
namespace FoundationInteractTests
{
    constexpr EAutomationTestFlags InteractTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    TSharedPtr<FJsonObject> LoadProjectJson(const TCHAR* Relative)
    {
        FString Text;
        TSharedPtr<FJsonObject> Json;
        if (FFileHelper::LoadFileToString(Text, *(FPaths::ProjectDir() / Relative)))
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
        return Json;
    }

    // Sets a console variable for one test and puts it back.
    struct FInteractCVarScope
    {
        IConsoleVariable* Variable;
        int32 Old;

        FInteractCVarScope(const TCHAR* Name, int32 Value)
            : Variable(IConsoleManager::Get().FindConsoleVariable(Name)), Old(Variable ? Variable->GetInt() : 0)
        {
            if (Variable) Variable->Set(Value, ECVF_SetByCode);
        }
        ~FInteractCVarScope()
        {
            if (Variable) Variable->Set(Old, ECVF_SetByCode);
        }
    };

    // An authority with the project's blocks and rules, block use on, a stone floor at z 0 and one player.
    struct FInteractLab
    {
        FFoundationAuthority A;
        int32 Player = 0;
        uint32 Sequence = 1;
        FFoundationVitals Vitals;

        FInteractLab()
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.PlantSurvival = VoxelBlocks::GetPlantSurvival(A.World.GetRegistry());
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            A.Options.bBlockUse = true;
            Player = A.AddPlayer();
            A.Reset();
            for (int32 X = -6; X <= 6; ++X)
                for (int32 Y = -6; Y <= 6; ++Y)
                    A.World.Set({X, Y, 0}, State(TEXT("stone")));
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }
        const FFoundationBlockRegistry& R() const { return A.World.GetRegistry(); }

        FFoundationBlockState State(const FString& Text) const
        {
            FFoundationBlockState State;
            ensureMsgf(R().Parse(Text, State), TEXT("parse %s"), *Text);
            return State;
        }

        FString At(const FIntVector& Cell) const { return R().ToString(A.World.Get(Cell).State); }

        // Looks at Point (world cm) from Point + EyeOffset, holding Item (none: an empty hand).
        void AimAt(const FVector& Point, const FVector& EyeOffset, FName Item, bool bSneak = false)
        {
            FFoundationPlayerFacts Facts = P().Facts;
            Facts.EyeCm = Point + EyeOffset;
            Facts.FeetCm = Facts.EyeCm - FVector(0, 0, 162);
            Facts.HeightCm = 180;
            Facts.Aim = (Point - Facts.EyeCm).GetSafeNormal();
            Facts.bGrounded = true;
            Facts.bSecondaryUse = bSneak;
            Facts.Step = P().Facts.Step + 1;
            A.SetFacts(Player, Facts);
            P().Inventory.SetSlot(
                0, Item.IsNone() ? FFoundationItemInstance() : P().Inventory.MakeStack(A.Rules.FindItem(Item), 1));
            P().Inventory.Select(0);
        }

        // A point on Cell's Face: Local in the cell (0-1 per axis), its coordinate along the face's axis on the face.
        FVector FacePoint(const FIntVector& Cell, EFoundationFace Face, const FVector& Local = FVector(.5)) const
        {
            FVector Point = Local;
            const int32 Axis = int32(Face) / 2;
            Point[Axis] = int32(Face) % 2 == 1 ? 1 : 0;
            return FoundationGrid::CellMin(Cell, A.World.OriginCm) + Point * UFoundationProfile::BlockCm;
        }

        // The centre of the state's first outline box at Cell (what a click on the block hits).
        FVector BoxPoint(const FIntVector& Cell) const
        {
            TArray<FBox> Boxes;
            A.World.GetOutlineBoxes(Cell, A.World.Get(Cell).State, Boxes);
            return Boxes.IsEmpty() ? FoundationGrid::CellBox(Cell, A.World.OriginCm).GetCenter() : Boxes[0].GetCenter();
        }

        FFoundationResult Run(EFoundationCommandType Type, bool bPlace, EFoundationHand Hand = EFoundationHand::Main)
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = Sequence++;
            Command.Type = Type;
            Command.Hand = Hand;
            Command.Item = P().Inventory.GetHand(Hand).GetRef();
            Command.Target = A.Resolve(P().Facts);
            if (bPlace)
            {
                FIntVector Destination;
                A.CheckPlacement(P(), Hand, Command.Target, Destination);
                Command.Destination = Destination;
                Command.DestinationRevision = A.World.Get(Destination).Revision;
            }
            return A.Execute(Player, Command, Vitals);
        }

        // Uses the block at Cell with an empty hand from 2.5 blocks north (looking south), or from the south.
        FFoundationResult UseBlock(const FIntVector& Cell, bool bFromNorth = true)
        {
            AimAt(BoxPoint(Cell), FVector(0, bFromNorth ? -250 : 250, 40), NAME_None);
            return Run(EFoundationCommandType::UseOn, true);
        }

        // Clicks Local on Cell's Face holding Item, from the point + EyeOffset.
        FFoundationResult PlaceAt(
            const FIntVector& Cell, EFoundationFace Face, const FVector& Local, FName Item, const FVector& EyeOffset)
        {
            AimAt(FacePoint(Cell, Face, Local), EyeOffset, Item);
            return Run(EFoundationCommandType::UseOn, true);
        }

        FFoundationResult Break(const FIntVector& Cell)
        {
            AimAt(BoxPoint(Cell), FVector(250, 30, 60), NAME_None);
            FFoundationResult R = Run(EFoundationCommandType::MineStart, false);
            for (int32 Step = 0; Step < 2000 && !R.bBroken && R.IsCommitted(); ++Step)
            {
                FFoundationPlayerFacts Facts = P().Facts;
                ++Facts.Step;
                A.SetFacts(Player, Facts);
                R = Run(EFoundationCommandType::MineContinue, false);
            }
            return R;
        }

        int32 CountEvents(EFoundationEventKind Kind, int64 AfterSerial) const
        {
            int32 Count = 0;
            for (const FFoundationEvent& Event : A.GetEvents())
                Count += Event.Serial > AfterSerial && Event.Kind == Kind;
            return Count;
        }

        const FFoundationEvent* LastEvent(EFoundationEventKind Kind) const
        {
            for (int32 Index = A.GetEvents().Num() - 1; Index >= 0; --Index)
                if (A.GetEvents()[Index].Kind == Kind) return &A.GetEvents()[Index];
            return nullptr;
        }
    };

    struct FSwitchRow
    {
        const TCHAR* Before;
        const TCHAR* First;
        const TCHAR* UpperAfter;
        const TCHAR* Second;
    };

    // SURVIVAL-FINDINGS-26.3.md F 5071-5135 (survival.r9.switches): the 57 door, trapdoor and gate rows (the lever's
    // is B3b's), copied verbatim: Before, First use, Upper half after first use (doors), Second use. The probe player
    // looked south.
    const FSwitchRow SwitchRows[] = {
        {TEXT("oak_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("oak_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("oak_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("oak_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("iron_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("iron_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("iron_door[facing=north,half=upper,hinge=left,open=false,powered=false]"),
            TEXT("iron_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("oak_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("oak_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"), TEXT(""),
            TEXT("oak_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("spruce_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("spruce_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"), TEXT(""),
            TEXT("spruce_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("birch_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("birch_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"), TEXT(""),
            TEXT("birch_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("jungle_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("jungle_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"), TEXT(""),
            TEXT("jungle_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("acacia_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("acacia_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"), TEXT(""),
            TEXT("acacia_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("cherry_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("cherry_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"), TEXT(""),
            TEXT("cherry_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("dark_oak_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("dark_oak_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"), TEXT(""),
            TEXT("dark_oak_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("pale_oak_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("pale_oak_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"), TEXT(""),
            TEXT("pale_oak_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("mangrove_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("mangrove_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"), TEXT(""),
            TEXT("mangrove_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("poplar_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("poplar_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"), TEXT(""),
            TEXT("poplar_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("bamboo_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("bamboo_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"), TEXT(""),
            TEXT("bamboo_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("oak_fence_gate[facing=north,in_wall=false,open=false,powered=false]"),
            TEXT("oak_fence_gate[facing=south,in_wall=false,open=true,powered=false]"), TEXT(""),
            TEXT("oak_fence_gate[facing=south,in_wall=false,open=false,powered=false]")},
        {TEXT("iron_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("iron_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"), TEXT(""),
            TEXT("iron_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("spruce_fence_gate[facing=north,in_wall=false,open=false,powered=false]"),
            TEXT("spruce_fence_gate[facing=south,in_wall=false,open=true,powered=false]"), TEXT(""),
            TEXT("spruce_fence_gate[facing=south,in_wall=false,open=false,powered=false]")},
        {TEXT("birch_fence_gate[facing=north,in_wall=false,open=false,powered=false]"),
            TEXT("birch_fence_gate[facing=south,in_wall=false,open=true,powered=false]"), TEXT(""),
            TEXT("birch_fence_gate[facing=south,in_wall=false,open=false,powered=false]")},
        {TEXT("jungle_fence_gate[facing=north,in_wall=false,open=false,powered=false]"),
            TEXT("jungle_fence_gate[facing=south,in_wall=false,open=true,powered=false]"), TEXT(""),
            TEXT("jungle_fence_gate[facing=south,in_wall=false,open=false,powered=false]")},
        {TEXT("acacia_fence_gate[facing=north,in_wall=false,open=false,powered=false]"),
            TEXT("acacia_fence_gate[facing=south,in_wall=false,open=true,powered=false]"), TEXT(""),
            TEXT("acacia_fence_gate[facing=south,in_wall=false,open=false,powered=false]")},
        {TEXT("cherry_fence_gate[facing=north,in_wall=false,open=false,powered=false]"),
            TEXT("cherry_fence_gate[facing=south,in_wall=false,open=true,powered=false]"), TEXT(""),
            TEXT("cherry_fence_gate[facing=south,in_wall=false,open=false,powered=false]")},
        {TEXT("dark_oak_fence_gate[facing=north,in_wall=false,open=false,powered=false]"),
            TEXT("dark_oak_fence_gate[facing=south,in_wall=false,open=true,powered=false]"), TEXT(""),
            TEXT("dark_oak_fence_gate[facing=south,in_wall=false,open=false,powered=false]")},
        {TEXT("pale_oak_fence_gate[facing=north,in_wall=false,open=false,powered=false]"),
            TEXT("pale_oak_fence_gate[facing=south,in_wall=false,open=true,powered=false]"), TEXT(""),
            TEXT("pale_oak_fence_gate[facing=south,in_wall=false,open=false,powered=false]")},
        {TEXT("mangrove_fence_gate[facing=north,in_wall=false,open=false,powered=false]"),
            TEXT("mangrove_fence_gate[facing=south,in_wall=false,open=true,powered=false]"), TEXT(""),
            TEXT("mangrove_fence_gate[facing=south,in_wall=false,open=false,powered=false]")},
        {TEXT("poplar_fence_gate[facing=north,in_wall=false,open=false,powered=false]"),
            TEXT("poplar_fence_gate[facing=south,in_wall=false,open=true,powered=false]"), TEXT(""),
            TEXT("poplar_fence_gate[facing=south,in_wall=false,open=false,powered=false]")},
        {TEXT("bamboo_fence_gate[facing=north,in_wall=false,open=false,powered=false]"),
            TEXT("bamboo_fence_gate[facing=south,in_wall=false,open=true,powered=false]"), TEXT(""),
            TEXT("bamboo_fence_gate[facing=south,in_wall=false,open=false,powered=false]")},
        {TEXT("spruce_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("spruce_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("spruce_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("spruce_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("birch_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("birch_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("birch_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("birch_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("jungle_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("jungle_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("jungle_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("jungle_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("acacia_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("acacia_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("acacia_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("acacia_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("cherry_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("cherry_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("cherry_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("cherry_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("dark_oak_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("dark_oak_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("dark_oak_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("dark_oak_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("pale_oak_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("pale_oak_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("pale_oak_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("pale_oak_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("mangrove_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("mangrove_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("mangrove_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("mangrove_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("poplar_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("poplar_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("poplar_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("poplar_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("bamboo_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("bamboo_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("bamboo_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("bamboo_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("crimson_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("crimson_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"), TEXT(""),
            TEXT("crimson_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("warped_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("warped_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"), TEXT(""),
            TEXT("warped_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("crimson_fence_gate[facing=north,in_wall=false,open=false,powered=false]"),
            TEXT("crimson_fence_gate[facing=south,in_wall=false,open=true,powered=false]"), TEXT(""),
            TEXT("crimson_fence_gate[facing=south,in_wall=false,open=false,powered=false]")},
        {TEXT("warped_fence_gate[facing=north,in_wall=false,open=false,powered=false]"),
            TEXT("warped_fence_gate[facing=south,in_wall=false,open=true,powered=false]"), TEXT(""),
            TEXT("warped_fence_gate[facing=south,in_wall=false,open=false,powered=false]")},
        {TEXT("crimson_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("crimson_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("crimson_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("crimson_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("warped_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("warped_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("warped_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("warped_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("copper_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("copper_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("copper_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("copper_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("exposed_copper_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("exposed_copper_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("exposed_copper_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("exposed_copper_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("weathered_copper_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("weathered_copper_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("weathered_copper_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("weathered_copper_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("oxidized_copper_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("oxidized_copper_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("oxidized_copper_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("oxidized_copper_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("waxed_copper_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("waxed_copper_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("waxed_copper_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("waxed_copper_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("waxed_exposed_copper_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("waxed_exposed_copper_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("waxed_exposed_copper_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("waxed_exposed_copper_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("waxed_weathered_copper_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("waxed_weathered_copper_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("waxed_weathered_copper_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("waxed_weathered_copper_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("waxed_oxidized_copper_door[facing=north,half=lower,hinge=left,open=false,powered=false]"),
            TEXT("waxed_oxidized_copper_door[facing=north,half=lower,hinge=left,open=true,powered=false]"),
            TEXT("waxed_oxidized_copper_door[facing=north,half=upper,hinge=left,open=true,powered=false]"),
            TEXT("waxed_oxidized_copper_door[facing=north,half=lower,hinge=left,open=false,powered=false]")},
        {TEXT("copper_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("copper_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"), TEXT(""),
            TEXT("copper_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("exposed_copper_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("exposed_copper_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"),
            TEXT(""),
            TEXT("exposed_copper_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("weathered_copper_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("weathered_copper_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"),
            TEXT(""),
            TEXT("weathered_copper_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("oxidized_copper_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("oxidized_copper_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"),
            TEXT(""),
            TEXT("oxidized_copper_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("waxed_copper_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("waxed_copper_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"), TEXT(""),
            TEXT("waxed_copper_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("waxed_exposed_copper_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("waxed_exposed_copper_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"),
            TEXT(""),
            TEXT("waxed_exposed_copper_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
        {TEXT("waxed_weathered_copper_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("waxed_weathered_copper_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"),
            TEXT(""),
            TEXT("waxed_weathered_copper_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged="
                 "false]")},
        {TEXT("waxed_oxidized_copper_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
            TEXT("waxed_oxidized_copper_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]"),
            TEXT(""),
            TEXT(
                "waxed_oxidized_copper_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")},
    };
}

using namespace FoundationInteractTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationInteractFamiliesTest, "Unnamed.Foundation.Interact.Families", InteractTestFlags)
bool FFoundationInteractFamiliesTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    int32 Doors = 0, Trapdoors = 0, Gates = 0;
    TArray<FString> NotUsable;
    for (int32 Index = 0; Index < Registry->GetBlockCount(); ++Index)
    {
        const FFoundationBlockType& Block = Registry->GetBlockAt(Index);
        const EFoundationBlockFamily Family = FoundationBlockFamilies::GetFamily(Block);
        Doors += Family == EFoundationBlockFamily::Door;
        Trapdoors += Family == EFoundationBlockFamily::Trapdoor;
        Gates += Family == EFoundationBlockFamily::FenceGate;
        const bool bOpenable = Family == EFoundationBlockFamily::Door || Family == EFoundationBlockFamily::Trapdoor ||
            Family == EFoundationBlockFamily::FenceGate;
        if (bOpenable && !FoundationBlockFamilies::IsUsable(Block)) NotUsable.Add(Block.Id.ToString());
        if (!bOpenable && FoundationBlockFamilies::IsUsable(Block))
            AddError(Block.Id.ToString() + TEXT(" is usable but not a door, trapdoor or gate"));
    }
    TestEqual(TEXT("doors (#minecraft:doors)"), Doors, 22);
    TestEqual(TEXT("trapdoors (#minecraft:trapdoors)"), Trapdoors, 22);
    TestEqual(TEXT("fence gates"), Gates, 13);
    NotUsable.Sort();
    TestEqual(TEXT("no hand use: the iron door and trapdoor only"), FString::Join(NotUsable, TEXT(",")),
        FString(TEXT("iron_door,iron_trapdoor")));
    // The use sounds against facts/block_use_sounds.csv, every door, trapdoor and gate row.
    FString Csv;
    TestTrue(TEXT("block_use_sounds.csv loads"),
        FFileHelper::LoadFileToString(
            Csv, *(FPaths::ProjectDir() / TEXT("Reference/minecraft-blocks-26.3/facts/block_use_sounds.csv"))));
    TArray<FString> Lines;
    Csv.ParseIntoArrayLines(Lines);
    int32 Rows = 0;
    for (const FString& Line : Lines)
    {
        TArray<FString> Cells;
        Line.ParseIntoArray(Cells, TEXT(","), false);
        if (Cells.Num() < 5 ||
            (Cells[1] != TEXT("door") && Cells[1] != TEXT("trapdoor") && Cells[1] != TEXT("fence_gate")))
            continue;
        ++Rows;
        const FFoundationBlockType* Block = Registry->FindBlock(FName(Cells[0]));
        if (!Block)
        {
            AddError(Cells[0] + TEXT(" missing"));
            continue;
        }
        const FName Open = FoundationBlockFamilies::GetUseSound(*Block, true);
        const FName Close = FoundationBlockFamilies::GetUseSound(*Block, false);
        if (Open != FName(Cells[3]) || Close != FName(Cells[4]))
            AddError(FString::Printf(TEXT("%s: %s / %s, want %s / %s"), *Cells[0], *Open.ToString(), *Close.ToString(),
                *Cells[3], *Cells[4]));
    }
    TestEqual(TEXT("csv rows checked (22 doors, 22 trapdoors, 13 gates)"), Rows, 57);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationInteractSwitchesTest, "Unnamed.Foundation.Interact.Switches", InteractTestFlags)
bool FFoundationInteractSwitchesTest::RunTest(const FString& Parameters)
{
    // Every row: put Before (a door's two halves), use it once with an empty hand looking south, then again. Both door
    // halves have changed when Execute returns, with no step between (survival.r9.switches, F 5051).
    TestEqual(TEXT("rows"), int32(UE_ARRAY_COUNT(SwitchRows)), 57);
    // One row per door, trapdoor and gate block.
    TSet<FString> RowBlocks, FamilyBlocks;
    for (const FSwitchRow& Row : SwitchRows)
        RowBlocks.Add(FString(Row.Before).Left(FString(Row.Before).Find(TEXT("["))));
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    for (int32 Index = 0; Index < Registry->GetBlockCount(); ++Index)
    {
        const EFoundationBlockFamily Family = FoundationBlockFamilies::GetFamily(Registry->GetBlockAt(Index));
        if (Family == EFoundationBlockFamily::Door || Family == EFoundationBlockFamily::Trapdoor ||
            Family == EFoundationBlockFamily::FenceGate)
            FamilyBlocks.Add(Registry->GetBlockAt(Index).Id.ToString());
    }
    TestTrue(TEXT("the rows name every door, trapdoor and gate once"),
        RowBlocks.Num() == 57 && RowBlocks.Difference(FamilyBlocks).IsEmpty() &&
            FamilyBlocks.Difference(RowBlocks).IsEmpty());
    FInteractLab Lab;
    const FIntVector Cell(0, 0, 1), Upper(0, 0, 2);
    int32 Wrong = 0;
    auto Expect = [&](const FIntVector& At, const FString& Want, const TCHAR* Row, const TCHAR* What)
    {
        if (Lab.At(At) == Want) return;
        ++Wrong;
        AddError(FString::Printf(TEXT("%s %s: %s, want %s"), Row, What, *Lab.At(At), *Want));
    };
    auto AsUpper = [](const FString& Lower) { return Lower.Replace(TEXT("half=lower"), TEXT("half=upper")); };
    for (const FSwitchRow& Row : SwitchRows)
    {
        const FString Before(Row.Before);
        const bool bDoor = Before.Contains(TEXT("_door["));
        Lab.A.World.Set(Cell, Lab.State(Before));
        Lab.A.World.Set(Upper, bDoor ? Lab.State(AsUpper(Before)) : FFoundationBlockState());
        const int64 Serial = Lab.A.GetEventSerial();
        const FFoundationResult First = Lab.UseBlock(Cell);
        Expect(Cell, Row.First, Row.Before, TEXT("first use"));
        if (bDoor) Expect(Upper, Row.UpperAfter, Row.Before, TEXT("upper half after the first use"));
        const bool bIron = Before.StartsWith(TEXT("iron_"));
        if (First.bInteracted == bIron || Lab.CountEvents(EFoundationEventKind::BlockUsed, Serial) != (bIron ? 0 : 1))
        {
            ++Wrong;
            AddError(FString::Printf(TEXT("%s: interacted %d"), Row.Before, int32(First.bInteracted)));
        }
        Lab.UseBlock(Cell);
        Expect(Cell, Row.Second, Row.Before, TEXT("second use"));
        if (bDoor) Expect(Upper, AsUpper(Row.Second), Row.Before, TEXT("upper half after the second use"));
    }
    TestEqual(TEXT("mismatches"), Wrong, 0);
    // The event carries the cell, the new state and the set's sound.
    Lab.A.World.Set(Cell, Lab.State(TEXT("bamboo_door[facing=north,half=lower,hinge=left,open=false,powered=false]")));
    Lab.A.World.Set(Upper, Lab.State(TEXT("bamboo_door[facing=north,half=upper,hinge=left,open=false,powered=false]")));
    Lab.UseBlock(Cell);
    const FFoundationEvent* Used = Lab.LastEvent(EFoundationEventKind::BlockUsed);
    TestTrue(TEXT("bamboo door: BlockUsed at the cell with the new state and block.bamboo_wood_door.open"),
        Used && Used->Cell == Cell && Used->Interaction == FName(TEXT("block.bamboo_wood_door.open")) &&
            Used->State == Lab.A.World.Get(Cell).State);
    // A use on the upper half closes both too.
    Lab.UseBlock(Upper);
    Used = Lab.LastEvent(EFoundationEventKind::BlockUsed);
    TestTrue(TEXT("the upper half's use closes both"),
        Lab.At(Cell).Contains(TEXT("open=false")) && Lab.At(Upper).Contains(TEXT("open=false")) && Used &&
            Used->Interaction == FName(TEXT("block.bamboo_wood_door.close")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationInteractGatesTest, "Unnamed.Foundation.Interact.Gates", InteractTestFlags)
bool FFoundationInteractGatesTest::RunTest(const FString& Parameters)
{
    FInteractLab Lab;
    const FIntVector Cell(0, 0, 1);
    // A gate facing north opened by a player looking north (standing south of it) keeps its facing; the switch rows
    // turn it when the player looks south.
    Lab.A.World.Set(Cell, Lab.State(TEXT("oak_fence_gate[facing=north,in_wall=false,open=false,powered=false]")));
    Lab.UseBlock(Cell, /*bFromNorth*/ false);
    TestEqual(TEXT("looking north: stays north, open"), Lab.At(Cell),
        FString(TEXT("oak_fence_gate[facing=north,in_wall=false,open=true,powered=false]")));
    // Collision follows the state: an open gate has none, a closed one its 1.5-tall box.
    TArray<FBox> Boxes;
    Lab.R().GetCollisionBoxes(Lab.A.World.Get(Cell).State, Boxes);
    TestEqual(TEXT("open gate: no collision"), Boxes.Num(), 0);
    Lab.UseBlock(Cell, false);
    Boxes.Reset();
    Lab.R().GetCollisionBoxes(Lab.A.World.Get(Cell).State, Boxes);
    TestTrue(
        TEXT("closed gate: one box, 1.5 tall"), Boxes.Num() == 1 && FMath::IsNearlyEqual(Boxes[0].Max.Z, 1.5, 1e-6));
    // The open door's slab is on its hinge side: facing north, hinge left, along the west side (x 0 to 3/16; scout B3
    // 4.1, block_state_facts.json).
    Lab.A.World.Set(Cell, Lab.State(TEXT("oak_door[facing=north,half=lower,hinge=left,open=false,powered=false]")));
    Lab.A.World.Set(Cell + FIntVector(0, 0, 1),
        Lab.State(TEXT("oak_door[facing=north,half=upper,hinge=left,open=false,powered=false]")));
    Lab.UseBlock(Cell);
    Boxes.Reset();
    Lab.R().GetCollisionBoxes(Lab.A.World.Get(Cell).State, Boxes);
    TestTrue(TEXT("open door: its slab on the hinge side"),
        Boxes.Num() == 1 && FMath::IsNearlyEqual(Boxes[0].Min.X, 0, 1e-6) &&
            FMath::IsNearlyEqual(Boxes[0].Max.X, .1875, 1e-6) && FMath::IsNearlyEqual(Boxes[0].Max.Y, 1, 1e-6));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationInteractPlacementTest, "Unnamed.Foundation.Interact.Placement", InteractTestFlags)
bool FFoundationInteractPlacementTest::RunTest(const FString& Parameters)
{
    const FIntVector Floor(0, 0, 0), Cell(0, 0, 1), Upper(0, 0, 2);
    // From 2.3 blocks north and 1.6 up: looking south and down.
    const FVector FromNorth(0, -230, 160);
    {
        // A door faces the look and the authority writes its upper half. VALUES "Door hinges": facing south with
        // nothing beside it, a click at x 0.25 gives the right hinge, at 0.75 the left.
        FInteractLab Lab;
        const FFoundationResult R =
            Lab.PlaceAt(Floor, EFoundationFace::Up, FVector(.25, .5, 1), TEXT("oak_door"), FromNorth);
        TestTrue(TEXT("door placed"), R.bPlaced);
        TestEqual(TEXT("south, x 0.25: lower half, right hinge"), Lab.At(Cell),
            FString(TEXT("oak_door[facing=south,half=lower,hinge=right,open=false,powered=false]")));
        TestEqual(TEXT("its upper half"), Lab.At(Upper),
            FString(TEXT("oak_door[facing=south,half=upper,hinge=right,open=false,powered=false]")));
        Lab.PlaceAt(FIntVector(3, 0, 0), EFoundationFace::Up, FVector(.75, .5, 1), TEXT("oak_door"), FromNorth);
        TestEqual(TEXT("south, x 0.75: left hinge"), Lab.At(FIntVector(3, 0, 1)),
            FString(TEXT("oak_door[facing=south,half=lower,hinge=left,open=false,powered=false]")));
        // Facing south, the player's right is west: the first door on its right puts the hinge left (a double door),
        // whatever the click.
        Lab.PlaceAt(FIntVector(1, 0, 0), EFoundationFace::Up, FVector(.25, .5, 1), TEXT("oak_door"), FromNorth);
        TestTrue(TEXT("a door on the right: the left hinge"), Lab.At(FIntVector(1, 0, 1)).Contains(TEXT("hinge=left")));
    }
    {
        // The upper cell occupied, or beyond the world: refused, nothing written.
        FInteractLab Lab;
        Lab.A.World.Set(Upper, Lab.State(TEXT("stone")));
        FFoundationResult R = Lab.PlaceAt(Floor, EFoundationFace::Up, FVector(.5), TEXT("oak_door"), FromNorth);
        TestTrue(TEXT("upper occupied: DestinationOccupied, nothing placed"),
            !R.bPlaced && R.Reason == EFoundationReason::DestinationOccupied && Lab.A.World.Get(Cell).State.IsAir());
        const FIntVector Top(0, 0, 14);
        Lab.A.World.Set(Top, Lab.State(TEXT("stone")));
        R = Lab.PlaceAt(Top, EFoundationFace::Up, FVector(.5), TEXT("oak_door"), FromNorth);
        TestTrue(TEXT("upper beyond the world: OutsideBounds, nothing placed"),
            !R.bPlaced && R.Reason == EFoundationReason::OutsideBounds &&
                Lab.A.World.Get(Top + FIntVector(0, 0, 1)).State.IsAir());
        // No sturdy top below: a bottom slab.
        Lab.A.World.Set(Cell, Lab.State(TEXT("oak_slab[type=bottom,waterlogged=false]")));
        Lab.A.World.Set(Upper, FFoundationBlockState());
        Lab.AimAt(
            FoundationGrid::CellMin(Cell, Lab.A.World.OriginCm) + FVector(50, 50, 50), FromNorth, TEXT("oak_door"));
        R = Lab.Run(EFoundationCommandType::UseOn, true);
        TestTrue(TEXT("on a bottom slab: CannotSurvive"), !R.bPlaced && R.Reason == EFoundationReason::CannotSurvive);
    }
    {
        // Trapdoors: on a top face the bottom half, its facing toward the player (opposite the look); on a side face
        // that face and the half from the click height; under a block the top half, facing the player; a click that
        // replaces the clicked block (short grass) faces the player, the top half off a top face.
        FInteractLab Lab;
        const TPair<const TCHAR*, FVector> Stands[] = {{TEXT("north"), FVector(0, -230, 160)},
            {TEXT("south"), FVector(0, 230, 160)}, {TEXT("west"), FVector(-230, 0, 160)},
            {TEXT("east"), FVector(230, 0, 160)}};
        for (const TPair<const TCHAR*, FVector>& Stand : Stands)
        {
            Lab.A.World.Set(Cell, FFoundationBlockState());
            Lab.PlaceAt(Floor, EFoundationFace::Up, FVector(.5), TEXT("oak_trapdoor"), Stand.Value);
            TestEqual(FString::Printf(TEXT("top face, the player %s: bottom half facing %s"), Stand.Key, Stand.Key),
                Lab.At(Cell),
                FString::Printf(
                    TEXT("oak_trapdoor[facing=%s,half=bottom,open=false,powered=false,waterlogged=false]"), Stand.Key));
        }
        Lab.A.World.Set(Cell, FFoundationBlockState());
        const FIntVector Side(3, 0, 1);
        Lab.A.World.Set(Side, Lab.State(TEXT("stone")));
        for (const EFoundationFace Face :
            {EFoundationFace::North, EFoundationFace::South, EFoundationFace::West, EFoundationFace::East})
        {
            const FIntVector Front = Side + FoundationGrid::FaceOffset(Face);
            const FVector Eye = FoundationGrid::FaceNormal(Face) * 230 + FVector(0, 0, 10);
            for (const double Height : {.75, .25})
            {
                Lab.A.World.Set(Front, FFoundationBlockState());
                Lab.PlaceAt(Side, Face, FVector(.5, .5, Height), TEXT("oak_trapdoor"), Eye);
                TestEqual(FString::Printf(TEXT("%s face at height %.2f"), FoundationGrid::FaceName(Face), Height),
                    Lab.At(Front),
                    FString::Printf(TEXT("oak_trapdoor[facing=%s,half=%s,open=false,powered=false,waterlogged=false]"),
                        FoundationGrid::FaceName(Face), Height > .5 ? TEXT("top") : TEXT("bottom")));
            }
        }
        const FIntVector Roof(-3, 0, 3), UnderRoof(-3, 0, 2);
        Lab.A.World.Set(Roof, Lab.State(TEXT("stone")));
        for (const TPair<const TCHAR*, FVector>& Stand : Stands)
        {
            Lab.A.World.Set(UnderRoof, FFoundationBlockState());
            Lab.PlaceAt(Roof, EFoundationFace::Down, FVector(.5), TEXT("oak_trapdoor"),
                FVector(Stand.Value.X, Stand.Value.Y, -80));
            TestEqual(FString::Printf(TEXT("underside, the player %s: top half facing %s"), Stand.Key, Stand.Key),
                Lab.At(UnderRoof),
                FString::Printf(
                    TEXT("oak_trapdoor[facing=%s,half=top,open=false,powered=false,waterlogged=false]"), Stand.Key));
        }
        const FIntVector Grass(0, 3, 1);
        Lab.A.World.Set(Grass - FIntVector(0, 0, 1), Lab.State(TEXT("grass_block")));
        Lab.A.World.Set(Grass, Lab.State(TEXT("short_grass")));
        Lab.AimAt(Lab.BoxPoint(Grass), FVector(0, -230, 0), TEXT("oak_trapdoor"));
        Lab.Run(EFoundationCommandType::UseOn, true);
        TestEqual(TEXT("short grass replaced from its north side: top half facing north"), Lab.At(Grass),
            FString(TEXT("oak_trapdoor[facing=north,half=top,open=false,powered=false,waterlogged=false]")));
    }
    {
        // foundation.Blocks.Interact 0: no hand use, and doors and trapdoors place as before B3a.
        FInteractCVarScope Off(TEXT("foundation.Blocks.Interact"), 0);
        FInteractLab Lab;
        Lab.A.World.Set(Cell, Lab.State(TEXT("oak_door[facing=north,half=lower,hinge=left,open=false,powered=false]")));
        Lab.A.World.Set(
            Upper, Lab.State(TEXT("oak_door[facing=north,half=upper,hinge=left,open=false,powered=false]")));
        const FFoundationResult R = Lab.UseBlock(Cell);
        TestTrue(TEXT("switch 0: an empty-hand use does nothing"),
            !R.bInteracted && Lab.At(Cell).Contains(TEXT("open=false")));
        TestFalse(TEXT("switch 0: no family placement for doors"),
            FoundationBlockFamilies::UsesFamilyPlacement(*Lab.R().FindBlock(TEXT("oak_door"))));
        TestFalse(TEXT("switch 0: no family placement for trapdoors"),
            FoundationBlockFamilies::UsesFamilyPlacement(*Lab.R().FindBlock(TEXT("oak_trapdoor"))));
        TestTrue(TEXT("switch 0: gates keep B2b's placement"),
            FoundationBlockFamilies::UsesFamilyPlacement(*Lab.R().FindBlock(TEXT("oak_fence_gate"))));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationInteractHandTest, "Unnamed.Foundation.Interact.Hand", InteractTestFlags)
bool FFoundationInteractHandTest::RunTest(const FString& Parameters)
{
    // The player stands south of a door facing north (its slab on the south side), looking north at it.
    const FIntVector Cell(0, 0, 1), Upper(0, 0, 2), Front(0, 1, 1);
    const FVector FromSouth(0, 250, 40);
    FInteractLab Lab;
    auto PutDoor = [&](const TCHAR* Block)
    {
        Lab.A.World.Set(Cell,
            Lab.State(FString::Printf(TEXT("%s[facing=north,half=lower,hinge=left,open=false,powered=false]"), Block)));
        Lab.A.World.Set(Upper,
            Lab.State(FString::Printf(TEXT("%s[facing=north,half=upper,hinge=left,open=false,powered=false]"), Block)));
        Lab.A.World.Set(Front, FFoundationBlockState());
    };
    // Sneaking with an item places it against the door; sneaking empty-handed still opens it (Docs/Items.md).
    PutDoor(TEXT("oak_door"));
    Lab.AimAt(Lab.BoxPoint(Cell), FromSouth, TEXT("stone"), /*bSneak*/ true);
    FFoundationResult R = Lab.Run(EFoundationCommandType::UseOn, true);
    TestTrue(TEXT("sneak with a block: placed against it, the door shut"),
        R.bPlaced && Lab.At(Front) == TEXT("stone") && Lab.At(Cell).Contains(TEXT("open=false")));
    PutDoor(TEXT("oak_door"));
    Lab.AimAt(Lab.BoxPoint(Cell), FromSouth, NAME_None, true);
    R = Lab.Run(EFoundationCommandType::UseOn, true);
    TestTrue(TEXT("sneak empty-handed: opens"), R.bInteracted && Lab.At(Cell).Contains(TEXT("open=true")));
    // The off hand never uses a block.
    PutDoor(TEXT("oak_door"));
    Lab.AimAt(Lab.BoxPoint(Cell), FromSouth, NAME_None);
    R = Lab.Run(EFoundationCommandType::UseOn, true, EFoundationHand::Off);
    TestTrue(TEXT("off hand: still shut"), !R.bInteracted && Lab.At(Cell).Contains(TEXT("open=false")));
    // An oak door counts as a use for the preview; an iron door has none, so a held block places against it and the
    // click passes to the item (vanilla PASS; Codex 12 (b), (d), (k)).
    Lab.AimAt(Lab.BoxPoint(Cell), FromSouth, TEXT("stone"));
    TestTrue(TEXT("oak door: a use for the preview"), Lab.A.WouldUseBlock(Lab.P(), Lab.A.Resolve(Lab.P().Facts)));
    PutDoor(TEXT("iron_door"));
    Lab.AimAt(Lab.BoxPoint(Cell), FromSouth, TEXT("stone"));
    TestFalse(TEXT("iron door: no use for the preview"), Lab.A.WouldUseBlock(Lab.P(), Lab.A.Resolve(Lab.P().Facts)));
    R = Lab.Run(EFoundationCommandType::UseOn, true);
    TestTrue(TEXT("iron door with a block: placed against it, still shut"),
        R.bPlaced && Lab.At(Front) == TEXT("stone") && Lab.At(Cell).Contains(TEXT("open=false")));
    PutDoor(TEXT("iron_door"));
    Lab.A.Options.bItemUse = true;
    Lab.Vitals.Food = 10;
    Lab.AimAt(Lab.BoxPoint(Cell), FromSouth, TEXT("bread"));
    R = Lab.Run(EFoundationCommandType::UseOn, false);
    TestFalse(TEXT("iron door with bread: the use passes"), R.IsCommitted());
    Lab.Run(EFoundationCommandType::UseItemStart, false);
    TestTrue(
        TEXT("...and the bread's use starts eating"), Lab.P().Use.bActive && Lab.At(Cell).Contains(TEXT("open=false")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationInteractSurvivalTest, "Unnamed.Foundation.Interact.Survival", InteractTestFlags)
bool FFoundationInteractSurvivalTest::RunTest(const FString& Parameters)
{
    FInteractLab Lab;
    FFoundationBlockWorld& World = Lab.A.World;
    const FFoundationBlockRegistry& Registry = Lab.R();
    const FIntVector Below(0, 0, 1), Cell(0, 0, 2);
    const FFoundationBlockState Lower =
        Lab.State(TEXT("oak_door[facing=north,half=lower,hinge=left,open=false,powered=false]"));
    // The scout's rows (family_survival.json): a sturdy top below holds the lower half.
    const TCHAR* Holds[] = {TEXT("stone"), TEXT("glass"), TEXT("oak_slab[type=top,waterlogged=false]"),
        TEXT("oak_slab[type=double,waterlogged=false]"),
        TEXT("oak_stairs[facing=north,half=top,shape=straight,waterlogged=false]"),
        TEXT("oak_trapdoor[facing=north,half=top,open=false,powered=false,waterlogged=false]")};
    const TCHAR* Fails[] = {TEXT("air"), TEXT("farmland"), TEXT("dirt_path"),
        TEXT("oak_slab[type=bottom,waterlogged=false]"),
        TEXT("oak_stairs[facing=north,half=bottom,shape=straight,waterlogged=false]"), TEXT("oak_fence"),
        TEXT("oak_leaves"), TEXT("oak_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]"),
        TEXT("oak_door[facing=north,half=upper,hinge=left,open=false,powered=false]")};
    for (const TCHAR* Support : Holds)
    {
        World.Set(Below, Lab.State(Support));
        TestEqual(FString(TEXT("holds on ")) + Support, int32(FoundationBlockFamilies::GetSurvival(World, Cell, Lower)),
            int32(EFoundationFamilySurvival::Survives));
    }
    for (const TCHAR* Support : Fails)
    {
        World.Set(Below, Lab.State(Support));
        TestEqual(FString(TEXT("fails on ")) + Support, int32(FoundationBlockFamilies::GetSurvival(World, Cell, Lower)),
            int32(EFoundationFamilySurvival::Fails));
    }
    // Every door's closed north lower and upper state against the facts' down probe: S at P, X below, air elsewhere,
    // for every candidate state X. The side and up probes have no candidate (nothing below).
    const TSharedPtr<FJsonObject> Facts =
        LoadProjectJson(TEXT("Reference/minecraft-blocks-26.3/facts/family_survival.json"));
    if (!TestTrue(TEXT("family_survival.json loads"), Facts.IsValid())) return false;
    const TArray<TSharedPtr<FJsonValue>>& Signatures = Facts->GetArrayField(TEXT("signatures"));
    const TSharedPtr<FJsonObject> Subjects = Facts->GetObjectField(TEXT("subjects"));
    int32 Checked = 0, Wrong = 0, Doors = 0;
    for (int32 Index = 0; Index < Registry.GetBlockCount(); ++Index)
    {
        const FFoundationBlockType& Door = Registry.GetBlockAt(Index);
        if (FoundationBlockFamilies::GetFamily(Door) != EFoundationBlockFamily::Door) continue;
        ++Doors;
        const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
        if (!TestTrue(Door.Id.ToString() + TEXT(" in the facts"), Subjects->TryGetArrayField(Door.Id.ToString(), Rows)))
            continue;
        for (const TCHAR* Half : {TEXT("lower"), TEXT("upper")})
        {
            const FFoundationBlockState S = Lab.State(FString::Printf(
                TEXT("%s[facing=north,half=%s,hinge=left,open=false,powered=false]"), *Door.Id.ToString(), Half));
            if (!TestTrue(TEXT("a facts row per state"), Rows->IsValidIndex(S.Id - Door.MinStateId))) continue;
            const TSharedPtr<FJsonValue>& Row = (*Rows)[S.Id - Door.MinStateId];
            if (!TestTrue(TEXT("a rule-bearing row"), Row.IsValid() && Row->Type == EJson::Array)) continue;
            const TArray<TSharedPtr<FJsonValue>>& Probes = Row->AsArray();
            for (int32 Probe = 0; Probe < 6; ++Probe)
                if (Probe != 4 && Probes[Probe]->AsNumber() != 0)
                    AddError(Door.Id.ToString() + TEXT(": a side or up candidate"));
            const TSharedPtr<FJsonObject> Down = Signatures[int32(Probes[4]->AsNumber())]->AsObject();
            for (int32 X = 0; X < Registry.GetStateLimit(); ++X)
            {
                const FFoundationBlockState Candidate{uint16(X)};
                if (X != 0 && !Registry.IsKnown(Candidate)) continue;
                const FFoundationBlockType& Block = Registry.GetBlock(Candidate);
                bool bWant = false;
                if (const TSharedPtr<FJsonValue> Entry =
                        Down->TryGetField(X == 0 ? FString(TEXT("air")) : Block.Id.ToString()))
                {
                    if (Entry->Type == EJson::String)
                        bWant = true;
                    else
                        for (const TSharedPtr<FJsonValue>& Local : Entry->AsArray())
                            bWant |= int32(Local->AsNumber()) == X - Block.MinStateId;
                }
                World.Set(Below, Candidate);
                const bool bGot =
                    FoundationBlockFamilies::GetSurvival(World, Cell, S) == EFoundationFamilySurvival::Survives;
                ++Checked;
                if (bGot != bWant && ++Wrong <= 20)
                    AddError(FString::Printf(TEXT("%s on %s: %d, want %d"), *Registry.ToString(S),
                        *Registry.ToString(Candidate), int32(bGot), int32(bWant)));
            }
        }
    }
    TestEqual(TEXT("doors"), Doors, 22);
    AddInfo(FString::Printf(TEXT("%d door-on-candidate cases checked"), Checked));
    TestEqual(TEXT("mismatches against family_survival.json"), Wrong, 0);
    // Through the authority: a door on a placed stone pops, both halves, when the stone is mined.
    FInteractLab Pop;
    const FIntVector Support(0, 0, 1), Door(0, 0, 2), DoorUpper(0, 0, 3);
    Pop.PlaceAt(FIntVector(0, 0, 0), EFoundationFace::Up, FVector(.5), TEXT("stone"), FVector(0, -230, 160));
    Pop.PlaceAt(Support, EFoundationFace::Up, FVector(.5), TEXT("oak_door"), FVector(0, -230, 160));
    TestTrue(TEXT("a door on the placed stone"),
        Pop.At(Door).Contains(TEXT("half=lower")) && Pop.At(DoorUpper).Contains(TEXT("half=upper")));
    const int64 Serial = Pop.A.GetEventSerial();
    Pop.Break(Support);
    TestTrue(TEXT("its support mined: both halves popped"),
        Pop.A.World.Get(Door).State.IsAir() && Pop.A.World.Get(DoorUpper).State.IsAir() &&
            Pop.CountEvents(EFoundationEventKind::Popped, Serial) == 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationInteractHingeTest, "Unnamed.Foundation.Interact.Hinge", InteractTestFlags)
bool FFoundationInteractHingeTest::RunTest(const FString& Parameters)
{
    // Every case of the black-box probe (Tests/Probes/door-hinge-26.3.json, ItemStack.useOn in 26.3): the lower half's
    // facing and hinge from the four cells beside it and the click.
    const TSharedPtr<FJsonObject> Probe = LoadProjectJson(TEXT("Tests/Probes/door-hinge-26.3.json"));
    if (!TestTrue(TEXT("door-hinge-26.3.json loads"), Probe.IsValid())) return false;
    TestEqual(TEXT("the probe's anomalies"), Probe->GetArrayField(TEXT("anomalies")).Num(), 0);
    FInteractLab Lab;
    FFoundationBlockWorld& World = Lab.A.World;
    const FFoundationBlockType& Door = *Lab.R().FindBlock(TEXT("oak_door"));
    const FIntVector Cell(0, 0, 2);
    auto Strip = [](const FString& Text) { return Text.Replace(TEXT("minecraft:"), TEXT("")); };
    // Left is the facing turned counter-clockwise seen from above (the probe's conventions).
    const TMap<EFoundationFace, EFoundationFace> LeftOf = {{EFoundationFace::North, EFoundationFace::West},
        {EFoundationFace::West, EFoundationFace::South}, {EFoundationFace::South, EFoundationFace::East},
        {EFoundationFace::East, EFoundationFace::North}};
    int32 Cases = 0, Wrong = 0;
    for (const TSharedPtr<FJsonValue>& Value : Probe->GetArrayField(TEXT("cases")))
    {
        const TSharedPtr<FJsonObject> Case = Value->AsObject();
        const FString Facing = Case->GetStringField(TEXT("facing"));
        EFoundationFace Face = EFoundationFace::None;
        for (int32 Side = 0; Side < 4; ++Side)
            if (Facing == FoundationGrid::FaceName(EFoundationFace(Side))) Face = EFoundationFace(Side);
        if (Face == EFoundationFace::None)
        {
            AddError(TEXT("unknown facing ") + Facing);
            continue;
        }
        const FIntVector Left = FoundationGrid::FaceOffset(LeftOf[Face]);
        for (int32 Z = 1; Z <= 3; ++Z)
            for (int32 X = -1; X <= 1; ++X)
                for (int32 Y = -1; Y <= 1; ++Y)
                    World.Set(FIntVector(X, Y, Z), FFoundationBlockState());
        World.Set(Cell - FIntVector(0, 0, 1), Lab.State(TEXT("stone")));
        World.Set(Cell + Left, Lab.State(Strip(Case->GetStringField(TEXT("left_lower")))));
        World.Set(Cell + Left + FIntVector(0, 0, 1), Lab.State(Strip(Case->GetStringField(TEXT("left_upper")))));
        World.Set(Cell - Left, Lab.State(Strip(Case->GetStringField(TEXT("right_lower")))));
        World.Set(Cell - Left + FIntVector(0, 0, 1), Lab.State(Strip(Case->GetStringField(TEXT("right_upper")))));
        FFoundationPlacementContext Context;
        Context.ClickedCell = Cell - FIntVector(0, 0, 1);
        Context.ClickedFace = EFoundationFace::Up;
        Context.Destination = Cell;
        // The probe's yaw: 0 south, 90 west, 180 north, 270 east; pitch 45.
        Context.Yaw = Face == EFoundationFace::South ? 0
            : Face == EFoundationFace::West          ? 90
            : Face == EFoundationFace::North         ? 180
                                                     : 270;
        Context.Pitch = 45;
        // click: the hit's lateral coordinate in the cell, Minecraft x (our X) facing north or south, z (our Y)
        // facing east or west; the other one 0.5.
        const double Click = Case->GetNumberField(TEXT("click"));
        const bool bNorthSouth = Face == EFoundationFace::North || Face == EFoundationFace::South;
        Context.ClickPointCm = FoundationGrid::CellMin(Cell, World.OriginCm) +
            FVector(bNorthSouth ? Click : .5, bNorthSouth ? .5 : Click, 0) * UFoundationProfile::BlockCm;
        const FFoundationBlockState Got = FoundationBlockFamilies::GetStateForPlacement(World, Door, Context, false);
        const FString Want = Strip(Case->GetStringField(TEXT("lower")));
        ++Cases;
        // The upper half the authority writes is the lower half's state with half=upper.
        const bool bUpperOk = Lab.R().ToString(Got).Replace(TEXT("half=lower"), TEXT("half=upper")) ==
            Strip(Case->GetStringField(TEXT("upper")));
        if ((Lab.R().ToString(Got) != Want || !bUpperOk) && ++Wrong <= 20)
            AddError(FString::Printf(TEXT("%s %s click %.2f, left %s / %s, right %s / %s: %s, want %s"),
                *Case->GetStringField(TEXT("part")), *Facing, Click, *Case->GetStringField(TEXT("left_lower")),
                *Case->GetStringField(TEXT("left_upper")), *Case->GetStringField(TEXT("right_lower")),
                *Case->GetStringField(TEXT("right_upper")), *Lab.R().ToString(Got), *Want));
    }
    TestEqual(TEXT("probe cases"), Cases, 11100);
    TestEqual(TEXT("mismatches"), Wrong, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationInteractEditsTest, "Unnamed.Foundation.Interact.Edits", InteractTestFlags)
bool FFoundationInteractEditsTest::RunTest(const FString& Parameters)
{
    const FIntVector Cell(0, 0, 1), Upper(0, 0, 2);
    for (const bool bUpper : {false, true})
    {
        // Mining either half removes both and yields one door: the mined half drops it, the other pops (no drop).
        FInteractLab Lab;
        Lab.PlaceAt(FIntVector(0, 0, 0), EFoundationFace::Up, FVector(.5), TEXT("oak_door"), FVector(0, -230, 160));
        // A survival break (a creative one, or the lab's infinite supply, drops nothing).
        Lab.Vitals.Mode = EFoundationPlayerMode::Survival;
        Lab.P().Inventory.bInfiniteSupply = false;
        const int64 Serial = Lab.A.GetEventSerial();
        const FFoundationResult R = Lab.Break(bUpper ? Upper : Cell);
        TestEqual(TEXT("the other half popped"), Lab.CountEvents(EFoundationEventKind::Popped, Serial), 1);
        int32 Items = 0;
        for (const FFoundationLootDrop& Drop : R.Drops)
            Items += Drop.Count;
        AddInfo(FString::Printf(TEXT("mined %s: broken %d, outcome %d, reason %s, %d items, lower %s, upper %s"),
            bUpper ? TEXT("upper") : TEXT("lower"), int32(R.bBroken), int32(R.Outcome), LexToString(R.Reason), Items,
            *Lab.At(Cell), *Lab.At(Upper)));
        TestTrue(bUpper ? TEXT("upper mined: both gone, one item") : TEXT("lower mined: both gone, one item"),
            R.bBroken && Lab.A.World.Get(Cell).State.IsAir() && Lab.A.World.Get(Upper).State.IsAir() && Items == 1);
    }
    {
        // The neighbour pass rings the written upper half and the used door: a fence south of the upper cell joins the
        // closed door's slab (facing north: on the south side), and lets go when the door opens.
        FInteractLab Lab;
        const FIntVector Fence(0, 1, 2);
        Lab.A.World.Set(Fence, Lab.State(TEXT("oak_fence")));
        Lab.PlaceAt(
            FIntVector(0, 0, 0), EFoundationFace::Up, FVector(.25, .5, 1), TEXT("oak_door"), FVector(0, 230, 160));
        TestTrue(TEXT("a door facing north"), Lab.At(Cell).Contains(TEXT("facing=north")));
        TestTrue(TEXT("the fence joins the upper half's slab"), Lab.At(Fence).Contains(TEXT("north=true")));
        Lab.UseBlock(Upper);
        TestTrue(TEXT("the door opened: the fence lets go"),
            Lab.At(Cell).Contains(TEXT("open=true")) && Lab.At(Fence).Contains(TEXT("north=false")));
    }
    {
        // A double plant replaced by its own kind keeps a whole pair and pops nothing (B3a: a half that still pairs
        // keeps its partner). The final world is the one before B3a.
        FInteractLab Lab;
        Lab.A.World.Set(Cell - FIntVector(0, 0, 1), Lab.State(TEXT("grass_block")));
        Lab.A.World.Set(Cell, Lab.State(TEXT("tall_grass[half=lower]")));
        Lab.A.World.Set(Upper, Lab.State(TEXT("tall_grass[half=upper]")));
        const int64 Serial = Lab.A.GetEventSerial();
        Lab.AimAt(Lab.BoxPoint(Cell), FVector(0, -230, 60), TEXT("tall_grass"));
        const FFoundationResult R = Lab.Run(EFoundationCommandType::UseOn, true);
        TestTrue(TEXT("tall grass on tall grass: placed"), R.bPlaced);
        TestEqual(TEXT("lower"), Lab.At(Cell), FString(TEXT("tall_grass[half=lower]")));
        TestEqual(TEXT("upper"), Lab.At(Upper), FString(TEXT("tall_grass[half=upper]")));
        TestEqual(TEXT("pops"), Lab.CountEvents(EFoundationEventKind::Popped, Serial), 0);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationInteractGrazingTest, "Unnamed.Foundation.Interact.Grazing", InteractTestFlags)
bool FFoundationInteractGrazingTest::RunTest(const FString& Parameters)
{
    // A sheep grazing (T6 20b1, FFoundationAuthority::EatGrass) turns the grass block under it to dirt without the
    // edit path's settling: a door and tall grass beside it keep both halves, nothing pops, and a fence beside it keeps
    // its sides (B3a's paired-half guard and ring stay out of grazing).
    FInteractLab Lab;
    Lab.A.Options.bMobs = true;
    const FIntVector Grass(0, 0, 0), DoorCell(1, 0, 1), TallGrass(-1, 0, 1), Fence(0, 1, 1);
    for (const FIntVector& Ground : {Grass, DoorCell - FIntVector(0, 0, 1), TallGrass - FIntVector(0, 0, 1)})
        Lab.A.World.Set(Ground, Lab.State(TEXT("grass_block[snowy=false]")));
    const FString Lower(TEXT("oak_door[facing=north,half=lower,hinge=left,open=false,powered=false]"));
    Lab.A.World.Set(DoorCell, Lab.State(Lower));
    Lab.A.World.Set(DoorCell + FIntVector(0, 0, 1), Lab.State(Lower.Replace(TEXT("lower"), TEXT("upper"))));
    Lab.A.World.Set(TallGrass, Lab.State(TEXT("tall_grass[half=lower]")));
    Lab.A.World.Set(TallGrass + FIntVector(0, 0, 1), Lab.State(TEXT("tall_grass[half=upper]")));
    Lab.A.World.Set(Fence, Lab.State(TEXT("oak_fence")));
    const FString FenceBefore = Lab.At(Fence);
    const int32 Id = Lab.A.SpawnMob(EFoundationMobType::Sheep, FVector(50, 50, 100));
    auto Steps = [&Lab](int32 Count)
    {
        for (int32 Index = 0; Index < Count; ++Index)
        {
            FFoundationPlayerFacts Facts = Lab.P().Facts;
            ++Facts.Step;
            Lab.A.SetFacts(Lab.Player, Facts);
            Lab.A.Step();
        }
    };
    Steps(1);
    if (!TestNotNull(TEXT("the sheep"), Lab.A.FindMob(Id))) return false;
    Lab.A.FindMob(Id)->GrazeTicks = 40;
    const int64 Serial = Lab.A.GetEventSerial();
    Steps(36);
    TestEqual(TEXT("the grass under the sheep is dirt"), Lab.At(Grass), FString(TEXT("dirt")));
    TestEqual(TEXT("the door's lower half"), Lab.At(DoorCell), Lower);
    TestTrue(TEXT("the door's upper half"), Lab.At(DoorCell + FIntVector(0, 0, 1)).Contains(TEXT("half=upper")));
    TestTrue(TEXT("the tall grass's two halves"),
        Lab.At(TallGrass) == TEXT("tall_grass[half=lower]") &&
            Lab.At(TallGrass + FIntVector(0, 0, 1)) == TEXT("tall_grass[half=upper]"));
    TestEqual(TEXT("the fence"), Lab.At(Fence), FenceBefore);
    TestEqual(TEXT("pops"), Lab.CountEvents(EFoundationEventKind::Popped, Serial), 0);
    return true;
}
#endif
