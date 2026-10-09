#include "Algo/IndexOf.h"
#include "Async/ParallelFor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Voxel/VoxelWorldgenJson.h"
#include "Voxel/VoxelWorldgenReader.h"
#include "VoxelLog.h"

using namespace VoxelWorldgenJson;

namespace
{
    // The schema: every type the 26.3 decoration data the overworld reaches uses, with its fields as the data writes
    // them. A field is "name:kinds" ("name?:" when some files leave it out); kinds are separated by "/" and a list of
    // a kind is "[kind]". Plain kinds: int, float, bool, str (free text), block (a block id), id (an id outside the
    // reference: fluids, loot tables, structures, processor lists), tag (a block tag's id), htag (a "#"-prefixed block
    // tag) and (a|b) (one of the words). Node kinds: placed, feature, modifier, predicate, rule_test, provider, state,
    // int_provider, float_provider, height_provider, anchor, trunk_placer, foliage_placer, root_placer, feature_size,
    // decorator, processor, entity_modifier and props; @name is an object schema below. Strings name entries where a
    // placed feature, feature or provider is expected; numbers are constants where an int or float provider is.
    struct FSchemaSource
    {
        EVoxelFeatureKind Kind;
        const TCHAR* Name;
        const TCHAR* Fields;
    };

#define DIRECTIONS TEXT("(down|up|north|south|west|east)")
#define HEIGHTMAPS                                                                                                     \
    TEXT("(WORLD_SURFACE_WG|WORLD_SURFACE|OCEAN_FLOOR_WG|OCEAN_FLOOR|MOTION_BLOCKING|MOTION_BLOCKING_NO_LEAVES)")
#define TRUNK TEXT("base_height:int height_rand_a:int height_rand_b:int")
#define FOLIAGE TEXT("radius:int_provider offset:int_provider")
#define VEGETATION_PATCH                                                                                               \
    TEXT(                                                                                                              \
        "replaceable:htag ground_state:provider vegetation_feature:placed surface:(floor|ceiling) depth:int_provider " \
        "vertical_range:int vegetation_chance:float xz_radius:int_provider extra_edge_column_chance:float "            \
        "extra_bottom_block_chance:float")

    using K = EVoxelFeatureKind;
    const FSchemaSource Sources[] = {
        // Objects without a type.
        {K::PlacedFeature, TEXT("placed"), TEXT("feature:feature placement:[modifier]")},
        {K::BlockState, TEXT("state"), TEXT("id:block properties?:props")},
        {K::Object, TEXT("ore_target"), TEXT("state:block/state target:rule_test")},
        {K::Object, TEXT("selector_entry"), TEXT("chance:float feature:placed")},
        {K::Object, TEXT("weighted_state"), TEXT("data:block/state weight:int")},
        {K::Object, TEXT("weighted_int"), TEXT("data:int_provider weight:int")},
        {K::Object, TEXT("weighted_placed"), TEXT("data:placed weight:int")},
        {K::Object, TEXT("weighted_template"), TEXT("data:@template_id weight:int")},
        {K::Object, TEXT("template_id"), TEXT("id:id")},
        {K::Object, TEXT("column_layer"), TEXT("height:int_provider provider:provider")},
        {K::Object, TEXT("provider_rule"), TEXT("if_true:predicate then:provider")},
        {K::Object, TEXT("noise_parameters"), TEXT("base_octave:int base_amplitude:float")},
        {K::Object, TEXT("int_range"), TEXT("min_inclusive:int max_inclusive:int")},
        {K::Object, TEXT("canyon_shape"),
            TEXT("distance_factor:float_provider thickness:float_provider width_smoothness:int "
                 "horizontal_radius_factor:float_provider vertical_radius_default_factor:float "
                 "vertical_radius_center_factor:float y_scale:float")},
        {K::Object, TEXT("geode_blocks"),
            TEXT("filling_provider:provider inner_layer_provider:provider alternate_inner_layer_provider:provider "
                 "middle_layer_provider:provider outer_layer_provider:provider inner_placements:[block/state] "
                 "cannot_replace:htag invalid_blocks:htag")},
        {K::Object, TEXT("geode_layers"),
            TEXT("filling?:float inner_layer?:float middle_layer?:float outer_layer?:float")},
        {K::Object, TEXT("geode_crack"),
            TEXT("generate_crack_chance:float base_crack_size?:float crack_point_offset?:int")},
        {K::Object, TEXT("mangrove_root_placement"),
            TEXT("can_grow_through:htag muddy_roots_in:htag/block/[block] muddy_roots_provider:provider "
                 "max_root_width:int max_root_length:int random_skew_chance:float")},
        {K::Object, TEXT("above_root_placement"),
            TEXT("above_root_placement_chance:float above_root_provider:provider")},
        {K::Object, TEXT("template_processors"), TEXT("processors:[processor]")},
        {K::Object, TEXT("processor_rule"),
            TEXT("input_predicate:rule_test location_predicate:rule_test output_state:block/state "
                 "block_entity_modifier?:entity_modifier")},

        // Features.
        {K::Feature, TEXT("bamboo"), TEXT("probability:float")},
        {K::Feature, TEXT("block_blob"), TEXT("state:block can_place_on:predicate")},
        {K::Feature, TEXT("block_column"),
            TEXT("layers:[@column_layer] direction:")
                DIRECTIONS TEXT(" allowed_placement:predicate prioritize_tip:bool")},
        {K::Feature, TEXT("blue_ice"), TEXT("")},
        {K::Feature, TEXT("coral_claw"), TEXT("feature:placed")},
        {K::Feature, TEXT("coral_tree"), TEXT("feature:placed")},
        {K::Feature, TEXT("disk"),
            TEXT("state_provider:provider target:predicate radius:int_provider half_height:int")},
        {K::Feature, TEXT("fallen_tree"),
            TEXT("trunk_provider:provider log_length:int_provider stump_decorators:[decorator] "
                 "log_decorators:[decorator]")},
        {K::Feature, TEXT("fossil"),
            TEXT("fossil_structures:[id] overlay_structures:[id] fossil_processors:id overlay_processors:id "
                 "max_empty_corners_allowed:int")},
        {K::Feature, TEXT("freeze_top_layer"), TEXT("")},
        {K::Feature, TEXT("geode"),
            TEXT("blocks:@geode_blocks layers:@geode_layers crack:@geode_crack use_alternate_layer0_chance:float "
                 "outer_wall_distance:int_provider invalid_blocks_threshold:int")},
        {K::Feature, TEXT("huge_brown_mushroom"),
            TEXT("cap_provider:provider stem_provider:provider foliage_radius:int can_place_on:predicate")},
        {K::Feature, TEXT("huge_red_mushroom"),
            TEXT("cap_provider:provider stem_provider:provider can_place_on:predicate")},
        {K::Feature, TEXT("iceberg"), TEXT("state:block")},
        {K::Feature, TEXT("lake"),
            TEXT("fluid:provider barrier:provider can_place_feature:predicate can_replace_with_air_or_fluid:predicate "
                 "can_replace_with_barrier:predicate")},
        {K::Feature, TEXT("large_dripstone"),
            TEXT("replaceable_blocks:htag column_radius:int_provider height_scale:float_provider "
                 "max_column_radius_to_cave_height_ratio:float stalactite_bluntness:float_provider "
                 "stalagmite_bluntness:float_provider wind_speed:float_provider min_radius_for_wind:int "
                 "min_bluntness_for_wind:float")},
        {K::Feature, TEXT("monster_room"), TEXT("")},
        {K::Feature, TEXT("multiface_growth"),
            TEXT("block:block search_range:int can_place_on_floor?:bool can_place_on_ceiling:bool "
                 "can_place_on_wall:bool chance_of_spreading?:float can_be_placed_on:htag/block/[block]")},
        {K::Feature, TEXT("no_op"), TEXT("")},
        {K::Feature, TEXT("ore"), TEXT("size:int discard_chance_on_air_exposure:float targets:[@ore_target]")},
        {K::Feature, TEXT("overlay"), TEXT("features:[placed]")},
        {K::Feature, TEXT("random_boolean_selector"), TEXT("feature_true:placed feature_false:placed")},
        {K::Feature, TEXT("random_selector"), TEXT("features:[@selector_entry] default:placed")},
        {K::Feature, TEXT("root_system"),
            TEXT("feature:placed required_vertical_space_for_tree:int root_radius:int root_replaceable:htag "
                 "root_state_provider:provider root_placement_attempts:int root_column_max_height:int "
                 "hanging_root_radius:int hanging_roots_vertical_span:int hanging_root_state_provider:provider "
                 "hanging_root_placement_attempts:int allowed_vertical_water_for_tree:int "
                 "allowed_tree_position:predicate level_test_distance:int max_level_deviation:int")},
        {K::Feature, TEXT("sculk_patch"),
            TEXT("charge_count:int amount_per_charge:int spread_attempts:int growth_rounds:int spread_rounds:int")},
        {K::Feature, TEXT("sequence"), TEXT("features:[placed]")},
        {K::Feature, TEXT("simple_block"), TEXT("to_place:provider schedule_tick?:bool")},
        {K::Feature, TEXT("simple_random_selector"), TEXT("features:[placed]")},
        {K::Feature, TEXT("speleothem"), TEXT("base_block:block pointed_block:block replaceable_blocks:htag")},
        {K::Feature, TEXT("speleothem_cluster"),
            TEXT("base_block:block pointed_block:block replaceable_blocks:htag floor_to_ceiling_search_range:int "
                 "height:int_provider radius:int_provider max_stalagmite_stalactite_height_diff:int "
                 "height_deviation:int speleothem_block_layer_thickness:int_provider density:float_provider "
                 "wetness:float_provider chance_of_speleothem_at_max_distance_from_center:float "
                 "max_distance_from_edge_affecting_chance_of_speleothem:int "
                 "max_distance_from_center_affecting_height_bias:int")},
        {K::Feature, TEXT("spike"), TEXT("state:block can_place_on:predicate can_replace:predicate")},
        {K::Feature, TEXT("spring_feature"), TEXT("state:state valid_blocks:htag/block/[block]")},
        {K::Feature, TEXT("template"), TEXT("templates:[@weighted_template] processors?:@template_processors")},
        {K::Feature, TEXT("tree"),
            TEXT("trunk_provider:provider trunk_placer:trunk_placer foliage_provider:provider "
                 "foliage_placer:foliage_placer root_placer?:root_placer below_trunk_provider:provider "
                 "minimum_size:feature_size decorators:[decorator] ignore_vines:bool")},
        {K::Feature, TEXT("underwater_magma"),
            TEXT("floor_search_range:int placement_radius_around_floor:int "
                 "placement_probability_per_valid_position:float")},
        {K::Feature, TEXT("vegetation_patch"), VEGETATION_PATCH},
        {K::Feature, TEXT("vines"), TEXT("")},
        {K::Feature, TEXT("waterlogged_vegetation_patch"), VEGETATION_PATCH},
        {K::Feature, TEXT("weighted_random_selector"), TEXT("features:[@weighted_placed]")},

        // Placement modifiers.
        {K::Modifier, TEXT("biome"), TEXT("")},
        {K::Modifier, TEXT("block_predicate_filter"), TEXT("predicate:predicate")},
        {K::Modifier, TEXT("count"), TEXT("count:int_provider")},
        {K::Modifier, TEXT("cuboid"),
            TEXT("xz_size:int_provider y_size:int_provider include_edges:bool include_interior:bool")},
        {K::Modifier, TEXT("environment_scan"),
            TEXT("direction_of_search:(up|down) target_condition:predicate allowed_search_condition?:predicate "
                 "max_steps:int")},
        {K::Modifier, TEXT("height_range"), TEXT("height:height_provider")},
        {K::Modifier, TEXT("heightmap"), TEXT("heightmap:") HEIGHTMAPS},
        {K::Modifier, TEXT("in_square"), TEXT("")},
        {K::Modifier, TEXT("noise_based_count"),
            TEXT("noise_to_count_ratio:int noise_factor:float noise_offset?:float")},
        {K::Modifier, TEXT("noise_threshold_count"), TEXT("noise_level:float below_noise:int above_noise:int")},
        {K::Modifier, TEXT("offset"), TEXT("x:int_provider y:int_provider z:int_provider")},
        {K::Modifier, TEXT("random_chance"), TEXT("chance:float")},
        {K::Modifier, TEXT("randomly_selected"), TEXT("placements:[modifier]")},
        {K::Modifier, TEXT("rarity_filter"), TEXT("chance:int")},
        {K::Modifier, TEXT("surface_relative_threshold_filter"),
            TEXT("heightmap:") HEIGHTMAPS TEXT(" min_inclusive?:int max_inclusive?:int")},
        {K::Modifier, TEXT("surface_water_depth_filter"), TEXT("max_water_depth:int")},

        // Block predicates.
        {K::BlockPredicate, TEXT("all_of"), TEXT("predicates:[predicate]")},
        {K::BlockPredicate, TEXT("any_of"), TEXT("predicates:[predicate]")},
        {K::BlockPredicate, TEXT("has_sturdy_face"), TEXT("direction:") DIRECTIONS TEXT(" offset?:[int]")},
        {K::BlockPredicate, TEXT("inside_world_bounds"), TEXT("offset:[int]")},
        {K::BlockPredicate, TEXT("matching_block_tag"), TEXT("tag:tag offset?:[int]")},
        {K::BlockPredicate, TEXT("matching_blocks"), TEXT("blocks:htag/block/[block] offset?:[int]")},
        {K::BlockPredicate, TEXT("matching_fluids"), TEXT("fluids:id/[id] offset?:[int]")},
        {K::BlockPredicate, TEXT("not"), TEXT("predicate:predicate")},
        {K::BlockPredicate, TEXT("replaceable"), TEXT("offset?:[int]")},
        {K::BlockPredicate, TEXT("solid"), TEXT("offset?:[int]")},
        {K::BlockPredicate, TEXT("true"), TEXT("")},
        {K::BlockPredicate, TEXT("volume_match"), TEXT("match:predicate min:[int] max:[int]")},
        {K::BlockPredicate, TEXT("would_survive"), TEXT("state:block/state offset?:[int]")},

        // Rule tests (ore targets, template rules).
        {K::RuleTest, TEXT("all_of"), TEXT("rules:[rule_test]")},
        {K::RuleTest, TEXT("always_true"), TEXT("")},
        {K::RuleTest, TEXT("any_of"), TEXT("rules:[rule_test]")},
        {K::RuleTest, TEXT("block_match"), TEXT("block:block")},
        {K::RuleTest, TEXT("height_match"), TEXT("min_inclusive:int max_inclusive:int")},
        {K::RuleTest, TEXT("not"), TEXT("rule:rule_test")},
        {K::RuleTest, TEXT("tag_match"), TEXT("tag:tag")},

        // Block state providers.
        {K::StateProvider, TEXT("dual_noise"),
            TEXT("seed:int noise:@noise_parameters scale:float states:[block/state] variety:[int] "
                 "slow_noise:@noise_parameters slow_scale:float")},
        {K::StateProvider, TEXT("noise"), TEXT("seed:int noise:@noise_parameters scale:float states:[block/state]")},
        {K::StateProvider, TEXT("noise_threshold"),
            TEXT("seed:int noise:@noise_parameters scale:float threshold:float high_chance:float "
                 "default_state:block/state low_states:[block/state] high_states:[block/state]")},
        {K::StateProvider, TEXT("random_block"), TEXT("blocks:htag/block/[block]")},
        {K::StateProvider, TEXT("randomized_int"), TEXT("property:str source:provider values:int_provider")},
        {K::StateProvider, TEXT("rotated"), TEXT("state:provider direction?:(north|south|west|east)")},
        {K::StateProvider, TEXT("rule_based"), TEXT("rules:[@provider_rule] fallback?:provider")},
        {K::StateProvider, TEXT("weighted"), TEXT("entries:[@weighted_state]")},

        // Number providers.
        {K::IntProvider, TEXT("biased_to_bottom"), TEXT("min_inclusive:int max_inclusive:int")},
        {K::IntProvider, TEXT("clamped"), TEXT("source:int_provider min_inclusive:int max_inclusive:int")},
        {K::IntProvider, TEXT("clamped_normal"),
            TEXT("mean:float deviation:float min_inclusive:int max_inclusive:int")},
        {K::IntProvider, TEXT("trapezoid"), TEXT("min:int max:int plateau:int")},
        {K::IntProvider, TEXT("uniform"), TEXT("min_inclusive:int max_inclusive:int")},
        {K::IntProvider, TEXT("very_biased_to_bottom"), TEXT("min_inclusive:int max_inclusive:int inner?:int")},
        {K::IntProvider, TEXT("weighted_list"), TEXT("distribution:[@weighted_int]")},
        {K::FloatProvider, TEXT("clamped_normal"), TEXT("mean:float deviation:float min:float max:float")},
        {K::FloatProvider, TEXT("trapezoid"), TEXT("min:float max:float plateau:float")},
        {K::FloatProvider, TEXT("uniform"), TEXT("min_inclusive:float max_exclusive:float")},
        {K::HeightProvider, TEXT("trapezoid"), TEXT("min_inclusive:anchor max_inclusive:anchor plateau?:int")},
        {K::HeightProvider, TEXT("uniform"), TEXT("min_inclusive:anchor max_inclusive:anchor")},
        {K::HeightProvider, TEXT("very_biased_to_bottom"),
            TEXT("min_inclusive:anchor max_inclusive:anchor inner?:int")},

        // Trees.
        {K::TrunkPlacer, TEXT("bending_trunk_placer"),
            TRUNK TEXT(" bend_length:int_provider min_height_for_leaves:int")},
        {K::TrunkPlacer, TEXT("cherry_trunk_placer"),
            TRUNK TEXT(" branch_count:int_provider branch_horizontal_length:int_provider "
                       "branch_start_offset_from_top:@int_range branch_end_offset_from_top:int_provider")},
        {K::TrunkPlacer, TEXT("dark_oak_trunk_placer"), TRUNK},
        {K::TrunkPlacer, TEXT("fancy_trunk_placer"), TRUNK},
        {K::TrunkPlacer, TEXT("forking_trunk_placer"), TRUNK},
        {K::TrunkPlacer, TEXT("giant_trunk_placer"), TRUNK},
        {K::TrunkPlacer, TEXT("mega_jungle_trunk_placer"), TRUNK},
        {K::TrunkPlacer, TEXT("poplar_trunk_placer"),
            TRUNK TEXT(" branch_amount:int_provider trunk_height_above_branches:int")},
        {K::TrunkPlacer, TEXT("straight_trunk_placer"), TRUNK},
        {K::TrunkPlacer, TEXT("upwards_branching_trunk_placer"),
            TRUNK TEXT(" extra_branch_steps:int_provider place_branch_per_log_probability:float "
                       "extra_branch_length:int_provider can_grow_through:htag")},
        {K::FoliagePlacer, TEXT("acacia_foliage_placer"), FOLIAGE},
        {K::FoliagePlacer, TEXT("blob_foliage_placer"), FOLIAGE TEXT(" height:int")},
        {K::FoliagePlacer, TEXT("bush_foliage_placer"), FOLIAGE TEXT(" height:int")},
        {K::FoliagePlacer, TEXT("cherry_foliage_placer"),
            FOLIAGE TEXT(" height:int_provider wide_bottom_layer_hole_chance:float corner_hole_chance:float "
                         "hanging_leaves_chance:float hanging_leaves_extension_chance:float")},
        {K::FoliagePlacer, TEXT("dark_oak_foliage_placer"), FOLIAGE},
        {K::FoliagePlacer, TEXT("fancy_foliage_placer"), FOLIAGE TEXT(" height:int")},
        {K::FoliagePlacer, TEXT("jungle_foliage_placer"), FOLIAGE TEXT(" height:int")},
        {K::FoliagePlacer, TEXT("mega_pine_foliage_placer"), FOLIAGE TEXT(" crown_height:int_provider")},
        {K::FoliagePlacer, TEXT("pine_foliage_placer"), FOLIAGE TEXT(" height:int_provider")},
        {K::FoliagePlacer, TEXT("poplar_foliage_placer"), FOLIAGE TEXT(" height:int_provider side_hole_chance:float")},
        {K::FoliagePlacer, TEXT("random_spread_foliage_placer"),
            FOLIAGE TEXT(" foliage_height:int_provider leaf_placement_attempts:int")},
        {K::FoliagePlacer, TEXT("spruce_foliage_placer"), FOLIAGE TEXT(" trunk_height:int_provider")},
        {K::RootPlacer, TEXT("mangrove_root_placer"),
            TEXT("root_provider:provider trunk_offset_y:int_provider above_root_placement?:@above_root_placement "
                 "mangrove_root_placement:@mangrove_root_placement")},
        {K::FeatureSize, TEXT("three_layers_feature_size"),
            TEXT("limit?:int upper_limit?:int lower_size?:int middle_size?:int upper_size?:int "
                 "min_clipped_height?:int")},
        {K::FeatureSize, TEXT("two_layers_feature_size"),
            TEXT("limit?:int lower_size?:int upper_size?:int min_clipped_height?:int")},
        {K::TreeDecorator, TEXT("alter_ground"), TEXT("provider:provider")},
        {K::TreeDecorator, TEXT("attached_to_leaves"),
            TEXT("probability:float exclusion_radius_xz:int exclusion_radius_y:int block_provider:provider "
                 "required_empty_blocks:int directions:[") DIRECTIONS TEXT("]")},
        {K::TreeDecorator, TEXT("attached_to_logs"),
            TEXT("probability:float block_provider:provider directions:[") DIRECTIONS TEXT("]")},
        {K::TreeDecorator, TEXT("beehive"), TEXT("probability:float")},
        {K::TreeDecorator, TEXT("cocoa"), TEXT("probability:float")},
        {K::TreeDecorator, TEXT("creaking_heart"), TEXT("probability:float")},
        {K::TreeDecorator, TEXT("leave_vine"), TEXT("probability:float")},
        {K::TreeDecorator, TEXT("pale_moss"),
            TEXT("leaves_probability:float trunk_probability:float ground_probability:float")},
        {K::TreeDecorator, TEXT("place_on_ground"),
            TEXT("tries:int radius?:int height:int block_state_provider:provider")},
        {K::TreeDecorator, TEXT("shelf_mushroom"), TEXT("probability:float")},
        {K::TreeDecorator, TEXT("trunk_vine"), TEXT("")},

        // Carvers, template processors.
        {K::Carver, TEXT("canyon"),
            TEXT("probability:float y:height_provider vertical_rotation:float_provider shape:@canyon_shape")},
        {K::Carver, TEXT("cave"),
            TEXT("probability:float y:height_provider count:int_provider floor_level:float_provider "
                 "horizontal_radius_multiplier:float_provider vertical_radius_multiplier:float_provider "
                 "room_vertical_radius_multiplier:float_provider thickness:float_provider weird_thickness_bias?:bool")},
        {K::Processor, TEXT("rule"), TEXT("rules:[@processor_rule]")},
        {K::BlockEntityModifier, TEXT("append_loot"), TEXT("loot_table:id")},
    };
#undef DIRECTIONS
#undef HEIGHTMAPS
#undef TRUNK
#undef FOLIAGE
#undef VEGETATION_PATCH

    // The tags the game's code reads by name (build_reference.py's CODE_TAGS).
    const TCHAR* const CodeTags[] = {TEXT("replaceable"), TEXT("replaceable_by_trees"), TEXT("features_cannot_replace"),
        TEXT("logs"), TEXT("blocks_motion_in_heightmap"), TEXT("blocks_motion_in_heightmap_no_leaves"),
        TEXT("supports_vegetation"), TEXT("supports_sugar_cane"), TEXT("supports_sugar_cane_adjacently"),
        TEXT("supports_cactus"), TEXT("supports_lily_pad"), TEXT("supports_bamboo"), TEXT("supports_azalea"),
        TEXT("supports_big_dripleaf"), TEXT("supports_small_dripleaf"), TEXT("supports_cocoa"),
        TEXT("supports_dry_vegetation"), TEXT("supports_mangrove_propagule"),
        TEXT("supports_hanging_mangrove_propagule"), TEXT("cannot_support_kelp"), TEXT("cannot_support_seagrass"),
        TEXT("cannot_support_snow_layer"), TEXT("support_override_snow_layer"), TEXT("support_override_cactus_flower")};

    enum class EBase : uint8
    {
        Int,
        Float,
        Bool,
        Str,
        Block,
        Id,
        Tag,
        HashTag,
        Enum,
        Kind,
        Object,
    };

    struct FAlternative
    {
        bool bList = false;
        EBase Base = EBase::Int;
        EVoxelFeatureKind Kind = EVoxelFeatureKind::Object;
        // The object schema (Object) or the words (Enum).
        TArray<FString> Names;
    };

    struct FFieldSchema
    {
        FString Name;
        bool bOptional = false;
        TArray<FAlternative> Alternatives;
    };

    struct FSchema
    {
        EVoxelFeatureKind Kind = EVoxelFeatureKind::Object;
        FString Name;
        TArray<FFieldSchema> Fields;
    };

    const TCHAR* const KindWords[] = {TEXT("placed"), TEXT("feature"), TEXT("modifier"), TEXT("predicate"),
        TEXT("rule_test"), TEXT("provider"), TEXT("state"), TEXT("int_provider"), TEXT("float_provider"),
        TEXT("height_provider"), TEXT("anchor"), TEXT("trunk_placer"), TEXT("foliage_placer"), TEXT("root_placer"),
        TEXT("feature_size"), TEXT("decorator"), TEXT("carver"), TEXT("processor"), TEXT("entity_modifier"),
        TEXT("props")};
    static_assert(UE_ARRAY_COUNT(KindWords) == int32(EVoxelFeatureKind::Object));

    // The key that holds a typed kind's type, or null for the untyped kinds.
    const TCHAR* GetTypeKey(EVoxelFeatureKind Kind)
    {
        switch (Kind)
        {
        case EVoxelFeatureKind::PlacedFeature:
        case EVoxelFeatureKind::BlockState:
        case EVoxelFeatureKind::VerticalAnchor:
        case EVoxelFeatureKind::Properties:
        case EVoxelFeatureKind::Object:
            return nullptr;
        case EVoxelFeatureKind::RuleTest:
            return TEXT("predicate_type");
        case EVoxelFeatureKind::Processor:
            return TEXT("processor_type");
        default:
            return TEXT("type");
        }
    }

    const TCHAR* const AnchorKeys[] = {
        TEXT("absolute"), TEXT("above_bottom"), TEXT("below_top"), TEXT("relative_to_sea_level")};

    // The schema table, parsed once.
    struct FSchemas
    {
        TMap<FString, FSchema> ByKind[int32(EVoxelFeatureKind::Count)];
        FString Error;

        static const FSchemas& Get()
        {
            static const FSchemas Parsed;
            return Parsed;
        }

        const FSchema* Find(EVoxelFeatureKind Kind, const FString& Name) const
        {
            return ByKind[int32(Kind)].Find(Name);
        }

    private:
        // Splits at "/" outside brackets and parentheses.
        static TArray<FString> SplitTopLevel(const FString& Text)
        {
            TArray<FString> Parts;
            int32 Depth = 0;
            int32 Start = 0;
            for (int32 Index = 0; Index <= Text.Len(); ++Index)
            {
                const TCHAR Char = Index < Text.Len() ? Text[Index] : TEXT('/');
                if (Char == TEXT('[') || Char == TEXT('(')) ++Depth;
                if (Char == TEXT(']') || Char == TEXT(')')) --Depth;
                if (Char == TEXT('/') && Depth == 0)
                {
                    Parts.Add(Text.Mid(Start, Index - Start));
                    Start = Index + 1;
                }
            }
            return Parts;
        }

        FSchemas()
        {
            for (const FSchemaSource& Source : Sources)
            {
                FSchema Schema{Source.Kind, Source.Name, {}};
                TArray<FString> Words;
                FString(Source.Fields).ParseIntoArrayWS(Words);
                for (const FString& Word : Words)
                {
                    FString Name, Kinds;
                    if (!Word.Split(TEXT(":"), &Name, &Kinds))
                    {
                        Error = FString::Printf(TEXT("schema %s: field without kinds: %s"), Source.Name, *Word);
                        return;
                    }
                    FFieldSchema Field;
                    Field.bOptional = Name.RemoveFromEnd(TEXT("?"));
                    Field.Name = Name;
                    // "[a/b]" is a list whose items are a or b: one list alternative per item kind.
                    TArray<TPair<FString, bool>> Parts;
                    for (FString Part : SplitTopLevel(Kinds))
                    {
                        if (Part.StartsWith(TEXT("[")) && Part.EndsWith(TEXT("]")))
                        {
                            for (const FString& Item : SplitTopLevel(Part.Mid(1, Part.Len() - 2)))
                                Parts.Add({Item, true});
                        }
                        else
                        {
                            Parts.Add({Part, false});
                        }
                    }
                    for (const TPair<FString, bool>& Pair : Parts)
                    {
                        const FString& Part = Pair.Key;
                        FAlternative Alternative;
                        Alternative.bList = Pair.Value;
                        static const TMap<FString, EBase> Plain = {{TEXT("int"), EBase::Int},
                            {TEXT("float"), EBase::Float}, {TEXT("bool"), EBase::Bool}, {TEXT("str"), EBase::Str},
                            {TEXT("block"), EBase::Block}, {TEXT("id"), EBase::Id}, {TEXT("tag"), EBase::Tag},
                            {TEXT("htag"), EBase::HashTag}};
                        const int32 Kind = Algo::IndexOf(KindWords, Part);
                        if (const EBase* Found = Plain.Find(Part))
                        {
                            Alternative.Base = *Found;
                        }
                        else if (Kind != INDEX_NONE)
                        {
                            Alternative.Base = EBase::Kind;
                            Alternative.Kind = EVoxelFeatureKind(Kind);
                        }
                        else if (Part.StartsWith(TEXT("@")))
                        {
                            Alternative.Base = EBase::Object;
                            Alternative.Names.Add(Part.RightChop(1));
                        }
                        else if (Part.StartsWith(TEXT("(")) && Part.EndsWith(TEXT(")")))
                        {
                            Alternative.Base = EBase::Enum;
                            Part.Mid(1, Part.Len() - 2).ParseIntoArray(Alternative.Names, TEXT("|"));
                        }
                        else
                        {
                            Error = FString::Printf(TEXT("schema %s: unknown kind %s"), Source.Name, *Part);
                            return;
                        }
                        Field.Alternatives.Add(MoveTemp(Alternative));
                    }
                    Schema.Fields.Add(MoveTemp(Field));
                }
                if (ByKind[int32(Source.Kind)].Contains(Source.Name))
                {
                    Error = FString::Printf(TEXT("schema %s listed twice"), Source.Name);
                    return;
                }
                ByKind[int32(Source.Kind)].Add(Source.Name, MoveTemp(Schema));
            }
            // Every object schema a field names exists.
            for (const TMap<FString, FSchema>& Kind : ByKind)
                for (const TPair<FString, FSchema>& Pair : Kind)
                    for (const FFieldSchema& Field : Pair.Value.Fields)
                        for (const FAlternative& Alternative : Field.Alternatives)
                            if (Alternative.Base == EBase::Object &&
                                !Find(EVoxelFeatureKind::Object, Alternative.Names[0]))
                                Error = FString::Printf(
                                    TEXT("schema %s: no object schema %s"), *Pair.Value.Name, *Alternative.Names[0]);
        }
    };

    // An id as the reference writes one: "minecraft:" and a lowercase path.
    bool IsWorldgenId(FStringView Text)
    {
        if (!Text.StartsWith(WorldgenIdPrefix, ESearchCase::CaseSensitive)) return false;
        const FStringView Name = Text.RightChop(FCString::Strlen(WorldgenIdPrefix));
        if (Name.IsEmpty() || Name.StartsWith(TEXT('/')) || Name.Contains(TEXT(".."))) return false;
        for (const TCHAR Char : Name)
        {
            if (!(FChar::IsLower(Char) || FChar::IsDigit(Char) || Char == TEXT('_') || Char == TEXT('-') ||
                    Char == TEXT('/') || Char == TEXT('.')))
                return false;
        }
        return true;
    }

    bool KeyIs(FStringView Key, const TCHAR* Name)
    {
        return Key.Equals(Name, ESearchCase::CaseSensitive);
    }

    const TCHAR* DescribeJson(const FJsonValue& Value)
    {
        switch (Value.Type)
        {
        case EJson::Number:
            return TEXT("a number");
        case EJson::String:
            return TEXT("a string");
        case EJson::Boolean:
            return TEXT("a bool");
        case EJson::Array:
            return TEXT("a list");
        case EJson::Object:
            return TEXT("an object");
        default:
            return TEXT("null");
        }
    }

    class FFeatureReader
    {
    public:
        FFeatureReader(VoxelWorldgen::FLoadFile InLoadFile, FVoxelFeatureGraph& InGraph,
            const TMap<FString, TSharedPtr<FJsonValue>>* InParsed = nullptr)
            : LoadFile(InLoadFile), Graph(InGraph), Parsed(InParsed)
        {
        }

        bool Run(FString& OutError)
        {
            const FSchemas& Schemas = FSchemas::Get();
            if (!Schemas.Error.IsEmpty())
            {
                OutError = TEXT("the feature schema is broken: ") + Schemas.Error;
                return false;
            }
            if (!ReadBiomes())
            {
                OutError = Error;
                return false;
            }
            for (const TCHAR* Tag : CodeTags)
                AddTag(FString(WorldgenIdPrefix) + Tag);
            // Entries and tags are read as they are first named, until nothing new is named.
            while (PendingEntries.Num() || PendingTags.Num())
            {
                if (PendingEntries.Num())
                {
                    const TPair<EVoxelFeatureRegistry, int32> Next = PendingEntries.Pop(EAllowShrinking::No);
                    if (!ReadEntry(Next.Key, Next.Value))
                    {
                        OutError = Error;
                        return false;
                    }
                }
                else if (!ReadTag(PendingTags.Pop(EAllowShrinking::No)))
                {
                    OutError = Error;
                    return false;
                }
            }
            FString Invalid;
            if (!Graph.Validate(&Invalid))
            {
                OutError = TEXT("the feature graph is invalid: ") + Invalid;
                return false;
            }
            return true;
        }

    private:
        // A field name or a list index on the way to the value being read.
        struct FPathPart
        {
            FStringView Key;
            int32 Index = INDEX_NONE;
        };

        VoxelWorldgen::FLoadFile LoadFile;
        FVoxelFeatureGraph& Graph;
        // Files already parsed, by path; others are loaded.
        const TMap<FString, TSharedPtr<FJsonValue>>* Parsed;
        TArray<TPair<EVoxelFeatureRegistry, int32>> PendingEntries;
        TArray<int32> PendingTags;
        TSet<int32> QueuedEntries[int32(EVoxelFeatureRegistry::Count)];
        TSet<int32> QueuedTags;
        FString File;
        TArray<FPathPart> Path;
        FString Error;

        bool Fail(const FString& Message)
        {
            FString Where;
            for (const FPathPart& Part : Path)
            {
                if (Part.Index != INDEX_NONE)
                    Where += FString::Printf(TEXT("[%d]"), Part.Index);
                else
                    Where += (Where.IsEmpty() ? TEXT("") : TEXT(".")) + FString(Part.Key);
            }
            Error = Where.IsEmpty() ? FString::Printf(TEXT("%s: %s"), *File, *Message)
                                    : FString::Printf(TEXT("%s: %s: %s"), *File, *Where, *Message);
            return false;
        }

        bool Load(const FString& RelativePath, TSharedPtr<FJsonValue>& Out)
        {
            File = RelativePath;
            Path.Reset();
            if (const TSharedPtr<FJsonValue>* Found = Parsed ? Parsed->Find(RelativePath) : nullptr)
            {
                Out = *Found;
                return Out.IsValid() || Fail(TEXT("not one JSON value"));
            }
            FString Text;
            if (!LoadFile(RelativePath, Text)) return Fail(TEXT("cannot read the file"));
            Out = ParseWorldgenJson(Text);
            return Out.IsValid() || Fail(TEXT("not one JSON value"));
        }

        bool IdPath(const FString& Id, const TCHAR* Folder, FString& OutPath)
        {
            return WorldgenIdToPath(Id, Folder, OutPath) || Fail(FString::Printf(TEXT("bad id \"%s\""), *Id));
        }

        bool ReadBiomes()
        {
            TSharedPtr<FJsonValue> Json;
            if (!Load(TEXT("tags/worldgen/biome/is_overworld.json"), Json)) return false;
            const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
            const TSharedPtr<FJsonObject>* Object = nullptr;
            FString Unexpected;
            if (!Json->TryGetObject(Object) || !(*Object)->TryGetArrayField(TEXT("values"), Values) ||
                FindUnexpectedWorldgenKey(**Object, {TEXT("values")}, Unexpected))
                return Fail(TEXT("expected {\"values\": [...]}"));
            const FString TagFile = File;
            TSet<FString> Seen;
            for (int32 Index = 0; Index < Values->Num(); ++Index)
            {
                const TSharedPtr<FJsonValue>& Value = (*Values)[Index];
                FString Biome, BiomePath;
                File = TagFile;
                Path.Reset();
                if (Value->Type != EJson::String || !IdPath(Value->AsString(), TEXT("worldgen/biome"), BiomePath) ||
                    Seen.Contains(Value->AsString()))
                    return Fail(FString::Printf(TEXT("values[%d]: expected biome ids, each once"), Index));
                Biome = Value->AsString();
                Seen.Add(Biome);
                TSharedPtr<FJsonValue> BiomeJson;
                if (!Load(BiomePath, BiomeJson)) return false;
                FVoxelBiomeDecoration& Decoration = Graph.Biomes.AddDefaulted_GetRef();
                Decoration.Biome = Biome;
                const TSharedPtr<FJsonObject>* BiomeObject = nullptr;
                const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
                const TArray<TSharedPtr<FJsonValue>>* Carvers = nullptr;
                if (!BiomeJson->TryGetObject(BiomeObject)) return Fail(TEXT("expected an object"));
                if (!(*BiomeObject)->TryGetArrayField(TEXT("features"), Steps) ||
                    Steps->Num() != FVoxelFeatureGraph::StepCount)
                    return Fail(FString::Printf(TEXT("features: expected %d lists"), FVoxelFeatureGraph::StepCount));
                if (!(*BiomeObject)->TryGetArrayField(TEXT("carvers"), Carvers))
                    return Fail(TEXT("carvers: expected a list"));
                for (int32 Step = 0; Step < Steps->Num(); ++Step)
                {
                    const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
                    if (!(*Steps)[Step]->TryGetArray(List))
                        return Fail(FString::Printf(TEXT("features[%d]: expected a list"), Step));
                    TArray<int32>& Entries = Decoration.Steps.AddDefaulted_GetRef();
                    for (const TSharedPtr<FJsonValue>& Feature : *List)
                    {
                        FString Id;
                        if (!Feature->TryGetString(Id) || !IsWorldgenId(Id))
                            return Fail(FString::Printf(TEXT("features[%d]: expected placed feature ids"), Step));
                        Entries.Add(AddEntry(EVoxelFeatureRegistry::PlacedFeature, Id));
                    }
                }
                for (const TSharedPtr<FJsonValue>& Carver : *Carvers)
                {
                    FString Id;
                    if (!Carver->TryGetString(Id) || !IsWorldgenId(Id)) return Fail(TEXT("carvers: expected ids"));
                    Decoration.Carvers.Add(AddEntry(EVoxelFeatureRegistry::Carver, Id));
                }
            }
            return true;
        }

        int32 AddEntry(EVoxelFeatureRegistry Registry, const FString& Id)
        {
            const int32 Entry = Graph.AddEntry(Registry, Id);
            if (!QueuedEntries[int32(Registry)].Contains(Entry))
            {
                QueuedEntries[int32(Registry)].Add(Entry);
                PendingEntries.Add({Registry, Entry});
            }
            return Entry;
        }

        int32 AddTag(const FString& Id)
        {
            const int32 Tag = Graph.AddTag(Id);
            if (!QueuedTags.Contains(Tag))
            {
                QueuedTags.Add(Tag);
                PendingTags.Add(Tag);
            }
            return Tag;
        }

        bool ReadEntry(EVoxelFeatureRegistry Registry, int32 Entry)
        {
            const FString Id = Graph.Entries[int32(Registry)][Entry].Id;
            FString FilePath;
            File = Id;
            Path.Reset();
            if (!IdPath(Id, *(FString(TEXT("worldgen/")) + FVoxelFeatureGraph::GetRegistryFolder(Registry)), FilePath))
                return false;
            TSharedPtr<FJsonValue> Json;
            if (!Load(FilePath, Json)) return false;
            static constexpr EVoxelFeatureKind RootKinds[] = {EVoxelFeatureKind::PlacedFeature,
                EVoxelFeatureKind::Feature, EVoxelFeatureKind::StateProvider, EVoxelFeatureKind::Carver};
            // A file holds the entry itself, never a name of another.
            if (Json->Type != EJson::Object) return Fail(TEXT("expected an object"));
            FVoxelFeatureValue Value;
            if (!ReadKind(*Json, RootKinds[int32(Registry)], Value)) return false;
            Graph.Entries[int32(Registry)][Entry].Node = Value.Index;
            return true;
        }

        bool ReadTag(int32 Tag)
        {
            FString FilePath;
            File = Graph.Tags[Tag].Id;
            Path.Reset();
            if (!IdPath(Graph.Tags[Tag].Id, TEXT("tags/block"), FilePath)) return false;
            TSharedPtr<FJsonValue> Json;
            if (!Load(FilePath, Json)) return false;
            const TSharedPtr<FJsonObject>* Object = nullptr;
            const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
            FString Unexpected;
            if (!Json->TryGetObject(Object) || !(*Object)->TryGetArrayField(TEXT("values"), Values) ||
                FindUnexpectedWorldgenKey(**Object, {TEXT("values")}, Unexpected))
                return Fail(TEXT("expected {\"values\": [...]}"));
            TArray<FString> Read;
            for (int32 Index = 0; Index < Values->Num(); ++Index)
            {
                const FString Value =
                    (*Values)[Index]->Type == EJson::String ? (*Values)[Index]->AsString() : FString();
                const bool bInclude = Value.StartsWith(TEXT("#"));
                if (!IsWorldgenId(bInclude ? Value.RightChop(1) : Value))
                    return Fail(FString::Printf(TEXT("values[%d]: expected a block id or a #tag"), Index));
                if (bInclude) AddTag(Value.RightChop(1));
                Read.Add(Value);
            }
            Graph.Tags[Tag].Values = MoveTemp(Read);
            return true;
        }

        // One JSON value against a field's kinds: a list kind takes a JSON list, any other the first kind whose shape
        // fits.
        bool ReadField(const FJsonValue& Json, const FFieldSchema& Field, FVoxelFeatureValue& Out)
        {
            if (Json.Type != EJson::Array)
            {
                for (const FAlternative& Alternative : Field.Alternatives)
                    if (!Alternative.bList && Fits(Json, Alternative)) return ReadOne(Json, Alternative, Out);
                return Fail(FString::Printf(TEXT("unexpected %s"), DescribeJson(Json)));
            }
            if (!Field.Alternatives.ContainsByPredicate(
                    [](const FAlternative& Alternative) { return Alternative.bList; }))
                return Fail(TEXT("unexpected a list"));
            TArray<FVoxelFeatureValue> List;
            const TArray<TSharedPtr<FJsonValue>>& Items = Json.AsArray();
            for (int32 Index = 0; Index < Items.Num(); ++Index)
            {
                Path.Add({FStringView(), Index});
                const FAlternative* Fitting =
                    Field.Alternatives.FindByPredicate([&Items, Index](const FAlternative& Alternative)
                        { return Alternative.bList && Fits(*Items[Index], Alternative); });
                if (!Fitting) return Fail(FString::Printf(TEXT("unexpected %s"), DescribeJson(*Items[Index])));
                if (!ReadOne(*Items[Index], *Fitting, List.AddDefaulted_GetRef())) return false;
                Path.Pop(EAllowShrinking::No);
            }
            Out.Type = EVoxelFeatureValueType::List;
            Out.Index = Graph.Items.Num();
            Out.Count = List.Num();
            Graph.Items.Append(List);
            return true;
        }

        static bool Fits(const FJsonValue& Json, const FAlternative& Alternative)
        {
            switch (Alternative.Base)
            {
            case EBase::Int:
            case EBase::Float:
                return Json.Type == EJson::Number;
            case EBase::Bool:
                return Json.Type == EJson::Boolean;
            case EBase::HashTag:
                return Json.Type == EJson::String && Json.AsString().StartsWith(TEXT("#"));
            case EBase::Str:
            case EBase::Block:
            case EBase::Id:
            case EBase::Tag:
            case EBase::Enum:
                return Json.Type == EJson::String && !Json.AsString().StartsWith(TEXT("#"));
            case EBase::Object:
                return Json.Type == EJson::Object;
            case EBase::Kind:
                switch (Alternative.Kind)
                {
                case EVoxelFeatureKind::PlacedFeature:
                case EVoxelFeatureKind::Feature:
                case EVoxelFeatureKind::StateProvider:
                    return Json.Type == EJson::Object ||
                        (Json.Type == EJson::String && !Json.AsString().StartsWith(TEXT("#")));
                case EVoxelFeatureKind::IntProvider:
                case EVoxelFeatureKind::FloatProvider:
                    return Json.Type == EJson::Object || Json.Type == EJson::Number;
                default:
                    return Json.Type == EJson::Object;
                }
            }
            return false;
        }

        bool ReadOne(const FJsonValue& Json, const FAlternative& Alternative, FVoxelFeatureValue& Out)
        {
            switch (Alternative.Base)
            {
            case EBase::Int:
            {
                const double Number = Json.AsNumber();
                if (Number != FMath::RoundToDouble(Number) || FMath::Abs(Number) > double(MAX_int32))
                    return Fail(FString::Printf(TEXT("expected an int, found %g"), Number));
                Out.Type = EVoxelFeatureValueType::Number;
                Out.Number = Number;
                return true;
            }
            case EBase::Float:
                Out.Type = EVoxelFeatureValueType::Number;
                Out.Number = Json.AsNumber();
                return true;
            case EBase::Bool:
                Out.Type = EVoxelFeatureValueType::Bool;
                Out.Number = Json.AsBool() ? 1 : 0;
                return true;
            case EBase::Enum:
                if (!Alternative.Names.ContainsByPredicate([&Json](const FString& Name)
                        { return Name.Equals(Json.AsString(), ESearchCase::CaseSensitive); }))
                    return Fail(FString::Printf(TEXT("unexpected value \"%s\""), *Json.AsString()));
                return ReadString(Json.AsString(), Out);
            case EBase::Str:
                return ReadString(Json.AsString(), Out);
            case EBase::Block:
            case EBase::Id:
                if (!IsWorldgenId(Json.AsString()))
                    return Fail(FString::Printf(TEXT("bad id \"%s\""), *Json.AsString()));
                return ReadString(Json.AsString(), Out);
            case EBase::Tag:
            case EBase::HashTag:
            {
                const FString Id = Alternative.Base == EBase::Tag ? Json.AsString() : Json.AsString().RightChop(1);
                if (!IsWorldgenId(Id)) return Fail(FString::Printf(TEXT("bad tag \"%s\""), *Json.AsString()));
                AddTag(Id);
                return ReadString(Json.AsString(), Out);
            }
            case EBase::Object:
            {
                const FSchema* Schema = FSchemas::Get().Find(EVoxelFeatureKind::Object, Alternative.Names[0]);
                return ReadObject(*Json.AsObject(), *Schema, nullptr, Out);
            }
            case EBase::Kind:
                return ReadKind(Json, Alternative.Kind, Out);
            }
            return Fail(TEXT("unknown kind"));
        }

        bool ReadString(const FString& Text, FVoxelFeatureValue& Out)
        {
            Out.Type = EVoxelFeatureValueType::String;
            Out.Index = Graph.AddString(Text);
            return true;
        }

        // A node of Kind: a name of an entry, a number for a provider constant, or an object.
        bool ReadKind(const FJsonValue& Json, EVoxelFeatureKind Kind, FVoxelFeatureValue& Out)
        {
            if (Json.Type == EJson::Number)
            {
                // An int or float provider's constant.
                if (Kind == EVoxelFeatureKind::IntProvider)
                    return ReadOne(Json, FAlternative{false, EBase::Int, Kind, {}}, Out);
                return ReadOne(Json, FAlternative{false, EBase::Float, Kind, {}}, Out);
            }
            if (Json.Type == EJson::String)
            {
                const EVoxelFeatureRegistry Registry = Kind == EVoxelFeatureKind::PlacedFeature
                    ? EVoxelFeatureRegistry::PlacedFeature
                    : Kind == EVoxelFeatureKind::Feature ? EVoxelFeatureRegistry::Feature
                                                         : EVoxelFeatureRegistry::Provider;
                if (!IsWorldgenId(Json.AsString()))
                    return Fail(FString::Printf(TEXT("bad id \"%s\""), *Json.AsString()));
                Out.Type = EVoxelFeatureValueType::Ref;
                Out.Registry = Registry;
                Out.Index = AddEntry(Registry, Json.AsString());
                return true;
            }
            const FJsonObject& Object = *Json.AsObject();
            const FSchemas& Schemas = FSchemas::Get();
            switch (Kind)
            {
            case EVoxelFeatureKind::PlacedFeature:
                return ReadObject(Object, *Schemas.Find(Kind, TEXT("placed")), nullptr, Out);
            case EVoxelFeatureKind::BlockState:
                return ReadObject(Object, *Schemas.Find(Kind, TEXT("state")), nullptr, Out);
            case EVoxelFeatureKind::Properties:
                return ReadProperties(Object, Out);
            case EVoxelFeatureKind::VerticalAnchor:
                return ReadAnchor(Object, Out);
            case EVoxelFeatureKind::StateProvider:
                // A bare block state is a provider of itself.
                if (!Object.HasField(TEXT("type")))
                    return ReadObject(
                        Object, *Schemas.Find(EVoxelFeatureKind::BlockState, TEXT("state")), nullptr, Out);
                break;
            case EVoxelFeatureKind::HeightProvider:
                // A bare anchor is a constant height.
                if (!Object.HasField(TEXT("type"))) return ReadAnchor(Object, Out);
                break;
            default:
                break;
            }
            const TCHAR* TypeKey = GetTypeKey(Kind);
            FString Type;
            if (!TypeKey || !Object.TryGetStringField(TypeKey, Type))
                return Fail(FString::Printf(TEXT("expected a %s with a \"%s\""), FVoxelFeatureGraph::GetKindName(Kind),
                    TypeKey ? TypeKey : TEXT("type")));
            const FSchema* Schema = Type.StartsWith(WorldgenIdPrefix, ESearchCase::CaseSensitive)
                ? Schemas.Find(Kind, Type.RightChop(FCString::Strlen(WorldgenIdPrefix)))
                : nullptr;
            // The schema maps find names whatever their case; a type matches only as written.
            if (!Schema ||
                !FStringView(Type)
                    .RightChop(FCString::Strlen(WorldgenIdPrefix))
                    .Equals(Schema->Name, ESearchCase::CaseSensitive))
                return Fail(
                    FString::Printf(TEXT("unknown %s type \"%s\""), FVoxelFeatureGraph::GetKindName(Kind), *Type));
            return ReadObject(Object, *Schema, TypeKey, Out);
        }

        // An object's fields against its schema: every field it holds is known, every field it needs is there.
        bool ReadObject(const FJsonObject& Object, const FSchema& Schema, const TCHAR* TypeKey, FVoxelFeatureValue& Out)
        {
            TArray<FVoxelFeatureField> Fields;
            for (const auto& Pair : Object.Values)
            {
                const FStringView Key = Pair.Key.ToView();
                if (TypeKey && KeyIs(Key, TypeKey)) continue;
                const FFieldSchema* Field = Schema.Fields.FindByPredicate(
                    [Key](const FFieldSchema& Candidate) { return KeyIs(Key, *Candidate.Name); });
                Path.Add({Key, INDEX_NONE});
                if (!Field)
                    return Fail(FString::Printf(
                        TEXT("unexpected field in %s %s"), FVoxelFeatureGraph::GetKindName(Schema.Kind), *Schema.Name));
                FVoxelFeatureField& Read = Fields.AddDefaulted_GetRef();
                Read.Key = Graph.AddString(Key);
                if (!ReadField(*Pair.Value, *Field, Read.Value)) return false;
                Path.Pop(EAllowShrinking::No);
            }
            for (const FFieldSchema& Field : Schema.Fields)
            {
                if (!Field.bOptional && !Object.HasField(Field.Name))
                    return Fail(FString::Printf(TEXT("missing field \"%s\" of %s %s"), *Field.Name,
                        FVoxelFeatureGraph::GetKindName(Schema.Kind), *Schema.Name));
            }
            FVoxelFeatureNode Node;
            Node.Kind = Schema.Kind;
            Node.Type = TypeKey                            ? Graph.AddString(FString(WorldgenIdPrefix) + Schema.Name)
                : Schema.Kind == EVoxelFeatureKind::Object ? Graph.AddString(Schema.Name)
                                                           : INDEX_NONE;
            return AddNode(Node, Fields, Out);
        }

        bool ReadProperties(const FJsonObject& Object, FVoxelFeatureValue& Out)
        {
            TArray<FVoxelFeatureField> Fields;
            for (const auto& Pair : Object.Values)
            {
                FString Text;
                if (!Pair.Value->TryGetString(Text) || Pair.Value->Type != EJson::String)
                {
                    Path.Add({Pair.Key.ToView(), INDEX_NONE});
                    return Fail(TEXT("expected a string"));
                }
                FVoxelFeatureField& Field = Fields.AddDefaulted_GetRef();
                Field.Key = Graph.AddString(Pair.Key.ToView());
                ReadString(Text, Field.Value);
            }
            return AddNode({EVoxelFeatureKind::Properties, INDEX_NONE, 0, 0}, Fields, Out);
        }

        bool ReadAnchor(const FJsonObject& Object, FVoxelFeatureValue& Out)
        {
            if (Object.Values.Num() != 1) return Fail(TEXT("expected a vertical anchor with one field"));
            const auto& Pair = *Object.Values.CreateConstIterator();
            const FStringView Key = Pair.Key.ToView();
            Path.Add({Key, INDEX_NONE});
            if (!Algo::FindByPredicate(AnchorKeys, [Key](const TCHAR* Name) { return KeyIs(Key, Name); }))
                return Fail(TEXT("unexpected field in vertical anchor"));
            if (Pair.Value->Type != EJson::Number) return Fail(TEXT("expected an int"));
            TArray<FVoxelFeatureField> Fields;
            FVoxelFeatureField& Field = Fields.AddDefaulted_GetRef();
            Field.Key = Graph.AddString(Key);
            if (!ReadOne(*Pair.Value, FAlternative{false, EBase::Int, EVoxelFeatureKind::Object, {}}, Field.Value))
                return false;
            Path.Pop(EAllowShrinking::No);
            return AddNode({EVoxelFeatureKind::VerticalAnchor, INDEX_NONE, 0, 0}, Fields, Out);
        }

        // Children are added before their parent, so a node's fields sit in one row after its children's.
        bool AddNode(FVoxelFeatureNode Node, const TArray<FVoxelFeatureField>& Fields, FVoxelFeatureValue& Out)
        {
            Node.FirstField = Graph.Fields.Num();
            Node.FieldCount = Fields.Num();
            Graph.Fields.Append(Fields);
            Out.Type = EVoxelFeatureValueType::Node;
            Out.Index = Graph.Nodes.Add(Node);
            return true;
        }
    };

    TSharedPtr<FJsonValue> WriteFeatureValue(const FVoxelFeatureGraph& Graph, const FVoxelFeatureValue& Value)
    {
        switch (Value.Type)
        {
        case EVoxelFeatureValueType::Number:
            return MakeShared<FJsonValueNumber>(Value.Number);
        case EVoxelFeatureValueType::Bool:
            return MakeShared<FJsonValueBoolean>(Value.Number != 0);
        case EVoxelFeatureValueType::String:
            return MakeShared<FJsonValueString>(Graph.Strings[Value.Index]);
        case EVoxelFeatureValueType::Node:
            return VoxelWorldgen::WriteFeatureNode(Graph, Value.Index);
        case EVoxelFeatureValueType::Ref:
            return MakeShared<FJsonValueString>(Graph.Entries[int32(Value.Registry)][Value.Index].Id);
        case EVoxelFeatureValueType::List:
        {
            TArray<TSharedPtr<FJsonValue>> Items;
            for (int32 Index = Value.Index; Index < Value.Index + Value.Count; ++Index)
                Items.Add(WriteFeatureValue(Graph, Graph.Items[Index]));
            return MakeShared<FJsonValueArray>(Items);
        }
        }
        return nullptr;
    }
}

bool VoxelWorldgen::ReadFeatures(FLoadFile LoadFile, FVoxelFeatureGraph& Out, FString& OutError)
{
    Out = FVoxelFeatureGraph();
    return FFeatureReader(LoadFile, Out).Run(OutError);
}

bool VoxelWorldgen::ReadFeatures(const FString& Root, FVoxelFeatureGraph& Out, FString& OutError)
{
    // Opening some 600 small files one after another costs more than reading them, so every file of the folders the
    // read draws on is loaded and parsed in parallel first. The read still asks for each file by name; any it asks for
    // that is not here is loaded then.
    const double Start = FPlatformTime::Seconds();
    const FString Data = FPaths::Combine(Root, TEXT("data/minecraft"));
    TArray<FString> Files = {TEXT("tags/worldgen/biome/is_overworld.json")};
    for (const TCHAR* Folder :
        {TEXT("worldgen/placed_feature"), TEXT("worldgen/feature"), TEXT("worldgen/block_state_provider"),
            TEXT("worldgen/carver"), TEXT("worldgen/biome"), TEXT("tags/block")})
    {
        TArray<FString> Found;
        const FString Directory = FPaths::Combine(Data, Folder);
        IFileManager::Get().FindFilesRecursive(Found, *Directory, TEXT("*.json"), true, false);
        for (FString& File : Found)
        {
            FPaths::MakePathRelativeTo(File, *(Directory + TEXT("/")));
            Files.Add(FString::Printf(TEXT("%s/%s"), Folder, *File));
        }
    }
    TArray<TSharedPtr<FJsonValue>> Values;
    Values.SetNum(Files.Num());
    ParallelFor(Files.Num(),
        [&Files, &Values, &Data](int32 Index)
        {
            FString Text;
            if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(Data, Files[Index])))
                Values[Index] = ParseWorldgenJson(Text);
        });
    TMap<FString, TSharedPtr<FJsonValue>> Parsed;
    for (int32 Index = 0; Index < Files.Num(); ++Index)
        if (Values[Index].IsValid()) Parsed.Add(Files[Index], Values[Index]);
    const auto LoadFile = [&Data](const FString& Path, FString& OutText)
    { return FFileHelper::LoadFileToString(OutText, *FPaths::Combine(Data, Path)); };
    Out = FVoxelFeatureGraph();
    const double Loaded = FPlatformTime::Seconds();
    const bool bRead = FFeatureReader(LoadFile, Out, &Parsed).Run(OutError);
    const double End = FPlatformTime::Seconds();
    UE_LOG(LogUnnamedVoxel, Display,
        TEXT("Decoration data: %d files read in %.1f ms (%.1f ms loading and parsing them, %.1f ms reading them)%s"),
        Files.Num(), (End - Start) * 1000, (Loaded - Start) * 1000, (End - Loaded) * 1000,
        bRead ? TEXT("") : TEXT(", failed"));
    return bRead;
}

TSharedPtr<FJsonValue> VoxelWorldgen::WriteFeatureNode(const FVoxelFeatureGraph& Graph, int32 Node)
{
    const FVoxelFeatureNode& Found = Graph.Nodes[Node];
    const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
    if (const TCHAR* TypeKey = GetTypeKey(Found.Kind)) Object->SetStringField(TypeKey, Graph.Strings[Found.Type]);
    for (int32 Field = Found.FirstField; Field < Found.FirstField + Found.FieldCount; ++Field)
        Object->SetField(Graph.Strings[Graph.Fields[Field].Key], WriteFeatureValue(Graph, Graph.Fields[Field].Value));
    return MakeShared<FJsonValueObject>(Object);
}

TSharedPtr<FJsonValue> VoxelWorldgen::WriteBlockTag(const FVoxelFeatureGraph& Graph, int32 Tag)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const FString& Value : Graph.Tags[Tag].Values)
        Values.Add(MakeShared<FJsonValueString>(Value));
    const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetArrayField(TEXT("values"), Values);
    return MakeShared<FJsonValueObject>(Object);
}

TConstArrayView<const TCHAR*> VoxelWorldgen::GetCodeTags()
{
    return CodeTags;
}
