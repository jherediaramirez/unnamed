#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "UnnamedProjectSettings.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelWaterFog.h"
#include "Voxel/VoxelWorldgenReader.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelWaterFogSwitchTest, "Unnamed.Voxel.WaterFog.Switch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelWaterFogSwitchTest::RunTest(const FString& Parameters)
{
    // voxel.Look.WaterFog defaults to 1; at 0 no fluid gives a fog.
    IConsoleVariable* Switch = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.Look.WaterFog"));
    if (!TestNotNull(TEXT("voxel.Look.WaterFog exists"), Switch)) return true;
    TestEqual(TEXT("voxel.Look.WaterFog's default"), Switch->GetDefaultValue(), FString(TEXT("1")));
    const int32 Was = Switch->GetInt();
    Switch->Set(0, ECVF_SetByCode);
    TestFalse(TEXT("IsOn at voxel.Look.WaterFog 0"), VoxelWaterFog::IsOn());
    Switch->Set(1, ECVF_SetByCode);
    TestTrue(TEXT("IsOn at voxel.Look.WaterFog 1"), VoxelWaterFog::IsOn());
    Switch->Set(Was, ECVF_SetByCode);
    IConsoleVariable* Bright = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.Look.WaterFogBright"));
    if (TestNotNull(TEXT("voxel.Look.WaterFogBright exists"), Bright))
        TestEqual(TEXT("voxel.Look.WaterFogBright's default"), Bright->GetDefaultValue(), FString(TEXT("1")));
    TestFalse(TEXT("off in water"), VoxelWaterFog::GetFog(false, EVoxelFluid::Water, nullptr, 100).IsSet());
    TestFalse(TEXT("off in lava"), VoxelWaterFog::GetFog(false, EVoxelFluid::Lava, nullptr, 100).IsSet());
    TestFalse(TEXT("on in air"), VoxelWaterFog::GetFog(true, EVoxelFluid::None, nullptr, 100).IsSet());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelWaterFogSurfaceTest, "Unnamed.Voxel.WaterFog.Surface",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelWaterFogSurfaceTest::RunTest(const FString& Parameters)
{
    // voxel.Look.WaterSurface defaults to 0 (the plain water materials); 1 picks the ones writing depth, which exist.
    IConsoleVariable* Switch = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.Look.WaterSurface"));
    if (!TestNotNull(TEXT("voxel.Look.WaterSurface exists"), Switch)) return true;
    TestEqual(TEXT("voxel.Look.WaterSurface's default"), Switch->GetDefaultValue(), FString(TEXT("0")));
    const int32 Was = Switch->GetInt();
    Switch->Set(1, ECVF_SetByCode);
    TestTrue(TEXT("IsSurface at 1"), VoxelWaterFog::IsSurface());
    Switch->Set(0, ECVF_SetByCode);
    TestFalse(TEXT("IsSurface at 0"), VoxelWaterFog::IsSurface());
    Switch->Set(Was, ECVF_SetByCode);
    TestEqual(
        TEXT("near, off"), FString(VoxelWaterFog::GetWaterMaterialName(false, false)), FString(TEXT("M_ChunkWater")));
    TestEqual(
        TEXT("far, off"), FString(VoxelWaterFog::GetWaterMaterialName(false, true)), FString(TEXT("M_ChunkFarWater")));
    for (const bool bFar : {false, true})
    {
        const TCHAR* Name = VoxelWaterFog::GetWaterMaterialName(true, bFar);
        const UMaterial* Material = Cast<UMaterial>(UUnnamedProjectSettings::GetBlockAssetPath(Name).TryLoad());
        if (TestNotNull(FString::Printf(TEXT("%s exists"), Name), Material))
        {
            TestTrue(FString::Printf(TEXT("%s writes its depth"), Name), Material->bOutputTranslucentVelocity != 0);
            TestTrue(FString::Printf(TEXT("%s is two-sided"), Name), Material->IsTwoSided());
        }
    }
    // The plain ones stay as they were: one-sided, no depth.
    for (const bool bFar : {false, true})
    {
        const TCHAR* Name = VoxelWaterFog::GetWaterMaterialName(false, bFar);
        const UMaterial* Material = Cast<UMaterial>(UUnnamedProjectSettings::GetBlockAssetPath(Name).TryLoad());
        if (TestNotNull(FString::Printf(TEXT("%s exists"), Name), Material))
        {
            TestFalse(FString::Printf(TEXT("%s writes no depth"), Name), Material->bOutputTranslucentVelocity != 0);
            TestFalse(FString::Printf(TEXT("%s is one-sided"), Name), Material->IsTwoSided());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelWaterFogColorsTest, "Unnamed.Voxel.WaterFog.Colors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelWaterFogColorsTest::RunTest(const FString& Parameters)
{
    // The defaults (#050533, -8 to 96 blocks), a biome's own colour and end, and lava's fog.
    const auto Expect =
        [this](const TCHAR* What, const TOptional<VoxelWaterFog::FFog>& Fog, int32 Rgb, float Start, float End)
    {
        const FColor Color = Fog ? Fog->Color.QuantizeRound() : FColor::Transparent;
        TestTrue(FString::Printf(TEXT("%s: #%02x%02x%02x %.1f to %.1f cm"), What, Color.R, Color.G, Color.B,
                     Fog ? Fog->StartCm : 0.f, Fog ? Fog->EndCm : 0.f),
            Fog && (Color.R << 16 | Color.G << 8 | Color.B) == Rgb && FMath::IsNearlyEqual(Fog->StartCm, Start) &&
                FMath::IsNearlyEqual(Fog->EndCm, End, .01f));
    };
    Expect(TEXT("water, no biome"), VoxelWaterFog::GetFog(true, EVoxelFluid::Water, nullptr, 100, false), 0x050533,
        -800, 9600);

    const FString Ids[] = {TEXT("minecraft:plains"), TEXT("minecraft:swamp"), TEXT("minecraft:warm_ocean"),
        TEXT("minecraft:mangrove_swamp")};
    FVoxelBiomeTable Table;
    FString Error;
    const bool bRead = VoxelWorldgen::ReadBiomes(VoxelWorldgen::GetReferenceRoot(), Ids, Table, Error);
    if (!TestTrue(FString::Printf(TEXT("the biomes read (%s)"), *Error), bRead)) return true;
    const auto Biome = [&Table](const TCHAR* Id) { return &Table.Biomes[Table.FindBiome(Id)]; };
    Expect(TEXT("plains"), VoxelWaterFog::GetFog(true, EVoxelFluid::Water, Biome(TEXT("minecraft:plains")), 100, false),
        0x050533, -800, 9600);
    Expect(TEXT("swamp"), VoxelWaterFog::GetFog(true, EVoxelFluid::Water, Biome(TEXT("minecraft:swamp")), 100, false),
        0x232317, -800, 9600 * .85f);
    Expect(TEXT("warm ocean"),
        VoxelWaterFog::GetFog(true, EVoxelFluid::Water, Biome(TEXT("minecraft:warm_ocean")), 100, false), 0x041f33,
        -800, 9600);
    Expect(TEXT("mangrove swamp"),
        VoxelWaterFog::GetFog(true, EVoxelFluid::Water, Biome(TEXT("minecraft:mangrove_swamp")), 100, false), 0x4d7a60,
        -800, 9600 * .85f);
    // Brightened (voxel.Look.WaterFogBright 1): the warm ocean's full fog is the wiki's 1.18 deep warm ocean frame's.
    Expect(TEXT("warm ocean, bright"),
        VoxelWaterFog::GetFog(true, EVoxelFluid::Water, Biome(TEXT("minecraft:warm_ocean")), 100), 0x1392f1, -800,
        9600);
    Expect(
        TEXT("no biome, bright"), VoxelWaterFog::GetFog(true, EVoxelFluid::Water, nullptr, 100), 0x1818f1, -800, 9600);
    Expect(TEXT("lava in swamp"), VoxelWaterFog::GetFog(true, EVoxelFluid::Lava, Biome(TEXT("minecraft:swamp")), 100),
        0x991a00, 0, 100);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelWaterFogEyeTest, "Unnamed.Voxel.WaterFog.Eye",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelWaterFogEyeTest::RunTest(const FString& Parameters)
{
    // The eye is in a fluid under its surface: a source's at 8/9, the cell's top when fluid lies above; held water
    // counts; lava is lava.
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    const uint16 Air = 0, Water = Table->FindState(TEXT("water")), Lava = Table->FindState(TEXT("lava")),
                 Seagrass = Table->FindState(TEXT("seagrass")), Stone = Table->FindState(TEXT("stone"));
    if (!TestTrue(TEXT("the states exist"), Water && Lava && Seagrass && Stone)) return true;
    const int32 Low = Table->WithValue(Water, TEXT("level"), TEXT("4"));
    if (!TestTrue(TEXT("water level 4 exists"), Low != INDEX_NONE)) return true;
    struct FCase
    {
        const TCHAR* Name;
        uint16 State;
        uint16 Above;
        double Height;
        EVoxelFluid Expected;
    };
    const FCase Cases[] = {
        {TEXT("air"), Air, Air, .5, EVoxelFluid::None},
        {TEXT("stone"), Stone, Water, .5, EVoxelFluid::None},
        {TEXT("water source, middle"), Water, Air, .5, EVoxelFluid::Water},
        {TEXT("water source, above its 8/9 surface"), Water, Air, .95, EVoxelFluid::None},
        {TEXT("water source under water, near the top"), Water, Water, .95, EVoxelFluid::Water},
        {TEXT("water level 4, under its surface"), uint16(Low), Air, .3, EVoxelFluid::Water},
        {TEXT("water level 4, above its surface"), uint16(Low), Air, .7, EVoxelFluid::None},
        {TEXT("seagrass holds water"), Seagrass, Water, .5, EVoxelFluid::Water},
        {TEXT("lava source"), Lava, Air, .5, EVoxelFluid::Lava},
        {TEXT("lava under water"), Lava, Water, .95, EVoxelFluid::Lava},
    };
    for (const FCase& Case : Cases)
        TestEqual(Case.Name, int32(VoxelWaterFog::GetFluidAt(*Table, Case.State, Case.Above, Case.Height)),
            int32(Case.Expected));
    return true;
}

#endif
