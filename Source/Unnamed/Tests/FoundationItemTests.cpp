#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2DArray.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockImport.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationItemAssets.h"
#include "GameplayTagsManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags ItemTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    TSharedPtr<FJsonObject> ReadItemReference(const TCHAR* Name)
    {
        FString Text;
        TSharedPtr<FJsonObject> Root;
        const FString Path = FPaths::ProjectDir() / TEXT("Reference/minecraft-items-26.3") / Name;
        if (FFileHelper::LoadFileToString(Text, *Path))
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
        return Root;
    }

    FString StringOr(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
    {
        FString Value;
        return Object->TryGetStringField(Field, Value) ? Value : FString();
    }

    double NumberOr(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double Default)
    {
        double Value = Default;
        return Object && Object->TryGetNumberField(Field, Value) ? Value : Default;
    }

    TSharedPtr<FJsonObject> ObjectOr(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
    {
        const TSharedPtr<FJsonObject>* Value = nullptr;
        return Object->TryGetObjectField(Field, Value) ? *Value : nullptr;
    }

    TArray<FString> Strings(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
    {
        TArray<FString> Values;
        if (Object) Object->TryGetStringArrayField(Field, Values);
        return Values;
    }

    template <typename T> TArray<FString> Names(const TArray<T>& Values)
    {
        TArray<FString> Out;
        for (const T& Value : Values)
            Out.Add(Value.ToString());
        return Out;
    }

    const FFoundationItemDefinition* Find(const FFoundationRules& Rules, const TCHAR* Id)
    {
        const int32 Index = Rules.FindItem(Id);
        return Index == INDEX_NONE ? nullptr : &Rules.Items[Index];
    }

    // The project tag each tool-rule block tag maps to (BlockImportData.tag_name), stated independently here.
    const TMap<FString, FString> RuleTags = {{TEXT("mineable/pickaxe"), TEXT("Block.Mineable.Pickaxe")},
        {TEXT("mineable/axe"), TEXT("Block.Mineable.Axe")}, {TEXT("mineable/shovel"), TEXT("Block.Mineable.Shovel")},
        {TEXT("mineable/hoe"), TEXT("Block.Mineable.Hoe")},
        {TEXT("incorrect_for_wooden_tool"), TEXT("Block.IncorrectForTool.Wooden")},
        {TEXT("incorrect_for_stone_tool"), TEXT("Block.IncorrectForTool.Stone")},
        {TEXT("incorrect_for_copper_tool"), TEXT("Block.IncorrectForTool.Copper")},
        {TEXT("incorrect_for_iron_tool"), TEXT("Block.IncorrectForTool.Iron")},
        {TEXT("incorrect_for_gold_tool"), TEXT("Block.IncorrectForTool.Gold")},
        {TEXT("incorrect_for_diamond_tool"), TEXT("Block.IncorrectForTool.Diamond")},
        {TEXT("incorrect_for_netherite_tool"), TEXT("Block.IncorrectForTool.Netherite")},
        {TEXT("sword_efficient"), TEXT("Block.SwordEfficient")},
        {TEXT("sword_instantly_mines"), TEXT("Block.SwordInstantlyMines")}};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemsRegistryTest, "Unnamed.Foundation.Items.Registry", ItemTestFlags)

bool FFoundationItemsRegistryTest::RunTest(const FString&)
{
    const UFoundationItemAssets* Assets = UFoundationItemAssets::LoadDefault();
    const TSharedPtr<FJsonObject> Reference = ReadItemReference(TEXT("items.json"));
    const TSharedPtr<FJsonObject> ModelsJson = ReadItemReference(TEXT("models.json"));
    const TSharedPtr<FJsonObject> TexturesJson = ReadItemReference(TEXT("textures.json"));
    const TSharedPtr<FJsonObject> IndexJson = ReadItemReference(TEXT("item_index.json"));
    if (!TestTrue(
            TEXT("DA_Items and the item reference"), Assets && Reference && ModelsJson && TexturesJson && IndexJson))
        return false;
    const TArray<TSharedPtr<FJsonValue>>& Items = Reference->GetArrayField(TEXT("items"));
    if (!TestEqual(TEXT("63 items"), Assets->Items.Num(), 63) ||
        !TestEqual(TEXT("as many as items.json"), Assets->Items.Num(), Items.Num()))
        return false;
    TestEqual(TEXT("reference digest"), Assets->SourceSha256.Len(), 64);
    FFoundationRules Rules;
    UFoundationInteractionSettings::Get().BuildRules(Rules);
    UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();
    for (int32 Index = 0; Index < Items.Num(); ++Index)
    {
        const TSharedPtr<FJsonObject> Json = Items[Index]->AsObject();
        const FFoundationItemAsset& Asset = Assets->Items[Index];
        const FString Id = Json->GetStringField(TEXT("id"));
        const FString What = Id + TEXT(": ");
        TestEqual(What + TEXT("registry order"), Asset.Id.ToString(), Id);
        TestEqual(What + TEXT("name"), Asset.DisplayName, Json->GetStringField(TEXT("name")));
        TestEqual(What + TEXT("protocol id"), Asset.ProtocolId, int32(Json->GetNumberField(TEXT("protocol_id"))));
        TestEqual(What + TEXT("group"), Asset.Group.ToString(), StringOr(Json, TEXT("group")));
        TestEqual(What + TEXT("material"), Asset.Material.IsNone() ? FString() : Asset.Material.ToString(),
            StringOr(Json, TEXT("material")));
        TestEqual(
            What + TEXT("kind"), Asset.Kind.IsNone() ? FString() : Asset.Kind.ToString(), StringOr(Json, TEXT("kind")));
        TestEqual(What + TEXT("max stack"), Asset.MaxStack, int32(Json->GetNumberField(TEXT("max_stack"))));
        TestEqual(What + TEXT("max damage"), Asset.MaxDamage, int32(NumberOr(Json, TEXT("max_damage"), 0)));
        const TSharedPtr<FJsonObject> Attack = ObjectOr(Json, TEXT("attack"));
        TestEqual(What + TEXT("attack damage"), Asset.AttackDamage, float(NumberOr(Attack, TEXT("damage"), 1)));
        TestEqual(What + TEXT("attack speed"), Asset.AttacksPerSecond, float(NumberOr(Attack, TEXT("speed"), 4)));
        const TSharedPtr<FJsonObject> Tool = ObjectOr(Json, TEXT("tool"));
        TestEqual(What + TEXT("tool component"), Asset.bTool, Tool.IsValid());
        TestEqual(What + TEXT("default mining speed"), Asset.DefaultMiningSpeed,
            float(NumberOr(Tool, TEXT("default_mining_speed"), 1)));
        TestEqual(
            What + TEXT("damage per block"), Asset.DamagePerBlock, int32(NumberOr(Tool, TEXT("damage_per_block"), 0)));
        bool bCanDestroy = true;
        if (Tool) Tool->TryGetBoolField(TEXT("can_destroy_blocks_in_creative"), bCanDestroy);
        TestEqual(What + TEXT("can destroy in creative"), Asset.bCanDestroyBlocksInCreative, bCanDestroy);
        const TArray<TSharedPtr<FJsonValue>>* RuleValues = nullptr;
        const int32 RuleCount = Tool && Tool->TryGetArrayField(TEXT("rules"), RuleValues) ? RuleValues->Num() : 0;
        if (TestEqual(What + TEXT("rule count"), Asset.Rules.Num(), RuleCount))
            for (int32 Rule = 0; Rule < RuleCount; ++Rule)
            {
                const TSharedPtr<FJsonObject> R = (*RuleValues)[Rule]->AsObject();
                const FFoundationItemRuleAsset& A = Asset.Rules[Rule];
                bool bCorrect = false;
                const bool bStates = R->TryGetBoolField(TEXT("correct_for_drops"), bCorrect);
                TestTrue(What + FString::Printf(TEXT("rule %d"), Rule),
                    A.Blocks == R->GetStringField(TEXT("blocks")) && A.Speed == float(NumberOr(R, TEXT("speed"), 0)) &&
                        A.bStatesCorrectForDrops == bStates && A.bCorrectForDrops == bCorrect);
            }
        const TSharedPtr<FJsonObject> Weapon = ObjectOr(Json, TEXT("weapon"));
        TestEqual(What + TEXT("damage per hit"), Asset.DamagePerHit,
            int32(NumberOr(Weapon, TEXT("item_damage_per_attack"), 0)));
        TestEqual(What + TEXT("disable blocking"), Asset.DisableBlockingSeconds,
            float(NumberOr(Weapon, TEXT("disable_blocking_for_seconds"), 0)));
        TestEqual(
            What + TEXT("enchantability"), Asset.Enchantability, int32(NumberOr(Json, TEXT("enchantability"), 0)));
        const TSharedPtr<FJsonObject> Repair = ObjectOr(Json, TEXT("repair"));
        TestEqual(What + TEXT("repair selector"), Asset.RepairSelector,
            Repair ? Repair->GetStringField(TEXT("items")) : FString());
        TestTrue(What + TEXT("repair items"), Names(Asset.RepairItems) == Strings(Repair, TEXT("resolved")));
        TestEqual(What + TEXT("item tags"), Asset.ItemTags.Num(), Strings(Json, TEXT("item_tags")).Num());
        for (const FName Tag : Asset.ItemTags)
            TestTrue(What + TEXT("registered ") + Tag.ToString(),
                Tag.ToString().StartsWith(TEXT("Item.")) && TagManager.RequestGameplayTag(Tag, false).IsValid());
        const TArray<TSharedPtr<FJsonValue>>& Creative = Json->GetArrayField(TEXT("creative"));
        if (TestEqual(What + TEXT("creative positions"), Asset.Creative.Num(), Creative.Num()))
            for (int32 Entry = 0; Entry < Creative.Num(); ++Entry)
                TestTrue(What + TEXT("creative ") + Asset.Creative[Entry].Tab.ToString(),
                    Asset.Creative[Entry].Tab.ToString() == Creative[Entry]->AsObject()->GetStringField(TEXT("tab")) &&
                        Asset.Creative[Entry].Index ==
                            int32(Creative[Entry]->AsObject()->GetNumberField(TEXT("index"))));
        const TSharedPtr<FJsonObject> Model = Json->GetObjectField(TEXT("model"));
        TestEqual(What + TEXT("model type"), Asset.ModelType.ToString(), Model->GetStringField(TEXT("type")));
        const TArray<TSharedPtr<FJsonValue>>& Cases = Model->GetArrayField(TEXT("models"));
        if (TestEqual(What + TEXT("model cases"), Asset.Models.Num(), Cases.Num()))
            for (int32 Case = 0; Case < Cases.Num(); ++Case)
            {
                // A plain model and the select's fallback cover every context: no list.
                const TSharedPtr<FJsonObject> When = ObjectOr(Cases[Case]->AsObject(), TEXT("when"));
                TArray<FString> Contexts = Strings(When, TEXT("is"));
                if (When && Contexts.IsEmpty() && StringOr(When, TEXT("is")) != TEXT("fallback"))
                    Contexts.Add(StringOr(When, TEXT("is")));
                TestTrue(What + FString::Printf(TEXT("model case %d"), Case),
                    Asset.Models[Case].Model.ToString() == Cases[Case]->AsObject()->GetStringField(TEXT("model")) &&
                        Names(Asset.Models[Case].Contexts) == Contexts);
            }
        bool bHandAnimation = false;
        Model->TryGetBoolField(TEXT("hand_animation_on_swap"), bHandAnimation);
        TestEqual(What + TEXT("hand animation on swap"), Asset.bHandAnimationOnSwap, bHandAnimation);
        TestEqual(What + TEXT("swap animation"), Asset.SwapAnimationScale,
            float(Model->GetNumberField(TEXT("swap_animation_scale"))));
        TestTrue(What + TEXT("textures"), Names(Asset.Textures) == Strings(Json, TEXT("textures")));
        // The attribute modifiers verbatim, namespaces dropped (the tooltip's lines).
        const TArray<TSharedPtr<FJsonValue>>* Modifiers = nullptr;
        if (!Attack || !Attack->TryGetArrayField(TEXT("modifiers"), Modifiers)) Modifiers = nullptr;
        if (TestEqual(What + TEXT("modifiers"), Asset.Modifiers.Num(), Modifiers ? Modifiers->Num() : 0))
            for (int32 Entry = 0; Entry < Asset.Modifiers.Num(); ++Entry)
            {
                const TSharedPtr<FJsonObject> Source = (*Modifiers)[Entry]->AsObject();
                const FFoundationItemModifierAsset& Modifier = Asset.Modifiers[Entry];
                TestTrue(What + FString::Printf(TEXT("modifier %d"), Entry),
                    TEXT("minecraft:") + Modifier.Attribute.ToString() == Source->GetStringField(TEXT("type")) &&
                        TEXT("minecraft:") + Modifier.Id.ToString() == Source->GetStringField(TEXT("id")) &&
                        Modifier.Amount == Source->GetNumberField(TEXT("amount")) &&
                        Modifier.Operation.ToString() == Source->GetStringField(TEXT("operation")) &&
                        Modifier.Slot.ToString() == Source->GetStringField(TEXT("slot")));
            }

        // The authority's definition carries the same numbers.
        const FFoundationItemDefinition* Definition = Find(Rules, *Id);
        if (!TestNotNull(*(What + TEXT("in the rules")), Definition)) continue;
        TestTrue(What + TEXT("definition numbers"),
            Definition->MaxStack == Asset.MaxStack && Definition->AttackDamage == Asset.AttackDamage &&
                Definition->AttacksPerSecond == Asset.AttacksPerSecond &&
                Definition->MaxDurability == Asset.MaxDamage &&
                Definition->DurabilityPerBlock == Asset.DamagePerBlock &&
                Definition->DurabilityPerHit == Asset.DamagePerHit &&
                Definition->DefaultMiningSpeed == Asset.DefaultMiningSpeed &&
                Definition->MiningRules.Num() == Asset.Rules.Num() && Definition->Tags.Num() == Asset.ItemTags.Num() &&
                Definition->PlacesBlock.IsNone() && Definition->DisplayName == Asset.DisplayName &&
                !Asset.Textures.IsEmpty() && Definition->Sprite == Asset.Textures[0]);
        for (int32 Rule = 0; Rule < FMath::Min(Definition->MiningRules.Num(), Asset.Rules.Num()); ++Rule)
            TestTrue(What + FString::Printf(TEXT("definition rule %d"), Rule),
                Definition->MiningRules[Rule].Speed == Asset.Rules[Rule].Speed &&
                    Definition->MiningRules[Rule].bStatesCorrectForDrops == Asset.Rules[Rule].bStatesCorrectForDrops &&
                    Definition->MiningRules[Rule].bCorrectForDrops == Asset.Rules[Rule].bCorrectForDrops);
    }
    // Spot values from scout F's appendix A (26.3 components).
    const FFoundationItemDefinition* IronPickaxe = Find(Rules, TEXT("iron_pickaxe"));
    const FFoundationItemDefinition* Hoe = Find(Rules, TEXT("wooden_hoe"));
    const FFoundationItemDefinition* Spear = Find(Rules, TEXT("iron_spear"));
    const FFoundationItemDefinition* Netherite = Find(Rules, TEXT("netherite_sword"));
    if (TestTrue(TEXT("spot items"), IronPickaxe && Hoe && Spear && Netherite))
    {
        TestTrue(TEXT("iron pickaxe 4 / 1.2, 250, 1 per block, 2 per hit"),
            IronPickaxe->AttackDamage == 4 && IronPickaxe->AttacksPerSecond == 1.2f &&
                IronPickaxe->MaxDurability == 250 && IronPickaxe->DurabilityPerBlock == 1 &&
                IronPickaxe->DurabilityPerHit == 2 && IronPickaxe->MaxStack == 1);
        TestEqual(TEXT("hoes wear 2 per hit (the data, not the wiki's 1)"), Hoe->DurabilityPerHit, 2);
        TestTrue(TEXT("spears: no tool component, so mining costs nothing"),
            Spear->MiningRules.IsEmpty() && Spear->DurabilityPerBlock == 0 && Spear->DurabilityPerHit == 1 &&
                Spear->MaxDurability == 250);
        TestTrue(TEXT("items set no Icon: the HUD draws Sprite and the held view the model (items S4)"),
            IronPickaxe->Icon.IsNone() && Find(Rules, TEXT("iron_sword"))->Icon.IsNone() &&
                Find(Rules, TEXT("iron_axe"))->Icon.IsNone() && Find(Rules, TEXT("stick"))->Icon.IsNone());
        TestTrue(TEXT("netherite sword 8 / 1.6, 2031"),
            Netherite->AttackDamage == 8 && Netherite->AttacksPerSecond == 1.6f && Netherite->MaxDurability == 2031);
    }
    // Models and textures for icons and held meshes.
    const TArray<TSharedPtr<FJsonValue>>& Models = ModelsJson->GetArrayField(TEXT("models"));
    if (TestEqual(TEXT("70 models"), Assets->Models.Num(), Models.Num()))
        for (int32 Index = 0; Index < Models.Num(); ++Index)
        {
            const TSharedPtr<FJsonObject> Json = Models[Index]->AsObject();
            const FFoundationItemModelAsset& Asset = Assets->Models[Index];
            const TSharedPtr<FJsonObject> Display = Json->GetObjectField(TEXT("display"));
            TestTrue(TEXT("model ") + Asset.Name.ToString(),
                Asset.Name.ToString() == Json->GetStringField(TEXT("id")) &&
                    Asset.Parent.ToString() == StringOr(Json, TEXT("parent")) &&
                    Asset.GuiLight.ToString() == StringOr(Json, TEXT("gui_light")) &&
                    Names(Asset.Layers) == Strings(Json, TEXT("layers")) &&
                    Asset.Display.Num() == Display->Values.Num());
            // Every context's transform, with the model format's defaults for omitted fields.
            for (const FFoundationItemDisplayAsset& Context : Asset.Display)
            {
                const TSharedPtr<FJsonObject> Transform = ObjectOr(Display, *Context.Context.ToString());
                auto Vector = [&Transform](const TCHAR* Field, double Default)
                {
                    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
                    return Transform && Transform->TryGetArrayField(Field, Values) && Values->Num() == 3
                        ? FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber())
                        : FVector(Default);
                };
                TestTrue(TEXT("display ") + Asset.Name.ToString() + TEXT(" ") + Context.Context.ToString(),
                    Transform.IsValid() && Context.Rotation == Vector(TEXT("rotation"), 0) &&
                        Context.Translation == Vector(TEXT("translation"), 0) &&
                        Context.Scale == Vector(TEXT("scale"), 1));
            }
        }
    const TArray<TSharedPtr<FJsonValue>>& Textures = TexturesJson->GetArrayField(TEXT("textures"));
    if (TestEqual(TEXT("70 textures"), Assets->Textures.Num(), Textures.Num()))
        for (int32 Index = 0; Index < Textures.Num(); ++Index)
        {
            const TSharedPtr<FJsonObject> Json = Textures[Index]->AsObject();
            const FFoundationItemTextureAsset& Asset = Assets->Textures[Index];
            TestTrue(TEXT("texture ") + Asset.Name.ToString(),
                Asset.Name.ToString() == Json->GetStringField(TEXT("name")) &&
                    Asset.Mask == Strings(Json, TEXT("mask")) &&
                    Asset.OpaquePixels == int32(Json->GetNumberField(TEXT("opaque_pixels"))) &&
                    Asset.RgbaSha256 == Json->GetStringField(TEXT("rgba_sha256")) &&
                    Asset.Width == int32(Json->GetArrayField(TEXT("size"))[0]->AsNumber()) &&
                    Asset.Height == int32(Json->GetArrayField(TEXT("size"))[1]->AsNumber()) &&
                    Asset.Height == Asset.Mask.Num());
        }
    // Every sprite is a layer of T_ItemTextures (16 x 16) or T_ItemTextures32, in textures.json order, holding the
    // jar's pixels.
    UTexture2DArray* Arrays[2] = {Assets->ItemTextures.LoadSynchronous(), Assets->ItemTextures32.LoadSynchronous()};
    int32 Layers[2] = {0, 0};
    for (const FFoundationItemTextureAsset& Asset : Assets->Textures)
    {
        const int32 Array = Asset.Width == 32 ? 1 : 0;
        TestEqual(TEXT("layer of ") + Asset.Name.ToString(), Asset.Layer, Layers[Array]++);
        if (Arrays[Array])
            TestEqual(TEXT("layer pixels of ") + Asset.Name.ToString(),
                UFoundationBlockImport::HashLayer(Arrays[Array], Asset.Layer), Asset.RgbaSha256);
    }
    TestTrue(TEXT("63 layers of 16 x 16 and 7 of 32 x 32"), Layers[0] == 63 && Layers[1] == 7);
    for (int32 Array = 0; Array < 2; ++Array)
        if (TestNotNull(Array ? TEXT("T_ItemTextures32") : TEXT("T_ItemTextures"), Arrays[Array]))
        {
            const UTexture2DArray* Texture = Arrays[Array];
            const int32 Size = Array ? 32 : 16;
#if WITH_EDITORONLY_DATA
            // The source data: -NullRHI runs build no platform data.
            TestEqual(TEXT("array layers"), Texture->Source.GetNumSlices(), Layers[Array]);
            TestTrue(TEXT("array size"), Texture->Source.GetSizeX() == Size && Texture->Source.GetSizeY() == Size);
            TestEqual(TEXT("array mips"), Texture->Source.GetNumMips(), 1);
#endif
            TestEqual(TEXT("array filter"), int32(Texture->Filter.GetValue()), int32(TF_Nearest));
        }
    // The tooltip's text, verbatim from en_us.json.
    const TSharedPtr<FJsonObject> LangJson = ReadItemReference(TEXT("lang.json"));
    const TSharedPtr<FJsonObject> Keys = LangJson ? ObjectOr(LangJson, TEXT("keys")) : nullptr;
    if (TestNotNull(TEXT("lang.json"), Keys.Get()) && TestEqual(TEXT("6 lang keys"), Assets->Lang.Num(), 6))
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Key : Keys->Values)
            TestEqual(TEXT("lang ") + Key.Key, Assets->Lang.FindRef(Key.Key), Key.Value->AsString());
    // Ids: no 26.3 item shares an id with a block or a block item; only air and wheat share a block's id in 26.3
    // without being that block's item, and neither is imported.
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    TArray<FFoundationItemDefinition> BlockItems;
    UFoundationInteractionSettings::AddBlockItems(*Registry, BlockItems);
    for (const FFoundationItemAsset& Asset : Assets->Items)
        TestTrue(TEXT("no block or block item named ") + Asset.Id.ToString(),
            !Registry->FindBlock(Asset.Id) &&
                !BlockItems.ContainsByPredicate(
                    [&Asset](const FFoundationItemDefinition& Item) { return Item.Id == Asset.Id; }));
    TestEqual(TEXT("block items, then DA_Items, then project items"), Rules.Items.Num(),
        BlockItems.Num() + Assets->Items.Num() + UFoundationInteractionSettings::Get().Items.Num());
    if (Rules.Items.IsValidIndex(BlockItems.Num()))
        TestEqual(TEXT("DA_Items follows the block items"), Rules.Items[BlockItems.Num()].Id, Assets->Items[0].Id);
    TArray<FString> Shared;
    for (const TSharedPtr<FJsonValue>& Value : IndexJson->GetArrayField(TEXT("items")))
    {
        const TSharedPtr<FJsonObject> Entry = Value->AsObject();
        bool bSameId = false;
        if (Entry->TryGetBoolField(TEXT("block_with_same_id"), bSameId) && bSameId &&
            StringOr(Entry, TEXT("places_block")) != Entry->GetStringField(TEXT("id")))
            Shared.Add(Entry->GetStringField(TEXT("id")));
    }
    TestTrue(TEXT("26.3 id clashes: air and wheat"), Shared == TArray<FString>{TEXT("air"), TEXT("wheat")});
    TestTrue(TEXT("neither is an item here"), !Find(Rules, TEXT("air")) && !Find(Rules, TEXT("wheat")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemsRulesTest, "Unnamed.Foundation.Items.Rules", ItemTestFlags)

bool FFoundationItemsRulesTest::RunTest(const FString&)
{
    const UFoundationItemAssets* Assets = UFoundationItemAssets::LoadDefault();
    const TSharedPtr<FJsonObject> TagsJson = ReadItemReference(TEXT("tags.json"));
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    if (!TestTrue(TEXT("DA_Items, tags.json and the block registry"), Assets && TagsJson && Registry->GetAssets()))
        return false;
    FFoundationRules Rules;
    UFoundationInteractionSettings::Get().BuildRules(Rules);
    const TSharedPtr<FJsonObject> BlockTags = TagsJson->GetObjectField(TEXT("block"));
    // Every selector: a block tag resolves to its Block.* tag, registered exactly when an imported block carries it
    // (then every imported member carries it); a single block, or a tag no imported block has, matches nothing.
    int32 Checked = 0;
    for (const FFoundationItemAsset& Asset : Assets->Items)
    {
        const FFoundationItemDefinition* Definition = Find(Rules, *Asset.Id.ToString());
        if (!Definition || Definition->MiningRules.Num() != Asset.Rules.Num()) continue;
        for (int32 Index = 0; Index < Asset.Rules.Num(); ++Index)
        {
            const FFoundationItemRuleAsset& Rule = Asset.Rules[Index];
            const FGameplayTagContainer& Tags = Definition->MiningRules[Index].BlockTags;
            const FString What = Asset.Id.ToString() + TEXT(" ") + Rule.Blocks + TEXT(": ");
            ++Checked;
            TArray<FString> Members;
            if (Rule.Blocks.StartsWith(TEXT("#minecraft:")))
            {
                const FString Vanilla = Rule.Blocks.RightChop(11);
                const FString* Expected = RuleTags.Find(Vanilla);
                TestTrue(What + TEXT("names its Block.* tag"), Expected && Rule.BlockTag.ToString() == *Expected);
                const TSharedPtr<FJsonObject> Tag = ObjectOr(BlockTags, *Vanilla);
                if (TestTrue(What + TEXT("in tags.json"), Tag.IsValid())) Members = Strings(Tag, TEXT("resolved"));
            }
            else
            {
                TestTrue(What + TEXT("a single block has no tag"), Rule.BlockTag.IsNone());
                Members.Add(Rule.Blocks.RightChop(10));
            }
            const FGameplayTag Registered = FGameplayTag::RequestGameplayTag(Rule.BlockTag, false);
            int32 Imported = 0;
            for (const FString& Member : Members)
                if (const FFoundationBlockType* Block = Registry->FindBlock(*Member))
                {
                    ++Imported;
                    TestTrue(What + Member + TEXT(" carries the tag"),
                        Registered.IsValid() && Block->Tags.HasTagExact(Registered));
                }
            if (Registered.IsValid())
                TestTrue(
                    What + TEXT("matches its tag"), Tags.Num() == 1 && Tags.HasTagExact(Registered) && Imported > 0);
            else
                TestTrue(What + TEXT("matches nothing: no imported block"), Tags.IsEmpty() && Imported == 0);
        }
    }
    TestEqual(TEXT("77 tool rules"), Checked, 77);

    // First-rule semantics on imported blocks: speed from the first matching rule that states one, drops from the
    // first matching rule that states correct_for_drops.
    const FFoundationBlockType* Stone = Registry->FindBlock(TEXT("stone"));
    const FFoundationBlockType* Obsidian = Registry->FindBlock(TEXT("obsidian"));
    const FFoundationBlockType* IronOre = Registry->FindBlock(TEXT("iron_ore"));
    const FFoundationBlockType* DiamondOre = Registry->FindBlock(TEXT("diamond_ore"));
    const FFoundationBlockType* Log = Registry->FindBlock(TEXT("oak_log"));
    const FFoundationBlockType* Dirt = Registry->FindBlock(TEXT("dirt"));
    const FFoundationBlockType* Leaves = Registry->FindBlock(TEXT("oak_leaves"));
    const FFoundationBlockType* Bamboo = Registry->FindBlock(TEXT("bamboo"));
    if (!TestTrue(TEXT("test blocks"), Stone && Obsidian && IronOre && DiamondOre && Log && Dirt && Leaves && Bamboo))
        return false;
    auto Expect = [this, &Rules](const TCHAR* Item, const FFoundationBlockType* Block, float Speed, bool bDrops)
    {
        const FFoundationItemDefinition* Tool = Find(Rules, Item);
        if (!TestNotNull(Item, Tool)) return;
        TestEqual(FString::Printf(TEXT("%s on %s: speed"), Item, *Block->Id.ToString()),
            Tool->GetMiningSpeed(Block->Tags), Speed);
        TestEqual(FString::Printf(TEXT("%s on %s: correct for drops"), Item, *Block->Id.ToString()),
            Tool->IsCorrectForDrops(Block->Tags), bDrops);
    };
    // A wrong tier keeps the pickaxe's speed (the tier rule states none) but loses the drop.
    Expect(TEXT("iron_pickaxe"), Obsidian, 6, false);
    Expect(TEXT("diamond_pickaxe"), Obsidian, 8, true);
    Expect(TEXT("netherite_pickaxe"), Obsidian, 9, true);
    Expect(TEXT("wooden_pickaxe"), IronOre, 2, false);
    Expect(TEXT("stone_pickaxe"), IronOre, 4, true);
    Expect(TEXT("copper_pickaxe"), IronOre, 5, true);
    Expect(TEXT("copper_pickaxe"), DiamondOre, 5, false);
    Expect(TEXT("golden_pickaxe"), Stone, 12, true);
    Expect(TEXT("golden_pickaxe"), IronOre, 12, false);
    Expect(TEXT("iron_pickaxe"), DiamondOre, 6, true);
    Expect(TEXT("iron_axe"), Log, 6, true);
    Expect(TEXT("iron_axe"), Stone, 1, false);
    Expect(TEXT("iron_shovel"), Dirt, 6, true);
    Expect(TEXT("iron_hoe"), Leaves, 6, true);
    Expect(TEXT("iron_sword"), Leaves, 1.5f, false);
    Expect(TEXT("iron_sword"), Stone, 1, false);
    Expect(TEXT("iron_sword"), Bamboo, FLT_MAX, false);
    Expect(TEXT("iron_spear"), Stone, 1, false);
    Expect(TEXT("stick"), Obsidian, 1, false);
    const FFoundationItemDefinition* Sword = Find(Rules, TEXT("iron_sword"));
    if (TestNotNull(TEXT("iron sword"), Sword) && TestEqual(TEXT("sword rules"), Sword->MiningRules.Num(), 3))
        TestTrue(TEXT("the cobweb rule stays, matching nothing: speed 15, correct for drops"),
            Sword->MiningRules[0].BlockTags.IsEmpty() && Sword->MiningRules[0].Speed == 15 &&
                Sword->MiningRules[0].bStatesCorrectForDrops && Sword->MiningRules[0].bCorrectForDrops);
    // The same order rule on a synthetic tool: the first rule states drops but no speed.
    FFoundationItemDefinition Synthetic;
    Synthetic.MiningRules.SetNum(2);
    Synthetic.MiningRules[0].BlockTags = Stone->Tags;
    Synthetic.MiningRules[0].bStatesCorrectForDrops = true;
    Synthetic.MiningRules[1].BlockTags = Stone->Tags;
    Synthetic.MiningRules[1].Speed = 7;
    Synthetic.MiningRules[1].bStatesCorrectForDrops = Synthetic.MiningRules[1].bCorrectForDrops = true;
    TestTrue(TEXT("speed from the second rule, drops from the first"),
        Synthetic.GetMiningSpeed(Stone->Tags) == 7 && !Synthetic.IsCorrectForDrops(Stone->Tags));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemsMergeTest, "Unnamed.Foundation.Items.Merge", ItemTestFlags)

bool FFoundationItemsMergeTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("DA_Items"), UFoundationItemAssets::LoadDefault())) return false;
    FFoundationRules Before;
    UFoundationInteractionSettings::Get().BuildRules(Before);
    // A project item reusing a 26.3 id is an error, and the later item is skipped: the iron pickaxe keeps its data.
    UFoundationInteractionSettings* Settings = GetMutableDefault<UFoundationInteractionSettings>();
    const TArray<FFoundationItemDefinition> SavedItems = Settings->Items;
    FFoundationItemDefinition Clash;
    Clash.Id = TEXT("iron_pickaxe");
    Clash.AttackDamage = 99;
    FFoundationItemDefinition Fresh;
    Fresh.Id = TEXT("test_project_item");
    Settings->Items = {Clash, Fresh};
    AddExpectedError(TEXT("Item id iron_pickaxe from UFoundationInteractionSettings::Items is already defined"),
        EAutomationExpectedErrorFlags::Contains, 1);
    FFoundationRules Rules;
    Settings->BuildRules(Rules);
    Settings->Items = SavedItems;
    const FFoundationItemDefinition* Pickaxe = Find(Rules, TEXT("iron_pickaxe"));
    TestTrue(TEXT("the 26.3 iron pickaxe stays"), Pickaxe && Pickaxe->AttackDamage == 4);
    TestEqual(TEXT("one iron pickaxe"),
        Rules.Items
            .FilterByPredicate([](const FFoundationItemDefinition& Item) { return Item.Id == TEXT("iron_pickaxe"); })
            .Num(),
        1);
    TestEqual(TEXT("the non-clashing project item is appended last"), Rules.Items.Last().Id,
        FName(TEXT("test_project_item")));
    TestEqual(TEXT("item count: one added, one skipped"), Rules.Items.Num(), Before.Items.Num() + 1);
    // A 26.3 item reusing a block item's id is refused the same way.
    TArray<FFoundationItemDefinition> Items = Before.Items;
    FFoundationItemDefinition Stone;
    Stone.Id = TEXT("stone");
    AddExpectedError(TEXT("Item id stone from test is already defined"), EAutomationExpectedErrorFlags::Contains, 1);
    TestEqual(TEXT("one clash"), UFoundationInteractionSettings::MergeItems(Items, {Stone}, TEXT("test")), 1);
    TestEqual(TEXT("nothing added"), Items.Num(), Before.Items.Num());
    return true;
}
#endif
