#if WITH_DEV_AUTOMATION_TESTS
#include "Blocks/VoxelBlockTable.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationItems.h"
#include "Foundation/FoundationTags.h"
#include "Foundation/FoundationVitals.h"
#include "Meshing/VoxelMesher.h"
#include "Misc/AutomationTest.h"
#include "Random/VoxelSeed.h"
#include "Storage/VoxelColumn.h"
#include "Streaming/VoxelStreamingPlanner.h"
#include "Terrain/VoxelHeightTerrain.h"
#include "Voxel/VoxelBlockStorage.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelColumnPipeline.h"
#include "VoxelConstants.h"

namespace
{
    // Radius 2 with the smallest margin: 37 columns in view, 69 generated.
    constexpr int32 EditViewRadius = 2;
    constexpr int32 EditUnloadMargin = 2;
    // Every column in view lies within this many columns of the center on both axes.
    constexpr int32 EditScanExtent = 6;
    // Rounds of Update, take and wait before a run gives up.
    constexpr int32 EditRoundCap = 4000;
    constexpr int32 EditSectionHeight = 16;
    constexpr int32 EditColumnWidth = 16;
    constexpr int32 EditErrorCap = 12;
    // Columns within this many of the center keep their section meshes (the pipeline's default).
    constexpr int32 EditCacheRadius = 2;

    using FEditSections = TArray<TSharedPtr<const FVoxelMeshData, ESPMode::ThreadSafe>>;
    using FEditKeys = TArray<FVoxelSectionKey, TInlineAllocator<8>>;

    // The column holding a world coordinate, rounded down for negatives: written out, not the pipeline's shift.
    int32 EditFloorDiv(int32 Value, int32 Divisor)
    {
        const int32 Quotient = Value / Divisor;
        return (Value % Divisor != 0 && Value < 0) ? Quotient - 1 : Quotient;
    }

    FIntPoint GetEditColumn(const FIntVector& Cell)
    {
        return FIntPoint(EditFloorDiv(Cell.X, EditColumnWidth), EditFloorDiv(Cell.Y, EditColumnWidth));
    }

    FString GetEditCellText(const FIntVector& Cell)
    {
        return FString::Printf(TEXT("(%d, %d, %d)"), Cell.X, Cell.Y, Cell.Z);
    }

    // Lists errors up to a cap, so one broken invariant does not drown the log.
    struct FEditReport
    {
        explicit FEditReport(FAutomationTestBase& InTest) : Test(InTest) {}

        FAutomationTestBase& Test;
        int32 Errors = 0;

        void Fail(const FString& Message)
        {
            if (Errors < EditErrorCap)
                Test.AddError(Message);
            else if (Errors == EditErrorCap)
                Test.AddError(TEXT("Further errors are not listed."));
            ++Errors;
        }
    };

    // FNV-1a over raw bytes: the streams hashed are plain floats, integers and colors without padding.
    struct FEditHash
    {
        uint64 Value = 14695981039346656037ull;

        void AddBytes(const void* Data, SIZE_T Size)
        {
            const uint8* Bytes = static_cast<const uint8*>(Data);
            for (SIZE_T Index = 0; Index < Size; ++Index)
            {
                Value ^= Bytes[Index];
                Value *= 1099511628211ull;
            }
        }

        template <typename T> void AddValue(const T& Item) { AddBytes(&Item, sizeof(T)); }

        template <typename T> void AddArray(const TArray<T>& Items)
        {
            AddValue(int32(Items.Num()));
            if (Items.Num() > 0) AddBytes(Items.GetData(), static_cast<SIZE_T>(Items.Num()) * sizeof(T));
        }
    };

    // A section's whole mesh, layer by layer, in stream order. A section without geometry (null or empty) hashes as
    // a single zero byte.
    void AddEditMesh(FEditHash& Hash, const FVoxelMeshData* Mesh)
    {
        if (!Mesh || Mesh->IsEmpty())
        {
            Hash.AddValue(uint8(0));
            return;
        }
        Hash.AddValue(uint8(1));
        for (const FVoxelMeshLayer& Layer : Mesh->Layers)
        {
            Hash.AddArray(Layer.Positions);
            Hash.AddArray(Layer.BlockUVs);
            Hash.AddArray(Layer.Codes);
            Hash.AddArray(Layer.TintFaces);
            Hash.AddArray(Layer.Colors);
            Hash.AddArray(Layer.Indices);
        }
    }

    FVoxelStreamingRings GetEditRings(FIntPoint Center)
    {
        return FVoxelStreamingRings(Center, EditViewRadius, EditUnloadMargin);
    }

    // The columns in view around the rings' center, in row order.
    void GetEditView(const FVoxelStreamingRings& Rings, TArray<FIntPoint>& Out)
    {
        Out.Reset();
        for (int32 DY = -EditScanExtent; DY <= EditScanExtent; ++DY)
            for (int32 DX = -EditScanExtent; DX <= EditScanExtent; ++DX)
            {
                const FIntPoint Column(Rings.Center.X + DX, Rings.Center.Y + DY);
                if (Rings.IsInView(Column)) Out.Add(Column);
            }
    }

    // A finished mesh taken for the screen, and how many sections its job meshed (the rest came from the cache).
    struct FEditTake
    {
        FIntPoint Column = FIntPoint::ZeroValue;
        int32 MeshedSections = 0;
    };

    // What the screen would hold for a column: the sections of the latest mesh taken.
    struct FEditScreenColumn
    {
        FEditSections Sections;
    };

    // Drives a pipeline the way the world subsystem does, minus the world: Update, take every finished mesh, wait for
    // the jobs, until it is idle.
    struct FEditDriver
    {
        FEditDriver(FEditReport& InReport, FVoxelColumnPipeline& InPipeline, const TCHAR* InName)
            : Report(InReport), Pipeline(InPipeline), Name(InName)
        {
        }

        FEditReport& Report;
        FVoxelColumnPipeline& Pipeline;
        FString Name;
        TMap<FIntPoint, FEditScreenColumn> Screen;
        // The meshes taken since the list was last cleared, in order.
        TArray<FEditTake> Taken;

        // False after EditRoundCap rounds.
        bool RunUntilIdle(const FString& Step)
        {
            for (int32 Round = 0; Round < EditRoundCap; ++Round)
            {
                Pipeline.Update();
                FVoxelColumnPipeline::FReady Ready;
                while (Pipeline.PopReady(Ready))
                {
                    Screen.FindOrAdd(Ready.Column).Sections = Ready.Mesh->Sections;
                    FEditTake& Take = Taken.AddDefaulted_GetRef();
                    Take.Column = Ready.Column;
                    Take.MeshedSections = Ready.Mesh->MeshedSections;
                }
                if (Pipeline.IsIdle()) return true;
                Pipeline.WaitForJobs();
            }
            Report.Test.AddError(
                FString::Printf(TEXT("%s, %s: not idle after %d rounds (%d jobs in flight, %d meshes waiting)"), *Name,
                    *Step, EditRoundCap, Pipeline.GetJobCount(), Pipeline.GetStats().Ready));
            return false;
        }
    };

    // The table and terrain every pipeline of a test shares. The table outlives the terrain built on it.
    struct FEditContext
    {
        FEditContext(
            const TSharedRef<const FVoxelBlockTable>& InTable, const TSharedRef<const IVoxelTerrain>& InTerrain)
            : Table(InTable), Terrain(InTerrain)
        {
        }

        TSharedRef<const FVoxelBlockTable> Table;
        TSharedRef<const IVoxelTerrain> Terrain;

        // 8 tasks, and section meshes cached within EditCacheRadius columns of the center.
        TUniquePtr<FVoxelColumnPipeline> MakePipeline() const
        {
            FVoxelColumnPipeline::FSettings Settings;
            Settings.MaxTasksInFlight = 8;
            Settings.MeshCacheRadius = EditCacheRadius;
            return MakeUnique<FVoxelColumnPipeline>(Table, Terrain, Settings);
        }
    };

    // A pipeline with its driver.
    struct FEditRun
    {
        FEditRun(FEditReport& InReport, const FEditContext& Context, const TCHAR* InName)
            : Pipeline(Context.MakePipeline()), Driver(InReport, *Pipeline, InName)
        {
        }

        TUniquePtr<FVoxelColumnPipeline> Pipeline;
        FEditDriver Driver;

        // Moves the focus (view sector 0) and runs until idle.
        bool MoveTo(FIntPoint Center, const FString& Step)
        {
            Pipeline->SetFocus(GetEditRings(Center), 0);
            return Driver.RunUntilIdle(Step);
        }
    };

    // The sections whose padded copy holds Cell, written out from the definition: column c reads X and Y in
    // [c * 16 - 1, c * 16 + 16], and section s reads Z in [MinZ + s * 16 - 1, MinZ + s * 16 + 16]. A cell outside the
    // world's height touches nothing.
    void GetEditBruteSections(const FIntVector& Cell, FEditKeys& Out)
    {
        Out.Reset();
        if (Cell.Z < VoxelConstants::MinZ || Cell.Z > VoxelConstants::MaxZ) return;
        const FIntPoint Home = GetEditColumn(Cell);
        for (int32 ColumnY = Home.Y - 1; ColumnY <= Home.Y + 1; ++ColumnY)
            for (int32 ColumnX = Home.X - 1; ColumnX <= Home.X + 1; ++ColumnX)
            {
                if (Cell.X < ColumnX * EditColumnWidth - 1 || Cell.X > ColumnX * EditColumnWidth + EditColumnWidth)
                    continue;
                if (Cell.Y < ColumnY * EditColumnWidth - 1 || Cell.Y > ColumnY * EditColumnWidth + EditColumnWidth)
                    continue;
                for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
                {
                    const int32 Low = VoxelConstants::MinZ + Section * EditSectionHeight;
                    if (Cell.Z < Low - 1 || Cell.Z > Low + EditSectionHeight) continue;
                    FVoxelSectionKey& Key = Out.AddDefaulted_GetRef();
                    Key.Column = FIntPoint(ColumnX, ColumnY);
                    Key.Section = Section;
                }
            }
    }

    // Sorted, so two lists with the same keys read the same.
    FString GetEditKeysText(FEditKeys Keys)
    {
        Keys.Sort(
            [](const FVoxelSectionKey& Left, const FVoxelSectionKey& Right)
            {
                if (Left.Column.Y != Right.Column.Y) return Left.Column.Y < Right.Column.Y;
                if (Left.Column.X != Right.Column.X) return Left.Column.X < Right.Column.X;
                return Left.Section < Right.Section;
            });
        FString Text;
        for (const FVoxelSectionKey& Key : Keys)
            Text += FString::Printf(
                TEXT("%s(%d, %d)#%d"), Text.IsEmpty() ? TEXT("") : TEXT(" "), Key.Column.X, Key.Column.Y, Key.Section);
        return Text.IsEmpty() ? FString(TEXT("none")) : Text;
    }

    bool IsEditSameKeys(const FEditKeys& Actual, const FEditKeys& Expected)
    {
        if (Actual.Num() != Expected.Num()) return false;
        for (int32 Index = 0; Index < Actual.Num(); ++Index)
            if (!Expected.Contains(Actual[Index]) || Actual.Find(Actual[Index]) != Index) return false;
        return true;
    }

    // GetSectionsTouching against the definition at one cell, and against the expected count when it is not negative.
    void CheckEditSections(FEditReport& Report, const FString& Label, const FIntVector& Cell, int32 ExpectedCount)
    {
        FEditKeys Actual, Expected;
        VoxelMesher::GetSectionsTouching(Cell, Actual);
        GetEditBruteSections(Cell, Expected);
        if (!IsEditSameKeys(Actual, Expected))
            Report.Fail(FString::Printf(
                TEXT("sections, %s: cell %s is touched by %d sections [%s], the definition gives %d [%s]"), *Label,
                *GetEditCellText(Cell), Actual.Num(), *GetEditKeysText(Actual), Expected.Num(),
                *GetEditKeysText(Expected)));
        if (ExpectedCount >= 0 && Actual.Num() != ExpectedCount)
            Report.Fail(FString::Printf(TEXT("sections, %s: cell %s is touched by %d sections, expected %d"), *Label,
                *GetEditCellText(Cell), Actual.Num(), ExpectedCount));
    }

    // Every cell of a box against the definition.
    void ScanEditSections(FEditReport& Report, const FString& Region, const FIntVector& Low, const FIntVector& High)
    {
        for (int32 Z = Low.Z; Z <= High.Z; ++Z)
            for (int32 Y = Low.Y; Y <= High.Y; ++Y)
                for (int32 X = Low.X; X <= High.X; ++X)
                    CheckEditSections(Report, Region, FIntVector(X, Y, Z), -1);
    }

    struct FEditSectionCase
    {
        const TCHAR* Label;
        FIntVector Cell;
        int32 Count;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelEditSectionsTest, "Unnamed.Voxel.Edit.Sections",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelEditSectionsTest::RunTest(const FString& Parameters)
{
    FEditReport Report(*this);
    const int32 BottomZ = VoxelConstants::MinZ, TopZ = VoxelConstants::MaxZ;

    // The documented counts: section 1 spans Z = BottomZ + 16 to BottomZ + 31, and each border is two cells thick.
    TArray<FEditSectionCase> Cases;
    Cases.Add({TEXT("interior"), FIntVector(8, 8, BottomZ + 24), 1});
    Cases.Add({TEXT("interior, negative"), FIntVector(-8, -8, BottomZ + 24), 1});
    Cases.Add({TEXT("section border, bottom of section 1"), FIntVector(8, 8, BottomZ + 16), 2});
    Cases.Add({TEXT("section border, top of section 0"), FIntVector(8, 8, BottomZ + 15), 2});
    Cases.Add({TEXT("column border, x = 0"), FIntVector(0, 8, BottomZ + 24), 2});
    Cases.Add({TEXT("column border, x = 15"), FIntVector(15, 8, BottomZ + 24), 2});
    Cases.Add({TEXT("column border, y = 0"), FIntVector(8, 0, BottomZ + 24), 2});
    Cases.Add({TEXT("column border, y = 15"), FIntVector(8, 15, BottomZ + 24), 2});
    Cases.Add({TEXT("column border, x = -1"), FIntVector(-1, 8, BottomZ + 24), 2});
    Cases.Add({TEXT("column corner"), FIntVector(0, 0, BottomZ + 24), 4});
    Cases.Add({TEXT("column corner, 15 and 15"), FIntVector(15, 15, BottomZ + 24), 4});
    Cases.Add({TEXT("column corner, 0 and 15"), FIntVector(0, 15, BottomZ + 24), 4});
    Cases.Add({TEXT("column corner, negative"), FIntVector(-1, -1, BottomZ + 24), 4});
    Cases.Add({TEXT("corner on a section border"), FIntVector(0, 0, BottomZ + 16), 8});
    Cases.Add({TEXT("corner on a section border, top of section 0"), FIntVector(15, 15, BottomZ + 15), 8});
    Cases.Add({TEXT("corner on a section border, negative"), FIntVector(-1, -1, BottomZ + 16), 8});
    Cases.Add({TEXT("world bottom, interior"), FIntVector(8, 8, BottomZ), 1});
    Cases.Add({TEXT("world bottom at a column corner"), FIntVector(0, 0, BottomZ), 4});
    Cases.Add({TEXT("world top, interior"), FIntVector(8, 8, TopZ), 1});
    Cases.Add({TEXT("world top at a column corner"), FIntVector(0, 0, TopZ), 4});
    Cases.Add({TEXT("just below the world"), FIntVector(0, 0, BottomZ - 1), 0});
    Cases.Add({TEXT("far below the world"), FIntVector(5, 5, BottomZ - 100), 0});
    Cases.Add({TEXT("just above the world"), FIntVector(0, 0, TopZ + 1), 0});
    Cases.Add({TEXT("far above the world"), FIntVector(5, 5, TopZ + 100), 0});
    for (const FEditSectionCase& Entry : Cases)
        CheckEditSections(Report, Entry.Label, Entry.Cell, Entry.Count);

    // Every cell around a column corner at the origin (columns -1 to 1, negative coordinates, the world's bottom and
    // the sections above it), at the top of the world, and at a far corner with mixed signs on a section border.
    ScanEditSections(Report, TEXT("scan around the origin corner, sections 0 to 3"), FIntVector(-18, -18, BottomZ - 2),
        FIntVector(33, 33, BottomZ + 65));
    ScanEditSections(
        Report, TEXT("scan at the top of the world"), FIntVector(-2, -2, TopZ - 33), FIntVector(17, 17, TopZ + 2));
    ScanEditSections(Report, TEXT("scan at a far corner on a section border"), FIntVector(1598, -1602, BottomZ + 158),
        FIntVector(1634, -1566, BottomZ + 178));
    return true;
}

namespace
{
    template <typename T> bool IsEditSameBits(const TArray<T>& Left, const TArray<T>& Right)
    {
        return Left.Num() == Right.Num() &&
            (Left.Num() == 0 ||
                FMemory::Memcmp(Left.GetData(), Right.GetData(), static_cast<SIZE_T>(Left.Num()) * sizeof(T)) == 0);
    }

    // Why a kept section mesh differs from a fresh one, or empty text when they are the same. Null and empty are the
    // same.
    FString GetEditMeshDifference(const FVoxelMeshData* Kept, const FVoxelMeshData* Fresh)
    {
        const bool bKeptEmpty = !Kept || Kept->IsEmpty();
        const bool bFreshEmpty = !Fresh || Fresh->IsEmpty();
        if (bKeptEmpty && bFreshEmpty) return FString();
        if (bKeptEmpty)
            return FString::Printf(TEXT("the kept mesh is empty, a fresh mesh has %d quads"), Fresh->GetQuadCount());
        if (bFreshEmpty)
            return FString::Printf(TEXT("the kept mesh has %d quads, a fresh mesh is empty"), Kept->GetQuadCount());
        for (int32 Layer = 0; Layer < int32(EVoxelLayer::Count); ++Layer)
        {
            const FVoxelMeshLayer& KeptLayer = Kept->Layers[Layer];
            const FVoxelMeshLayer& FreshLayer = Fresh->Layers[Layer];
            if (KeptLayer.Positions.Num() != FreshLayer.Positions.Num())
                return FString::Printf(TEXT("layer %d has %d vertices, a fresh mesh has %d"), Layer,
                    KeptLayer.Positions.Num(), FreshLayer.Positions.Num());
            if (!IsEditSameBits(KeptLayer.Positions, FreshLayer.Positions))
                return FString::Printf(TEXT("layer %d positions differ from a fresh mesh"), Layer);
            if (!IsEditSameBits(KeptLayer.BlockUVs, FreshLayer.BlockUVs))
                return FString::Printf(TEXT("layer %d block UVs differ from a fresh mesh"), Layer);
            if (!IsEditSameBits(KeptLayer.Codes, FreshLayer.Codes))
                return FString::Printf(TEXT("layer %d codes differ from a fresh mesh"), Layer);
            if (!IsEditSameBits(KeptLayer.TintFaces, FreshLayer.TintFaces))
                return FString::Printf(TEXT("layer %d tints and faces differ from a fresh mesh"), Layer);
            if (!IsEditSameBits(KeptLayer.Colors, FreshLayer.Colors))
                return FString::Printf(TEXT("layer %d colors differ from a fresh mesh"), Layer);
            if (!IsEditSameBits(KeptLayer.Indices, FreshLayer.Indices))
                return FString::Printf(TEXT("layer %d indices differ from a fresh mesh"), Layer);
        }
        return FString();
    }

    // Every in-view column's latest section meshes against a fresh full mesh of the current data: the 3 x 3 columns
    // gathered from the pipeline, each section padded and meshed with the pipeline's default options.
    void CheckEditMeshesCurrent(
        FEditReport& Report, const FEditRun& Run, const FVoxelBlockTable& Table, const FString& Step)
    {
        FVoxelColumnPipeline::FSettings Defaults;
        TUniquePtr<FVoxelPaddedSection> Padded = MakeUnique<FVoxelPaddedSection>();
        FVoxelMeshData Fresh;
        TArray<FIntPoint> InView;
        GetEditView(Run.Pipeline->GetRings(), InView);
        for (const FIntPoint Column : InView)
        {
            const FEditScreenColumn* Shown = Run.Driver.Screen.Find(Column);
            if (!Shown || Shown->Sections.Num() != VoxelConstants::SectionCount)
            {
                Report.Fail(FString::Printf(
                    TEXT("%s: column (%d, %d) has no complete mesh taken for the screen"), *Step, Column.X, Column.Y));
                continue;
            }
            FVoxelNeighborhood Neighborhood;
            for (int32 DY = -1; DY <= 1; ++DY)
                for (int32 DX = -1; DX <= 1; ++DX)
                    Neighborhood.Set(DX, DY, Run.Pipeline->FindColumn(Column + FIntPoint(DX, DY)));
            for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
            {
                Fresh.Reset();
                VoxelMesher::GatherPadded(Neighborhood, Section, *Padded);
                VoxelMesher::MeshSection(Table, *Padded, Section, Defaults.MeshOptions, Fresh);
                const FString Difference = GetEditMeshDifference(Shown->Sections[Section].Get(), &Fresh);
                if (Difference.IsEmpty()) continue;
                const int32 FirstZ = VoxelConstants::MinZ + Section * EditSectionHeight;
                Report.Fail(FString::Printf(TEXT("%s: column (%d, %d) section %d (Z %d to %d): %s"), *Step, Column.X,
                    Column.Y, Section, FirstZ, FirstZ + EditSectionHeight - 1, *Difference));
            }
        }
    }

    // A column's four heightmaps against those of a column built from a dense buffer of its states.
    void CheckEditHeightmaps(
        FEditReport& Report, const FVoxelBlockTable& Table, const FVoxelColumn& Column, const FString& Step)
    {
        TArray<uint16> Dense;
        Dense.SetNumUninitialized(FVoxelColumn::DenseVolume);
        for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; ++Z)
            for (int32 Y = 0; Y < EditColumnWidth; ++Y)
                for (int32 X = 0; X < EditColumnWidth; ++X)
                    Dense[FVoxelColumn::GetDenseIndex(X, Y, Z)] = Column.GetState(X, Y, Z);
        TUniquePtr<FVoxelColumn> Rebuilt = MakeUnique<FVoxelColumn>(Column.GetPosition());
        Rebuilt->Build(Dense, Table);
        static const TCHAR* const MapNames[] = {
            TEXT("WorldSurface"), TEXT("OceanFloor"), TEXT("MotionBlocking"), TEXT("MotionBlockingNoLeaves")};
        for (int32 Map = 0; Map < int32(EVoxelHeightmap::Count); ++Map)
            for (int32 Y = 0; Y < EditColumnWidth; ++Y)
                for (int32 X = 0; X < EditColumnWidth; ++X)
                {
                    const EVoxelHeightmap Heightmap = static_cast<EVoxelHeightmap>(Map);
                    const int32 Kept = Column.GetHeight(Heightmap, X, Y);
                    const int32 Built = Rebuilt->GetHeight(Heightmap, X, Y);
                    if (Kept == Built) continue;
                    Report.Fail(FString::Printf(
                        TEXT("%s: column (%d, %d) %s at cell (%d, %d) is %d after the edit, %d when built from its "
                             "states"),
                        *Step, Column.GetPosition().X, Column.GetPosition().Y,
                        Map < int32(UE_ARRAY_COUNT(MapNames)) ? MapNames[Map] : TEXT("a heightmap"),
                        Column.GetPosition().X * EditColumnWidth + X, Column.GetPosition().Y * EditColumnWidth + Y,
                        Kept, Built));
                }
    }

    // One edit made through SetCell.
    struct FEditMade
    {
        FString Label;
        FIntVector Cell = FIntVector::ZeroValue;
        uint16 State = 0;
        int64 Stamp = 0;
    };

    bool IsEditMade(const TArray<FEditMade>& Made, const FIntVector& Cell)
    {
        for (const FEditMade& Edit : Made)
            if (Edit.Cell == Cell) return true;
        return false;
    }

    // The state the pipeline's column holds at a cell; false when the column is not generated.
    bool GetEditColumnState(const FVoxelColumnPipeline& Pipeline, const FIntVector& Cell, uint16& OutState)
    {
        const FIntPoint Column = GetEditColumn(Cell);
        const FVoxelColumn* Data = Pipeline.FindColumn(Column);
        if (!Data) return false;
        OutState = Data->GetState(Cell.X - Column.X * EditColumnWidth, Cell.Y - Column.Y * EditColumnWidth, Cell.Z);
        return true;
    }

    // Every edit made reads back through GetCell with its state and stamp, GetCell agrees with the column, and the
    // cells beside an edit that nothing edited read stamp 0.
    void CheckEditCells(
        FEditReport& Report, const FVoxelColumnPipeline& Pipeline, const TArray<FEditMade>& Made, const FString& Step)
    {
        static const FIntVector Offsets[6] = {FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0),
            FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1)};
        for (const FEditMade& Edit : Made)
        {
            uint16 State = 0;
            int64 Stamp = -1;
            if (!Pipeline.GetCell(Edit.Cell, State, Stamp))
                Report.Fail(FString::Printf(TEXT("%s: edit '%s' at cell %s: GetCell is false, column not generated"),
                    *Step, *Edit.Label, *GetEditCellText(Edit.Cell)));
            else if (State != Edit.State || Stamp != Edit.Stamp)
                Report.Fail(FString::Printf(
                    TEXT("%s: edit '%s' at cell %s reads state %d, stamp %lld; it was set to state %d, stamp %lld"),
                    *Step, *Edit.Label, *GetEditCellText(Edit.Cell), int32(State), Stamp, int32(Edit.State),
                    Edit.Stamp));
            uint16 ColumnState = 0;
            if (GetEditColumnState(Pipeline, Edit.Cell, ColumnState) && ColumnState != Edit.State)
                Report.Fail(FString::Printf(TEXT("%s: edit '%s': the column holds state %d at cell %s, expected %d"),
                    *Step, *Edit.Label, int32(ColumnState), *GetEditCellText(Edit.Cell), int32(Edit.State)));
            for (const FIntVector& Offset : Offsets)
            {
                const FIntVector Beside = Edit.Cell + Offset;
                uint16 BesideState = 0;
                int64 BesideStamp = -1;
                if (IsEditMade(Made, Beside) || !Pipeline.GetCell(Beside, BesideState, BesideStamp)) continue;
                uint16 BesideColumnState = 0;
                if (BesideStamp != 0)
                    Report.Fail(FString::Printf(
                        TEXT("%s: cell %s beside edit '%s' was never edited, but GetCell gives it stamp %lld"), *Step,
                        *GetEditCellText(Beside), *Edit.Label, BesideStamp));
                if (GetEditColumnState(Pipeline, Beside, BesideColumnState) && BesideColumnState != BesideState)
                    Report.Fail(FString::Printf(TEXT("%s: GetCell gives cell %s state %d, its column holds state %d"),
                        *Step, *GetEditCellText(Beside), int32(BesideState), int32(BesideColumnState)));
            }
        }
    }

    // A column's blocks and the mesh the screen shows for it.
    struct FEditHashes
    {
        bool bBlocks = false;
        bool bMesh = false;
        uint64 Blocks = 0;
        uint64 Mesh = 0;
        TArray<uint64> MeshSections;
    };

    FEditHashes GetEditHashes(const FEditRun& Run, FIntPoint Column)
    {
        FEditHashes Hashes;
        if (const FVoxelColumn* Data = Run.Pipeline->FindColumn(Column))
        {
            Hashes.bBlocks = true;
            FEditHash Hash;
            for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; ++Z)
                for (int32 Y = 0; Y < EditColumnWidth; ++Y)
                    for (int32 X = 0; X < EditColumnWidth; ++X)
                        Hash.AddValue(Data->GetState(X, Y, Z));
            Hashes.Blocks = Hash.Value;
        }
        if (const FEditScreenColumn* Shown = Run.Driver.Screen.Find(Column))
        {
            Hashes.bMesh = true;
            FEditHash Total;
            for (const TSharedPtr<const FVoxelMeshData, ESPMode::ThreadSafe>& Section : Shown->Sections)
            {
                FEditHash Part;
                AddEditMesh(Part, Section.Get());
                Hashes.MeshSections.Add(Part.Value);
                Total.AddValue(Part.Value);
            }
            Hashes.Mesh = Total.Value;
        }
        return Hashes;
    }

    void CompareEditHashes(FEditReport& Report, const FString& Step, FIntPoint Column, const TCHAR* LeftName,
        const FEditHashes& Left, const TCHAR* RightName, const FEditHashes& Right)
    {
        if (!Left.bBlocks || !Right.bBlocks)
            Report.Fail(FString::Printf(TEXT("%s: column (%d, %d) is not generated in %s"), *Step, Column.X, Column.Y,
                Left.bBlocks ? RightName : LeftName));
        else if (Left.Blocks != Right.Blocks)
            Report.Fail(FString::Printf(
                TEXT("%s: column (%d, %d) blocks differ between %s (hash %016llx) and %s (hash %016llx)"), *Step,
                Column.X, Column.Y, LeftName, Left.Blocks, RightName, Right.Blocks));
        if (!Left.bMesh || !Right.bMesh)
            Report.Fail(FString::Printf(TEXT("%s: column (%d, %d) has no mesh taken in %s"), *Step, Column.X, Column.Y,
                Left.bMesh ? RightName : LeftName));
        else if (Left.Mesh != Right.Mesh)
        {
            int32 Section = INDEX_NONE;
            for (int32 Index = 0; Index < FMath::Min(Left.MeshSections.Num(), Right.MeshSections.Num()); ++Index)
                if (Left.MeshSections[Index] != Right.MeshSections[Index])
                {
                    Section = Index;
                    break;
                }
            Report.Fail(FString::Printf(
                TEXT("%s: column (%d, %d) meshes differ between %s (hash %016llx) and %s (hash %016llx), first in "
                     "section %d"),
                *Step, Column.X, Column.Y, LeftName, Left.Mesh, RightName, Right.Mesh, Section));
        }
    }

    // The first cell at which two columns differ, named for the report.
    void ReportEditCellDifference(FEditReport& Report, const FString& Step, FIntPoint Column, const TCHAR* LeftName,
        const FVoxelColumn& Left, const TCHAR* RightName, const FVoxelColumn& Right)
    {
        for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; ++Z)
            for (int32 Y = 0; Y < EditColumnWidth; ++Y)
                for (int32 X = 0; X < EditColumnWidth; ++X)
                    if (Left.GetState(X, Y, Z) != Right.GetState(X, Y, Z))
                    {
                        Report.Fail(FString::Printf(
                            TEXT("%s: column (%d, %d) cell (%d, %d, %d) holds state %d in %s and state %d in %s"),
                            *Step, Column.X, Column.Y, Column.X * EditColumnWidth + X, Column.Y * EditColumnWidth + Y,
                            Z, int32(Left.GetState(X, Y, Z)), LeftName, int32(Right.GetState(X, Y, Z)), RightName));
                        return;
                    }
    }

    // An edit to make: a cell found from the surface at a local X and Y of a column.
    struct FEditPlan
    {
        const TCHAR* Label;
        FIntPoint Column;
        int32 LocalX;
        int32 LocalY;
        // The cell's Z within its section, 0 to 15.
        int32 LocalZ;
        // Stone into the lowest air cell above the surface with that Z, or the highest solid cell at or below the
        // surface with that Z mined to air.
        bool bPlace;
    };

    // Five kinds of cell, each placed and mined: interior, section border, column border, column corner (touching
    // columns of negative coordinates too), and a corner on a section border.
    void GetEditPlans(TArray<FEditPlan>& Out)
    {
        const FIntPoint Home(0, 0);
        Out.Reset();
        Out.Add({TEXT("interior, place"), Home, 5, 9, 7, true});
        Out.Add({TEXT("interior, mine"), Home, 10, 4, 7, false});
        Out.Add({TEXT("section border, place"), Home, 3, 12, 0, true});
        Out.Add({TEXT("section border, mine"), Home, 12, 11, 15, false});
        Out.Add({TEXT("column border, place"), Home, 0, 6, 7, true});
        Out.Add({TEXT("column border, mine"), Home, 9, 15, 7, false});
        Out.Add({TEXT("column corner, place"), Home, 15, 15, 7, true});
        Out.Add({TEXT("column corner, mine"), Home, 0, 0, 7, false});
        Out.Add({TEXT("corner on a section border, place"), Home, 15, 0, 0, true});
        Out.Add({TEXT("corner on a section border, mine"), Home, 0, 15, 15, false});
    }

    // The cell a plan names, from the column's surface: air to place into, a block to mine. Steps a section up or
    // down, keeping the Z within the section, when the first candidate does not fit.
    bool ResolveEditCell(const FVoxelColumnPipeline& Pipeline, const FEditPlan& Plan, FIntVector& OutCell)
    {
        const FVoxelColumn* Data = Pipeline.FindColumn(Plan.Column);
        if (!Data) return false;
        const int32 Surface = Data->GetHeight(EVoxelHeightmap::WorldSurface, Plan.LocalX, Plan.LocalY);
        if (Surface < VoxelConstants::MinZ) return false;
        int32 Z = 0;
        if (Plan.bPlace)
        {
            const int32 Start = Surface + 1;
            Z = Start + ((Plan.LocalZ - ((Start - VoxelConstants::MinZ) & 15)) & 15);
        }
        else
            Z = Surface - ((((Surface - VoxelConstants::MinZ) & 15) - Plan.LocalZ) & 15);
        for (int32 Attempt = 0; Attempt < 6; ++Attempt, Z += Plan.bPlace ? EditSectionHeight : -EditSectionHeight)
        {
            if (Z < VoxelConstants::MinZ || Z > VoxelConstants::MaxZ) return false;
            if ((Data->GetState(Plan.LocalX, Plan.LocalY, Z) == 0) != Plan.bPlace) continue;
            OutCell = FIntVector(
                Plan.Column.X * EditColumnWidth + Plan.LocalX, Plan.Column.Y * EditColumnWidth + Plan.LocalY, Z);
            return true;
        }
        return false;
    }

    bool IsEditWithin(FIntPoint Column, FIntPoint Center, int32 Extent)
    {
        return FMath::Abs(Column.X - Center.X) <= Extent && FMath::Abs(Column.Y - Center.Y) <= Extent;
    }

    // One edit: SetCell, run until idle, then every check that follows an edit.
    bool ApplyEdit(FEditReport& Report, FEditRun& Run, const FVoxelBlockTable& Table, const FEditPlan& Plan,
        uint16 Stone, TArray<FEditMade>& Made)
    {
        FVoxelColumnPipeline& Pipeline = *Run.Pipeline;
        const FString Step = FString::Printf(TEXT("edit %d (%s)"), Made.Num() + 1, Plan.Label);
        FIntVector Cell = FIntVector::ZeroValue;
        if (!ResolveEditCell(Pipeline, Plan, Cell))
        {
            Report.Fail(FString::Printf(TEXT("%s: no cell to %s at %d, %d of column (%d, %d)"), *Step,
                Plan.bPlace ? TEXT("place stone in") : TEXT("mine"), Plan.LocalX, Plan.LocalY, Plan.Column.X,
                Plan.Column.Y));
            return false;
        }
        FEditMade Edit;
        Edit.Label = Plan.Label;
        Edit.Cell = Cell;
        Edit.State = Plan.bPlace ? Stone : uint16(0);
        Edit.Stamp = 9000 + Made.Num();
        uint16 OldState = 0;
        int64 OldStamp = -1;
        if (!Pipeline.GetCell(Cell, OldState, OldStamp) || OldState == Edit.State || OldStamp != 0)
        {
            Report.Fail(FString::Printf(
                TEXT("%s: cell %s holds state %d, stamp %lld before the edit; expected another state than %d at "
                     "stamp 0"),
                *Step, *GetEditCellText(Cell), int32(OldState), OldStamp, int32(Edit.State)));
            return false;
        }

        // The sections the edit touches; only those in columns that are generated and in view mesh.
        FEditKeys Touching;
        VoxelMesher::GetSectionsTouching(Cell, Touching);
        TMap<FIntPoint, uint32> Dirty;
        for (const FVoxelSectionKey& Key : Touching)
            if (Pipeline.IsGenerated(Key.Column) && Pipeline.GetRings().IsInView(Key.Column))
                Dirty.FindOrAdd(Key.Column) |= 1u << Key.Section;

        Run.Driver.Taken.Reset();
        if (!Pipeline.SetCell(Cell, Edit.State, Edit.Stamp))
        {
            Report.Fail(FString::Printf(TEXT("%s: SetCell refused cell %s"), *Step, *GetEditCellText(Cell)));
            return false;
        }
        Made.Add(Edit);

        // Right after the edit, exactly those sections wait to be meshed.
        TArray<FIntPoint> InView;
        GetEditView(Pipeline.GetRings(), InView);
        for (const FIntPoint Column : InView)
        {
            const uint32* Expected = Dirty.Find(Column);
            const uint32 Actual = Pipeline.GetStatus(Column).DirtySections;
            if (Actual != (Expected ? *Expected : 0u))
                Report.Fail(FString::Printf(TEXT("%s: column (%d, %d) has dirty sections 0x%x after SetCell at %s, "
                                                 "expected 0x%x"),
                    *Step, Column.X, Column.Y, Actual, *GetEditCellText(Cell), Expected ? *Expected : 0u));
        }
        if (!Run.Driver.RunUntilIdle(Step)) return false;

        // The columns that got a new mesh are exactly the touched ones. Near the center, where meshes are cached,
        // each remeshed only the touched sections that hold blocks (an all-air section has nothing to mesh).
        TMap<FIntPoint, int32> Meshed;
        for (const FEditTake& Take : Run.Driver.Taken)
            Meshed.Add(Take.Column, Take.MeshedSections);
        for (const TPair<FIntPoint, uint32>& Touched : Dirty)
        {
            const int32* MeshedCount = Meshed.Find(Touched.Key);
            if (!MeshedCount)
            {
                Report.Fail(FString::Printf(TEXT("%s: column (%d, %d) is touched by the edit, but no new mesh was "
                                                 "taken for it"),
                    *Step, Touched.Key.X, Touched.Key.Y));
                continue;
            }
            if (!IsEditWithin(Touched.Key, Pipeline.GetRings().Center, EditCacheRadius)) continue;
            int32 ExpectedCount = 0;
            for (const FVoxelSectionKey& Key : Touching)
                if (Key.Column == Touched.Key && !Pipeline.FindColumn(Key.Column)->GetSection(Key.Section).IsEmpty())
                    ++ExpectedCount;
            if (*MeshedCount != ExpectedCount)
                Report.Fail(FString::Printf(
                    TEXT("%s: column (%d, %d) meshed %d sections for the edit, expected the %d touched ones"), *Step,
                    Touched.Key.X, Touched.Key.Y, *MeshedCount, ExpectedCount));
        }
        for (const TPair<FIntPoint, int32>& Remeshed : Meshed)
            if (!Dirty.Contains(Remeshed.Key))
                Report.Fail(FString::Printf(TEXT("%s: column (%d, %d) got a new mesh, but the edit does not touch it"),
                    *Step, Remeshed.Key.X, Remeshed.Key.Y));

        CheckEditMeshesCurrent(Report, Run, Table, Step);
        if (const FVoxelColumn* Edited = Pipeline.FindColumn(GetEditColumn(Cell)))
            CheckEditHeightmaps(Report, Table, *Edited, Step);
        CheckEditCells(Report, Pipeline, Made, Step);
        const int32 EditCount = Pipeline.GetStats().Edits;
        if (EditCount != Made.Num())
            Report.Fail(FString::Printf(
                TEXT("%s: the statistics count %d edited cells, %d were made"), *Step, EditCount, Made.Num()));
        return true;
    }

    // Moving away and back: every edit survives the unload with its state and stamp, and the columns return with
    // the blocks and meshes they had before.
    bool CheckEditSurvival(FEditReport& Report, FEditRun& Run, const FVoxelBlockTable& Table,
        const TArray<FEditMade>& Made, const TArray<FIntPoint>& InView)
    {
        TMap<FIntPoint, FEditHashes> Before;
        for (const FIntPoint Column : InView)
            Before.Add(Column, GetEditHashes(Run, Column));
        if (!Run.MoveTo(FIntPoint(40, 0), TEXT("move to (40, 0)"))) return false;
        for (const FEditMade& Edit : Made)
        {
            uint16 State = 0;
            int64 Stamp = 0;
            if (Run.Pipeline->GetCell(Edit.Cell, State, Stamp))
                Report.Fail(FString::Printf(
                    TEXT("survival: cell %s of edit '%s' is read through GetCell while its column is unloaded"),
                    *GetEditCellText(Edit.Cell), *Edit.Label));
        }
        const int32 KeptEdits = Run.Pipeline->GetStats().Edits;
        if (KeptEdits != Made.Num())
            Report.Fail(FString::Printf(
                TEXT("survival: the statistics count %d edited cells while their columns are unloaded, %d were made"),
                KeptEdits, Made.Num()));

        // The screen forgets the unloaded columns, so the second visit is compared with fresh meshes.
        Run.Driver.Screen.Reset();
        if (!Run.MoveTo(FIntPoint(0, 0), TEXT("move back to (0, 0)"))) return false;
        const FString Step = TEXT("survival: after the reload");
        CheckEditCells(Report, *Run.Pipeline, Made, Step);
        for (const FIntPoint Column : InView)
            CompareEditHashes(Report, Step, Column, TEXT("before the unload"), Before.FindChecked(Column),
                TEXT("after the reload"), GetEditHashes(Run, Column));
        CheckEditMeshesCurrent(Report, Run, Table, Step);
        return true;
    }

    // DropEdits: the edited cells read as the terrain generated them (a fresh pipeline never edited), their columns
    // and the neighbours that read them are generated and meshed again, and no edit is left.
    bool CheckEditDrop(FEditReport& Report, const FEditContext& Context, FEditRun& Run, const TArray<FEditMade>& Made,
        const TArray<FIntPoint>& InView)
    {
        FEditRun Fresh(Report, Context, TEXT("fresh"));
        if (!Fresh.MoveTo(FIntPoint(0, 0), TEXT("stream a fresh pipeline"))) return false;
        Run.Pipeline->DropEdits();
        if (!Run.Driver.RunUntilIdle(TEXT("after DropEdits"))) return false;
        const FString Step = TEXT("drop: after DropEdits");
        const int32 EditsLeft = Run.Pipeline->GetStats().Edits;
        if (EditsLeft != 0)
            Report.Fail(FString::Printf(TEXT("%s: the statistics still count %d edited cells"), *Step, EditsLeft));
        for (const FEditMade& Edit : Made)
        {
            uint16 State = 0, GeneratedState = 0;
            int64 Stamp = -1, GeneratedStamp = -1;
            if (!Run.Pipeline->GetCell(Edit.Cell, State, Stamp) ||
                !Fresh.Pipeline->GetCell(Edit.Cell, GeneratedState, GeneratedStamp))
                Report.Fail(FString::Printf(TEXT("%s: GetCell is false for cell %s of edit '%s'"), *Step,
                    *GetEditCellText(Edit.Cell), *Edit.Label));
            else if (State != GeneratedState || Stamp != 0 || GeneratedStamp != 0)
                Report.Fail(FString::Printf(
                    TEXT("%s: cell %s of edit '%s' reads state %d, stamp %lld; the terrain gives state %d, stamp 0"),
                    *Step, *GetEditCellText(Edit.Cell), *Edit.Label, int32(State), Stamp, int32(GeneratedState)));
        }
        for (const FIntPoint Column : InView)
        {
            const FEditHashes Kept = GetEditHashes(Run, Column);
            const FEditHashes Generated = GetEditHashes(Fresh, Column);
            CompareEditHashes(
                Report, Step, Column, TEXT("the dropped pipeline"), Kept, TEXT("a fresh pipeline"), Generated);
            if (Kept.bBlocks && Generated.bBlocks && Kept.Blocks != Generated.Blocks)
                ReportEditCellDifference(Report, Step, Column, TEXT("the dropped pipeline"),
                    *Run.Pipeline->FindColumn(Column), TEXT("a fresh pipeline"), *Fresh.Pipeline->FindColumn(Column));
        }
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelEditRemeshTest, "Unnamed.Voxel.Edit.Remesh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelEditRemeshTest::RunTest(const FString& Parameters)
{
    if (!FFoundationBlockRegistry::GetDefault()->GetAssets())
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return true;
    }
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    const auto Seed = FVoxelSeed::Parse(TEXT("Unnamed")).Value;
    const TSharedRef<const IVoxelTerrain> Terrain = MakeShared<FVoxelHeightTerrain>(Seed, *Table);
    const FEditContext Context(Table, Terrain);
    FEditReport Report(*this);

    const int32 StoneId = int32(Table->FindState(TEXT("stone")));
    if (StoneId <= 0 || StoneId >= int32(Table->GetStateCount()))
    {
        AddError(TEXT("The block table has no stone state"));
        return false;
    }

    // Stream the center, and start from meshes that match the data.
    FEditRun Run(Report, Context, TEXT("edit"));
    if (!Run.MoveTo(FIntPoint(0, 0), TEXT("stream (0, 0)"))) return false;
    TArray<FIntPoint> InView;
    GetEditView(Run.Pipeline->GetRings(), InView);
    CheckEditMeshesCurrent(Report, Run, *Table, TEXT("baseline"));

    // Ten edits: five kinds of cell, each placed and mined.
    TArray<FEditPlan> Plans;
    GetEditPlans(Plans);
    TArray<FEditMade> Made;
    for (const FEditPlan& Plan : Plans)
        if (!ApplyEdit(Report, Run, *Table, Plan, static_cast<uint16>(StoneId), Made)) return false;

    if (!CheckEditSurvival(Report, Run, *Table, Made, InView)) return false;
    if (!CheckEditDrop(Report, Context, Run, Made, InView)) return false;
    return true;
}

namespace
{
    // FLab of FoundationInteractionTests.cpp over the streamed columns instead of a lab floor: the project's rules and
    // the default registry, a stone item in the first slot with an infinite supply, and one player.
    struct FEditLab
    {
        FFoundationAuthority Authority;
        FFoundationVitals Vitals;
        int32 Player = 0;
        uint32 Sequence = 1;
        int64 StepIndex = 0;

        // The block world's storage is the streamed columns of Pipeline, with bounds that cover the whole world.
        explicit FEditLab(FVoxelColumnPipeline* Pipeline)
        {
            const UFoundationInteractionSettings& Settings = UFoundationInteractionSettings::Get();
            Settings.BuildRules(Authority.Rules);
            FFoundationItemDefinition Item;
            Item.Id = TEXT("edit_stone");
            Item.DisplayName = TEXT("Edit Stone");
            Item.Tags = FGameplayTagContainer(FoundationTags::ItemBlock);
            Item.MaxStack = 64;
            Item.PlacesBlock = TEXT("stone");
            Item.Icon = TEXT("block");
            Authority.Rules.Items.Add(Item);
            Authority.Rules.Loadout[0] = TEXT("edit_stone");
            Authority.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            Authority.World.BoundsMin = FIntVector(-30000000, -30000000, VoxelConstants::MinZ);
            Authority.World.BoundsMax = FIntVector(30000000, 30000000, VoxelConstants::MaxZ);
            Authority.Options.bInfiniteSupply = true;
            Authority.Options.bToolWear = false;
            Player = Authority.AddPlayer();
            Authority.Reset();
            Vitals.bNaturalRegeneration = false;
            Authority.World.SetStorage(MakeUnique<FVoxelBlockStorage>([Pipeline]() { return Pipeline; }));
            GetPlayer().Inventory.Select(0);
        }

        FFoundationPlayer& GetPlayer() { return *Authority.FindPlayer(Player); }

        // Standing facts with the gameplay eye 162 cm above the feet.
        void Stand(const FVector& Feet, const FVector& Aim)
        {
            FFoundationPlayerFacts Facts;
            Facts.FeetCm = Feet;
            Facts.HeightCm = 180;
            Facts.EyeCm = Feet + FVector(0, 0, 162);
            Facts.Aim = Aim.GetSafeNormal();
            Facts.bGrounded = true;
            Facts.Step = ++StepIndex;
            Authority.SetFacts(Player, Facts);
        }

        void NextStep()
        {
            FFoundationPlayerFacts Facts = GetPlayer().Facts;
            Facts.Step = ++StepIndex;
            Authority.SetFacts(Player, Facts);
            Authority.Step();
        }

        // A command for the main hand at what the player aims at, with the destination the client would send.
        FFoundationCommand Make(EFoundationCommandType Type)
        {
            FFoundationCommand Command;
            Command.SessionId = Authority.GetSessionId();
            Command.Type = Type;
            Command.Hand = EFoundationHand::Main;
            Command.Item = GetPlayer().Inventory.GetHand(EFoundationHand::Main).GetRef();
            Command.Target = Authority.Resolve(GetPlayer().Facts);
            FIntVector Destination;
            EFoundationReason Ignored{};
            if (Authority.GetDestination(Command.Target, Destination, Ignored) && Authority.World.IsKnown(Destination))
            {
                Command.Destination = Destination;
                Command.DestinationRevision = Authority.World.Get(Destination).Revision;
            }
            return Command;
        }

        FFoundationResult Execute(FFoundationCommand Command)
        {
            if (Command.Sequence == 0) Command.Sequence = Sequence++;
            return Authority.Execute(Player, Command, Vitals);
        }

        // Mines the targeted block through the command path: start, then continue until it breaks. The steps taken;
        // -1 when a continue was refused first, -2 when it did not break within Limit steps.
        int32 MineTargeted(int32 Limit)
        {
            FFoundationResult Result = Execute(Make(EFoundationCommandType::MineStart));
            if (Result.bBroken) return 0;
            for (int32 Step = 1; Step <= Limit; ++Step)
            {
                Result = Execute(Make(EFoundationCommandType::MineContinue));
                if (Result.bBroken) return Step;
                if (!Result.IsCommitted()) return -1;
                NextStep();
            }
            return -2;
        }
    };

    // Stands with the feet clear above the cell and the eye 264 cm over its top face, looking straight down at it.
    void StandEditAbove(FEditLab& Lab, const FIntVector& Cell)
    {
        Lab.Stand(FVector(Cell.X * 100.0 + 50.0, Cell.Y * 100.0 + 50.0, (Cell.Z + 2) * 100.0 + 2.0), FVector(0, 0, -1));
    }

    // Stands east of the cell with the eye level with its middle, looking west at its +X face.
    void StandEditEast(FEditLab& Lab, const FIntVector& Cell)
    {
        const FVector Eye((Cell.X + 1) * 100.0 + 150.0, Cell.Y * 100.0 + 50.0, Cell.Z * 100.0 + 50.0);
        Lab.Stand(Eye - FVector(0, 0, 162), FVector(-1, 0, 0));
    }

    // The first surface block of a column that is a solid full cube, so a ray from above or the side targets it. With
    // RequiredLocalX not negative, only that local X is searched. With bBelowSurface, the highest such block within 64
    // cells under the surface counts too (a sea floor or ground under plants): enough for a ray from the side.
    bool FindEditSolidSurface(const FVoxelColumnPipeline& Pipeline, const FVoxelBlockTable& Table, FIntPoint Column,
        int32 RequiredLocalX, bool bBelowSurface, FIntVector& OutCell)
    {
        const FVoxelColumn* Data = Pipeline.FindColumn(Column);
        if (!Data) return false;
        for (int32 Index = 0; Index < FVoxelColumn::Area; ++Index)
        {
            const int32 LocalX = Index % EditColumnWidth, LocalY = Index / EditColumnWidth;
            if (RequiredLocalX >= 0 && LocalX != RequiredLocalX) continue;
            const int32 Surface = Data->GetHeight(EVoxelHeightmap::WorldSurface, LocalX, LocalY);
            if (Surface < VoxelConstants::MinZ || Surface > VoxelConstants::MaxZ - 4) continue;
            const int32 Lowest = bBelowSurface ? FMath::Max(VoxelConstants::MinZ, Surface - 64) : Surface;
            for (int32 Z = Surface; Z >= Lowest; --Z)
            {
                const FVoxelStateInfo& Info = Table.Get(Data->GetState(LocalX, LocalY, Z));
                if (Info.Shape != EVoxelShape::Full || !Info.bSolid || Info.IsFluid()) continue;
                OutCell = FIntVector(Column.X * EditColumnWidth + LocalX, Column.Y * EditColumnWidth + LocalY, Z);
                return true;
            }
        }
        return false;
    }

    // What a refused command must leave alone: the block world's revision and change log, and the pipeline's edits.
    struct FEditWorldMark
    {
        int64 Revision = 0;
        int32 Changes = 0;
        int32 Edits = 0;
    };

    FEditWorldMark MarkEditWorld(const FEditLab& Lab, const FVoxelColumnPipeline& Pipeline)
    {
        FEditWorldMark Mark;
        Mark.Revision = Lab.Authority.World.Revision;
        Mark.Changes = Lab.Authority.World.GetPendingChangeCount();
        Mark.Edits = Pipeline.GetStats().Edits;
        return Mark;
    }

    void CheckEditUnchanged(FEditReport& Report, const FString& Step, const FEditWorldMark& Before, const FEditLab& Lab,
        const FVoxelColumnPipeline& Pipeline)
    {
        const FEditWorldMark After = MarkEditWorld(Lab, Pipeline);
        if (After.Revision != Before.Revision || After.Changes != Before.Changes || After.Edits != Before.Edits)
            Report.Fail(FString::Printf(
                TEXT("%s: a refused command changed the world: revision %lld to %lld, %d to %d logged changes, "
                     "%d to %d edited cells"),
                *Step, Before.Revision, After.Revision, Before.Changes, After.Changes, Before.Edits, After.Edits));
    }

    // IsKnown follows the generated columns: true inside one, false 100 columns away, and around the center true
    // exactly for the generated columns, at their corners and middle (negative coordinates included).
    void CheckEditKnown(FEditReport& Report, const FEditLab& Lab, const FVoxelColumnPipeline& Pipeline)
    {
        const FFoundationBlockWorld& Blocks = Lab.Authority.World;
        const FIntVector Loaded(8, 8, 70), Far(100 * EditColumnWidth + 8, 8, 70);
        if (!Blocks.IsKnown(Loaded))
            Report.Fail(FString::Printf(
                TEXT("authority: IsKnown(%s) is false for a cell in a generated column"), *GetEditCellText(Loaded)));
        if (Blocks.IsKnown(Far))
            Report.Fail(FString::Printf(
                TEXT("authority: IsKnown(%s) is true for a cell 100 columns away"), *GetEditCellText(Far)));
        static const FIntPoint Locals[5] = {
            FIntPoint(0, 0), FIntPoint(15, 0), FIntPoint(0, 15), FIntPoint(15, 15), FIntPoint(8, 8)};
        for (int32 DY = -EditScanExtent; DY <= EditScanExtent; ++DY)
            for (int32 DX = -EditScanExtent; DX <= EditScanExtent; ++DX)
            {
                const FIntPoint Column(DX, DY);
                const bool bGenerated = Pipeline.IsGenerated(Column);
                for (const FIntPoint& Local : Locals)
                {
                    const FIntVector Cell(
                        Column.X * EditColumnWidth + Local.X, Column.Y * EditColumnWidth + Local.Y, 70);
                    if (Blocks.IsKnown(Cell) != bGenerated)
                        Report.Fail(FString::Printf(TEXT("authority: IsKnown(%s) is %d, but column (%d, %d) is %s"),
                            *GetEditCellText(Cell), int32(!bGenerated), Column.X, Column.Y,
                            bGenerated ? TEXT("generated") : TEXT("not generated")));
                }
            }
    }

    // The column of the change meshes again, and every mesh on screen matches the data.
    bool CheckEditRemeshed(
        FEditReport& Report, FEditRun& Run, const FVoxelBlockTable& Table, const FIntVector& Cell, const FString& Step)
    {
        if (!Run.Driver.RunUntilIdle(Step)) return false;
        const FIntPoint Column = GetEditColumn(Cell);
        bool bRemeshed = false;
        for (const FEditTake& Take : Run.Driver.Taken)
            bRemeshed = bRemeshed || Take.Column == Column;
        if (!bRemeshed)
            Report.Fail(FString::Printf(TEXT("%s: column (%d, %d) was not meshed again after the change at %s"), *Step,
                Column.X, Column.Y, *GetEditCellText(Cell)));
        CheckEditMeshesCurrent(Report, Run, Table, Step);
        return true;
    }

    // A UseOn aimed down at a loaded surface block places stone in the cell above it: World.Get and the pipeline read
    // it, the cell's revision is the pipeline's stamp, and the column meshes again.
    bool CheckEditPlace(FEditReport& Report, FEditRun& Run, FEditLab& Lab, const FVoxelBlockTable& Table, uint16 Stone)
    {
        FVoxelColumnPipeline& Pipeline = *Run.Pipeline;
        FFoundationBlockWorld& Blocks = Lab.Authority.World;
        FIntVector Surface = FIntVector::ZeroValue;
        if (!FindEditSolidSurface(Pipeline, Table, FIntPoint(0, 0), INDEX_NONE, false, Surface))
        {
            Report.Fail(TEXT("authority, place: column (0, 0) has no solid full-cube surface block to aim at"));
            return false;
        }
        const FIntVector Above(Surface.X, Surface.Y, Surface.Z + 1);
        const FString Step = FString::Printf(TEXT("authority, place on %s"), *GetEditCellText(Surface));
        if (!Blocks.Get(Above).State.IsAir())
        {
            Report.Fail(FString::Printf(
                TEXT("%s: the cell above the surface block, %s, is not air"), *Step, *GetEditCellText(Above)));
            return false;
        }
        StandEditAbove(Lab, Surface);
        const int64 RevisionBefore = Blocks.Revision;
        Run.Driver.Taken.Reset();
        if (!Lab.Execute(Lab.Make(EFoundationCommandType::UseOn)).IsCommitted())
        {
            Report.Fail(FString::Printf(TEXT("%s: UseOn aimed down at the block was refused"), *Step));
            return false;
        }

        FFoundationCell After = Blocks.Get(Above);
        const FFoundationBlockType* StoneBlock = Blocks.FindBlock(TEXT("stone"));
        const FFoundationBlockType* PlacedBlock = After.State.IsAir() ? nullptr : &Blocks.GetBlock(After.State);
        if (!StoneBlock || PlacedBlock != StoneBlock)
            Report.Fail(FString::Printf(
                TEXT("%s: World.Get(%s) does not hold stone after the placement"), *Step, *GetEditCellText(Above)));
        if (After.Revision <= RevisionBefore || After.Revision > Blocks.Revision)
            Report.Fail(FString::Printf(TEXT("%s: the placed cell has revision %lld; the world was at revision %lld "
                                             "before and is at %lld"),
                *Step, After.Revision, RevisionBefore, Blocks.Revision));
        uint16 State = 0;
        int64 Stamp = 0;
        if (!Pipeline.GetCell(Above, State, Stamp))
            Report.Fail(FString::Printf(
                TEXT("%s: the pipeline has no column for the placed cell %s"), *Step, *GetEditCellText(Above)));
        else
        {
            if (State != Stone)
                Report.Fail(FString::Printf(TEXT("%s: the pipeline reads state %d at %s, expected stone (state %d)"),
                    *Step, int32(State), *GetEditCellText(Above), int32(Stone)));
            if (Stamp != After.Revision)
                Report.Fail(FString::Printf(TEXT("%s: the pipeline's stamp at %s is %lld, the cell's revision is %lld"),
                    *Step, *GetEditCellText(Above), Stamp, After.Revision));
        }
        return CheckEditRemeshed(Report, Run, Table, Above, Step);
    }

    // MineStart then MineContinue until the surface block of another column breaks: World.Get and the pipeline read
    // air, with the break's revision as the stamp, and the column meshes again.
    bool CheckEditMine(FEditReport& Report, FEditRun& Run, FEditLab& Lab, const FVoxelBlockTable& Table)
    {
        FVoxelColumnPipeline& Pipeline = *Run.Pipeline;
        FFoundationBlockWorld& Blocks = Lab.Authority.World;
        FIntVector Target = FIntVector::ZeroValue;
        if (!FindEditSolidSurface(Pipeline, Table, FIntPoint(-1, 0), INDEX_NONE, false, Target))
        {
            Report.Fail(TEXT("authority, mine: column (-1, 0) has no solid full-cube surface block to aim at"));
            return false;
        }
        const FString Step = FString::Printf(TEXT("authority, mine %s"), *GetEditCellText(Target));
        StandEditAbove(Lab, Target);
        const int64 RevisionBefore = Blocks.Revision;
        Run.Driver.Taken.Reset();
        const int32 Steps = Lab.MineTargeted(4000);
        if (Steps < 0)
        {
            Report.Fail(FString::Printf(TEXT("%s: the block did not break: %s"), *Step,
                Steps == -1 ? TEXT("a MineContinue was refused first") : TEXT("4000 steps passed")));
            return false;
        }

        FFoundationCell After = Blocks.Get(Target);
        if (!After.State.IsAir())
            Report.Fail(FString::Printf(
                TEXT("%s: World.Get(%s) still holds a block after it broke"), *Step, *GetEditCellText(Target)));
        if (After.Revision <= RevisionBefore)
            Report.Fail(FString::Printf(TEXT("%s: the broken cell has revision %lld; the world was at revision %lld"),
                *Step, After.Revision, RevisionBefore));
        uint16 State = 1;
        int64 Stamp = 0;
        if (!Pipeline.GetCell(Target, State, Stamp))
            Report.Fail(FString::Printf(
                TEXT("%s: the pipeline has no column for the broken cell %s"), *Step, *GetEditCellText(Target)));
        else
        {
            if (State != 0)
                Report.Fail(FString::Printf(TEXT("%s: the pipeline reads state %d at %s after the break, expected air"),
                    *Step, int32(State), *GetEditCellText(Target)));
            if (Stamp != After.Revision)
                Report.Fail(FString::Printf(TEXT("%s: the pipeline's stamp at %s is %lld, the cell's revision is %lld"),
                    *Step, *GetEditCellText(Target), Stamp, After.Revision));
        }
        return CheckEditRemeshed(Report, Run, Table, Target, Step);
    }

    // Aiming at a cell of a column 100 columns away: nothing is loaded there to hit, every command is refused, and
    // nothing changes.
    void CheckEditRefusedFar(FEditReport& Report, FEditRun& Run, FEditLab& Lab)
    {
        FVoxelColumnPipeline& Pipeline = *Run.Pipeline;
        const FFoundationBlockWorld& Blocks = Lab.Authority.World;
        const FIntVector Far(100 * EditColumnWidth + 8, 8, 70);
        const FString Step = FString::Printf(TEXT("authority, aim at the unloaded cell %s"), *GetEditCellText(Far));
        StandEditAbove(Lab, Far);
        const FEditWorldMark Mark = MarkEditWorld(Lab, Pipeline);
        if (Lab.Execute(Lab.Make(EFoundationCommandType::UseOn)).IsCommitted())
            Report.Fail(FString::Printf(TEXT("%s: UseOn was committed"), *Step));
        if (Lab.Execute(Lab.Make(EFoundationCommandType::MineStart)).bBroken)
            Report.Fail(FString::Printf(TEXT("%s: MineStart broke a block"), *Step));
        for (int32 Index = 1; Index <= 5; ++Index)
        {
            FFoundationResult Mined = Lab.Execute(Lab.Make(EFoundationCommandType::MineContinue));
            if (Mined.IsCommitted() || Mined.bBroken)
                Report.Fail(FString::Printf(TEXT("%s: MineContinue %d was committed or broke a block"), *Step, Index));
            Lab.NextStep();
        }
        CheckEditUnchanged(Report, Step, Mark, Lab, Pipeline);
        uint16 State = 0;
        int64 Stamp = 0;
        if (Pipeline.GetCell(Far, State, Stamp) || Blocks.IsKnown(Far) || !Blocks.Get(Far).State.IsAir())
            Report.Fail(FString::Printf(TEXT("%s: the cell is loaded, known or not air"), *Step));
    }

    // Placing against the last loaded block of a row so the new block would lie in an unloaded column: refused, and
    // nothing changes. Column (4, 0) is the last generated one along +X; column (5, 0) is not.
    void CheckEditRefusedEdge(FEditReport& Report, FEditRun& Run, FEditLab& Lab, const FVoxelBlockTable& Table)
    {
        FVoxelColumnPipeline& Pipeline = *Run.Pipeline;
        const FFoundationBlockWorld& Blocks = Lab.Authority.World;
        FIntVector Edge = FIntVector::ZeroValue;
        if (!Pipeline.IsGenerated(FIntPoint(4, 0)) || Pipeline.IsGenerated(FIntPoint(5, 0)) ||
            !FindEditSolidSurface(Pipeline, Table, FIntPoint(4, 0), EditColumnWidth - 1, true, Edge))
        {
            Report.Fail(TEXT("authority, edge: needs column (4, 0) generated, column (5, 0) not, and a solid block "
                             "near the surface at the last X of column (4, 0)"));
            return;
        }
        const FIntVector Beyond(Edge.X + 1, Edge.Y, Edge.Z);
        const FString Step = FString::Printf(
            TEXT("authority, place east of %s into the unloaded %s"), *GetEditCellText(Edge), *GetEditCellText(Beyond));
        StandEditEast(Lab, Edge);
        uint16 EdgeState = 0;
        int64 EdgeStamp = 0;
        Pipeline.GetCell(Edge, EdgeState, EdgeStamp);
        const FEditWorldMark Mark = MarkEditWorld(Lab, Pipeline);
        if (Blocks.IsKnown(Beyond))
            Report.Fail(FString::Printf(TEXT("%s: IsKnown is true for the cell of an unloaded column"), *Step));
        if (Lab.Execute(Lab.Make(EFoundationCommandType::UseOn)).IsCommitted())
            Report.Fail(FString::Printf(TEXT("%s: UseOn was committed"), *Step));
        CheckEditUnchanged(Report, Step, Mark, Lab, Pipeline);
        uint16 State = 0;
        int64 Stamp = 0;
        if (!Pipeline.GetCell(Edge, State, Stamp) || State != EdgeState || Stamp != EdgeStamp)
            Report.Fail(FString::Printf(TEXT("%s: the block at %s changed"), *Step, *GetEditCellText(Edge)));
        if (Pipeline.GetCell(Beyond, State, Stamp) || !Blocks.Get(Beyond).State.IsAir())
            Report.Fail(FString::Printf(TEXT("%s: the unloaded cell is loaded or not air"), *Step));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelEditAuthorityTest, "Unnamed.Voxel.Edit.Authority",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelEditAuthorityTest::RunTest(const FString& Parameters)
{
    if (!FFoundationBlockRegistry::GetDefault()->GetAssets())
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return true;
    }
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    const auto Seed = FVoxelSeed::Parse(TEXT("Unnamed")).Value;
    const TSharedRef<const IVoxelTerrain> Terrain = MakeShared<FVoxelHeightTerrain>(Seed, *Table);
    const FEditContext Context(Table, Terrain);
    FEditReport Report(*this);

    const int32 StoneId = int32(Table->FindState(TEXT("stone")));
    if (StoneId <= 0 || StoneId >= int32(Table->GetStateCount()))
    {
        AddError(TEXT("The block table has no stone state"));
        return false;
    }

    // The pipeline streams the center, then the block world reads and writes it through FVoxelBlockStorage. The lab
    // is declared after the run so it goes first.
    FEditRun Run(Report, Context, TEXT("authority"));
    if (!Run.MoveTo(FIntPoint(0, 0), TEXT("stream (0, 0)"))) return false;
    FEditLab Lab(Run.Pipeline.Get());

    CheckEditKnown(Report, Lab, *Run.Pipeline);
    if (!CheckEditPlace(Report, Run, Lab, *Table, static_cast<uint16>(StoneId))) return false;
    if (!CheckEditMine(Report, Run, Lab, *Table)) return false;
    CheckEditRefusedFar(Report, Run, Lab);
    CheckEditRefusedEdge(Report, Run, Lab, *Table);
    return true;
}
#endif
