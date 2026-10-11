#include "Blocks/VoxelPlantSurvival.h"

namespace
{
    bool SurvivalFail(FString* OutError, const FString& Message)
    {
        if (OutError) *OutError = Message;
        return false;
    }

    // The plants of each rule, by block id (minecraft.wiki's placement rules; the support tags' own descriptions in
    // the worldgen reference's CODE_TAGS). Two-cell plants take UpperHalf for their upper half.
    const TCHAR* const VegetationBlocks[] = {TEXT("short_grass"), TEXT("fern"), TEXT("bush"), TEXT("firefly_bush"),
        TEXT("red_shrub"), TEXT("sweet_berry_bush"), TEXT("pink_petals"), TEXT("wildflowers"), TEXT("tall_grass"),
        TEXT("large_fern"), TEXT("sunflower"), TEXT("lilac"), TEXT("rose_bush"), TEXT("peony"), TEXT("oak_sapling"),
        TEXT("spruce_sapling"), TEXT("birch_sapling"), TEXT("jungle_sapling"), TEXT("acacia_sapling"),
        TEXT("dark_oak_sapling"), TEXT("cherry_sapling"), TEXT("pale_oak_sapling")};
    const TCHAR* const DryVegetationBlocks[] = {TEXT("dead_bush"), TEXT("short_dry_grass"), TEXT("tall_dry_grass")};
    const TCHAR* const SturdyBelowBlocks[] = {TEXT("leaf_litter"), TEXT("sea_pickle")};
    const TCHAR* const CarpetBlocks[] = {TEXT("moss_carpet"), TEXT("pale_moss_carpet"), TEXT("white_carpet"),
        TEXT("orange_carpet"), TEXT("magenta_carpet"), TEXT("light_blue_carpet"), TEXT("yellow_carpet"),
        TEXT("lime_carpet"), TEXT("pink_carpet"), TEXT("gray_carpet"), TEXT("light_gray_carpet"), TEXT("cyan_carpet"),
        TEXT("purple_carpet"), TEXT("blue_carpet"), TEXT("brown_carpet"), TEXT("green_carpet"), TEXT("red_carpet"),
        TEXT("black_carpet")};
}

bool FVoxelPlantSurvival::Initialize(
    const FVoxelBlockTable& InTable, FVoxelBlockTags& Tags, FString* OutError, FStringView DroppedTag)
{
    *this = FVoxelPlantSurvival();
    const auto AddSet = [&](FStringView Tag, int32& OutSet) -> bool
    {
        const FVoxelBlockSet* Set = Tags.Find(Tag, OutError);
        if (!Set) return SurvivalFail(OutError, FString::Printf(TEXT("no support tag %s"), *FString(Tag)));
        OutSet = Sets.Add(*Set);
        if (!DroppedTag.IsEmpty() && Tag == DroppedTag) Sets[OutSet].Init(Tags.GetBlockLimit(), false);
        return true;
    };
    int32 VegetationSet, DryVegetationSet, KelpSet, SeagrassSet;
    if (!AddSet(TEXT("minecraft:supports_vegetation"), VegetationSet) ||
        !AddSet(TEXT("minecraft:supports_dry_vegetation"), DryVegetationSet) ||
        !AddSet(TEXT("minecraft:supports_sugar_cane"), SugarCaneSet) ||
        !AddSet(TEXT("minecraft:supports_sugar_cane_adjacently"), SugarCaneAdjacentSet) ||
        !AddSet(TEXT("minecraft:supports_cactus"), CactusSet) ||
        !AddSet(TEXT("minecraft:support_override_cactus_flower"), CactusFlowerSet) ||
        !AddSet(TEXT("minecraft:supports_lily_pad"), LilyPadSet) ||
        !AddSet(TEXT("minecraft:cannot_support_kelp"), KelpSet) ||
        !AddSet(TEXT("minecraft:cannot_support_seagrass"), SeagrassSet))
        return false;
    // The small flowers are a tag of their own: every one survives where vegetation does.
    const FVoxelBlockSet* SmallFlowers = Tags.Find(TEXT("minecraft:small_flowers"), OutError);
    if (!SmallFlowers) return SurvivalFail(OutError, TEXT("no tag minecraft:small_flowers"));

    WaterBlock = InTable.FindBlock(TEXT("water"));
    LavaBlock = InTable.FindBlock(TEXT("lava"));
    SugarCaneBlock = InTable.FindBlock(TEXT("sugar_cane"));
    CactusBlock = InTable.FindBlock(TEXT("cactus"));
    KelpBlock = InTable.FindBlock(TEXT("kelp"));
    KelpPlantBlock = InTable.FindBlock(TEXT("kelp_plant"));

    TMap<int32, FVoxelSurvivalRule> OfBlock;
    const auto Assign = [&](FStringView Id, EVoxelSurvival Kind, int32 Set = INDEX_NONE)
    {
        const int32 Block = InTable.FindBlock(Id);
        if (Block != INDEX_NONE && Block != 0) OfBlock.Add(Block, {Kind, Set});
    };
    for (const TCHAR* Id : VegetationBlocks)
        Assign(Id, EVoxelSurvival::OnTag, VegetationSet);
    for (const TCHAR* Id : DryVegetationBlocks)
        Assign(Id, EVoxelSurvival::OnTag, DryVegetationSet);
    for (const TCHAR* Id : SturdyBelowBlocks)
        Assign(Id, EVoxelSurvival::SturdyBelow);
    for (const TCHAR* Id : CarpetBlocks)
        Assign(Id, EVoxelSurvival::NotAirBelow);
    Assign(TEXT("brown_mushroom"), EVoxelSurvival::Mushroom);
    Assign(TEXT("red_mushroom"), EVoxelSurvival::Mushroom);
    Assign(TEXT("lily_pad"), EVoxelSurvival::LilyPad, LilyPadSet);
    Assign(TEXT("sugar_cane"), EVoxelSurvival::SugarCane, SugarCaneSet);
    Assign(TEXT("cactus"), EVoxelSurvival::Cactus, CactusSet);
    Assign(TEXT("cactus_flower"), EVoxelSurvival::CactusFlower, CactusFlowerSet);
    Assign(TEXT("seagrass"), EVoxelSurvival::Underwater, SeagrassSet);
    Assign(TEXT("tall_seagrass"), EVoxelSurvival::Underwater, SeagrassSet);
    Assign(TEXT("kelp"), EVoxelSurvival::Underwater, KelpSet);
    Assign(TEXT("kelp_plant"), EVoxelSurvival::Underwater, KelpSet);

    // S7's plants (scout H's measured support table, T1): what each attached, hanging, multiface or pointed block
    // needs, from its block and its properties.
    int32 AzaleaSet, PropaguleSet, HangingPropaguleSet, BambooSet, BigDripleafSet, SmallDripleafSet, CocoaSet,
        CaveVinesSet, LeavesSet;
    if (!AddSet(TEXT("minecraft:supports_azalea"), AzaleaSet) ||
        !AddSet(TEXT("minecraft:supports_mangrove_propagule"), PropaguleSet) ||
        !AddSet(TEXT("minecraft:supports_hanging_mangrove_propagule"), HangingPropaguleSet) ||
        !AddSet(TEXT("minecraft:supports_bamboo"), BambooSet) ||
        !AddSet(TEXT("minecraft:supports_big_dripleaf"), BigDripleafSet) ||
        !AddSet(TEXT("minecraft:supports_small_dripleaf"), SmallDripleafSet) ||
        !AddSet(TEXT("minecraft:supports_cocoa"), CocoaSet) || !AddSet(TEXT("minecraft:cave_vines"), CaveVinesSet) ||
        !AddSet(TEXT("minecraft:leaves"), LeavesSet))
        return false;
    VineBlock = InTable.FindBlock(TEXT("vine"));
    DripleafBlock = InTable.FindBlock(TEXT("big_dripleaf"));
    DripleafStemBlock = InTable.FindBlock(TEXT("big_dripleaf_stem"));
    const auto AddTo = [this](int32 Set, int32 Block)
    {
        if (Block > 0 && (Block >> 6) < Sets[Set].Words.Num()) Sets[Set].Add(Block);
    };
    const auto Union = [this](int32 A, int32 B)
    {
        FVoxelBlockSet Joined = Sets[A];
        for (int32 Word = 0; Word < FMath::Min(Joined.Words.Num(), Sets[B].Words.Num()); ++Word)
            Joined.Words[Word] |= Sets[B].Words[Word];
        return Sets.Add(MoveTemp(Joined));
    };
    // A big dripleaf stem stands on the dripleaf soil or a stem, the leaf also on a leaf; pale hanging moss hangs
    // from leaves or itself; a waterlogged small dripleaf also takes the vegetation soil.
    FVoxelBlockSet StemSoil = Sets[BigDripleafSet];
    const int32 StemSet = Sets.Add(MoveTemp(StemSoil));
    AddTo(StemSet, DripleafStemBlock);
    AddTo(BigDripleafSet, DripleafStemBlock);
    AddTo(BigDripleafSet, DripleafBlock);
    AddTo(LeavesSet, InTable.FindBlock(TEXT("pale_hanging_moss")));
    const int32 WetDripleafSet = Union(SmallDripleafSet, VegetationSet);
    TSet<int32> AwayFromFacing, OnFloor, UnderBlock;
    for (const TCHAR* Id :
        {TEXT("amethyst_cluster"), TEXT("large_amethyst_bud"), TEXT("medium_amethyst_bud"), TEXT("small_amethyst_bud"),
            TEXT("shelf_mushroom"), TEXT("tube_coral_wall_fan"), TEXT("brain_coral_wall_fan"),
            TEXT("bubble_coral_wall_fan"), TEXT("fire_coral_wall_fan"), TEXT("horn_coral_wall_fan")})
        AwayFromFacing.Add(InTable.FindBlock(Id));
    // Corals need no water here: there are no dead corals yet (scout H, Q6).
    for (const TCHAR* Id : {TEXT("tube_coral"), TEXT("brain_coral"), TEXT("bubble_coral"), TEXT("fire_coral"),
             TEXT("horn_coral"), TEXT("tube_coral_fan"), TEXT("brain_coral_fan"), TEXT("bubble_coral_fan"),
             TEXT("fire_coral_fan"), TEXT("horn_coral_fan")})
        OnFloor.Add(InTable.FindBlock(Id));
    for (const TCHAR* Id : {TEXT("spore_blossom"), TEXT("hanging_roots")})
        UnderBlock.Add(InTable.FindBlock(Id));
    const TMap<int32, int32> UnderTag = {{InTable.FindBlock(TEXT("cave_vines")), CaveVinesSet},
        {InTable.FindBlock(TEXT("cave_vines_plant")), CaveVinesSet},
        {InTable.FindBlock(TEXT("pale_hanging_moss")), LeavesSet}};
    const TMap<int32, int32> OnTagBelow = {{InTable.FindBlock(TEXT("azalea")), AzaleaSet},
        {InTable.FindBlock(TEXT("flowering_azalea")), AzaleaSet}, {InTable.FindBlock(TEXT("bamboo")), BambooSet},
        {DripleafBlock, BigDripleafSet}};
    const int32 Cocoa = InTable.FindBlock(TEXT("cocoa")), Propagule = InTable.FindBlock(TEXT("mangrove_propagule")),
                SmallDripleaf = InTable.FindBlock(TEXT("small_dripleaf")),
                GlowLichen = InTable.FindBlock(TEXT("glow_lichen")), SculkVein = InTable.FindBlock(TEXT("sculk_vein")),
                Dripstone = InTable.FindBlock(TEXT("pointed_dripstone")),
                SulfurSpike = InTable.FindBlock(TEXT("sulfur_spike"));
    const FName FaceNames[6] = {TEXT("west"), TEXT("east"), TEXT("north"), TEXT("south"), TEXT("down"), TEXT("up")};
    const FName True(TEXT("true")), Facing(TEXT("facing")), Hanging(TEXT("hanging")), Waterlogged(TEXT("waterlogged")),
        Vertical(TEXT("vertical_direction")), Pointing(TEXT("up"));
    constexpr int8 Down = int8(EVoxelFace::Down), Up = int8(EVoxelFace::Up);
    const auto AttachedRule = [&](uint16 State, int32 Block) -> FVoxelSurvivalRule
    {
        const auto Value = [&InTable, State](FName Property) { return InTable.GetValue(State, Property); };
        const FName FacingValue = Value(Facing);
        int8 FacingFace = -1;
        for (int8 Face = 0; Face < 6; ++Face)
            if (FacingValue == FaceNames[Face]) FacingFace = Face;
        if (AwayFromFacing.Contains(Block))
            return FacingFace < 0 ? FVoxelSurvivalRule{}
                                  : FVoxelSurvivalRule{EVoxelSurvival::Attached, INDEX_NONE, int8(FacingFace ^ 1)};
        if (OnFloor.Contains(Block)) return {EVoxelSurvival::Attached, INDEX_NONE, Down};
        if (UnderBlock.Contains(Block)) return {EVoxelSurvival::Attached, INDEX_NONE, Up};
        if (const int32* Set = UnderTag.Find(Block)) return {EVoxelSurvival::Attached, *Set, Up};
        if (const int32* Set = OnTagBelow.Find(Block)) return {EVoxelSurvival::TagAt, *Set, Down};
        // Cocoa hangs on the log its facing points at.
        if (Block == Cocoa && FacingFace >= 0) return {EVoxelSurvival::TagAt, CocoaSet, FacingFace};
        if (Block == Propagule)
            return Value(Hanging) == True ? FVoxelSurvivalRule{EVoxelSurvival::TagAt, HangingPropaguleSet, Up}
                                          : FVoxelSurvivalRule{EVoxelSurvival::TagAt, PropaguleSet, Down};
        if (Block == SmallDripleaf)
            return {EVoxelSurvival::TagAt, Value(Waterlogged) == True ? WetDripleafSet : SmallDripleafSet, Down};
        if (Block == DripleafStemBlock) return {EVoxelSurvival::DripleafStem, StemSet};
        if (Block == Dripstone || Block == SulfurSpike)
            return {EVoxelSurvival::Pointed, INDEX_NONE, Value(Vertical) == Pointing ? Down : Up};
        if (Block == VineBlock || Block == GlowLichen || Block == SculkVein)
        {
            FVoxelSurvivalRule Rule{EVoxelSurvival::Multiface};
            for (int32 Face = 0; Face < 6; ++Face)
                if (Value(FaceNames[Face]) == True) Rule.Faces |= uint8(1 << Face);
            return Rule;
        }
        return {};
    };

    const FName Half(TEXT("half")), Upper(TEXT("upper"));
    Rules.SetNum(InTable.GetStateCount());
    Sturdy.SetNumZeroed(InTable.GetStateCount());
    for (int32 State = 0; State < InTable.GetStateCount(); ++State)
    {
        const FVoxelStateInfo& Info = InTable.Get(uint16(State));
        if (const FVoxelSurvivalRule* Rule = OfBlock.Find(Info.Block))
            Rules[State] = *Rule;
        else if (State != 0 && SmallFlowers->Contains(Info.Block))
            Rules[State] = {EVoxelSurvival::OnTag, VegetationSet};
        else if (State != 0)
            Rules[State] = AttachedRule(uint16(State), Info.Block);
        if (Rules[State].Kind != EVoxelSurvival::Always && InTable.GetValue(uint16(State), Half) == Upper)
            Rules[State] = {EVoxelSurvival::UpperHalf, INDEX_NONE};
        switch (Info.Shape)
        {
        case EVoxelShape::Full:
            // Leaves are not sturdy (scout H measured 26.3: neither full nor centre support).
            Sturdy[State] = Info.bLeaves ? 0 : 0x3F;
            break;
        case EVoxelShape::SlabBottom:
            Sturdy[State] = uint8(1 << int32(EVoxelFace::Down));
            break;
        case EVoxelShape::SlabTop:
            Sturdy[State] = uint8(1 << int32(EVoxelFace::Up));
            break;
        case EVoxelShape::Layers:
            Sturdy[State] = Info.Layers > 1 ? uint8(1 << int32(EVoxelFace::Down)) : 0;
            break;
        default:
            break;
        }
    }
    Table = &InTable;
    return true;
}

bool FVoxelPlantSurvival::SurvivesOn(uint16 State, uint16 Below, uint16 Cell) const
{
    const FVoxelSurvivalRule& Rule = GetRule(State);
    const FVoxelStateInfo& Under = Table->Get(Below);
    const bool bSturdyTop = (GetSturdyFaces(Below) >> int32(EVoxelFace::Up) & 1) != 0;
    switch (Rule.Kind)
    {
    case EVoxelSurvival::OnTag:
        return Sets[Rule.Tag].Contains(Under.Block) && Below != 0;
    case EVoxelSurvival::SugarCane:
        return Under.Block == SugarCaneBlock || (Below != 0 && Sets[SugarCaneSet].Contains(Under.Block));
    case EVoxelSurvival::Cactus:
        return Under.Block == CactusBlock || (Below != 0 && Sets[CactusSet].Contains(Under.Block));
    case EVoxelSurvival::CactusFlower:
        return bSturdyTop || (Below != 0 && Sets[CactusFlowerSet].Contains(Under.Block));
    case EVoxelSurvival::UpperHalf:
        return Below != 0 && Under.Block == Table->Get(State).Block;
    case EVoxelSurvival::Underwater:
    {
        if (!IsWater(Cell)) return false;
        const int32 Block = Table->Get(State).Block;
        if ((Block == KelpBlock || Block == KelpPlantBlock) &&
            (Under.Block == KelpBlock || Under.Block == KelpPlantBlock) && Below != 0)
            return true;
        return bSturdyTop && !Sets[Rule.Tag].Contains(Under.Block);
    }
    case EVoxelSurvival::Mushroom:
        return Under.Shape == EVoxelShape::Full && Under.Layer == EVoxelLayer::Opaque;
    case EVoxelSurvival::SturdyBelow:
        return bSturdyTop;
    case EVoxelSurvival::LilyPad:
        return !Table->Get(Cell).HoldsFluid() &&
            (IsWaterSource(Below) || (Below != 0 && Sets[LilyPadSet].Contains(Under.Block)));
    case EVoxelSurvival::NotAirBelow:
        return Below != 0;
    case EVoxelSurvival::DripleafStem:
        return Below != 0 && Sets[Rule.Tag].Contains(Under.Block);
    default:
        return true;
    }
}

bool FVoxelPlantSurvival::HoldsAt(uint16 State, const FVoxelSurvivalRule& Rule, uint16 Support) const
{
    const FVoxelStateInfo& Info = Table->Get(Support);
    const bool bInTag = Rule.Tag != INDEX_NONE && Support != 0 && Sets[Rule.Tag].Contains(Info.Block);
    const bool bSturdy = (GetSturdyFaces(Support) >> (Rule.Face ^ 1) & 1) != 0;
    switch (Rule.Kind)
    {
    case EVoxelSurvival::Attached:
        return bSturdy || bInTag;
    case EVoxelSurvival::TagAt:
        return bInTag;
    case EVoxelSurvival::Pointed:
        // Or the same block pointing the same way: a dripstone column.
        return bSturdy || (Info.Block == Table->Get(State).Block && GetRule(Support).Face == Rule.Face);
    default:
        return true;
    }
}
