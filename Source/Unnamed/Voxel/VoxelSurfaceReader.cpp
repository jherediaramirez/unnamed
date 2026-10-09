#include "Biome/VoxelBiomeTable.h"
#include "Surface/VoxelSurfaceEvaluator.h"
#include "Surface/VoxelSurfaceRules.h"
#include "Voxel/VoxelWorldgenJson.h"
#include "Voxel/VoxelWorldgenReader.h"

// The biome and surface rule half of VoxelWorldgen: the same strict reading as the density reader, with the file and
// the field in every error.
namespace
{
    using VoxelWorldgenJson::FindUnexpectedWorldgenKey;
    using VoxelWorldgenJson::IsWorldgenKey;
    using VoxelWorldgenJson::LoadWorldgenFile;
    using VoxelWorldgenJson::ParseWorldgenJson;
    using VoxelWorldgenJson::WorldgenIdToPath;

    const TCHAR* const SurfaceAnchorKeys[] = {TEXT("absolute"), TEXT("above_bottom"), TEXT("below_top")};
    const TCHAR* const BiomeClimateColumns[] = {TEXT("biome"), TEXT("temperature"), TEXT("humidity"),
        TEXT("continentalness"), TEXT("erosion"), TEXT("depth"), TEXT("weirdness"), TEXT("offset")};

    // A field by case-sensitive name (the engine's own lookup ignores case).
    TSharedPtr<FJsonValue> FindSurfaceField(const FJsonObject& Object, const TCHAR* Key)
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object.Values)
            if (IsWorldgenKey(Field.Key, Key)) return Field.Value;
        return nullptr;
    }

    FString JoinSurfacePath(const FString& Path, const TCHAR* Key)
    {
        return Path.IsEmpty() ? FString(Key) : Path + TEXT(".") + Key;
    }

    // The current file and the first failure; every reader below fails through it.
    class FSurfaceReadContext
    {
    public:
        explicit FSurfaceReadContext(const FString& InRoot) : Root(InRoot) {}

        FString Root;
        FString File = TEXT("(request)");
        FString Error;

        bool Fail(const FString& Path, const FString& Message)
        {
            if (Error.IsEmpty())
                Error = Path.IsEmpty() ? FString::Printf(TEXT("%s: %s"), *File, *Message)
                                       : FString::Printf(TEXT("%s at %s: %s"), *File, *Path, *Message);
            return false;
        }

        // A file under data/minecraft/worldgen/ (or, with bFacts, the reference's facts/ folder) as one JSON value.
        TSharedPtr<FJsonValue> Load(const FString& Path, bool bFacts = false)
        {
            File = bFacts ? TEXT("facts/") + Path : Path;
            FString Text;
            const bool bRead = bFacts ? FFileHelper::LoadFileToString(Text, *FPaths::Combine(Root, TEXT("facts"), Path))
                                      : LoadWorldgenFile(Root, Path, Text);
            if (!bRead)
            {
                Fail(TEXT(""), TEXT("cannot read the file"));
                return nullptr;
            }
            TSharedPtr<FJsonValue> Value = ParseWorldgenJson(Text);
            if (!Value) Fail(TEXT(""), TEXT("is not one JSON value"));
            return Value;
        }

        const FJsonObject* AsObject(const TSharedPtr<FJsonValue>& Value, const FString& Path)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            if (!Value || !Value->TryGetObject(Object) || !Object || !Object->IsValid())
            {
                Fail(Path, TEXT("expected an object"));
                return nullptr;
            }
            return Object->Get();
        }

        bool OnlyKeys(const FJsonObject& Object, const FString& Path, std::initializer_list<const TCHAR*> Keys)
        {
            FString Unexpected;
            return !FindUnexpectedWorldgenKey(Object, Keys, Unexpected) ||
                Fail(JoinSurfacePath(Path, *Unexpected), FString::Printf(TEXT("unexpected field \"%s\""), *Unexpected));
        }

        TSharedPtr<FJsonValue> Field(const FJsonObject& Object, const FString& Path, const TCHAR* Key)
        {
            TSharedPtr<FJsonValue> Value = FindSurfaceField(Object, Key);
            if (!Value) Fail(JoinSurfacePath(Path, Key), TEXT("missing field"));
            return Value;
        }

        bool Number(const FJsonObject& Object, const FString& Path, const TCHAR* Key, double& Out)
        {
            const TSharedPtr<FJsonValue> Value = Field(Object, Path, Key);
            return Value &&
                (Value->Type == EJson::Number || Fail(JoinSurfacePath(Path, Key), TEXT("expected a number"))) &&
                (Out = Value->AsNumber(), true);
        }

        // A whole number in int32 range.
        bool Integer(const FJsonObject& Object, const FString& Path, const TCHAR* Key, int32& Out)
        {
            double Value = 0;
            if (!Number(Object, Path, Key, Value)) return false;
            if (Value != FMath::RoundToDouble(Value) || Value < MIN_int32 || Value > MAX_int32)
                return Fail(JoinSurfacePath(Path, Key), TEXT("expected a whole number"));
            Out = int32(Value);
            return true;
        }

        bool Bool(const FJsonObject& Object, const FString& Path, const TCHAR* Key, bool& Out)
        {
            const TSharedPtr<FJsonValue> Value = Field(Object, Path, Key);
            return Value &&
                (Value->Type == EJson::Boolean || Fail(JoinSurfacePath(Path, Key), TEXT("expected true or false"))) &&
                (Out = Value->AsBool(), true);
        }

        bool String(const FJsonObject& Object, const FString& Path, const TCHAR* Key, FString& Out)
        {
            const TSharedPtr<FJsonValue> Value = Field(Object, Path, Key);
            return Value &&
                (Value->Type == EJson::String || Fail(JoinSurfacePath(Path, Key), TEXT("expected a string"))) &&
                (Out = Value->AsString(), true);
        }

        // A namespaced id that maps to a file name ("minecraft:overworld/surface").
        bool Id(const FJsonObject& Object, const FString& Path, const TCHAR* Key, FString& Out)
        {
            FString Ignored;
            return String(Object, Path, Key, Out) &&
                (WorldgenIdToPath(Out, TEXT("x"), Ignored) ||
                    Fail(JoinSurfacePath(Path, Key), Out + TEXT(" is not an id")));
        }
    };

    // "#rrggbb" -> 0xRRGGBB.
    bool ParseBiomeColor(const FString& Text, int32& Out)
    {
        if (Text.Len() != 7 || Text[0] != TEXT('#')) return false;
        int32 Value = 0;
        for (int32 Index = 1; Index < 7; ++Index)
        {
            if (!FChar::IsHexDigit(Text[Index])) return false;
            Value = Value * 16 + FParse::HexDigit(Text[Index]);
        }
        Out = Value;
        return true;
    }

    bool ReadBiomeFile(FSurfaceReadContext& Context, const FString& Id, FVoxelBiome& Out)
    {
        FString Path;
        if (!WorldgenIdToPath(Id, TEXT("biome"), Path)) return Context.Fail(TEXT(""), Id + TEXT(" is not a biome id"));
        const TSharedPtr<FJsonValue> Value = Context.Load(Path);
        const FJsonObject* Object = Value ? Context.AsObject(Value, TEXT("")) : nullptr;
        if (!Object) return false;
        // Spawns, sounds, sky and fog (attributes), features and carvers are for later systems.
        if (!Context.OnlyKeys(*Object, TEXT(""),
                {TEXT("temperature"), TEXT("downfall"), TEXT("has_precipitation"), TEXT("temperature_modifier"),
                    TEXT("effects"), TEXT("attributes"), TEXT("features"), TEXT("carvers")}))
            return false;
        Out.Name = Id;
        double Temperature = 0, Downfall = 0;
        if (!Context.Number(*Object, TEXT(""), TEXT("temperature"), Temperature) ||
            !Context.Number(*Object, TEXT(""), TEXT("downfall"), Downfall) ||
            !Context.Bool(*Object, TEXT(""), TEXT("has_precipitation"), Out.bHasPrecipitation))
            return false;
        Out.Temperature = float(Temperature);
        Out.Downfall = float(Downfall);
        if (FindSurfaceField(*Object, TEXT("temperature_modifier")))
        {
            FString Modifier;
            if (!Context.String(*Object, TEXT(""), TEXT("temperature_modifier"), Modifier)) return false;
            if (IsWorldgenKey(Modifier, TEXT("frozen")))
                Out.TemperatureModifier = EVoxelTemperatureModifier::Frozen;
            else if (!IsWorldgenKey(Modifier, TEXT("none")))
                return Context.Fail(TEXT("temperature_modifier"), Modifier + TEXT(" is not none or frozen"));
        }
        const FJsonObject* Effects =
            Context.AsObject(Context.Field(*Object, TEXT(""), TEXT("effects")), TEXT("effects"));
        if (!Effects ||
            !Context.OnlyKeys(*Effects, TEXT("effects"),
                {TEXT("water_color"), TEXT("grass_color"), TEXT("foliage_color"), TEXT("dry_foliage_color"),
                    TEXT("grass_color_modifier")}))
            return false;
        struct FColorField
        {
            const TCHAR* Key;
            int32* Target;
            bool bRequired;
        };
        const FColorField Colors[] = {{TEXT("water_color"), &Out.WaterColor, true},
            {TEXT("grass_color"), &Out.GrassColor, false}, {TEXT("foliage_color"), &Out.FoliageColor, false},
            {TEXT("dry_foliage_color"), &Out.DryFoliageColor, false}};
        for (const FColorField& Color : Colors)
        {
            if (!Color.bRequired && !FindSurfaceField(*Effects, Color.Key)) continue;
            FString Text;
            if (!Context.String(*Effects, TEXT("effects"), Color.Key, Text)) return false;
            if (!ParseBiomeColor(Text, *Color.Target))
                return Context.Fail(JoinSurfacePath(TEXT("effects"), Color.Key), Text + TEXT(" is not #rrggbb"));
        }
        if (FindSurfaceField(*Effects, TEXT("grass_color_modifier")))
        {
            FString Modifier;
            if (!Context.String(*Effects, TEXT("effects"), TEXT("grass_color_modifier"), Modifier)) return false;
            if (IsWorldgenKey(Modifier, TEXT("dark_forest")))
                Out.GrassColorModifier = EVoxelGrassColorModifier::DarkForest;
            else if (IsWorldgenKey(Modifier, TEXT("swamp")))
                Out.GrassColorModifier = EVoxelGrassColorModifier::Swamp;
            else if (!IsWorldgenKey(Modifier, TEXT("none")))
                return Context.Fail(TEXT("effects.grass_color_modifier"), Modifier + TEXT(" is not a known modifier"));
        }
        return true;
    }

    bool ReadBiomeIds(FSurfaceReadContext& Context, TConstArrayView<FString> Ids, FVoxelBiomeTable& Out)
    {
        for (const FString& Id : Ids)
        {
            if (Out.FindBiome(Id) != INDEX_NONE) continue;
            FVoxelBiome Biome;
            if (!ReadBiomeFile(Context, Id, Biome)) return false;
            Out.Biomes.Add(MoveTemp(Biome));
        }
        return true;
    }

    // Reads rule and condition files into a tree. Children are read before their node is added, so every node lands
    // after its inputs.
    class FSurfaceRuleParser
    {
    public:
        FSurfaceRuleParser(FSurfaceReadContext& InContext, FVoxelSurfaceRules& InOut) : Context(InContext), Out(InOut)
        {
        }

        int32 ReadNamedRule(const FString& Id) { return ReadNamed(Id, true); }

    private:
        FSurfaceReadContext& Context;
        FVoxelSurfaceRules& Out;
        // The named files being read, outermost first, to report cycles.
        TArray<FString> Reading;

        int32 ReadNamed(const FString& Id, bool bRule)
        {
            if (const int32 Found = bRule ? Out.FindRule(Id) : Out.FindCondition(Id); Found != INDEX_NONE) return Found;
            FString Path;
            if (!WorldgenIdToPath(Id, bRule ? TEXT("material_rule") : TEXT("material_condition"), Path))
            {
                Context.Fail(TEXT(""),
                    FString::Printf(TEXT("%s is not a %s id"), *Id, bRule ? TEXT("rule") : TEXT("condition")));
                return INDEX_NONE;
            }
            // A rule and a condition may share an id (separate folders), so the check keys by kind.
            const FString Key = (bRule ? TEXT("rule ") : TEXT("condition ")) + Id;
            if (Reading.Contains(Key))
            {
                Context.Fail(TEXT(""),
                    FString::Printf(
                        TEXT("%s references itself: %s -> %s"), *Id, *FString::Join(Reading, TEXT(" -> ")), *Key));
                return INDEX_NONE;
            }
            const FString Outer = Context.File;
            const TSharedPtr<FJsonValue> Value = Context.Load(Path);
            if (!Value) return INDEX_NONE;
            Reading.Add(Key);
            const int32 Index = bRule ? ReadRule(Value, TEXT("")) : ReadCondition(Value, TEXT(""));
            Reading.Pop();
            if (Index == INDEX_NONE) return INDEX_NONE;
            (bRule ? Out.NamedRules : Out.NamedConditions).Add({Id, Index});
            Context.File = Outer;
            return Index;
        }

        // An object's "type" without its namespace, or empty after a failure.
        FString ReadType(const FJsonObject& Object, const FString& Path)
        {
            FString Type;
            if (!Context.String(Object, Path, TEXT("type"), Type)) return FString();
            if (!Type.StartsWith(VoxelWorldgenJson::WorldgenIdPrefix, ESearchCase::CaseSensitive))
            {
                Context.Fail(JoinSurfacePath(Path, TEXT("type")), Type + TEXT(" is not a minecraft type"));
                return FString();
            }
            const FString Name = Type.RightChop(FCString::Strlen(VoxelWorldgenJson::WorldgenIdPrefix));
            if (Name.IsEmpty()) Context.Fail(JoinSurfacePath(Path, TEXT("type")), TEXT("names no type"));
            return Name;
        }

        bool ReadAnchor(const FJsonObject& Object, const FString& Path, const TCHAR* Key, FVoxelVerticalAnchor& Anchor)
        {
            const FString AnchorPath = JoinSurfacePath(Path, Key);
            const FJsonObject* Value = Context.AsObject(Context.Field(Object, Path, Key), AnchorPath);
            if (!Value) return false;
            if (Value->Values.Num() != 1)
                return Context.Fail(AnchorPath, TEXT("expected one of absolute, above_bottom, below_top"));
            for (int32 Kind = 0; Kind < UE_ARRAY_COUNT(SurfaceAnchorKeys); ++Kind)
            {
                if (!FindSurfaceField(*Value, SurfaceAnchorKeys[Kind])) continue;
                Anchor.Kind = EVoxelAnchorKind(Kind);
                return Context.Integer(*Value, AnchorPath, SurfaceAnchorKeys[Kind], Anchor.Value);
            }
            return Context.Fail(AnchorPath, TEXT("expected one of absolute, above_bottom, below_top"));
        }

        int32 ReadRule(const TSharedPtr<FJsonValue>& Value, const FString& Path)
        {
            if (Value && Value->Type == EJson::String) return ReadNamed(Value->AsString(), true);
            const FJsonObject* Object = Context.AsObject(Value, Path);
            if (!Object) return INDEX_NONE;
            const FString Type = ReadType(*Object, Path);
            if (Type.IsEmpty()) return INDEX_NONE;
            FVoxelSurfaceRule Rule;
            if (IsWorldgenKey(Type, TEXT("sequence")))
            {
                Rule.Op = EVoxelSurfaceRuleOp::Sequence;
                if (!Context.OnlyKeys(*Object, Path, {TEXT("type"), TEXT("sequence")})) return INDEX_NONE;
                const TSharedPtr<FJsonValue> List = Context.Field(*Object, Path, TEXT("sequence"));
                const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
                if (!List || !List->TryGetArray(Entries) || !Entries || Entries->IsEmpty())
                {
                    Context.Fail(JoinSurfacePath(Path, TEXT("sequence")), TEXT("expected a non-empty list"));
                    return INDEX_NONE;
                }
                TArray<int32> Children;
                for (int32 Entry = 0; Entry < Entries->Num(); ++Entry)
                {
                    const int32 Child = ReadRule((*Entries)[Entry],
                        FString::Printf(TEXT("%s[%d]"), *JoinSurfacePath(Path, TEXT("sequence")), Entry));
                    if (Child == INDEX_NONE) return INDEX_NONE;
                    Children.Add(Child);
                }
                Rule.First = Out.RuleList.Num();
                Rule.Count = Children.Num();
                Out.RuleList.Append(Children);
            }
            else if (IsWorldgenKey(Type, TEXT("condition")))
            {
                Rule.Op = EVoxelSurfaceRuleOp::Condition;
                if (!Context.OnlyKeys(*Object, Path, {TEXT("type"), TEXT("if_true"), TEXT("then_run")}))
                    return INDEX_NONE;
                const TSharedPtr<FJsonValue> If = Context.Field(*Object, Path, TEXT("if_true"));
                Rule.Condition = If ? ReadCondition(If, JoinSurfacePath(Path, TEXT("if_true"))) : INDEX_NONE;
                const TSharedPtr<FJsonValue> Then =
                    Rule.Condition != INDEX_NONE ? Context.Field(*Object, Path, TEXT("then_run")) : nullptr;
                Rule.Then = Then ? ReadRule(Then, JoinSurfacePath(Path, TEXT("then_run"))) : INDEX_NONE;
                if (Rule.Then == INDEX_NONE) return INDEX_NONE;
            }
            else if (IsWorldgenKey(Type, TEXT("block")))
            {
                Rule.Op = EVoxelSurfaceRuleOp::Block;
                FString State;
                if (!Context.OnlyKeys(*Object, Path, {TEXT("type"), TEXT("result_state")}) ||
                    !Context.String(*Object, Path, TEXT("result_state"), State))
                    return INDEX_NONE;
                if (State.IsEmpty())
                {
                    Context.Fail(JoinSurfacePath(Path, TEXT("result_state")), TEXT("is empty"));
                    return INDEX_NONE;
                }
                Rule.Block = Out.AddString(State);
            }
            else if (IsWorldgenKey(Type, TEXT("ore_vein")))
            {
                Rule.Op = EVoxelSurfaceRuleOp::OreVein;
                FString Ore, RawOre, Filler, Density, Richness, FillerGap;
                if (!Context.OnlyKeys(*Object, Path,
                        {TEXT("type"), TEXT("density"), TEXT("filler_block"), TEXT("filler_gap"), TEXT("ore_block"),
                            TEXT("raw_ore_block"), TEXT("raw_ore_chance"), TEXT("richness")}) ||
                    !Context.Id(*Object, Path, TEXT("ore_block"), Ore) ||
                    !Context.Id(*Object, Path, TEXT("raw_ore_block"), RawOre) ||
                    !Context.Id(*Object, Path, TEXT("filler_block"), Filler) ||
                    !Context.Id(*Object, Path, TEXT("density"), Density) ||
                    !Context.Id(*Object, Path, TEXT("richness"), Richness) ||
                    !Context.Id(*Object, Path, TEXT("filler_gap"), FillerGap) ||
                    !Context.Number(*Object, Path, TEXT("raw_ore_chance"), Rule.RawOreChance))
                    return INDEX_NONE;
                Rule.Block = Out.AddString(Ore);
                Rule.RawOreBlock = Out.AddString(RawOre);
                Rule.FillerBlock = Out.AddString(Filler);
                Rule.Density = Out.AddString(Density);
                Rule.Richness = Out.AddString(Richness);
                Rule.FillerGap = Out.AddString(FillerGap);
            }
            else if (IsWorldgenKey(Type, TEXT("bandlands")))
            {
                Rule.Op = EVoxelSurfaceRuleOp::Bandlands;
                if (!Context.OnlyKeys(*Object, Path, {TEXT("type")})) return INDEX_NONE;
            }
            else
            {
                Context.Fail(JoinSurfacePath(Path, TEXT("type")), Type + TEXT(" is not a known rule type"));
                return INDEX_NONE;
            }
            return Out.Rules.Add(Rule);
        }

        int32 ReadCondition(const TSharedPtr<FJsonValue>& Value, const FString& Path)
        {
            if (Value && Value->Type == EJson::String) return ReadNamed(Value->AsString(), false);
            const FJsonObject* Object = Context.AsObject(Value, Path);
            if (!Object) return INDEX_NONE;
            const FString Type = ReadType(*Object, Path);
            if (Type.IsEmpty()) return INDEX_NONE;
            FVoxelSurfaceCondition Condition;
            bool bRead = true;
            if (IsWorldgenKey(Type, TEXT("biome")))
            {
                Condition.Op = EVoxelSurfaceConditionOp::Biome;
                bRead = Context.OnlyKeys(*Object, Path, {TEXT("type"), TEXT("biome_is")});
                // One biome is written bare, several as a list.
                const TSharedPtr<FJsonValue> Biomes = bRead ? Context.Field(*Object, Path, TEXT("biome_is")) : nullptr;
                TArray<TSharedPtr<FJsonValue>> Single;
                const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
                if (Biomes && Biomes->Type == EJson::String)
                {
                    Single.Add(Biomes);
                    Entries = &Single;
                }
                else if (Biomes && (!Biomes->TryGetArray(Entries) || !Entries || Entries->IsEmpty()))
                    Entries = nullptr;
                if (bRead && !Entries)
                    bRead = Context.Fail(
                        JoinSurfacePath(Path, TEXT("biome_is")), TEXT("expected a biome id or a list of them"));
                TArray<FString> Ids;
                for (int32 Entry = 0; bRead && Entry < Entries->Num(); ++Entry)
                {
                    FString Id, Ignored;
                    if (!(*Entries)[Entry]->TryGetString(Id) || !WorldgenIdToPath(Id, TEXT("biome"), Ignored))
                        bRead = Context.Fail(JoinSurfacePath(Path, TEXT("biome_is")), TEXT("expected biome ids"));
                    Ids.Add(Id);
                }
                // The list's ids sit next to each other in Strings, even where one appeared before.
                if (bRead)
                {
                    Condition.First = Out.Strings.Num();
                    Condition.Count = Ids.Num();
                    Out.Strings.Append(Ids);
                }
            }
            else if (IsWorldgenKey(Type, TEXT("noise_threshold")))
            {
                Condition.Op = EVoxelSurfaceConditionOp::NoiseThreshold;
                FString Noise;
                bRead =
                    Context.OnlyKeys(*Object, Path,
                        {TEXT("type"), TEXT("noise"), TEXT("min_threshold"), TEXT("max_threshold"), TEXT("is_3d")}) &&
                    Context.Id(*Object, Path, TEXT("noise"), Noise) &&
                    Context.Number(*Object, Path, TEXT("min_threshold"), Condition.MinThreshold) &&
                    Context.Number(*Object, Path, TEXT("max_threshold"), Condition.MaxThreshold) &&
                    (!FindSurfaceField(*Object, TEXT("is_3d")) ||
                        Context.Bool(*Object, Path, TEXT("is_3d"), Condition.bIs3D));
                Condition.First = bRead ? Out.AddString(Noise) : INDEX_NONE;
            }
            else if (IsWorldgenKey(Type, TEXT("vertical_gradient")))
            {
                Condition.Op = EVoxelSurfaceConditionOp::VerticalGradient;
                FString Name;
                bRead =
                    Context.OnlyKeys(*Object, Path,
                        {TEXT("type"), TEXT("random_name"), TEXT("true_at_and_below"), TEXT("false_at_and_above")}) &&
                    Context.Id(*Object, Path, TEXT("random_name"), Name) &&
                    ReadAnchor(*Object, Path, TEXT("true_at_and_below"), Condition.Anchor) &&
                    ReadAnchor(*Object, Path, TEXT("false_at_and_above"), Condition.FalseAnchor);
                Condition.First = bRead ? Out.AddString(Name) : INDEX_NONE;
            }
            else if (IsWorldgenKey(Type, TEXT("y_above")))
            {
                Condition.Op = EVoxelSurfaceConditionOp::YAbove;
                bRead =
                    Context.OnlyKeys(*Object, Path,
                        {TEXT("type"), TEXT("anchor"), TEXT("surface_depth_multiplier"), TEXT("add_stone_depth")}) &&
                    ReadAnchor(*Object, Path, TEXT("anchor"), Condition.Anchor) &&
                    Context.Integer(
                        *Object, Path, TEXT("surface_depth_multiplier"), Condition.SurfaceDepthMultiplier) &&
                    Context.Bool(*Object, Path, TEXT("add_stone_depth"), Condition.bAddDepth);
            }
            else if (IsWorldgenKey(Type, TEXT("water")))
            {
                Condition.Op = EVoxelSurfaceConditionOp::Water;
                bRead =
                    Context.OnlyKeys(*Object, Path,
                        {TEXT("type"), TEXT("offset"), TEXT("surface_depth_multiplier"), TEXT("add_stone_depth")}) &&
                    Context.Integer(*Object, Path, TEXT("offset"), Condition.Offset) &&
                    Context.Integer(
                        *Object, Path, TEXT("surface_depth_multiplier"), Condition.SurfaceDepthMultiplier) &&
                    Context.Bool(*Object, Path, TEXT("add_stone_depth"), Condition.bAddDepth);
            }
            else if (IsWorldgenKey(Type, TEXT("stone_depth")))
            {
                Condition.Op = EVoxelSurfaceConditionOp::StoneDepth;
                FString Surface;
                bRead = Context.OnlyKeys(*Object, Path,
                            {TEXT("type"), TEXT("offset"), TEXT("add_surface_depth"), TEXT("secondary_depth_range"),
                                TEXT("surface_type")}) &&
                    Context.Integer(*Object, Path, TEXT("offset"), Condition.Offset) &&
                    Context.Bool(*Object, Path, TEXT("add_surface_depth"), Condition.bAddDepth) &&
                    Context.Integer(*Object, Path, TEXT("secondary_depth_range"), Condition.SecondaryDepthRange) &&
                    Context.String(*Object, Path, TEXT("surface_type"), Surface);
                Condition.bCeiling = IsWorldgenKey(Surface, TEXT("ceiling"));
                if (bRead && !Condition.bCeiling && !IsWorldgenKey(Surface, TEXT("floor")))
                    bRead = Context.Fail(
                        JoinSurfacePath(Path, TEXT("surface_type")), Surface + TEXT(" is not floor or ceiling"));
            }
            else if (IsWorldgenKey(Type, TEXT("not")))
            {
                Condition.Op = EVoxelSurfaceConditionOp::Not;
                const TSharedPtr<FJsonValue> Invert = Context.OnlyKeys(*Object, Path, {TEXT("type"), TEXT("invert")})
                    ? Context.Field(*Object, Path, TEXT("invert"))
                    : nullptr;
                Condition.Input = Invert ? ReadCondition(Invert, JoinSurfacePath(Path, TEXT("invert"))) : INDEX_NONE;
                bRead = Condition.Input != INDEX_NONE;
            }
            else if (IsWorldgenKey(Type, TEXT("steep")) || IsWorldgenKey(Type, TEXT("hole")) ||
                IsWorldgenKey(Type, TEXT("above_preliminary_surface")))
            {
                Condition.Op = IsWorldgenKey(Type, TEXT("steep")) ? EVoxelSurfaceConditionOp::Steep
                    : IsWorldgenKey(Type, TEXT("hole"))           ? EVoxelSurfaceConditionOp::Hole
                                                                  : EVoxelSurfaceConditionOp::AbovePreliminarySurface;
                bRead = Context.OnlyKeys(*Object, Path, {TEXT("type")});
            }
            else
                bRead =
                    Context.Fail(JoinSurfacePath(Path, TEXT("type")), Type + TEXT(" is not a known condition type"));
            return bRead ? Out.Conditions.Add(Condition) : INDEX_NONE;
        }
    };

    // Writes rules and conditions back as the reference's JSON.
    class FSurfaceRuleWriter
    {
    public:
        explicit FSurfaceRuleWriter(const FVoxelSurfaceRules& InRules) : Rules(InRules) {}

        TSharedPtr<FJsonValue> Rule(int32 Index, bool bExpand) const
        {
            if (!Rules.Rules.IsValidIndex(Index)) return MakeShared<FJsonValueNull>();
            if (!bExpand)
                if (const FVoxelSurfaceName* Named = FindName(Rules.NamedRules, Index))
                    return MakeShared<FJsonValueString>(Named->Name);
            const FVoxelSurfaceRule& Node = Rules.Rules[Index];
            TSharedRef<FJsonObject> Object = Typed(FVoxelSurfaceRules::GetRuleOpName(Node.Op));
            switch (Node.Op)
            {
            case EVoxelSurfaceRuleOp::Sequence:
            {
                TArray<TSharedPtr<FJsonValue>> Entries;
                for (int32 Entry = 0; Entry < Node.Count; ++Entry)
                    Entries.Add(Rule(Rules.RuleList[Node.First + Entry], false));
                Object->SetArrayField(TEXT("sequence"), Entries);
                break;
            }
            case EVoxelSurfaceRuleOp::Condition:
                Object->SetField(TEXT("if_true"), Condition(Node.Condition, false));
                Object->SetField(TEXT("then_run"), Rule(Node.Then, false));
                break;
            case EVoxelSurfaceRuleOp::Block:
                Object->SetStringField(TEXT("result_state"), Text(Node.Block));
                break;
            case EVoxelSurfaceRuleOp::OreVein:
                Object->SetStringField(TEXT("density"), Text(Node.Density));
                Object->SetStringField(TEXT("filler_block"), Text(Node.FillerBlock));
                Object->SetStringField(TEXT("filler_gap"), Text(Node.FillerGap));
                Object->SetStringField(TEXT("ore_block"), Text(Node.Block));
                Object->SetStringField(TEXT("raw_ore_block"), Text(Node.RawOreBlock));
                Object->SetNumberField(TEXT("raw_ore_chance"), Node.RawOreChance);
                Object->SetStringField(TEXT("richness"), Text(Node.Richness));
                break;
            default:
                break;
            }
            return MakeShared<FJsonValueObject>(Object);
        }

        TSharedPtr<FJsonValue> Condition(int32 Index, bool bExpand) const
        {
            if (!Rules.Conditions.IsValidIndex(Index)) return MakeShared<FJsonValueNull>();
            if (!bExpand)
                if (const FVoxelSurfaceName* Named = FindName(Rules.NamedConditions, Index))
                    return MakeShared<FJsonValueString>(Named->Name);
            const FVoxelSurfaceCondition& Node = Rules.Conditions[Index];
            TSharedRef<FJsonObject> Object = Typed(FVoxelSurfaceRules::GetConditionOpName(Node.Op));
            switch (Node.Op)
            {
            case EVoxelSurfaceConditionOp::Biome:
                if (Node.Count == 1)
                    Object->SetStringField(TEXT("biome_is"), Text(Node.First));
                else
                {
                    TArray<TSharedPtr<FJsonValue>> Entries;
                    for (int32 Entry = 0; Entry < Node.Count; ++Entry)
                        Entries.Add(MakeShared<FJsonValueString>(Text(Node.First + Entry)));
                    Object->SetArrayField(TEXT("biome_is"), Entries);
                }
                break;
            case EVoxelSurfaceConditionOp::NoiseThreshold:
                Object->SetStringField(TEXT("noise"), Text(Node.First));
                Object->SetNumberField(TEXT("min_threshold"), Node.MinThreshold);
                Object->SetNumberField(TEXT("max_threshold"), Node.MaxThreshold);
                if (Node.bIs3D) Object->SetBoolField(TEXT("is_3d"), true);
                break;
            case EVoxelSurfaceConditionOp::VerticalGradient:
                Object->SetStringField(TEXT("random_name"), Text(Node.First));
                Object->SetField(TEXT("true_at_and_below"), Anchor(Node.Anchor));
                Object->SetField(TEXT("false_at_and_above"), Anchor(Node.FalseAnchor));
                break;
            case EVoxelSurfaceConditionOp::YAbove:
                Object->SetField(TEXT("anchor"), Anchor(Node.Anchor));
                Object->SetNumberField(TEXT("surface_depth_multiplier"), Node.SurfaceDepthMultiplier);
                Object->SetBoolField(TEXT("add_stone_depth"), Node.bAddDepth);
                break;
            case EVoxelSurfaceConditionOp::Water:
                Object->SetNumberField(TEXT("offset"), Node.Offset);
                Object->SetNumberField(TEXT("surface_depth_multiplier"), Node.SurfaceDepthMultiplier);
                Object->SetBoolField(TEXT("add_stone_depth"), Node.bAddDepth);
                break;
            case EVoxelSurfaceConditionOp::StoneDepth:
                Object->SetNumberField(TEXT("offset"), Node.Offset);
                Object->SetBoolField(TEXT("add_surface_depth"), Node.bAddDepth);
                Object->SetNumberField(TEXT("secondary_depth_range"), Node.SecondaryDepthRange);
                Object->SetStringField(TEXT("surface_type"), Node.bCeiling ? TEXT("ceiling") : TEXT("floor"));
                break;
            case EVoxelSurfaceConditionOp::Not:
                Object->SetField(TEXT("invert"), Condition(Node.Input, false));
                break;
            default:
                break;
            }
            return MakeShared<FJsonValueObject>(Object);
        }

    private:
        const FVoxelSurfaceRules& Rules;

        static const FVoxelSurfaceName* FindName(const TArray<FVoxelSurfaceName>& Names, int32 Index)
        {
            return Names.FindByPredicate([Index](const FVoxelSurfaceName& Entry) { return Entry.Index == Index; });
        }

        static TSharedRef<FJsonObject> Typed(const TCHAR* Type)
        {
            TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
            Object->SetStringField(TEXT("type"), FString(VoxelWorldgenJson::WorldgenIdPrefix) + Type);
            return Object;
        }

        FString Text(int32 Index) const { return Rules.Strings.IsValidIndex(Index) ? Rules.Strings[Index] : FString(); }

        static TSharedPtr<FJsonValue> Anchor(const FVoxelVerticalAnchor& Value)
        {
            TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
            Object->SetNumberField(SurfaceAnchorKeys[FMath::Min(int32(Value.Kind), 2)], Value.Value);
            return MakeShared<FJsonValueObject>(Object);
        }
    };
} // namespace

bool VoxelWorldgen::ReadBiomes(
    const FString& Root, TConstArrayView<FString> Ids, FVoxelBiomeTable& Out, FString& OutError)
{
    FSurfaceReadContext Context(Root);
    if (!ReadBiomeIds(Context, Ids, Out))
    {
        OutError = Context.Error;
        return false;
    }
    return true;
}

bool VoxelWorldgen::ReadBiomeParameters(
    const FString& Root, const FString& List, FVoxelBiomeTable& Out, FString& OutError)
{
    Out = FVoxelBiomeTable();
    FSurfaceReadContext Context(Root);
    const auto Finish = [&Context, &OutError]()
    {
        OutError = Context.Error;
        return false;
    };
    FString Path;
    if (!WorldgenIdToPath(TEXT("minecraft:") + List, TEXT("multi_noise_biome_source_parameter_list"), Path))
    {
        Context.Fail(TEXT(""), List + TEXT(" is not a parameter list name"));
        return Finish();
    }
    const TSharedPtr<FJsonValue> ListValue = Context.Load(Path);
    const FJsonObject* ListObject = ListValue ? Context.AsObject(ListValue, TEXT("")) : nullptr;
    if (!ListObject || !Context.OnlyKeys(*ListObject, TEXT(""), {TEXT("preset")}) ||
        !Context.Id(*ListObject, TEXT(""), TEXT("preset"), Out.Preset))
        return Finish();

    // The jar builds the preset's points in code; tools/WorldgenFacts.java dumped them.
    const TSharedPtr<FJsonValue> Facts = Context.Load(TEXT("biome_parameters.json"), true);
    const FJsonObject* Object = Facts ? Context.AsObject(Facts, TEXT("")) : nullptr;
    if (!Object ||
        !Context.OnlyKeys(*Object, TEXT(""),
            {TEXT("generated_by"), TEXT("preset"), TEXT("quantization"), TEXT("columns"), TEXT("biomes"),
                TEXT("points")}))
        return Finish();
    FString Preset;
    double Quantization = 0;
    if (!Context.String(*Object, TEXT(""), TEXT("preset"), Preset) ||
        !Context.Number(*Object, TEXT(""), TEXT("quantization"), Quantization))
        return Finish();
    if (!IsWorldgenKey(Preset, *Out.Preset))
    {
        Context.Fail(TEXT("preset"), FString::Printf(TEXT("holds %s, the list names %s"), *Preset, *Out.Preset));
        return Finish();
    }
    if (Quantization != double(FVoxelBiomeTable::Quantization))
    {
        Context.Fail(TEXT("quantization"), TEXT("expected 10000"));
        return Finish();
    }
    const TSharedPtr<FJsonValue> Columns = Context.Field(*Object, TEXT(""), TEXT("columns"));
    const TArray<TSharedPtr<FJsonValue>>* ColumnList = nullptr;
    bool bColumns = Columns && Columns->TryGetArray(ColumnList) && ColumnList &&
        ColumnList->Num() == UE_ARRAY_COUNT(BiomeClimateColumns);
    for (int32 Column = 0; bColumns && Column < ColumnList->Num(); ++Column)
        bColumns = (*ColumnList)[Column]->Type == EJson::String &&
            IsWorldgenKey((*ColumnList)[Column]->AsString(), BiomeClimateColumns[Column]);
    if (!bColumns)
    {
        if (Columns) Context.Fail(TEXT("columns"), TEXT("expected biome, the six parameters in order, then offset"));
        return Finish();
    }

    const TSharedPtr<FJsonValue> BiomeValue = Context.Field(*Object, TEXT(""), TEXT("biomes"));
    const TArray<TSharedPtr<FJsonValue>>* BiomeList = nullptr;
    if (!BiomeValue || !BiomeValue->TryGetArray(BiomeList) || !BiomeList || BiomeList->IsEmpty())
    {
        if (BiomeValue) Context.Fail(TEXT("biomes"), TEXT("expected a non-empty list of biome ids"));
        return Finish();
    }
    TArray<FString> Ids;
    for (int32 Index = 0; Index < BiomeList->Num(); ++Index)
    {
        FString Id;
        if (!(*BiomeList)[Index]->TryGetString(Id))
        {
            Context.Fail(FString::Printf(TEXT("biomes[%d]"), Index), TEXT("expected a biome id"));
            return Finish();
        }
        Ids.Add(Id);
    }
    const TSharedPtr<FJsonValue> PointValue = Context.Field(*Object, TEXT(""), TEXT("points"));
    const TArray<TSharedPtr<FJsonValue>>* PointList = nullptr;
    if (!PointValue || !PointValue->TryGetArray(PointList) || !PointList || PointList->IsEmpty())
    {
        if (PointValue) Context.Fail(TEXT("points"), TEXT("expected a non-empty list"));
        return Finish();
    }
    Out.Points.Reserve(PointList->Num());
    for (int32 Index = 0; Index < PointList->Num(); ++Index)
    {
        const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
        constexpr int32 RowWidth = 2 + 2 * int32(EVoxelClimateAxis::Count);
        double Values[RowWidth] = {};
        bool bRow = (*PointList)[Index]->TryGetArray(Row) && Row && Row->Num() == RowWidth;
        for (int32 Column = 0; bRow && Column < RowWidth; ++Column)
        {
            bRow = (*Row)[Column]->Type == EJson::Number;
            Values[Column] = bRow ? (*Row)[Column]->AsNumber() : 0;
            bRow = bRow && Values[Column] == FMath::RoundToDouble(Values[Column]) && FMath::Abs(Values[Column]) < 1e15;
        }
        if (!bRow || Values[0] < 0 || Values[0] >= Ids.Num())
        {
            Context.Fail(FString::Printf(TEXT("points[%d]"), Index),
                TEXT("expected a biome index, six min and max pairs and an offset, all whole numbers"));
            return Finish();
        }
        FVoxelBiomeParameter& Point = Out.Points.AddDefaulted_GetRef();
        // Biomes are read in the list's order below, so the list's index is the table's.
        Point.Biome = int32(Values[0]);
        for (int32 Axis = 0; Axis < int32(EVoxelClimateAxis::Count); ++Axis)
        {
            Point.Min[Axis] = int64(Values[1 + 2 * Axis]);
            Point.Max[Axis] = int64(Values[2 + 2 * Axis]);
        }
        Point.Offset = int64(Values[RowWidth - 1]);
    }
    if (!ReadBiomeIds(Context, Ids, Out)) return Finish();
    if (Out.Biomes.Num() != Ids.Num())
    {
        Context.Fail(TEXT("biomes"), TEXT("lists a biome twice"));
        return Finish();
    }
    FString Invalid;
    if (!Out.Validate(&Invalid))
    {
        OutError = TEXT("facts/biome_parameters.json: ") + Invalid;
        return false;
    }
    return true;
}

bool VoxelWorldgen::ReadSurfaceRules(
    const FString& Root, const FString& Rule, FVoxelSurfaceRules& Out, FString& OutError)
{
    Out = FVoxelSurfaceRules();
    FSurfaceReadContext Context(Root);
    FSurfaceRuleParser Parser(Context, Out);
    Out.Root = Parser.ReadNamedRule(TEXT("minecraft:") + Rule);
    if (!Context.Error.IsEmpty())
    {
        OutError = Context.Error;
        return false;
    }
    FString Invalid;
    if (!Out.Validate(&Invalid))
    {
        OutError = FString::Printf(TEXT("material_rule/%s.json: %s"), *Rule, *Invalid);
        return false;
    }
    return true;
}

bool VoxelWorldgen::ReadSurface(const FString& Root, const FString& Rule, FVoxelSurfaceInputs& Out, FString& OutError)
{
    Out = FVoxelSurfaceInputs();
    if (!Rule.StartsWith(TEXT("minecraft:")))
    {
        OutError = FString::Printf(TEXT("%s is not a surface rule id"), *Rule);
        return false;
    }
    if (!ReadSurfaceRules(Root, Rule.RightChop(10), Out.Rules, OutError)) return false;
    FVoxelDensityGraph Graph;
    if (!ReadReferences(Root, {}, FVoxelSurfaceEvaluator::GetNoiseIds(Out.Rules), Graph, OutError)) return false;
    Out.Noises = MoveTemp(Graph.Noises);
    return true;
}

TSharedPtr<FJsonValue> VoxelWorldgen::WriteSurfaceRule(const FVoxelSurfaceRules& Rules, int32 Index, bool bExpand)
{
    return FSurfaceRuleWriter(Rules).Rule(Index, bExpand);
}

TSharedPtr<FJsonValue> VoxelWorldgen::WriteSurfaceCondition(const FVoxelSurfaceRules& Rules, int32 Index, bool bExpand)
{
    return FSurfaceRuleWriter(Rules).Condition(Index, bExpand);
}
