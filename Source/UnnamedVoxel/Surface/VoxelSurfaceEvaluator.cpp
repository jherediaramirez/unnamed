#include "Surface/VoxelSurfaceEvaluator.h"
#include "Biome/VoxelBiomeStorage.h"
#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Random/VoxelRandom.h"
#include "VoxelConstants.h"

namespace
{
    constexpr uint64 SurfaceDepthSalt = VoxelRandom::Salt("surface/depth");
    constexpr uint64 SurfaceBandsSalt = VoxelRandom::Salt("surface/bands");
    // Biome masks hold up to this many 64-bit words (256 biomes).
    constexpr int32 SurfaceMaxBiomeWords = 4;
    const TCHAR* const SurfaceCodeNoises[] = {
        TEXT("minecraft:surface"), TEXT("minecraft:surface_secondary"), TEXT("minecraft:clay_bands_offset")};

    int32 SurfaceResolveAnchor(const FVoxelVerticalAnchor& Anchor)
    {
        switch (Anchor.Kind)
        {
        case EVoxelAnchorKind::AboveBottom:
            return VoxelConstants::MinZ + Anchor.Value;
        case EVoxelAnchorKind::BelowTop:
            return VoxelConstants::MaxZ - Anchor.Value;
        default:
            return Anchor.Value;
        }
    }

    // A random name's salt, as VoxelRandom::Salt makes one from a literal.
    uint64 SurfaceRuntimeSalt(const FString& Name)
    {
        const FTCHARToUTF8 Utf8(*Name);
        return VoxelRandom::Mix64(VoxelRandom::Fnv1a64(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()));
    }

    uint16 SurfaceFindBlock(const FVoxelBlockTable& Table, const FString& Id, bool& bFound)
    {
        const FString Name = Id.StartsWith(TEXT("minecraft:")) ? Id.RightChop(10) : Id;
        if (Name == TEXT("air"))
        {
            bFound = true;
            return 0;
        }
        const uint16 State = Table.FindState(FName(*Name));
        bFound = State != 0;
        return State;
    }
} // namespace

TArray<FString> FVoxelSurfaceEvaluator::GetNoiseIds(const FVoxelSurfaceRules& Rules)
{
    TArray<FString> Result;
    for (const FVoxelSurfaceCondition& Condition : Rules.Conditions)
        if (Condition.Op == EVoxelSurfaceConditionOp::NoiseThreshold && Rules.Strings.IsValidIndex(Condition.First))
            Result.AddUnique(Rules.Strings[Condition.First]);
    for (const TCHAR* Id : SurfaceCodeNoises)
        Result.AddUnique(Id);
    return Result;
}

TArray<FString> FVoxelSurfaceEvaluator::GetVeinFunctionIds(const FVoxelSurfaceRules& Rules)
{
    TArray<FString> Result;
    for (const FVoxelSurfaceRule& Rule : Rules.Rules)
        if (Rule.Op == EVoxelSurfaceRuleOp::OreVein)
            for (const int32 Id : {Rule.Density, Rule.Richness, Rule.FillerGap})
                if (Rules.Strings.IsValidIndex(Id)) Result.AddUnique(Rules.Strings[Id]);
    return Result;
}

bool FVoxelSurfaceEvaluator::Compile(const FVoxelSurfaceInputs& Inputs, const FVoxelBlockTable& Table,
    const FVoxelBiomeTable& BiomeTable, uint64 WorldSeed, FString* OutError)
{
    *this = FVoxelSurfaceEvaluator();
    const auto Fail = [OutError](const FString& Message)
    {
        if (OutError) *OutError = Message;
        return false;
    };
    const FVoxelSurfaceRules& Rules = Inputs.Rules;
    FString Invalid;
    if (!Rules.Validate(&Invalid)) return Fail(TEXT("surface rules: ") + Invalid);
    Seed = WorldSeed;

    NoiseIds = GetNoiseIds(Rules);
    if (NoiseIds.Num() > FVoxelSurfaceColumn::MaxNoises)
        return Fail(FString::Printf(TEXT("surface rules read %d noises; at most %d fit a column"), NoiseIds.Num(),
            FVoxelSurfaceColumn::MaxNoises));
    for (const FString& Id : NoiseIds)
    {
        const FVoxelDensityNoise* Description =
            Inputs.Noises.FindByPredicate([&Id](const FVoxelDensityNoise& Noise) { return Noise.Name == Id; });
        if (!Description) return Fail(FString::Printf(TEXT("surface rules: no noise %s"), *Id));
        Noises.Emplace(WorldSeed, *Description);
    }
    SurfaceSlot = FindNoiseSlot(SurfaceCodeNoises[0]);
    SecondarySlot = FindNoiseSlot(SurfaceCodeNoises[1]);
    BandsSlot = FindNoiseSlot(SurfaceCodeNoises[2]);

    BiomeWords = FMath::Max(1, (BiomeTable.Biomes.Num() + 63) / 64);
    if (BiomeWords > SurfaceMaxBiomeWords)
        return Fail(FString::Printf(TEXT("surface rules: %d biomes; at most %d fit a mask"), BiomeTable.Biomes.Num(),
            SurfaceMaxBiomeWords * 64));

    Conditions.SetNum(Rules.Conditions.Num());
    for (int32 Index = 0; Index < Rules.Conditions.Num(); ++Index)
    {
        const FVoxelSurfaceCondition& Source = Rules.Conditions[Index];
        FCondition& Data = Conditions[Index];
        Data.Op = Source.Op;
        Data.Input = Source.Input;
        Data.bIs3D = Source.bIs3D;
        Data.bAddDepth = Source.bAddDepth;
        Data.bCeiling = Source.bCeiling;
        Data.Min = Source.MinThreshold;
        Data.Max = Source.MaxThreshold;
        Data.Multiplier = Source.SurfaceDepthMultiplier;
        Data.Offset = Source.Offset;
        Data.SecondaryRange = Source.SecondaryDepthRange;
        Data.Anchor = SurfaceResolveAnchor(Source.Anchor);
        Data.FalseAnchor = SurfaceResolveAnchor(Source.FalseAnchor);
        switch (Source.Op)
        {
        case EVoxelSurfaceConditionOp::Biome:
            Data.Mask = BiomeMasks.Num();
            BiomeMasks.AddZeroed(BiomeWords);
            for (int32 Entry = Source.First; Entry < Source.First + Source.Count; ++Entry)
            {
                // A biome the world type does not have never matches.
                const int32 Biome = BiomeTable.FindBiome(Rules.Strings[Entry]);
                if (Biome != INDEX_NONE) BiomeMasks[Data.Mask + Biome / 64] |= 1ull << (Biome % 64);
            }
            break;
        case EVoxelSurfaceConditionOp::NoiseThreshold:
            Data.Noise = FindNoiseSlot(Rules.Strings[Source.First]);
            break;
        case EVoxelSurfaceConditionOp::VerticalGradient:
            Data.Salt = SurfaceRuntimeSalt(Rules.Strings[Source.First]);
            break;
        default:
            break;
        }
    }

    // Blocks by string index, resolved once.
    TArray<int32> States;
    States.Init(INDEX_NONE, Rules.Strings.Num());
    for (const FVoxelSurfaceRule& Rule : Rules.Rules)
    {
        if (Rule.Op == EVoxelSurfaceRuleOp::Bandlands) bHasBands = true;
        if (Rule.Op != EVoxelSurfaceRuleOp::Block || States[Rule.Block] != INDEX_NONE) continue;
        bool bFound = false;
        States[Rule.Block] = SurfaceFindBlock(Table, Rules.Strings[Rule.Block], bFound);
        if (!bFound) return Fail(FString::Printf(TEXT("surface rules: no block %s"), *Rules.Strings[Rule.Block]));
    }
    if (bHasBands)
    {
        FString Error;
        BuildBands(Table, WorldSeed, Error);
        if (!Error.IsEmpty()) return Fail(Error);
    }
    // Ore veins by rule index: their blocks and their functions' nodes in Inputs.Functions.
    TMap<int32, FVoxelOreVein> VeinRules;
    for (int32 Index = 0; Index < Rules.Rules.Num(); ++Index)
    {
        const FVoxelSurfaceRule& Rule = Rules.Rules[Index];
        if (Rule.Op != EVoxelSurfaceRuleOp::OreVein) continue;
        FVoxelOreVein Vein;
        for (const TPair<int32, uint16*>& Block :
            {TPair<int32, uint16*>(Rule.Block, &Vein.Ore), TPair<int32, uint16*>(Rule.RawOreBlock, &Vein.RawOre),
                TPair<int32, uint16*>(Rule.FillerBlock, &Vein.Filler)})
        {
            bool bFound = false;
            *Block.Value = SurfaceFindBlock(Table, Rules.Strings[Block.Key], bFound);
            if (!bFound) return Fail(FString::Printf(TEXT("surface rules: no block %s"), *Rules.Strings[Block.Key]));
        }
        for (const TPair<int32, int32*>& Function : {TPair<int32, int32*>(Rule.Density, &Vein.Density),
                 TPair<int32, int32*>(Rule.Richness, &Vein.Richness), TPair<int32, int32*>(Rule.FillerGap, &Vein.Gap)})
        {
            *Function.Value = Inputs.Functions.FindFunction(Rules.Strings[Function.Key]);
            if (*Function.Value == INDEX_NONE)
                return Fail(FString::Printf(
                    TEXT("surface rules: no density function %s for an ore vein"), *Rules.Strings[Function.Key]));
        }
        Vein.RawOreChance = float(Rule.RawOreChance);
        VeinRules.Add(Index, Vein);
    }
    TArray<FVoxelOreVein> Veins;

    // The tree as a linear program: a condition that fails jumps past its rule, and the first block given ends it.
    struct FCompiler
    {
        const FVoxelSurfaceRules& Rules;
        const TArray<int32>& BlockStates;
        const TMap<int32, FVoxelOreVein>& VeinRules;
        TArray<FVoxelOreVein>& Veins;
        TArray<FInstruction>& Out;

        void Rule(int32 Index)
        {
            const FVoxelSurfaceRule& Data = Rules.Rules[Index];
            switch (Data.Op)
            {
            case EVoxelSurfaceRuleOp::Sequence:
                for (int32 Entry = Data.First; Entry < Data.First + Data.Count; ++Entry)
                    Rule(Rules.RuleList[Entry]);
                break;
            case EVoxelSurfaceRuleOp::Condition:
            {
                // Inversions fold into the test.
                int32 Condition = Data.Condition;
                bool bExpect = true;
                while (Rules.Conditions[Condition].Op == EVoxelSurfaceConditionOp::Not)
                {
                    Condition = Rules.Conditions[Condition].Input;
                    bExpect = !bExpect;
                }
                FInstruction Instruction;
                Instruction.Op = EOp::Test;
                Instruction.bExpect = bExpect;
                Instruction.Condition = Condition;
                const int32 At = Out.Add(Instruction);
                Rule(Data.Then);
                Out[At].Jump = Out.Num();
                break;
            }
            case EVoxelSurfaceRuleOp::Block:
            {
                FInstruction Instruction;
                Instruction.Op = EOp::Give;
                Instruction.State = uint16(BlockStates[Data.Block]);
                Out.Add(Instruction);
                break;
            }
            case EVoxelSurfaceRuleOp::Bandlands:
            {
                FInstruction Instruction;
                Instruction.Op = EOp::Bands;
                Out.Add(Instruction);
                break;
            }
            case EVoxelSurfaceRuleOp::OreVein:
            {
                // One vein per occurrence, numbered in program order.
                FInstruction Instruction;
                Instruction.Op = EOp::Vein;
                Instruction.State = uint16(Veins.Add(VeinRules.FindChecked(Index)));
                Out.Add(Instruction);
                break;
            }
            default:
                break;
            }
        }
    };
    FCompiler{Rules, States, VeinRules, Veins, Program}.Rule(Rules.Root);
    {
        FString Reason;
        if (!OreVeins.Compile(Inputs.Functions, WorldSeed, Veins, &Reason))
            return Fail(TEXT("surface rules: ") + Reason);
    }
    // Jumps only go forward, so one pass from the end finds what each instruction can still read.
    const auto ReadsContext = [this](int32 Condition)
    {
        while (Conditions[Condition].Op == EVoxelSurfaceConditionOp::Not)
            Condition = Conditions[Condition].Input;
        const FCondition& Data = Conditions[Condition];
        return Data.Op == EVoxelSurfaceConditionOp::StoneDepth || Data.Op == EVoxelSurfaceConditionOp::Water ||
            (Data.Op == EVoxelSurfaceConditionOp::YAbove && Data.bAddDepth);
    };
    // Below the preliminary surface (where only the deep table resumes), above_preliminary_surface is false.
    NeedsContext.Init(false, Program.Num() + 1);
    VeinsFrom.Init(0, Program.Num() + 1);
    for (int32 Pc = Program.Num() - 1; Pc >= 0; --Pc)
    {
        const FInstruction& Instruction = Program[Pc];
        VeinsFrom[Pc] = VeinsFrom[Pc + 1] | (Instruction.Op == EOp::Vein ? uint8(1u << Instruction.State) : uint8(0));
        // A vein that gives nothing goes on to the next instruction.
        if (Instruction.Op == EOp::Vein) NeedsContext[Pc] = NeedsContext[Pc + 1];
        if (Instruction.Op != EOp::Test) continue;
        if (Conditions[Instruction.Condition].Op == EVoxelSurfaceConditionOp::AbovePreliminarySurface)
            NeedsContext[Pc] = NeedsContext[Instruction.bExpect ? Instruction.Jump : Pc + 1];
        else
            NeedsContext[Pc] =
                ReadsContext(Instruction.Condition) || NeedsContext[Pc + 1] || NeedsContext[Instruction.Jump];
    }
    bCompiled = true;
    return true;
}

void FVoxelSurfaceEvaluator::BuildBands(const FVoxelBlockTable& Table, uint64 WorldSeed, FString& OutError)
{
    const TCHAR* const Names[] = {TEXT("terracotta"), TEXT("orange_terracotta"), TEXT("yellow_terracotta"),
        TEXT("brown_terracotta"), TEXT("red_terracotta"), TEXT("white_terracotta"), TEXT("light_gray_terracotta")};
    uint16 Found[UE_ARRAY_COUNT(Names)] = {};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
    {
        Found[Index] = Table.FindState(FName(Names[Index]));
        if (!Found[Index])
        {
            OutError = FString::Printf(TEXT("surface rules: no block %s for the badlands bands"), Names[Index]);
            return;
        }
    }
    const uint16 Terracotta = Found[0], Orange = Found[1], Yellow = Found[2], Brown = Found[3], Red = Found[4],
                 White = Found[5], LightGray = Found[6];

    // Our own pattern, aimed at 26.3's look (plain terracotta about half, orange singles every few blocks, short
    // yellow, brown and red runs, white beside light gray) from one seeded stream.
    FVoxelXoroshiro Random(VoxelRandom::Hash64(WorldSeed, SurfaceBandsSalt, 0, 0));
    Bands.Init(Terracotta, BandPeriod);
    for (int32 Index = VoxelRandom::ToIndex(Random.Next(), 4); Index < BandPeriod;
        Index += VoxelRandom::ToRange(Random.Next(), 4, 8))
        Bands[Index] = Orange;
    const auto Runs = [&](uint16 State, int32 Count, int32 MinLength, int32 MaxLength)
    {
        for (int32 Run = 0; Run < Count; ++Run)
        {
            const int32 Start = VoxelRandom::ToIndex(Random.Next(), BandPeriod);
            const int32 Length = VoxelRandom::ToRange(Random.Next(), MinLength, MaxLength);
            for (int32 Step = 0; Step < Length; ++Step)
                Bands[(Start + Step) % BandPeriod] = State;
        }
    };
    Runs(Yellow, 4, 1, 6);
    Runs(Brown, 6, 1, 4);
    Runs(Red, 8, 1, 3);
    for (int32 Pair = 0; Pair < 10; ++Pair)
    {
        const int32 Start = VoxelRandom::ToIndex(Random.Next(), BandPeriod);
        Bands[Start] = White;
        Bands[(Start + ((Random.Next() >> 63) ? 1 : BandPeriod - 1)) % BandPeriod] = LightGray;
    }
}

int32 FVoxelSurfaceEvaluator::FindNoiseSlot(FStringView Id) const
{
    return NoiseIds.IndexOfByPredicate([Id](const FString& Name) { return FStringView(Name) == Id; });
}

uint16 FVoxelSurfaceEvaluator::GetBand(int32 Z, int32 BandOffset) const
{
    if (Bands.IsEmpty()) return 0;
    const int32 Index = (Z + BandOffset) % BandPeriod;
    return Bands[Index < 0 ? Index + BandPeriod : Index];
}

double FVoxelSurfaceEvaluator::GetNoise(FVoxelSurfaceColumn& Column, int32 Slot) const
{
    if (!(Column.NoiseKnown >> Slot & 1)) Column.SetNoise(Slot, Noises[Slot].Sample(Column.X, 0, Column.Y));
    return Column.Noise[Slot];
}

void FVoxelSurfaceEvaluator::PrepareColumn(
    FVoxelSurfaceColumn& Column, int32 X, int32 Y, double PreliminarySurface, bool bSteep) const
{
    Column = FVoxelSurfaceColumn();
    Column.X = X;
    Column.Y = Y;
    Column.bSteep = bSteep;
    const double Jitter = VoxelRandom::ToUnitDouble(VoxelRandom::Hash64(Seed, SurfaceDepthSalt, X, Y));
    Column.SurfaceDepth = int32(GetNoise(Column, SurfaceSlot) * 2.75 + 3. + Jitter * .25);
    Column.MinSurfaceLevel = FMath::FloorToInt32(PreliminarySurface) + Column.SurfaceDepth - 8;
}

int32 FVoxelSurfaceEvaluator::GetStoneDepthLimit(const FCondition& Data, FVoxelSurfaceColumn& Column) const
{
    int32 Limit = 1 + Data.Offset + (Data.bAddDepth ? Column.SurfaceDepth : 0);
    // The secondary depth is not clamped, and it counts whether or not the surface depth does.
    if (Data.SecondaryRange != 0) Limit += int32((GetNoise(Column, SecondarySlot) + 1) / 2 * Data.SecondaryRange);
    return Limit;
}

void FVoxelSurfaceEvaluator::GetLayerMask(const FVoxelSurfaceChunk& Chunk, int32 Z, uint64* Out) const
{
    const int32 Layer = (Z - VoxelConstants::MinZ) / FVoxelBiomeStorage::QuartSize;
    const int32 Above = FMath::Min(Layer + 1, FVoxelBiomeStorage::Layers - 1);
    for (int32 Word = 0; Word < BiomeWords; ++Word)
        Out[Word] = Chunk.LayerBiomes[Layer * BiomeWords + Word] | Chunk.LayerBiomes[Above * BiomeWords + Word];
}

void FVoxelSurfaceEvaluator::PrepareChunk(
    FVoxelSurfaceChunk& Chunk, const FVoxelBiomeStorage* Biomes, bool bShortcut, bool bVeins, bool bVeinShortcut) const
{
    Chunk.Biomes = Biomes;
    Chunk.bShortcut = bShortcut;
    Chunk.bVeins = bVeins && OreVeins.Num() > 0;
    Chunk.bVeinShortcut = bVeinShortcut;
    Chunk.Veins.bFilled = false;
    Chunk.LayerBiomes.Reset();
    Chunk.DeepStates.Reset();
    Chunk.DeepVeins.Reset();
    Chunk.InteriorStates.Reset();
    Chunk.InteriorVeins.Reset();
    Chunk.JitterLayers.Reset();
    Chunk.DeepBlocks = Chunk.InteriorBlocks = Chunk.FullBlocks = Chunk.InteriorRuns = 0;
    Chunk.Steps = 0;
    if (!bShortcut) return;
    constexpr int32 PerLayer = FVoxelBiomeStorage::Width * FVoxelBiomeStorage::Width;
    Chunk.LayerBiomes.Init(0, FVoxelBiomeStorage::Layers * BiomeWords);
    if (Biomes && !Biomes->IsEmpty())
    {
        const TConstArrayView<uint8> Quarts = Biomes->GetQuarts();
        for (int32 Index = 0; Index < Quarts.Num(); ++Index)
        {
            const uint8 Biome = Quarts[Index];
            // A quart without a biome reads as none; the layers' masks cannot say that, so go without them.
            if (Biome == FVoxelBiomeStorage::None || Biome >= BiomeWords * 64)
            {
                Chunk.bShortcut = false;
                Chunk.LayerBiomes.Reset();
                return;
            }
            Chunk.LayerBiomes[(Index / PerLayer) * BiomeWords + Biome / 64] |= 1ull << (Biome % 64);
        }
    }
    Chunk.InteriorStates.Init(FVoxelSurfaceChunk::NotComputed, VoxelConstants::Height);
    Chunk.InteriorVeins.Init(0, VoxelConstants::Height);
    if (Biomes && !Biomes->IsEmpty())
    {
        // Filled a quart layer at a time, before any read of that layer (JitterLayers).
        Chunk.Jitters.SetNumUninitialized(FVoxelBiomeStorage::Volume);
        Chunk.JitterLayers.Init(false, FVoxelBiomeStorage::Layers);
    }
    Chunk.DeepStates.SetNumUninitialized(VoxelConstants::Height);
    Chunk.DeepVeins.SetNumUninitialized(VoxelConstants::Height);
    uint64 Mask[SurfaceMaxBiomeWords];
    for (int32 Index = 0; Index < VoxelConstants::Height; ++Index)
    {
        const int32 Z = Index + VoxelConstants::MinZ;
        GetLayerMask(Chunk, Z, Mask);
        Chunk.DeepStates[Index] = RunDeep(Chunk, Z, Mask, Chunk.DeepVeins[Index]);
    }
    // The stone_depth conditions reachable with the column's biomes: biome tests decided by every layer's biomes.
    uint64 All[SurfaceMaxBiomeWords] = {};
    for (int32 Layer = 0; Layer < FVoxelBiomeStorage::Layers; ++Layer)
        for (int32 Word = 0; Word < BiomeWords; ++Word)
            All[Word] |= Chunk.LayerBiomes[Layer * BiomeWords + Word];
    TArray<bool, TInlineAllocator<1024>> Reached;
    Reached.Init(false, Program.Num() + 1);
    Reached[0] = true;
    Chunk.FloorConditions.Reset();
    Chunk.CeilingConditions.Reset();
    for (int32 Pc = 0; Pc < Program.Num(); ++Pc)
    {
        const FInstruction& Instruction = Program[Pc];
        if (Reached[Pc] && Instruction.Op == EOp::Vein) Reached[Pc + 1] = true;
        if (!Reached[Pc] || Instruction.Op != EOp::Test) continue;
        const FCondition& Data = Conditions[Instruction.Condition];
        if (Data.Op == EVoxelSurfaceConditionOp::StoneDepth)
            (Data.bCeiling ? Chunk.CeilingConditions : Chunk.FloorConditions).AddUnique(Instruction.Condition);
        const int32 Result = Data.Op == EVoxelSurfaceConditionOp::Biome ? DecideBiome(Data, All) : -1;
        if (Result < 0 || (Result != 0) == Instruction.bExpect) Reached[Pc + 1] = true;
        if (Result < 0 || (Result != 0) != Instruction.bExpect) Reached[Instruction.Jump] = true;
    }
    Chunk.ActiveDeep.Reset();
    Chunk.ContextFreeTop = VoxelConstants::Height - 1;
    for (int32 Index = 0; Index < VoxelConstants::Height; ++Index)
    {
        const int32 Result = Chunk.DeepStates[Index];
        if (Result <= FVoxelSurfaceChunk::ResumeBase && NeedsContext[FVoxelSurfaceChunk::ResumeBase - Result])
        {
            Chunk.ContextFreeTop = Index - 1;
            break;
        }
    }
    for (int32 Index = VoxelConstants::Height - 1; Index >= 0; --Index)
        if (Chunk.DeepStates[Index] != NoState) Chunk.ActiveDeep.Add(Index);
}

void FVoxelSurfaceEvaluator::PrepareVeins(
    FVoxelSurfaceChunk& Chunk, int32 X0, int32 Y0, TConstArrayView<EVoxelSurfaceKind> Kinds) const
{
    if (!Chunk.bVeins) return;
    OreVeins.Generate(X0, Y0, Kinds, Chunk.bVeinShortcut, Chunk.VeinScratch, Chunk.Veins);
}

uint32 FVoxelSurfaceEvaluator::GetActiveVeins(const FVoxelSurfaceChunk& Chunk, int32 Z) const
{
    if (!Chunk.bVeins) return 0;
    uint32 Mask = 0;
    for (int32 Index = 0; Index < OreVeins.Num(); ++Index)
    {
        const FVoxelOreVein& Vein = OreVeins.GetVein(Index);
        if (!Chunk.bVeinShortcut || (Z >= Vein.MinZ && Z <= Vein.MaxZ)) Mask |= 1u << Index;
    }
    return Mask;
}

int32 FVoxelSurfaceEvaluator::GiveVein(const FVoxelSurfaceChunk& Chunk, uint32 Veins, int32 Cell, int32 Z) const
{
    // The column mask is part of the vein shortcut: the oracle (shortcut off) asks every block.
    if (Chunk.bVeinShortcut && !(Chunk.Veins.Columns[Cell >> 6] >> (Cell & 63) & 1)) return NoState;
    for (; Veins != 0; Veins &= Veins - 1)
    {
        const int32 Index = int32(FMath::CountTrailingZeros(Veins));
        const int32 Given = FVoxelOreVeins::Get(OreVeins.GetVein(Index), Chunk.Veins, Index, Cell, Z);
        if (Given >= 0) return Given;
    }
    return NoState;
}

int32 FVoxelSurfaceEvaluator::TestPartial(
    int32 Condition, int32 Z, const uint64* LayerMask, bool bAbove, bool bInterior) const
{
    const FCondition& Data = Conditions[Condition];
    switch (Data.Op)
    {
    case EVoxelSurfaceConditionOp::Biome:
        return DecideBiome(Data, LayerMask);
    case EVoxelSurfaceConditionOp::VerticalGradient:
        return Z <= Data.Anchor ? 1 : Z >= Data.FalseAnchor ? 0 : -1;
    case EVoxelSurfaceConditionOp::YAbove:
        return !Data.bAddDepth && Data.Multiplier == 0 ? int32(Z >= Data.Anchor) : -1;
    case EVoxelSurfaceConditionOp::AbovePreliminarySurface:
        return bAbove ? 1 : 0;
    case EVoxelSurfaceConditionOp::StoneDepth:
        return bInterior ? 0 : -1;
    case EVoxelSurfaceConditionOp::Not:
    {
        const int32 Input = TestPartial(Data.Input, Z, LayerMask, bAbove, bInterior);
        return Input < 0 ? Input : 1 - Input;
    }
    default:
        return -1;
    }
}

int32 FVoxelSurfaceEvaluator::DecideBiome(const FCondition& Data, const uint64* Mask) const
{
    bool bAny = false, bAll = true;
    for (int32 Word = 0; Word < BiomeWords; ++Word)
    {
        const uint64 Set = BiomeMasks[Data.Mask + Word];
        bAny |= (Mask[Word] & Set) != 0;
        bAll &= (Mask[Word] & ~Set) == 0;
    }
    return !bAny ? 0 : bAll ? 1 : -1;
}

int32 FVoxelSurfaceEvaluator::RunDeep(
    const FVoxelSurfaceChunk& Chunk, int32 Z, const uint64* LayerMask, uint8& OutVeins) const
{
    OutVeins = 0;
    const uint32 Active = GetActiveVeins(Chunk, Z);
    for (int32 Pc = 0; Pc < Program.Num();)
    {
        const FInstruction& Instruction = Program[Pc];
        switch (Instruction.Op)
        {
        case EOp::Vein:
            OutVeins |= uint8(Active & 1u << Instruction.State);
            ++Pc;
            break;
        case EOp::Test:
        {
            const int32 Result = TestPartial(Instruction.Condition, Z, LayerMask, false, false);
            if (Result < 0) return FVoxelSurfaceChunk::ResumeBase - Pc;
            Pc = (Result != 0) == Instruction.bExpect ? Pc + 1 : Instruction.Jump;
            break;
        }
        case EOp::Give:
            return Instruction.State;
        default:
            // Bands need the column's offset.
            return FVoxelSurfaceChunk::ResumeBase - Pc;
        }
    }
    return NoState;
}

int32 FVoxelSurfaceEvaluator::RunInterior(const FVoxelSurfaceChunk& Chunk, int32 Z, uint8& OutVeins) const
{
    OutVeins = 0;
    const uint32 Active = GetActiveVeins(Chunk, Z);
    uint64 Mask[SurfaceMaxBiomeWords];
    GetLayerMask(Chunk, Z, Mask);
    TArray<int32>& Outcomes = Chunk.Outcomes;
    Outcomes.SetNumUninitialized(Program.Num() + 1);
    Outcomes[Program.Num()] = NoState;
    for (int32 Pc = Program.Num() - 1; Pc >= 0; --Pc)
    {
        const FInstruction& Instruction = Program[Pc];
        switch (Instruction.Op)
        {
        case EOp::Test:
        {
            const int32 Result = TestPartial(Instruction.Condition, Z, Mask, true, true);
            const int32 Next = Outcomes[Pc + 1], Skip = Outcomes[Instruction.Jump];
            Outcomes[Pc] = Result >= 0 ? ((Result != 0) == Instruction.bExpect ? Next : Skip)
                : Next == Skip         ? Next
                                       : FVoxelSurfaceChunk::Deep;
            break;
        }
        case EOp::Give:
            Outcomes[Pc] = Instruction.State;
            break;
        case EOp::Vein:
            // Its blocks are asked for per block (OutVeins); the table holds what follows when it gives none.
            Outcomes[Pc] = Outcomes[Pc + 1];
            break;
        default:
            // Bands need the column's offset.
            Outcomes[Pc] = FVoxelSurfaceChunk::Deep;
            break;
        }
    }
    // The decided path, to the first undecided instruction, and the veins on it.
    int32 Pc = 0;
    uint8 Veins = 0;
    while (Pc < Program.Num())
    {
        if (Program[Pc].Op == EOp::Vein)
        {
            Veins |= uint8(Active & 1u << Program[Pc].State);
            ++Pc;
            continue;
        }
        if (Program[Pc].Op != EOp::Test) break;
        const int32 Result = TestPartial(Program[Pc].Condition, Z, Mask, true, true);
        if (Result < 0) break;
        Pc = (Result != 0) == Program[Pc].bExpect ? Pc + 1 : Program[Pc].Jump;
    }
    if (Outcomes[0] != FVoxelSurfaceChunk::Deep)
    {
        // Decided, but a vein past an undecided test may run on one of its ways: the full tree decides.
        if (Pc < Program.Num() && Program[Pc].Op == EOp::Test && (VeinsFrom[Pc] & Active) != 0)
            return FVoxelSurfaceChunk::Deep;
        OutVeins = Veins;
        return Outcomes[0];
    }
    // Undecided: resume at the first undecided instruction on the decided path.
    OutVeins = Veins;
    return FVoxelSurfaceChunk::ResumeBase - Pc;
}

int32 FVoxelSurfaceEvaluator::GetBlockBiome(
    const FVoxelSurfaceChunk& Chunk, int32 LocalX, int32 LocalY, FVoxelSurfaceBlock& Block) const
{
    if (Block.Biome != FVoxelSurfaceBlock::UnknownBiome) return Block.Biome;
    if (!Chunk.Biomes)
        Block.Biome = INDEX_NONE;
    else if (Chunk.JitterLayers.IsEmpty() || Block.Z < VoxelConstants::MinZ || Block.Z > VoxelConstants::MaxZ)
        Block.Biome = Chunk.Biomes->GetBlock(LocalX, LocalY, Block.Z);
    else
    {
        // The block reads corners on its quart layer and the one above.
        const int32 Layer = (Block.Z - VoxelConstants::MinZ) / FVoxelBiomeStorage::QuartSize;
        for (int32 Read = Layer; Read <= FMath::Min(Layer + 1, FVoxelBiomeStorage::Layers - 1); ++Read)
            if (!Chunk.JitterLayers[Read])
            {
                Chunk.Biomes->FillJitterLayer(Read, Chunk.Jitters.GetData());
                Chunk.JitterLayers[Read] = true;
            }
        Block.Biome = Chunk.Biomes->GetBlock(LocalX, LocalY, Block.Z, Chunk.Jitters.GetData());
    }
    return Block.Biome;
}

bool FVoxelSurfaceEvaluator::TestBiome(const FCondition& Data, const FVoxelSurfaceChunk& Chunk, int32 LocalX,
    int32 LocalY, FVoxelSurfaceBlock& Block) const
{
    if (Block.Biome == FVoxelSurfaceBlock::UnknownBiome && Chunk.bShortcut && !Chunk.LayerBiomes.IsEmpty())
    {
        // The block takes one of its quart layers' biomes, so a set holding none or all of them decides without the
        // lookup.
        uint64 Mask[SurfaceMaxBiomeWords];
        GetLayerMask(Chunk, Block.Z, Mask);
        const int32 Decided = DecideBiome(Data, Mask);
        if (Decided >= 0) return Decided != 0;
    }
    const int32 Biome = GetBlockBiome(Chunk, LocalX, LocalY, Block);
    return Biome >= 0 && Biome < BiomeWords * 64 && (BiomeMasks[Data.Mask + Biome / 64] >> (Biome % 64) & 1) != 0;
}

bool FVoxelSurfaceEvaluator::TestCondition(int32 Condition, const FVoxelSurfaceChunk& Chunk,
    FVoxelSurfaceColumn& Column, int32 LocalX, int32 LocalY, FVoxelSurfaceBlock& Block) const
{
    const FCondition& Data = Conditions[Condition];
    switch (Data.Op)
    {
    case EVoxelSurfaceConditionOp::Biome:
        return TestBiome(Data, Chunk, LocalX, LocalY, Block);
    case EVoxelSurfaceConditionOp::NoiseThreshold:
    {
        // Both bounds inclusive; the reference's y is our Z.
        const double Value =
            Data.bIs3D ? Noises[Data.Noise].Sample(Column.X, Block.Z, Column.Y) : GetNoise(Column, Data.Noise);
        return Value >= Data.Min && Value <= Data.Max;
    }
    case EVoxelSurfaceConditionOp::VerticalGradient:
        if (Block.Z <= Data.Anchor) return true;
        if (Block.Z >= Data.FalseAnchor) return false;
        return VoxelRandom::ToUnitFloat(VoxelRandom::Hash64(Seed, Data.Salt, Column.X, Column.Y, Block.Z)) <
            double(Data.FalseAnchor - Block.Z) / double(Data.FalseAnchor - Data.Anchor);
    case EVoxelSurfaceConditionOp::YAbove:
        return Block.Z + (Data.bAddDepth ? Block.DepthAbove : 0) >= Data.Anchor + Column.SurfaceDepth * Data.Multiplier;
    case EVoxelSurfaceConditionOp::Water:
        return Block.WaterHeight == FVoxelSurfaceBlock::NoWater ||
            Block.Z + (Data.bAddDepth ? Block.DepthAbove : 0) >=
            Block.WaterHeight + Data.Offset + Column.SurfaceDepth * Data.Multiplier;
    case EVoxelSurfaceConditionOp::StoneDepth:
        return (Data.bCeiling ? Block.DepthBelow : Block.DepthAbove) <= GetStoneDepthLimit(Data, Column);
    case EVoxelSurfaceConditionOp::Not:
        return !TestCondition(Data.Input, Chunk, Column, LocalX, LocalY, Block);
    case EVoxelSurfaceConditionOp::Steep:
        return Column.bSteep;
    case EVoxelSurfaceConditionOp::Hole:
        return Column.SurfaceDepth <= 0;
    case EVoxelSurfaceConditionOp::AbovePreliminarySurface:
        return Block.Z >= Column.MinSurfaceLevel;
    default:
        return false;
    }
}

int32 FVoxelSurfaceEvaluator::Evaluate(const FVoxelSurfaceChunk& Chunk, FVoxelSurfaceColumn& Column, int32 LocalX,
    int32 LocalY, FVoxelSurfaceBlock& Block, int32 StartPc) const
{
    const FInstruction* Code = Program.GetData();
    for (int32 Pc = StartPc, End = Program.Num(); Pc < End; ++Chunk.Steps)
    {
        const FInstruction& Instruction = Code[Pc];
        switch (Instruction.Op)
        {
        case EOp::Test:
            Pc = TestCondition(Instruction.Condition, Chunk, Column, LocalX, LocalY, Block) == Instruction.bExpect
                ? Pc + 1
                : Instruction.Jump;
            break;
        case EOp::Give:
            return Instruction.State;
        case EOp::Vein:
            // Most block columns hold no vein block: the field's column mask skips them first.
            if (const int32 Cell = LocalY * VoxelConstants::ColumnSize + LocalX;
                Chunk.bVeins && (!Chunk.bVeinShortcut || (Chunk.Veins.Columns[Cell >> 6] >> (Cell & 63) & 1)))
            {
                const int32 Given =
                    GiveVein(Chunk, 1u << Instruction.State & GetActiveVeins(Chunk, Block.Z), Cell, Block.Z);
                if (Given != NoState) return Given;
            }
            ++Pc;
            break;
        default:
            if (!Column.bBandOffsetKnown)
            {
                // Rounded to nearest, as Java's Math.round.
                Column.BandOffset = FMath::FloorToInt32(GetNoise(Column, BandsSlot) * 4 + .5);
                Column.bBandOffsetKnown = true;
            }
            return GetBand(Block.Z, Column.BandOffset);
        }
    }
    return NoState;
}

void FVoxelSurfaceEvaluator::ApplyColumn(const FVoxelSurfaceChunk& Chunk, FVoxelSurfaceColumn& Column, int32 LocalX,
    int32 LocalY, TConstArrayView<EVoxelSurfaceKind> Kinds, TArrayView<uint16> States, int32 Stride, int32 Top) const
{
    constexpr int32 Height = VoxelConstants::Height;
    check(Kinds.Num() == Height && Stride >= 1 && States.Num() >= (Height - 1) * Stride + 1);
    const EVoxelSurfaceKind* Kind = Kinds.GetData();
    uint16* Out = States.GetData();
    const bool bDeep = Chunk.bShortcut && Chunk.DeepStates.Num() == Height;
    check(!Chunk.bVeins || Chunk.Veins.bFilled);
    const int32 Cell = LocalY * VoxelConstants::ColumnSize + LocalX;
    // At and below LowerTop the deep table decides, and nothing left to run reads depths or water: only its active
    // heights are visited.
    const int32 LowerTop =
        bDeep ? FMath::Min(Column.MinSurfaceLevel - 1 - VoxelConstants::MinZ, Chunk.ContextFreeTop) : -1;
    if (bDeep)
    {
        Column.FloorReach = Column.CeilingReach = MIN_int32;
        for (const int32 Condition : Chunk.FloorConditions)
            Column.FloorReach = FMath::Max(Column.FloorReach, GetStoneDepthLimit(Conditions[Condition], Column));
        for (const int32 Condition : Chunk.CeilingConditions)
            Column.CeilingReach = FMath::Max(Column.CeilingReach, GetStoneDepthLimit(Conditions[Condition], Column));
    }
    FVoxelSurfaceBlock Block;
    int32 DepthAbove = 0, Water = FVoxelSurfaceBlock::NoWater, RunBottom = 0;
    // The walk starts at Top, so everything above it must be air for the depths and the water height to hold.
    const int32 First = FMath::Clamp(Top - VoxelConstants::MinZ, -1, Height - 1);
    check(First == Height - 1 || Kind[First + 1] == EVoxelSurfaceKind::Air);
    for (int32 Index = First; Index > LowerTop; --Index)
    {
        const EVoxelSurfaceKind Here = Kind[Index];
        const int32 Z = Index + VoxelConstants::MinZ;
        if (Here == EVoxelSurfaceKind::Air)
        {
            DepthAbove = 0;
            Water = FVoxelSurfaceBlock::NoWater;
            continue;
        }
        if (Here != EVoxelSurfaceKind::Solid)
        {
            if (Water == FVoxelSurfaceBlock::NoWater) Water = Z + 1;
            continue;
        }
        // A run's first block from above: find its bottom, which the next air or fluid (or the world's bottom) ends.
        if (Index == Height - 1 || Kind[Index + 1] != EVoxelSurfaceKind::Solid)
        {
            RunBottom = Index;
            while (RunBottom > 0 && Kind[RunBottom - 1] == EVoxelSurfaceKind::Solid)
                --RunBottom;
        }
        ++DepthAbove;
        const int32 DepthBelow = Index - RunBottom + 1;
        int32 Result = FVoxelSurfaceChunk::Deep;
        uint8 Veins = 0;
        if (bDeep)
        {
            if (Z < Column.MinSurfaceLevel)
            {
                Result = Chunk.DeepStates[Index];
                Veins = Chunk.DeepVeins[Index];
                Chunk.DeepBlocks += Result >= NoState;
            }
            else if (DepthAbove > Column.FloorReach && DepthBelow > Column.CeilingReach)
            {
                int32& Interior = Chunk.InteriorStates[Index];
                if (Interior == FVoxelSurfaceChunk::NotComputed)
                {
                    Interior = RunInterior(Chunk, Z, Chunk.InteriorVeins[Index]);
                    ++Chunk.InteriorRuns;
                }
                Result = Interior;
                Veins = Chunk.InteriorVeins[Index];
                Chunk.InteriorBlocks += Result >= NoState;
            }
        }
        // The veins the table passed come first.
        if (Veins != 0)
        {
            const int32 Given = GiveVein(Chunk, Veins, Cell, Z);
            if (Given != NoState)
            {
                Out[Index * Stride] = uint16(Given);
                continue;
            }
        }
        if (Result < NoState)
        {
            Block.Z = Z;
            Block.DepthAbove = DepthAbove;
            Block.DepthBelow = DepthBelow;
            Block.WaterHeight = Water;
            Block.Biome = FVoxelSurfaceBlock::UnknownBiome;
            Result = Evaluate(Chunk, Column, LocalX, LocalY, Block,
                Result <= FVoxelSurfaceChunk::ResumeBase ? FVoxelSurfaceChunk::ResumeBase - Result : 0);
            ++Chunk.FullBlocks;
        }
        if (Result != NoState) Out[Index * Stride] = uint16(Result);
    }
    if (LowerTop < 0) return;
    // Below: the deep table's active heights only. What runs from their resume points reads no depths or water.
    for (const int32 Index : Chunk.ActiveDeep)
    {
        if (Index > LowerTop || Kind[Index] != EVoxelSurfaceKind::Solid) continue;
        int32 Result = Chunk.DeepStates[Index];
        if (Chunk.DeepVeins[Index] != 0)
        {
            const int32 Given = GiveVein(Chunk, Chunk.DeepVeins[Index], Cell, Index + VoxelConstants::MinZ);
            if (Given != NoState)
            {
                Out[Index * Stride] = uint16(Given);
                continue;
            }
        }
        if (Result >= NoState)
            ++Chunk.DeepBlocks;
        else
        {
            Block = FVoxelSurfaceBlock();
            Block.Z = Index + VoxelConstants::MinZ;
            Result = Evaluate(Chunk, Column, LocalX, LocalY, Block,
                Result <= FVoxelSurfaceChunk::ResumeBase ? FVoxelSurfaceChunk::ResumeBase - Result : 0);
            ++Chunk.FullBlocks;
        }
        if (Result != NoState) Out[Index * Stride] = uint16(Result);
    }
    if (!Chunk.bVeins) return;
    // Heights the deep table leaves as they are (not visited above) but whose veins it passes: only the blocks a vein
    // gives. The last vein first, so where two give, the first in the program wins.
    for (int32 VeinIndex = OreVeins.Num() - 1; VeinIndex >= 0; --VeinIndex)
    {
        const FVoxelOreVein& Vein = OreVeins.GetVein(VeinIndex);
        const FVoxelOreVeinField::FSpan& Span = Chunk.Veins.Spans[VeinIndex];
        const uint64* Words = Chunk.Veins.Bits.GetData() + Span.Offset + Cell * Span.Words * 3;
        for (int32 Word = 0; Word < Span.Words; ++Word)
            for (uint64 Bits = Words[Word * 3]; Bits != 0; Bits &= Bits - 1)
            {
                const int32 Z = Span.Low + Word * 64 + int32(FMath::CountTrailingZeros64(Bits));
                const int32 Index = Z - VoxelConstants::MinZ;
                if (Index > LowerTop || Chunk.DeepStates[Index] != NoState ||
                    !(Chunk.DeepVeins[Index] >> VeinIndex & 1) || Kind[Index] != EVoxelSurfaceKind::Solid)
                    continue;
                Out[Index * Stride] = uint16(FVoxelOreVeins::Get(Vein, Chunk.Veins, VeinIndex, Cell, Z));
            }
    }
}
