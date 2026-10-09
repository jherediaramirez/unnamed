#if WITH_DEV_AUTOMATION_TESTS
#include "Core/RealtimeMeshBuilder.h"
#include "Foundation/FoundationGrid.h"
#include "Misc/AutomationTest.h"
#include "RealtimeMeshSimple.h"
#include "Tasks/Task.h"
#include "Voxel/VoxelConsole.h"
#include "Voxel/VoxelWorldSettings.h"
#include "VoxelConstants.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelConstantsTest, "Unnamed.Voxel.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelConstantsTest::RunTest(const FString& Parameters)
{
    // The voxel core cannot see the game module, so it keeps its own copy of the section size.
    TestEqual(TEXT("Voxel sections are the block world's sections"), VoxelConstants::SectionSize,
        FoundationGrid::SectionCells);
    TestEqual(TEXT("24 sections"), VoxelConstants::SectionCount, 24);
    TestEqual(TEXT("Lowest block"), VoxelConstants::MinZ, -64);
    TestEqual(TEXT("Highest block"), VoxelConstants::MaxZ, 319);
    TestEqual(TEXT("Sea level"), VoxelConstants::SeaLevel, 63);
    TestEqual(TEXT("The bottom section starts on a section boundary"),
        FoundationGrid::FloorDivide(VoxelConstants::MinZ, VoxelConstants::SectionSize) * VoxelConstants::SectionSize,
        VoxelConstants::MinZ);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSettingsTest, "Unnamed.Voxel.Settings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSettingsTest::RunTest(const FString& Parameters)
{
    using namespace VoxelConsole;
    const UVoxelWorldSettings& Settings = UVoxelWorldSettings::Get();
    Settings.PushToConsoleVariables();

    // A variable set at a higher priority (console, command line, ini) keeps its value; every other one mirrors the
    // project setting.
    auto Mirrors = [this](const TCHAR* What, IConsoleVariable* Variable, const FString& Expected)
    {
        if ((Variable->GetFlags() & ECVF_SetByMask) > ECVF_SetByProjectSetting)
            AddInfo(FString(What) + TEXT(" is overridden at a higher priority"));
        else
            TestEqual(FString(What) + TEXT(" mirrors the project setting"), Variable->GetString(), Expected);
    };
    Mirrors(TEXT("voxel.ViewRadius"), CVarViewRadius.AsVariable(), FString::FromInt(Settings.ViewRadius));
    Mirrors(TEXT("voxel.UnloadMargin"), CVarUnloadMargin.AsVariable(), FString::FromInt(Settings.UnloadMargin));
    Mirrors(
        TEXT("voxel.MaxTasksInFlight"), CVarMaxTasksInFlight.AsVariable(), FString::FromInt(Settings.MaxTasksInFlight));
    if ((CVarApplyBudgetMs->GetFlags() & ECVF_SetByMask) <= ECVF_SetByProjectSetting)
        TestEqual(TEXT("voxel.ApplyBudgetMs mirrors the project setting"), CVarApplyBudgetMs.GetValueOnGameThread(),
            Settings.ApplyBudgetMs);
    if ((CVarAmbientOcclusion->GetFlags() & ECVF_SetByMask) <= ECVF_SetByProjectSetting)
        TestEqual(TEXT("voxel.AmbientOcclusion mirrors the project setting"),
            CVarAmbientOcclusion.GetValueOnGameThread(), Settings.bAmbientOcclusion);
    if ((CVarGreedyMeshing->GetFlags() & ECVF_SetByMask) <= ECVF_SetByProjectSetting)
        TestEqual(TEXT("voxel.GreedyMeshing mirrors the project setting"), CVarGreedyMeshing.GetValueOnGameThread(),
            Settings.bGreedyMeshing);

    TestTrue(TEXT("Unloading starts beyond the view radius"), Settings.UnloadMargin >= 1);
    TestTrue(TEXT("At least two tasks in flight"), GetMaxTasksInFlight() >= 2);

    const FString Info = DescribeInfo();
    for (const TCHAR* Name : {TEXT("voxel.ViewRadius"), TEXT("voxel.UnloadMargin"), TEXT("voxel.ApplyBudgetMs"),
             TEXT("voxel.MaxTasksInFlight"), TEXT("voxel.AmbientOcclusion"), TEXT("voxel.GreedyMeshing")})
        TestTrue(TEXT("voxel.Info lists ") + FString(Name), Info.Contains(Name));
    TestTrue(TEXT("voxel.Info shows the dev seed"), Info.Contains(TEXT("\"") + Settings.DevSeed + TEXT("\"")));
    TestTrue(TEXT("voxel.Info shows the world range"), Info.Contains(TEXT("Z -64..319")));
    TestTrue(TEXT("The voxel.Info command is registered"),
        IConsoleManager::Get().FindConsoleObject(TEXT("voxel.Info")) != nullptr);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelMeshBackendTest, "Unnamed.Voxel.MeshBackend",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelMeshBackendTest::RunTest(const FString& Parameters)
{
    using namespace RealtimeMesh;
    // The chunk pipeline's split: a worker builds plain stream data, then the game thread hands it to a mesh.
    UE::Tasks::TTask<FRealtimeMeshStreamSet> Build = UE::Tasks::Launch(UE_SOURCE_LOCATION,
        []
        {
            FRealtimeMeshStreamSet Streams;
            TRealtimeMeshBuilderLocal<uint32, FPackedNormal, FVector2DHalf, 1> Builder(Streams);
            Builder.EnableTangents();
            Builder.EnableTexCoords();
            Builder.EnableColors();
            // One upward-facing block face, 100 cm square.
            for (const FVector2f Corner : {FVector2f(0, 0), FVector2f(100, 0), FVector2f(100, 100), FVector2f(0, 100)})
                Builder.AddVertex(FVector3f(Corner.X, Corner.Y, 0))
                    .SetNormalAndTangent(FVector3f(0, 0, 1), FVector3f(1, 0, 0))
                    .SetTexCoord(Corner / 100)
                    .SetColor(FColor::White);
            Builder.AddTriangle(0, 2, 1);
            Builder.AddTriangle(0, 3, 2);
            return Streams;
        });

    URealtimeMeshSimple* Mesh = NewObject<URealtimeMeshSimple>();
    Mesh->SetupMaterialSlot(0, TEXT("Opaque"));
    const FRealtimeMeshBufferSetKey Key = FRealtimeMeshBufferSetKey::Create(0, TEXT("Section0"));
    Mesh->CreateBufferSet(Key, Build.GetResult());

    int32 Positions = 0, Indices = 0;
    Mesh->ProcessMesh(Key,
        [&Positions, &Indices](const FRealtimeMeshStreamSet& Streams)
        {
            if (const FRealtimeMeshStream* Stream = Streams.Find(FRealtimeMeshStreams::Position))
                Positions = Stream->Num();
            if (const FRealtimeMeshStream* Stream = Streams.Find(FRealtimeMeshStreams::Triangles))
                Indices = Stream->Num() * Stream->GetNumElements();
        });
    TestEqual(TEXT("The mesh holds the worker's four corners"), Positions, 4);
    TestEqual(TEXT("and its two triangles"), Indices, 6);
    return true;
}
#endif
