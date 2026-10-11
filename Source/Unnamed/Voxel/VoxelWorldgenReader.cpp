#include "Voxel/VoxelWorldgenReader.h"

#include "Biome/VoxelBiomeTable.h"
#include "Density/VoxelDensityGraph.h"
#include "Voxel/VoxelWorldgenJson.h"

namespace
{
    using VoxelWorldgenJson::HasOnlyWorldgenKeys;
    using VoxelWorldgenJson::IsWorldgenKey;
    using VoxelWorldgenJson::LoadWorldgenFile;
    using VoxelWorldgenJson::WorldgenIdPrefix;
    using VoxelWorldgenJson::WorldgenIdToPath;
    const TCHAR* const WorldgenAxes[] = {TEXT("x"), TEXT("y"), TEXT("z")};

    // The reference's field names for an op's inputs and params, in slot order, and the fields read by hand.
    struct FWorldgenOpFields
    {
        const TCHAR* Inputs[3] = {};
        const TCHAR* Params[5] = {};
        const TCHAR* Other[3] = {};
    };

    FWorldgenOpFields GetWorldgenOpFields(EVoxelDensityOp Op)
    {
        switch (Op)
        {
        case EVoxelDensityOp::Add:
        case EVoxelDensityOp::Sub:
        case EVoxelDensityOp::Mul:
        case EVoxelDensityOp::Div:
        case EVoxelDensityOp::Min:
        case EVoxelDensityOp::Max:
            return {{TEXT("left"), TEXT("right")}};
        case EVoxelDensityOp::Abs:
        case EVoxelDensityOp::Negate:
        case EVoxelDensityOp::Square:
        case EVoxelDensityOp::Cube:
        case EVoxelDensityOp::HalfNegative:
        case EVoxelDensityOp::QuarterNegative:
        case EVoxelDensityOp::Squeeze:
        case EVoxelDensityOp::Cache:
        case EVoxelDensityOp::BlendDensity:
            return {{TEXT("input")}};
        case EVoxelDensityOp::Clamp:
            return {{TEXT("input")}, {TEXT("min"), TEXT("max")}};
        case EVoxelDensityOp::Gradient:
            return {{}, {TEXT("from_coordinate"), TEXT("from_value"), TEXT("to_coordinate"), TEXT("to_value")},
                {TEXT("axis")}};
        case EVoxelDensityOp::Lerp:
            return {{TEXT("alpha"), TEXT("first"), TEXT("second")}};
        case EVoxelDensityOp::RangeChoice:
            return {{TEXT("input"), TEXT("when_in_range"), TEXT("when_out_of_range")},
                {TEXT("min_inclusive"), TEXT("max_exclusive")}};
        case EVoxelDensityOp::IntervalSelect:
            return {{TEXT("input")}, {}, {TEXT("functions"), TEXT("thresholds")}};
        case EVoxelDensityOp::Noise:
            return {{}, {TEXT("xz_scale"), TEXT("y_scale")}, {TEXT("noise"), TEXT("shift_x"), TEXT("shift_z")}};
        case EVoxelDensityOp::ShiftA:
        case EVoxelDensityOp::ShiftB:
            return {{}, {}, {TEXT("noise")}};
        case EVoxelDensityOp::OldBlendedNoise:
            return {{},
                {TEXT("xz_scale"), TEXT("y_scale"), TEXT("xz_factor"), TEXT("y_factor"),
                    TEXT("smear_scale_multiplier")}};
        case EVoxelDensityOp::Spline:
            return {{}, {}, {TEXT("spline")}};
        case EVoxelDensityOp::Interpolated:
            return {{TEXT("input")}, {TEXT("cell_size_xz"), TEXT("cell_size_y")}};
        case EVoxelDensityOp::FindTopSurface:
            return {{TEXT("density"), TEXT("upper_bound")}, {TEXT("lower_bound"), TEXT("cell_height")}};
        default:
            return {};
        }
    }

    bool HasWorldgenField(const FWorldgenOpFields& Fields, const FString& Key)
    {
        for (const TCHAR* Field : Fields.Inputs)
        {
            if (Field && IsWorldgenKey(Key, Field))
            {
                return true;
            }
        }
        for (const TCHAR* Field : Fields.Params)
        {
            if (Field && IsWorldgenKey(Key, Field))
            {
                return true;
            }
        }
        for (const TCHAR* Field : Fields.Other)
        {
            if (Field && IsWorldgenKey(Key, Field))
            {
                return true;
            }
        }
        return false;
    }

    struct FWorldgenEntry
    {
        const TCHAR* Key;
        int32* Node;
    };

    // Reads files into a graph. Children are read before their node is added, so every node lands after its inputs.
    class FWorldgenParser
    {
    public:
        FWorldgenParser(VoxelWorldgen::FLoadFile InLoadFile, FVoxelDensityGraph& InGraph)
            : LoadFile(InLoadFile), Graph(InGraph)
        {
        }

        // The first failure, naming the file and the field.
        FString Error;
        // The file being read.
        FString File = TEXT("(request)");

        bool Fail(const FString& Path, const FString& Message)
        {
            if (Error.IsEmpty())
            {
                Error = Path.IsEmpty() ? FString::Printf(TEXT("%s: %s"), *File, *Message)
                                       : FString::Printf(TEXT("%s at %s: %s"), *File, *Path, *Message);
            }
            return false;
        }

        // A file's one JSON value. The text is wrapped in an array, because the engine's reader wants an object or an
        // array at the top and a density function file may be a bare number.
        TSharedPtr<FJsonValue> Load(const FString& Path)
        {
            File = Path;
            FString Text;
            if (!LoadFile(Path, Text))
            {
                Fail(TEXT(""), TEXT("cannot read the file"));
                return nullptr;
            }
            TArray<TSharedPtr<FJsonValue>> Values;
            if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("[") + Text + TEXT("]")), Values) ||
                Values.Num() != 1 || !Values[0].IsValid())
            {
                Fail(TEXT(""), TEXT("is not one JSON value"));
                return nullptr;
            }
            return Values[0];
        }

        int32 ReadFunction(const FString& Id)
        {
            if (const int32 Found = Graph.FindFunction(Id); Found != INDEX_NONE)
            {
                return Found;
            }
            FString Path;
            if (!WorldgenIdToPath(Id, TEXT("density_function"), Path))
            {
                Fail(TEXT(""), FString::Printf(TEXT("%s is not a density function id"), *Id));
                return INDEX_NONE;
            }
            if (Reading.Contains(Id))
            {
                Fail(TEXT(""),
                    FString::Printf(
                        TEXT("%s references itself: %s -> %s"), *Id, *FString::Join(Reading, TEXT(" -> ")), *Id));
                return INDEX_NONE;
            }
            const FString Outer = File;
            const TSharedPtr<FJsonValue> Value = Load(Path);
            if (!Value)
            {
                return INDEX_NONE;
            }
            Reading.Add(Id);
            const int32 Node = ReadValue(Value, TEXT(""));
            Reading.Pop();
            if (Node != INDEX_NONE)
            {
                Graph.Functions.Add({Id, Node});
                File = Outer;
            }
            return Node;
        }

        int32 ReadNoise(const FString& Id)
        {
            if (const int32 Found = Graph.FindNoise(Id); Found != INDEX_NONE)
            {
                return Found;
            }
            FString Path;
            if (!WorldgenIdToPath(Id, TEXT("noise"), Path))
            {
                Fail(TEXT(""), FString::Printf(TEXT("%s is not a noise id"), *Id));
                return INDEX_NONE;
            }
            const FString Outer = File;
            const TSharedPtr<FJsonValue> Value = Load(Path);
            const TSharedPtr<FJsonObject>* Object = nullptr;
            if (!Value || !Value->TryGetObject(Object))
            {
                Fail(TEXT(""), TEXT("a noise must be an object"));
                return INDEX_NONE;
            }
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*Object)->Values)
            {
                if (!IsWorldgenKey(Field.Key, TEXT("base_octave")) &&
                    !IsWorldgenKey(Field.Key, TEXT("base_amplitude")) &&
                    !IsWorldgenKey(Field.Key, TEXT("octave_count")) &&
                    !IsWorldgenKey(Field.Key, TEXT("amplitude_modifiers")))
                {
                    Fail(TEXT(""), FString::Printf(TEXT("unexpected field %s"), *Field.Key));
                    return INDEX_NONE;
                }
            }

            FVoxelDensityNoise Noise;
            Noise.Name = Id;
            double BaseOctave = 0;
            double OctaveCount = 1;
            const bool bHasCount = (*Object)->HasField(TEXT("octave_count"));
            if (!ReadNumber(**Object, TEXT("base_octave"), TEXT(""), BaseOctave) ||
                !ReadNumber(**Object, TEXT("base_amplitude"), TEXT(""), Noise.BaseAmplitude) ||
                (bHasCount && !ReadNumber(**Object, TEXT("octave_count"), TEXT(""), OctaveCount)))
            {
                return INDEX_NONE;
            }
            if (BaseOctave != FMath::RoundToDouble(BaseOctave) || FMath::Abs(BaseOctave) > 64 ||
                OctaveCount != FMath::RoundToDouble(OctaveCount) || OctaveCount < 1 || OctaveCount > 64)
            {
                Fail(TEXT(""), TEXT("base_octave and octave_count must be small whole numbers"));
                return INDEX_NONE;
            }
            Noise.BaseOctave = int32(BaseOctave);

            if ((*Object)->HasField(TEXT("amplitude_modifiers")))
            {
                const TArray<TSharedPtr<FJsonValue>>* Modifiers = nullptr;
                if (!(*Object)->TryGetArrayField(TEXT("amplitude_modifiers"), Modifiers))
                {
                    Fail(TEXT(""), TEXT("amplitude_modifiers must be an array"));
                    return INDEX_NONE;
                }
                for (const TSharedPtr<FJsonValue>& Modifier : *Modifiers)
                {
                    if (Modifier->Type != EJson::Number)
                    {
                        Fail(TEXT(""), TEXT("amplitude_modifiers must be numbers"));
                        return INDEX_NONE;
                    }
                    Noise.AmplitudeModifiers.Add(Modifier->AsNumber());
                }
                if (Noise.AmplitudeModifiers.Num() != int32(OctaveCount))
                {
                    Fail(TEXT(""), TEXT("amplitude_modifiers must have octave_count entries (1 when it is left out)"));
                    return INDEX_NONE;
                }
            }
            else
            {
                Noise.AmplitudeModifiers.Init(1.0, int32(OctaveCount));
            }
            File = Outer;
            return Graph.Noises.Add(MoveTemp(Noise));
        }

        // A density function: a number, a function id, or a node object.
        int32 ReadValue(const TSharedPtr<FJsonValue>& Value, const FString& Path)
        {
            switch (Value->Type)
            {
            case EJson::Number:
            {
                FVoxelDensityNode Node;
                Node.Params[0] = Value->AsNumber();
                return Graph.Nodes.Add(Node);
            }
            case EJson::String:
                return ReadFunction(Value->AsString());
            case EJson::Object:
                return ReadNode(*Value->AsObject(), Path);
            default:
                Fail(Path, TEXT("expected a number, a function id or a node"));
                return INDEX_NONE;
            }
        }

        int32 ReadField(const FJsonObject& Object, const TCHAR* Key, const FString& Path)
        {
            const TSharedPtr<FJsonValue> Value = Object.TryGetField(Key);
            if (!Value)
            {
                Fail(Path, FString::Printf(TEXT("missing field %s"), Key));
                return INDEX_NONE;
            }
            return ReadValue(Value, Path + TEXT("/") + Key);
        }

        bool ReadNumber(const FJsonObject& Object, const TCHAR* Key, const FString& Path, double& Out)
        {
            const TSharedPtr<FJsonValue> Value = Object.TryGetField(Key);
            if (!Value || Value->Type != EJson::Number)
            {
                return Fail(Path, FString::Printf(TEXT("%s must be a number"), Key));
            }
            Out = Value->AsNumber();
            return true;
        }

        int32 ReadNode(const FJsonObject& Object, const FString& Path)
        {
            FString Type;
            if (!Object.TryGetStringField(TEXT("type"), Type))
            {
                Fail(Path, TEXT("a node needs a type"));
                return INDEX_NONE;
            }
            // Constants are bare numbers, so their op has no type to match.
            EVoxelDensityOp Op = EVoxelDensityOp::Count;
            for (int32 Index = int32(EVoxelDensityOp::Add); Index < int32(EVoxelDensityOp::Count); ++Index)
            {
                if (Type.Equals(FString(WorldgenIdPrefix) + FVoxelDensityGraph::GetOpName(EVoxelDensityOp(Index)),
                        ESearchCase::CaseSensitive))
                {
                    Op = EVoxelDensityOp(Index);
                    break;
                }
            }
            if (Op == EVoxelDensityOp::Count)
            {
                Fail(Path, FString::Printf(TEXT("unknown type %s"), *Type));
                return INDEX_NONE;
            }
            const FWorldgenOpFields Fields = GetWorldgenOpFields(Op);
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object.Values)
            {
                if (!IsWorldgenKey(Field.Key, TEXT("type")) && !HasWorldgenField(Fields, Field.Key))
                {
                    Fail(Path, FString::Printf(TEXT("unexpected field %s in %s"), *Field.Key, *Type));
                    return INDEX_NONE;
                }
            }

            FVoxelDensityNode Node;
            Node.Op = Op;
            for (int32 Slot = 0; Slot < UE_ARRAY_COUNT(Fields.Inputs) && Fields.Inputs[Slot]; ++Slot)
            {
                Node.Inputs[Slot] = ReadField(Object, Fields.Inputs[Slot], Path);
                if (Node.Inputs[Slot] == INDEX_NONE)
                {
                    return INDEX_NONE;
                }
            }
            for (int32 Slot = 0; Slot < UE_ARRAY_COUNT(Fields.Params) && Fields.Params[Slot]; ++Slot)
            {
                if (!ReadNumber(Object, Fields.Params[Slot], Path, Node.Params[Slot]))
                {
                    return INDEX_NONE;
                }
            }

            switch (Op)
            {
            case EVoxelDensityOp::Gradient:
            {
                FString Axis;
                Object.TryGetStringField(TEXT("axis"), Axis);
                const int32 AxisIndex = IsWorldgenKey(Axis, TEXT("x")) ? 0
                    : IsWorldgenKey(Axis, TEXT("y"))                   ? 1
                    : IsWorldgenKey(Axis, TEXT("z"))                   ? 2
                                                                       : INDEX_NONE;
                if (AxisIndex == INDEX_NONE)
                {
                    Fail(Path, TEXT("axis must be x, y or z"));
                    return INDEX_NONE;
                }
                Node.Axis = uint8(AxisIndex);
                break;
            }
            case EVoxelDensityOp::IntervalSelect:
                if (!ReadIntervals(Object, Path, Node))
                {
                    return INDEX_NONE;
                }
                break;
            case EVoxelDensityOp::Noise:
                for (int32 Slot = 0; Slot < 2; ++Slot)
                {
                    const TCHAR* Key = Slot == 0 ? TEXT("shift_x") : TEXT("shift_z");
                    if (Object.HasField(Key))
                    {
                        Node.Inputs[Slot] = ReadField(Object, Key, Path);
                        if (Node.Inputs[Slot] == INDEX_NONE)
                        {
                            return INDEX_NONE;
                        }
                    }
                }
                [[fallthrough]];
            case EVoxelDensityOp::ShiftA:
            case EVoxelDensityOp::ShiftB:
            {
                const TSharedPtr<FJsonValue> NoiseId = Object.TryGetField(TEXT("noise"));
                if (!NoiseId || NoiseId->Type != EJson::String)
                {
                    Fail(Path, TEXT("noise must be a noise id"));
                    return INDEX_NONE;
                }
                Node.Ref = ReadNoise(NoiseId->AsString());
                if (Node.Ref == INDEX_NONE)
                {
                    return INDEX_NONE;
                }
                break;
            }
            case EVoxelDensityOp::Spline:
            {
                const TSharedPtr<FJsonObject>* Spline = nullptr;
                if (!Object.TryGetObjectField(TEXT("spline"), Spline))
                {
                    Fail(Path, TEXT("spline must be an object"));
                    return INDEX_NONE;
                }
                Node.Ref = ReadSpline(**Spline, Path + TEXT("/spline"));
                if (Node.Ref == INDEX_NONE)
                {
                    return INDEX_NONE;
                }
                break;
            }
            default:
                break;
            }
            return Graph.Nodes.Add(Node);
        }

        bool ReadIntervals(const FJsonObject& Object, const FString& Path, FVoxelDensityNode& Node)
        {
            const TArray<TSharedPtr<FJsonValue>>* Functions = nullptr;
            const TArray<TSharedPtr<FJsonValue>>* Thresholds = nullptr;
            if (!Object.TryGetArrayField(TEXT("functions"), Functions) ||
                !Object.TryGetArrayField(TEXT("thresholds"), Thresholds) || Functions->IsEmpty() ||
                Thresholds->Num() != Functions->Num() - 1)
            {
                return Fail(Path, TEXT("interval_select needs n functions and n - 1 thresholds"));
            }
            TArray<int32, TInlineAllocator<8>> Nodes;
            for (int32 Index = 0; Index < Functions->Num(); ++Index)
            {
                const int32 Function =
                    ReadValue((*Functions)[Index], FString::Printf(TEXT("%s/functions/%d"), *Path, Index));
                if (Function == INDEX_NONE)
                {
                    return false;
                }
                Nodes.Add(Function);
            }
            for (const TSharedPtr<FJsonValue>& Threshold : *Thresholds)
            {
                if (Threshold->Type != EJson::Number)
                {
                    return Fail(Path, TEXT("thresholds must be numbers"));
                }
            }
            Node.Ref = Graph.ListNodes.Num();
            Node.ListCount = Nodes.Num();
            for (int32 Index = 0; Index < Nodes.Num(); ++Index)
            {
                Graph.ListNodes.Add(Nodes[Index]);
                Graph.ListValues.Add(Index < Thresholds->Num() ? (*Thresholds)[Index]->AsNumber() : 0.0);
            }
            return true;
        }

        // A spline object: a coordinate and points whose values are numbers or nested splines, read before it.
        int32 ReadSpline(const FJsonObject& Object, const FString& Path)
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object.Values)
            {
                if (!IsWorldgenKey(Field.Key, TEXT("coordinate")) && !IsWorldgenKey(Field.Key, TEXT("points")))
                {
                    Fail(Path, FString::Printf(TEXT("unexpected field %s in a spline"), *Field.Key));
                    return INDEX_NONE;
                }
            }
            FVoxelDensitySpline Spline;
            Spline.Coordinate = ReadField(Object, TEXT("coordinate"), Path);
            if (Spline.Coordinate == INDEX_NONE)
            {
                return INDEX_NONE;
            }
            const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
            if (!Object.TryGetArrayField(TEXT("points"), Points) || Points->IsEmpty())
            {
                Fail(Path, TEXT("a spline needs points"));
                return INDEX_NONE;
            }

            TArray<FVoxelDensitySplinePoint, TInlineAllocator<16>> Read;
            for (int32 Index = 0; Index < Points->Num(); ++Index)
            {
                const FString PointPath = FString::Printf(TEXT("%s/points/%d"), *Path, Index);
                const TSharedPtr<FJsonObject>* Point = nullptr;
                if (!(*Points)[Index]->TryGetObject(Point))
                {
                    Fail(PointPath, TEXT("a point must be an object"));
                    return INDEX_NONE;
                }
                for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*Point)->Values)
                {
                    if (!IsWorldgenKey(Field.Key, TEXT("location")) && !IsWorldgenKey(Field.Key, TEXT("value")) &&
                        !IsWorldgenKey(Field.Key, TEXT("derivative")))
                    {
                        Fail(PointPath, FString::Printf(TEXT("unexpected field %s in a point"), *Field.Key));
                        return INDEX_NONE;
                    }
                }
                FVoxelDensitySplinePoint Out;
                if (!ReadNumber(**Point, TEXT("location"), PointPath, Out.Location) ||
                    !ReadNumber(**Point, TEXT("derivative"), PointPath, Out.Derivative))
                {
                    return INDEX_NONE;
                }
                const TSharedPtr<FJsonValue> Value = (*Point)->TryGetField(TEXT("value"));
                if (Value && Value->Type == EJson::Number)
                {
                    Out.Value = Value->AsNumber();
                }
                else if (Value && Value->Type == EJson::Object)
                {
                    Out.Child = ReadSpline(*Value->AsObject(), PointPath + TEXT("/value"));
                    if (Out.Child == INDEX_NONE)
                    {
                        return INDEX_NONE;
                    }
                }
                else
                {
                    Fail(PointPath, TEXT("value must be a number or a spline"));
                    return INDEX_NONE;
                }
                Read.Add(Out);
            }
            Spline.FirstPoint = Graph.SplinePoints.Num();
            Spline.PointCount = Read.Num();
            Graph.SplinePoints.Append(Read);
            return Graph.Splines.Add(Spline);
        }

        bool ReadSettings(const FJsonObject& Settings, FVoxelTerrainPreset& Out)
        {
            // Read here, then left for later phases: mob generation, legacy seeding. The surface rule tree's id is
            // kept; ReadSurface reads the tree.
            static const TCHAR* const Known[] = {TEXT("sea_level"), TEXT("noise"), TEXT("default_block"),
                TEXT("default_fluid"), TEXT("noise_router"), TEXT("aquifers"), TEXT("debug_functions"),
                TEXT("material_rule"), TEXT("spawn_target"), TEXT("disable_mob_generation"),
                TEXT("legacy_random_source")};
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Settings.Values)
            {
                if (!Algo::FindByPredicate(Known, [&Field](const TCHAR* Key) { return IsWorldgenKey(Field.Key, Key); }))
                {
                    return Fail(TEXT(""), FString::Printf(TEXT("unexpected field %s"), *Field.Key));
                }
            }

            double SeaLevel = 0;
            double MinY = 0;
            double Height = 0;
            const TSharedPtr<FJsonObject>* Noise = nullptr;
            if (!Settings.TryGetObjectField(TEXT("noise"), Noise) ||
                !HasOnlyWorldgenKeys(**Noise, {TEXT("min_y"), TEXT("height")}) ||
                !ReadNumber(**Noise, TEXT("min_y"), TEXT("/noise"), MinY) ||
                !ReadNumber(**Noise, TEXT("height"), TEXT("/noise"), Height) ||
                !ReadNumber(Settings, TEXT("sea_level"), TEXT(""), SeaLevel) ||
                !Settings.TryGetStringField(TEXT("default_block"), Out.DefaultBlock) ||
                !Settings.TryGetStringField(TEXT("default_fluid"), Out.DefaultFluid))
            {
                return Fail(TEXT(""), TEXT("needs sea_level, noise {min_y, height}, default_block and default_fluid"));
            }
            Out.SeaLevel = int32(SeaLevel);
            Out.MinY = int32(MinY);
            Out.Height = int32(Height);
            Settings.TryGetStringField(TEXT("material_rule"), Out.MaterialRule);

            const FWorldgenEntry Router[] = {
                {TEXT("continents"), &Out.Router.Continents},
                {TEXT("erosion"), &Out.Router.Erosion},
                {TEXT("ridges"), &Out.Router.Ridges},
                {TEXT("temperature"), &Out.Router.Temperature},
                {TEXT("vegetation"), &Out.Router.Vegetation},
                {TEXT("depth"), &Out.Router.Depth},
                {TEXT("chunk_surface_level"), &Out.Router.ChunkSurfaceLevel},
                {TEXT("final_density"), &Out.Router.FinalDensity},
            };
            const FWorldgenEntry Aquifers[] = {
                {TEXT("barrier"), &Out.Aquifers.Barrier},
                {TEXT("exclusion"), &Out.Aquifers.Exclusion},
                {TEXT("fluid_level_floodedness"), &Out.Aquifers.FluidLevelFloodedness},
                {TEXT("fluid_level_spread"), &Out.Aquifers.FluidLevelSpread},
                {TEXT("lava"), &Out.Aquifers.Lava},
                {TEXT("surface_level"), &Out.Aquifers.SurfaceLevel},
            };
            if (!ReadEntries(Settings, TEXT("noise_router"), Router) ||
                !ReadEntries(Settings, TEXT("aquifers"), Aquifers))
            {
                return false;
            }

            const TArray<TSharedPtr<FJsonValue>>* Views = nullptr;
            if (!Settings.TryGetArrayField(TEXT("debug_functions"), Views))
            {
                return Fail(TEXT(""), TEXT("debug_functions must be an array"));
            }
            for (int32 Index = 0; Index < Views->Num(); ++Index)
            {
                const FString ViewPath = FString::Printf(TEXT("/debug_functions/%d"), Index);
                const TSharedPtr<FJsonObject>* View = nullptr;
                FVoxelTerrainPreset::FDebugView DebugView;
                if (!(*Views)[Index]->TryGetObject(View) ||
                    !HasOnlyWorldgenKeys(**View, {TEXT("function"), TEXT("label")}) ||
                    !(*View)->TryGetStringField(TEXT("label"), DebugView.Label))
                {
                    return Fail(ViewPath, TEXT("a debug view is {function, label}"));
                }
                DebugView.Node = ReadField(**View, TEXT("function"), ViewPath);
                if (DebugView.Node == INDEX_NONE)
                {
                    return false;
                }
                Out.DebugViews.Add(MoveTemp(DebugView));
            }

            // The spawn search's climate targets: each maps function ids to a [min, max] range.
            const TArray<TSharedPtr<FJsonValue>>* Targets = nullptr;
            if (Settings.HasField(TEXT("spawn_target")) && !Settings.TryGetArrayField(TEXT("spawn_target"), Targets))
            {
                return Fail(TEXT(""), TEXT("spawn_target must be an array"));
            }
            for (int32 Index = 0; Targets && Index < Targets->Num(); ++Index)
            {
                const FString TargetPath = FString::Printf(TEXT("/spawn_target/%d"), Index);
                const TSharedPtr<FJsonObject>* Target = nullptr;
                if (!(*Targets)[Index]->TryGetObject(Target))
                {
                    return Fail(TargetPath, TEXT("a spawn target maps function ids to [min, max]"));
                }
                TArray<FVoxelTerrainPreset::FSpawnRange>& Ranges = Out.SpawnTargets.AddDefaulted_GetRef();
                for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*Target)->Values)
                {
                    const TArray<TSharedPtr<FJsonValue>>* Bounds = nullptr;
                    if (!Field.Value->TryGetArray(Bounds) || Bounds->Num() != 2 ||
                        (*Bounds)[0]->Type != EJson::Number || (*Bounds)[1]->Type != EJson::Number ||
                        (*Bounds)[0]->AsNumber() > (*Bounds)[1]->AsNumber())
                    {
                        return Fail(TargetPath + TEXT("/") + Field.Key, TEXT("a range is [min, max]"));
                    }
                    FVoxelTerrainPreset::FSpawnRange Range;
                    Range.Node = ReadFunction(Field.Key);
                    if (Range.Node == INDEX_NONE)
                    {
                        return false;
                    }
                    // Quantized as the biome table's ranges are.
                    Range.Min = FVoxelBiomeTable::Quantize(float((*Bounds)[0]->AsNumber()));
                    Range.Max = FVoxelBiomeTable::Quantize(float((*Bounds)[1]->AsNumber()));
                    Ranges.Add(Range);
                }
            }
            return true;
        }

        template <int32 Count>
        bool ReadEntries(const FJsonObject& Settings, const TCHAR* Key, const FWorldgenEntry (&Entries)[Count])
        {
            const FString Path = FString(TEXT("/")) + Key;
            const TSharedPtr<FJsonObject>* Object = nullptr;
            if (!Settings.TryGetObjectField(Key, Object) || (*Object)->Values.Num() != Count)
            {
                return Fail(Path, FString::Printf(TEXT("needs exactly %d entries"), Count));
            }
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*Object)->Values)
            {
                if (!Algo::FindByPredicate(
                        Entries, [&Field](const FWorldgenEntry& Entry) { return IsWorldgenKey(Field.Key, Entry.Key); }))
                {
                    return Fail(Path, FString::Printf(TEXT("unexpected field %s"), *Field.Key));
                }
            }
            for (const FWorldgenEntry& Entry : Entries)
            {
                *Entry.Node = ReadField(**Object, Entry.Key, Path);
                if (*Entry.Node == INDEX_NONE)
                {
                    return false;
                }
            }
            return true;
        }

    private:
        VoxelWorldgen::FLoadFile LoadFile;
        FVoxelDensityGraph& Graph;
        // Functions being read, outermost first, to catch a cycle.
        TArray<FString> Reading;
    };

    class FWorldgenWriter
    {
    public:
        explicit FWorldgenWriter(const FVoxelDensityGraph& InGraph) : Graph(InGraph)
        {
            for (const FVoxelDensityFunction& Function : Graph.Functions)
            {
                Names.FindOrAdd(Function.Node, &Function.Name);
            }
        }

        TSharedPtr<FJsonValue> Write(int32 NodeIndex, bool bExpand) const
        {
            if (!Graph.Nodes.IsValidIndex(NodeIndex))
            {
                return MakeShared<FJsonValueNull>();
            }
            if (const FString* const* Name = bExpand ? nullptr : Names.Find(NodeIndex))
            {
                return MakeShared<FJsonValueString>(**Name);
            }
            const FVoxelDensityNode& Node = Graph.Nodes[NodeIndex];
            if (Node.Op == EVoxelDensityOp::Constant)
            {
                return MakeShared<FJsonValueNumber>(Node.Params[0]);
            }

            const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
            Object->SetStringField(TEXT("type"), FString(WorldgenIdPrefix) + FVoxelDensityGraph::GetOpName(Node.Op));
            const FWorldgenOpFields Fields = GetWorldgenOpFields(Node.Op);
            for (int32 Slot = 0; Slot < UE_ARRAY_COUNT(Fields.Inputs) && Fields.Inputs[Slot]; ++Slot)
            {
                Object->SetField(Fields.Inputs[Slot], Write(Node.Inputs[Slot], false));
            }
            for (int32 Slot = 0; Slot < UE_ARRAY_COUNT(Fields.Params) && Fields.Params[Slot]; ++Slot)
            {
                Object->SetNumberField(Fields.Params[Slot], Node.Params[Slot]);
            }
            switch (Node.Op)
            {
            case EVoxelDensityOp::Gradient:
                Object->SetStringField(TEXT("axis"), WorldgenAxes[FMath::Min<int32>(Node.Axis, 2)]);
                break;
            case EVoxelDensityOp::IntervalSelect:
            {
                TArray<TSharedPtr<FJsonValue>> Functions;
                TArray<TSharedPtr<FJsonValue>> Thresholds;
                for (int32 Entry = 0; Entry < Node.ListCount; ++Entry)
                {
                    Functions.Add(Write(Graph.ListNodes[Node.Ref + Entry], false));
                    if (Entry + 1 < Node.ListCount)
                    {
                        Thresholds.Add(MakeShared<FJsonValueNumber>(Graph.ListValues[Node.Ref + Entry]));
                    }
                }
                Object->SetArrayField(TEXT("functions"), Functions);
                Object->SetArrayField(TEXT("thresholds"), Thresholds);
                break;
            }
            case EVoxelDensityOp::Noise:
                if (Node.Inputs[0] != INDEX_NONE)
                {
                    Object->SetField(TEXT("shift_x"), Write(Node.Inputs[0], false));
                }
                if (Node.Inputs[1] != INDEX_NONE)
                {
                    Object->SetField(TEXT("shift_z"), Write(Node.Inputs[1], false));
                }
                [[fallthrough]];
            case EVoxelDensityOp::ShiftA:
            case EVoxelDensityOp::ShiftB:
                Object->SetStringField(TEXT("noise"), Graph.Noises[Node.Ref].Name);
                break;
            case EVoxelDensityOp::Spline:
                Object->SetObjectField(TEXT("spline"), WriteSpline(Node.Ref));
                break;
            default:
                break;
            }
            return MakeShared<FJsonValueObject>(Object);
        }

        TSharedRef<FJsonObject> WriteSpline(int32 Index) const
        {
            const FVoxelDensitySpline& Spline = Graph.Splines[Index];
            const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
            Object->SetField(TEXT("coordinate"), Write(Spline.Coordinate, false));
            TArray<TSharedPtr<FJsonValue>> Points;
            for (int32 PointIndex = 0; PointIndex < Spline.PointCount; ++PointIndex)
            {
                const FVoxelDensitySplinePoint& Point = Graph.SplinePoints[Spline.FirstPoint + PointIndex];
                const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
                Out->SetNumberField(TEXT("location"), Point.Location);
                if (Point.Child == INDEX_NONE)
                {
                    Out->SetNumberField(TEXT("value"), Point.Value);
                }
                else
                {
                    Out->SetObjectField(TEXT("value"), WriteSpline(Point.Child));
                }
                Out->SetNumberField(TEXT("derivative"), Point.Derivative);
                Points.Add(MakeShared<FJsonValueObject>(Out));
            }
            Object->SetArrayField(TEXT("points"), Points);
            return Object;
        }

    private:
        const FVoxelDensityGraph& Graph;
        TMap<int32, const FString*> Names;
    };
} // namespace

FString VoxelWorldgen::GetReferenceRoot()
{
    return FPaths::ConvertRelativePathToFull(
        FPaths::Combine(FPaths::ProjectDir(), TEXT("Reference/minecraft-worldgen-26.3")));
}

bool VoxelWorldgen::ReadPreset(const FString& Root, const FString& Preset, FVoxelTerrainPreset& Out, FString& OutError)
{
    Out = FVoxelTerrainPreset();
    Out.Name = Preset;
    const auto LoadFile = [&Root](const FString& Path, FString& OutText)
    { return LoadWorldgenFile(Root, Path, OutText); };
    FWorldgenParser Parser(LoadFile, Out.Graph);
    FString Path;
    if (!WorldgenIdToPath(FString(WorldgenIdPrefix) + Preset, TEXT("noise_settings"), Path))
    {
        OutError = FString::Printf(TEXT("%s is not a preset name"), *Preset);
        return false;
    }
    const TSharedPtr<FJsonValue> Value = Parser.Load(Path);
    const TSharedPtr<FJsonObject>* Settings = nullptr;
    if (!Value || !Value->TryGetObject(Settings))
    {
        Parser.Fail(TEXT(""), TEXT("noise settings must be an object"));
    }
    else
    {
        Parser.ReadSettings(**Settings, Out);
    }
    if (!Parser.Error.IsEmpty())
    {
        OutError = Parser.Error;
        return false;
    }
    return Out.Graph.Validate(&OutError);
}

bool VoxelWorldgen::ReadReferences(const FString& Root, TConstArrayView<FString> Functions,
    TConstArrayView<FString> Noises, FVoxelDensityGraph& Graph, FString& OutError)
{
    const auto LoadFile = [&Root](const FString& Path, FString& OutText)
    { return LoadWorldgenFile(Root, Path, OutText); };
    return ReadReferences(LoadFile, Functions, Noises, Graph, OutError);
}

bool VoxelWorldgen::ReadReferences(FLoadFile LoadFile, TConstArrayView<FString> Functions,
    TConstArrayView<FString> Noises, FVoxelDensityGraph& Graph, FString& OutError)
{
    FWorldgenParser Parser(LoadFile, Graph);
    for (const FString& Id : Functions)
    {
        if (Parser.ReadFunction(Id) == INDEX_NONE)
        {
            OutError = Parser.Error;
            return false;
        }
    }
    for (const FString& Id : Noises)
    {
        if (Parser.ReadNoise(Id) == INDEX_NONE)
        {
            OutError = Parser.Error;
            return false;
        }
    }
    return Graph.Validate(&OutError);
}

TSharedPtr<FJsonValue> VoxelWorldgen::WriteValue(const FVoxelDensityGraph& Graph, int32 Node, bool bExpand)
{
    return FWorldgenWriter(Graph).Write(Node, bExpand);
}

TSharedPtr<FJsonValue> VoxelWorldgen::WriteNoise(const FVoxelDensityNoise& Noise)
{
    const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("base_octave"), Noise.BaseOctave);
    Object->SetNumberField(TEXT("base_amplitude"), Noise.BaseAmplitude);
    if (Noise.AmplitudeModifiers.Num() != 1)
    {
        Object->SetNumberField(TEXT("octave_count"), Noise.AmplitudeModifiers.Num());
    }
    if (Noise.AmplitudeModifiers.ContainsByPredicate([](double Modifier) { return Modifier != 1; }))
    {
        TArray<TSharedPtr<FJsonValue>> Modifiers;
        for (const double Modifier : Noise.AmplitudeModifiers)
        {
            Modifiers.Add(MakeShared<FJsonValueNumber>(Modifier));
        }
        Object->SetArrayField(TEXT("amplitude_modifiers"), Modifiers);
    }
    return MakeShared<FJsonValueObject>(Object);
}
