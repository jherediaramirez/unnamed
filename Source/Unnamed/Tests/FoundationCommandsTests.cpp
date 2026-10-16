#include "Misc/AutomationTest.h"
#include "Clock/VoxelWorldClock.h"
#include "Dom/JsonObject.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationChat.h"
#include "Foundation/FoundationCommands.h"
#include "Foundation/FoundationCoordinates.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationVitals.h"
#include "HAL/IConsoleManager.h"
#include "Hud/HudDrawList.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace CommandsTestsLocal
{
    // One player on the project's rules, standing at Minecraft (10.5, 64, -3.5); /tp lands are recorded.
    struct FCommandLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        FVoxelWorldClock Clock;
        int32 Player = 0;
        FVector Feet = FoundationCoordinates::ToUnreal(FVector(10.5, 64, -3.5));
        int32 Teleports = 0;
        bool bClock = true;
        // The world's "Allow Cheats".
        bool bCommands = true;

        FCommandLab()
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            Player = A.AddPlayer();
            A.Reset();
            Vitals.bNaturalRegeneration = false;
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }

        TArray<TArray<FHudChatRun>> Lines(const FString& Line)
        {
            FFoundationCommandContext Context;
            Context.Authority = &A;
            Context.PlayerId = Player;
            Context.Vitals = &Vitals;
            Context.FeetCm = Feet;
            Context.Teleport = [this](const FVector& To)
            {
                Feet = To;
                ++Teleports;
            };
            Context.Clock = bClock ? &Clock : nullptr;
            Context.bCommands = bCommands;
            return FoundationCommands::Run(Line, Context);
        }

        // The lines' text, one per line.
        FString Run(const FString& Line)
        {
            TArray<FString> Out;
            for (const TArray<FHudChatRun>& Runs : Lines(Line))
            {
                FString Text;
                for (const FHudChatRun& Run : Runs)
                    Text += Run.Text;
                Out.Add(Text);
            }
            return FString::Join(Out, TEXT("\n"));
        }

        int32 Count(FName Item) const
        {
            const int32 Index = A.Rules.FindItem(Item);
            const FFoundationInventory& Inventory = A.FindPlayer(Player)->Inventory;
            int32 Total = Inventory.Offhand.Item == Index ? Inventory.Offhand.Count : 0;
            for (const FFoundationItemInstance& Stack : Inventory.Hotbar)
                if (Stack.Item == Index) Total += Stack.Count;
            for (const FFoundationItemInstance& Stack : Inventory.Main)
                if (Stack.Item == Index) Total += Stack.Count;
            return Total;
        }
        int32 DroppedCount(FName Item) const
        {
            int32 Total = 0;
            for (const FFoundationItemEntity& Entity : A.GetItemEntities())
                if (Entity.Item == A.Rules.FindItem(Item)) Total += Entity.Count;
            return Total;
        }
    };

    const TCHAR* const UnknownCommand = TEXT("Unknown or incomplete command. See below for error");
    const TCHAR* const IncorrectArgument = TEXT("Incorrect argument for command");
}
using namespace CommandsTestsLocal;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationCommandsLangTest, "Unnamed.Foundation.Commands.Lang",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationCommandsLangTest::RunTest(const FString& Parameters)
{
    // Every string the commands print is the 26.3 jar's en_us.json text, verbatim.
    const FString Path = FPaths::ProjectDir() / TEXT("Reference/minecraft-menu/assets/minecraft/lang/en_us.json");
    FString Json;
    TSharedPtr<FJsonObject> Lang;
    if (!TestTrue(TEXT("en_us.json reads"), FFileHelper::LoadFileToString(Json, *Path)) ||
        !TestTrue(TEXT("en_us.json parses"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Lang) && Lang.IsValid()))
        return false;
    TestTrue(TEXT("strings to check"), FoundationCommands::GetLangStrings().Num() >= 30);
    for (const FoundationCommands::FLangString& String : FoundationCommands::GetLangStrings())
    {
        FString Vanilla;
        if (TestTrue(
                FString::Printf(TEXT("%s is a vanilla key"), String.Key), Lang->TryGetStringField(String.Key, Vanilla)))
            TestEqual(String.Key, FString(String.Text), Vanilla);
    }
    TestEqual(TEXT("format fills %s in order"),
        FoundationCommands::Format(TEXT("commands.give.success.single"), {TEXT("3"), TEXT("[Apple]"), TEXT("Player")}),
        FString(TEXT("Gave 3 [Apple] to Player")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationCommandsParseTest, "Unnamed.Foundation.Commands.Parse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationCommandsParseTest::RunTest(const FString& Parameters)
{
    FCommandLab Lab;
    auto Expect = [this, &Lab](const TCHAR* Line, const FString& Expected)
    { TestEqual(Line, Lab.Run(Line), Expected); };
    // Unknown and incomplete commands: the whole rest in red, the cursor where parsing stopped.
    Expect(TEXT("/foo bar"), FString(UnknownCommand) + TEXT("\nfoo bar<--[HERE]"));
    Expect(TEXT("/"), FString(UnknownCommand) + TEXT("\n<--[HERE]"));
    Expect(TEXT("/GameMode creative"), FString(UnknownCommand) + TEXT("\nGameMode creative<--[HERE]"));
    Expect(TEXT("/give @s"), FString(UnknownCommand) + TEXT("\ngive @s<--[HERE]"));
    Expect(TEXT("/time"), FString(UnknownCommand) + TEXT("\ntime<--[HERE]"));
    // Incorrect arguments: the cursor before the part that matched nothing; ten characters of context, "..." when cut.
    Expect(TEXT("/time foo"), FString(IncorrectArgument) + TEXT("\ntime foo<--[HERE]"));
    Expect(TEXT("/kill @s extra"), FString(IncorrectArgument) + TEXT("\nkill @s extra<--[HERE]"));
    Expect(TEXT("/time query daytime"), FString(IncorrectArgument) + TEXT("\ntime query daytime<--[HERE]"));
    Expect(TEXT("/give @s apple 3 more"), FString(IncorrectArgument) + TEXT("\n...@s apple 3 more<--[HERE]"));
    // An argument's own errors.
    Expect(TEXT("/gamemode banana"), TEXT("Unknown game mode: banana\n...ode banana<--[HERE]"));
    Expect(TEXT("/give @s nothing_here"), TEXT("Unknown item 'minecraft:nothing_here'\ngive @s nothing_here<--[HERE]"));
    Expect(TEXT("/give @s other:apple"), TEXT("Unknown item 'other:apple'\ngive @s other:apple<--[HERE]"));
    Expect(TEXT("/give @s apple 0"), TEXT("Integer must not be less than 1: found 0\n... @s apple 0<--[HERE]"));
    Expect(TEXT("/give @s apple x"), TEXT("Expected integer\n... @s apple x<--[HERE]"));
    Expect(TEXT("/give @s apple 1.5"), TEXT("Invalid integer '1.5'\n... @s apple 1.5<--[HERE]"));
    Expect(TEXT("/give @s apple 3x"),
        TEXT("Expected whitespace to end one argument, but found trailing data\n...@s apple 3x<--[HERE]"));
    Expect(TEXT("/kill @x"), TEXT("Unknown selector type '@x'\nkill @x<--[HERE]"));
    Expect(TEXT("/give @e apple"),
        TEXT("Only players may be affected by this command, but the provided selector includes entities\ngive "
             "@e apple<--[HERE]"));
    Expect(TEXT("/tp ~ ~"), TEXT("Incomplete (expected 3 coordinates)\ntp ~ ~<--[HERE]"));
    Expect(TEXT("/tp ~ ^ ~"),
        TEXT("Cannot mix world and local coordinates (everything must either use ^ or not)\ntp ~ ^ ~<--[HERE]"));
    Expect(TEXT("/tp 1 2 abc"), TEXT("Expected double\ntp 1 2 abc<--[HERE]"));
    Expect(TEXT("/time set 5x"), TEXT("Invalid unit\n...ime set 5x<--[HERE]"));
    Expect(TEXT("/tp . . ."), TEXT("Invalid double '.'\ntp . . .<--[HERE]"));
    Expect(TEXT("/time set -"), TEXT("Invalid float '-'\ntime set -<--[HERE]"));
    Expect(TEXT("/time set -5"), TEXT("The tick count must not be less than 0: found -5\ntime set -5<--[HERE]"));
    Expect(TEXT("/time add -2147483649"),
        TEXT("The tick count must not be less than -2147483648: found -2147483649\ntime add -2147483649<--[HERE]"));
    // /tp's destination is one entity: @a and @e may be more.
    Expect(TEXT("/tp @e"),
        TEXT("Only one entity is allowed, but the provided selector allows more than one\ntp @e<--[HERE]"));
    Expect(TEXT("/tp @s @a"),
        TEXT("Only one entity is allowed, but the provided selector allows more than one\ntp @s @a<--[HERE]"));
    // Execution errors: one red line, no context.
    Expect(TEXT("/kill Steve"), TEXT("No entity was found"));
    Expect(TEXT("/give Steve apple"), TEXT("No player was found"));
    TestEqual(TEXT("nothing ran"), Lab.Teleports + Lab.Vitals.Deaths + Lab.Count(TEXT("apple")), 0);

    // Colours: the error in red, the context's text before the cursor in gray, the rest and the marker in red.
    const TArray<TArray<FHudChatRun>> Lines = Lab.Lines(TEXT("/time foo"));
    if (TestEqual(TEXT("two lines"), Lines.Num(), 2) && TestEqual(TEXT("three runs"), Lines[1].Num(), 3))
    {
        TestEqual(TEXT("message red"), Lines[0][0].Color, FoundationChat::Red());
        TestEqual(TEXT("before the cursor"), Lines[1][0].Text, FString(TEXT("time")));
        TestEqual(TEXT("before the cursor gray"), Lines[1][0].Color, FoundationChat::Gray());
        TestEqual(TEXT("the rest"), Lines[1][1].Text, FString(TEXT(" foo")));
        TestEqual(TEXT("the rest red"), Lines[1][1].Color, FoundationChat::Red());
        TestEqual(TEXT("marker red"), Lines[1][2].Color, FoundationChat::Red());
    }
    TestEqual(TEXT("FFF55 red"), FoundationChat::Red().ToFColorSRGB(), FColor(0xFF, 0x55, 0x55));
    TestEqual(TEXT("AAAAAA gray"), FoundationChat::Gray().ToFColorSRGB(), FColor(0xAA, 0xAA, 0xAA));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationCommandsGameModeTest, "Unnamed.Foundation.Commands.GameMode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationCommandsGameModeTest::RunTest(const FString& Parameters)
{
    FCommandLab Lab;
    TestEqual(
        TEXT("creative"), Lab.Run(TEXT("/gamemode creative")), FString(TEXT("Set own game mode to Creative Mode")));
    TestEqual(TEXT("mode"), Lab.Vitals.Mode, EFoundationPlayerMode::Creative);
    TestTrue(TEXT("creative rules"),
        Lab.A.Options.bInfiniteSupply && !Lab.A.Options.bToolWear && Lab.A.Options.bCreativeInventory &&
            Lab.P().Inventory.bInfiniteSupply);
    TestEqual(TEXT("the same mode prints nothing"), Lab.Run(TEXT("/gamemode creative @s")), FString());
    TestEqual(TEXT("by name, any case"), Lab.Run(TEXT("/gamemode survival player")),
        FString(TEXT("Set own game mode to Survival Mode")));
    TestEqual(TEXT("survival"), Lab.Vitals.Mode, EFoundationPlayerMode::Survival);
    TestTrue(TEXT("survival rules"),
        !Lab.A.Options.bInfiniteSupply && Lab.A.Options.bToolWear && !Lab.A.Options.bCreativeInventory &&
            !Lab.P().Inventory.bInfiniteSupply);
    TestEqual(TEXT("@p"), Lab.Run(TEXT("/gamemode creative @p")), FString(TEXT("Set own game mode to Creative Mode")));
    TestEqual(TEXT("adventure refused"), Lab.Run(TEXT("/gamemode adventure")),
        FString(TEXT("Adventure Mode isn't available in this build")));
    TestEqual(TEXT("spectator refused"), Lab.Run(TEXT("/gamemode spectator")),
        FString(TEXT("Spectator Mode isn't available in this build")));
    TestEqual(TEXT("still creative"), Lab.Vitals.Mode, EFoundationPlayerMode::Creative);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationCommandsCheatsOffTest, "Unnamed.Foundation.Commands.CheatsOff",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationCommandsCheatsOffTest::RunTest(const FString& Parameters)
{
    // A world without "Allow Cheats" (Hardcore included): every command answers as an unknown one and changes nothing.
    FCommandLab Lab;
    Lab.bCommands = false;
    const bool bCreativeInventory = Lab.A.Options.bCreativeInventory;
    TestEqual(TEXT("gamemode refused"), Lab.Run(TEXT("/gamemode creative")),
        FString(UnknownCommand) + TEXT("\ngamemode creative<--[HERE]"));
    TestEqual(TEXT("mode unchanged"), Lab.Vitals.Mode, EFoundationPlayerMode::Survival);
    TestEqual(TEXT("creative rules unchanged"), Lab.A.Options.bCreativeInventory, bCreativeInventory);
    TestEqual(TEXT("kill refused"), Lab.Run(TEXT("/kill")), FString(UnknownCommand) + TEXT("\nkill<--[HERE]"));
    TestFalse(TEXT("alive"), Lab.Vitals.IsDead());
    TestEqual(TEXT("time refused"), Lab.Run(TEXT("/time set night")),
        FString(UnknownCommand) + TEXT("\ntime set night<--[HERE]"));
    TestEqual(TEXT("clock unchanged"), Lab.Clock.GetDayClock().TotalTicks, int64(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationCommandsGiveTest, "Unnamed.Foundation.Commands.Give",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationCommandsGiveTest::RunTest(const FString& Parameters)
{
    FCommandLab Lab;
    TestEqual(TEXT("three apples"), Lab.Run(TEXT("/give @s apple 3")), FString(TEXT("Gave 3 [Apple] to Player")));
    TestEqual(TEXT("in the inventory"), Lab.Count(TEXT("apple")), 3);
    TestEqual(TEXT("namespaced, count 1"), Lab.Run(TEXT("/give Player minecraft:apple")),
        FString(TEXT("Gave 1 [Apple] to Player")));
    TestEqual(TEXT("four"), Lab.Count(TEXT("apple")), 4);
    TestEqual(TEXT("over 100 stacks"), Lab.Run(TEXT("/give @s apple 6401")),
        FString(TEXT("Can't give more than 6400 of [Apple]")));
    TestEqual(TEXT("over 100 stacks gives nothing"), Lab.Count(TEXT("apple")), 4);
    // What does not fit drops at the player's feet, with no pickup delay.
    const int32 Space = Lab.P().Inventory.GetSpaceFor(Lab.A.Rules.FindItem(TEXT("stick")), 64);
    TestEqual(
        TEXT("a pile of sticks"), Lab.Run(TEXT("/give @s stick 6400")), FString(TEXT("Gave 6400 [Stick] to Player")));
    TestEqual(TEXT("sticks held"), Lab.Count(TEXT("stick")), Space);
    TestEqual(TEXT("sticks dropped"), Lab.DroppedCount(TEXT("stick")), 6400 - Space);
    if (TestTrue(TEXT("item entities"), Lab.A.GetItemEntities().Num() > 0))
    {
        const FFoundationItemEntity& Entity = Lab.A.GetItemEntities().Last();
        TestEqual(TEXT("no pickup delay"), Entity.PickupDelay, 0);
        TestTrue(TEXT("at the feet"), Entity.LocalCm.Equals(Lab.Feet - Lab.A.World.OriginCm, .01));
        TestTrue(TEXT("whole stacks"), Entity.Count <= 64);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationCommandsTeleportTest, "Unnamed.Foundation.Commands.Teleport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationCommandsTeleportTest::RunTest(const FString& Parameters)
{
    FCommandLab Lab;
    auto Where = [&Lab] { return FoundationCoordinates::ToMinecraft(Lab.Feet); };
    // Whole absolute x and z stand at the block's centre; y does not; a decimal is taken as written.
    TestEqual(TEXT("absolute"), Lab.Run(TEXT("/tp 100 70 -20")),
        FString(TEXT("Teleported Player to 100.500000, 70.000000, -19.500000")));
    TestTrue(TEXT("moved"), Where().Equals(FVector(100.5, 70, -19.5), 1e-6));
    TestEqual(TEXT("decimal"), Lab.Run(TEXT("/teleport 1.25 64 2.0")),
        FString(TEXT("Teleported Player to 1.250000, 64.000000, 2.000000")));
    // ~ is relative to where the player stands; a bare ~ keeps the axis.
    TestEqual(TEXT("relative"), Lab.Run(TEXT("/tp ~10 ~-1.5 ~")),
        FString(TEXT("Teleported Player to 11.250000, 62.500000, 2.000000")));
    TestTrue(TEXT("moved relative"), Where().Equals(FVector(11.25, 62.5, 2), 1e-6));
    TestEqual(TEXT("targets then location"), Lab.Run(TEXT("/tp @s ~ ~ ~1")),
        FString(TEXT("Teleported Player to 11.250000, 62.500000, 3.000000")));
    TestEqual(TEXT("to itself"), Lab.Run(TEXT("/tp @p")), FString(TEXT("Teleported Player to Player")));
    TestEqual(TEXT("four moves"), Lab.Teleports, 4);
    TestEqual(
        TEXT("out of the world"), Lab.Run(TEXT("/tp 30000001 64 0")), FString(TEXT("Invalid position for teleport")));
    TestEqual(TEXT("not moved"), Lab.Teleports, 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationCommandsTimeTest, "Unnamed.Foundation.Commands.Time",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationCommandsTimeTest::RunTest(const FString& Parameters)
{
    FCommandLab Lab;
    TestEqual(TEXT("set night"), Lab.Run(TEXT("/time set night")),
        FString(TEXT("Set minecraft:overworld to time marker minecraft:night")));
    TestEqual(TEXT("night"), Lab.Clock.GetTimeOfDay(), 13000);
    TestEqual(TEXT("add 1d"), Lab.Run(TEXT("/time add 1d")), FString(TEXT("Set minecraft:overworld to 37000 tick(s)")));
    TestEqual(TEXT("query day"), Lab.Run(TEXT("/time query day")),
        FString(TEXT("Timeline minecraft:day is at 13000 tick(s)")));
    TestEqual(TEXT("query time"), Lab.Run(TEXT("/time query time")),
        FString(TEXT("Clock minecraft:overworld is at 37000 tick(s)")));
    TestEqual(
        TEXT("set ticks"), Lab.Run(TEXT("/time set 1000")), FString(TEXT("Set minecraft:overworld to 1000 tick(s)")));
    TestEqual(TEXT("unknown marker"), Lab.Run(TEXT("/time set dusk")),
        FString(TEXT("Time marker minecraft:dusk does not exist for clock minecraft:overworld")));
    TestEqual(TEXT("markers are lower case"), Lab.Run(TEXT("/time set MINECRAFT:night")),
        FString(TEXT("Time marker MINECRAFT:night does not exist for clock minecraft:overworld")));
    TestEqual(TEXT("unchanged"), Lab.Clock.GetDayClock().TotalTicks, int64(1000));
    // add takes negative times (26.3's minimum is -2147483648); the total stops at 0.
    TestEqual(TEXT("add -1"), Lab.Run(TEXT("/time add -1")), FString(TEXT("Set minecraft:overworld to 999 tick(s)")));
    TestEqual(TEXT("add -1d"), Lab.Run(TEXT("/time add -1d")), FString(TEXT("Set minecraft:overworld to 0 tick(s)")));
    // Pausing a paused clock, or resuming a running one, is an error.
    TestEqual(TEXT("resume running"), Lab.Run(TEXT("/time resume")),
        FString(TEXT("Clock minecraft:overworld is already running")));
    TestEqual(TEXT("pause"), Lab.Run(TEXT("/time pause")), FString(TEXT("Paused clock minecraft:overworld")));
    TestEqual(TEXT("pause paused"), Lab.Run(TEXT("/time pause")),
        FString(TEXT("Clock minecraft:overworld is already paused")));
    TestTrue(TEXT("still paused"), Lab.Clock.GetDayClock().bPaused);
    TestEqual(TEXT("resume"), Lab.Run(TEXT("/time resume")), FString(TEXT("Resumed clock minecraft:overworld")));
    Lab.bClock = false;
    TestEqual(TEXT("no clock (the Playground)"), Lab.Run(TEXT("/time set day")),
        FString(TEXT("There is no default clock in dimension minecraft:overworld")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationCommandsKillTest, "Unnamed.Foundation.Commands.Kill",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationCommandsKillTest::RunTest(const FString& Parameters)
{
    FCommandLab Lab;
    TestEqual(TEXT("kill"), Lab.Run(TEXT("/kill")), FString(TEXT("Killed Player")));
    TestTrue(TEXT("dead"), Lab.Vitals.IsDead());
    FCommandLab Creative;
    Creative.Run(TEXT("/gamemode creative"));
    TestEqual(TEXT("kill in creative"), Creative.Run(TEXT("/kill @s")), FString(TEXT("Killed Player")));
    TestTrue(TEXT("through creative's invulnerability"), Creative.Vitals.IsDead());
    // @e kills every entity: the player and the item entities and experience orbs (commands.kill.success.multiple).
    FCommandLab Entities;
    const int32 Stone = Entities.A.Rules.FindItem(TEXT("stone"));
    Entities.A.SpawnItem(Stone, 1, FVector(50, 50, 300), FVector::ZeroVector, 10);
    Entities.A.SpawnItem(Stone, 2, FVector(250, 50, 300), FVector::ZeroVector, 10);
    TestEqual(TEXT("two item entities"), Entities.A.GetItemEntities().Num(), 2);
    TestEqual(TEXT("one orb"), Entities.A.SpawnExperience(FVector(150, 150, 300), 1, 10), 1);
    TestEqual(TEXT("kill @e"), Entities.Run(TEXT("/kill @e")), FString(TEXT("Killed 4 entities")));
    TestTrue(TEXT("@e: the player is dead"), Entities.Vitals.IsDead());
    TestEqual(TEXT("@e: no item entities left"), Entities.A.GetItemEntities().Num(), 0);
    TestEqual(TEXT("@e: no orbs left"), Entities.A.GetExperienceOrbs().Num(), 0);
    // Only the player: one entity, by name.
    FCommandLab Alone;
    TestEqual(TEXT("kill @e alone"), Alone.Run(TEXT("/kill @e")), FString(TEXT("Killed Player")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationChatTest, "Unnamed.Foundation.Commands.Chat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationChatTest::RunTest(const FString& Parameters)
{
    // The switch: 1 ships; 0 is the old local recording (the panel's branch), checked at runtime by the control.
    IConsoleVariable* Switch = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Chat.Commands"));
    if (TestNotNull(TEXT("foundation.Chat.Commands exists"), Switch))
    {
        if ((Switch->GetFlags() & ECVF_SetByMask) == ECVF_SetByConstructor)
            TestEqual(TEXT("ships at 1"), Switch->GetInt(), 1);
        const int32 Before = Switch->GetInt();
        Switch->Set(0, ECVF_SetByCode);
        TestFalse(TEXT("0: no commands"), FoundationChat::AreCommandsEnabled());
        Switch->Set(1, ECVF_SetByCode);
        TestTrue(TEXT("1: commands"), FoundationChat::AreCommandsEnabled());
        Switch->Set(Before, ECVF_SetByCode);
    }

    TestEqual(TEXT("full for 9 s"), FFoundationChat::GetAlpha(9), 1.f);
    TestEqual(TEXT("a quarter at 9.5 s"), FFoundationChat::GetAlpha(9.5), .25f);
    TestEqual(TEXT("gone at 10 s"), FFoundationChat::GetAlpha(10), 0.f);
    FFoundationChat Chat;
    for (int32 Index = 0; Index < 105; ++Index)
        Chat.AddText(FString::FromInt(Index), FoundationChat::White(), Index);
    TestEqual(TEXT("100 kept"), Chat.Num(), 100);
    TestEqual(TEXT("oldest dropped"), Chat.GetText(0), FString(TEXT("5")));
    TestEqual(TEXT("closed: the last 10 s"), Chat.GetVisible(104.5, false).Num(), 10);
    TestEqual(TEXT("fading"), Chat.GetVisible(104.5, false)[0].Alpha, .25f);
    TestEqual(TEXT("open: everything"), Chat.GetVisible(1000, true).Num(), 100);

    // The draw list: an empty chat adds nothing; a message draws its band and glyphs; long text wraps at 320 px.
    TArray<FHudGlyph> Glyphs;
    for (int32 Code = 32; Code < 127; ++Code)
    {
        FHudGlyph Glyph;
        Glyph.Code = Code;
        Glyph.Origin = FIntPoint(Code % 16 * 8, Code / 16 * 8);
        Glyph.InkWidth = Code == ' ' ? 0 : 5;
        Glyph.Advance = 6;
        Glyphs.Add(Glyph);
    }
    FHudSnapshot S;
    const FHudLayout Layout = HudModel::GetLayout({640, 360}, 0, false);
    // The closed chat's rectangle: its ten rows above the bottom offset, the bands' full width.
    const FIntRect ChatRect(0,
        360 - HudDrawList::ChatBottomOffset - HudDrawList::ChatClosedRows * HudDrawList::ChatLineHeight,
        HudDrawList::ChatWidth + 8, 360 - HudDrawList::ChatBottomOffset);
    auto DrawsInChat = [&ChatRect](const TArray<FHudDraw>& Draws)
    { return Draws.ContainsByPredicate([&ChatRect](const FHudDraw& Draw) { return Draw.Rect.Intersect(ChatRect); }); };
    S.Chat = {};
    const TArray<FHudDraw> Plain = HudDrawList::Build(S, FHudPresentation{}, Layout, 0, Glyphs);
    TestFalse(TEXT("empty chat: no draws in the chat's rectangle"), DrawsInChat(Plain));
    S.Chat = {{{{TEXT("<Player> hi"), FoundationChat::White()}}, .5f}};
    const TArray<FHudDraw> WithChat = HudDrawList::Build(S, FHudPresentation{}, Layout, 0, Glyphs);
    TestTrue(TEXT("a message draws in the chat's rectangle"), DrawsInChat(WithChat));
    // The band, then 10 glyphs with their shadows.
    TestEqual(TEXT("band and glyphs"), WithChat.Num() - Plain.Num(), 1 + 2 * 10);
    const FHudDraw* Band =
        WithChat.FindByPredicate([](const FHudDraw& Draw) { return Draw.Sprite.IsNone() && Draw.Color.A == .25f; });
    if (TestNotNull(TEXT("half-opaque band at half the line's opacity"), Band))
        TestEqual(TEXT("band rect"), Band->Rect, FIntRect(0, 360 - 40 - 9, 328, 360 - 40));
    // The open chat adds vanilla's input bar across the bottom and the typed line with its caret.
    S.bChatOpen = true;
    S.ChatInput = TEXT("/give");
    const TArray<FHudDraw> Open = HudDrawList::Build(S, FHudPresentation{}, Layout, 0, Glyphs);
    TestEqual(TEXT("bar, then 6 glyphs with shadows"), Open.Num() - WithChat.Num(), 1 + 2 * 6);
    S.ChatInput = FString::ChrN(200, 'a');
    // 6 px glyphs in 632 px: the line's last 105 characters (caret included) show, each with its shadow.
    TestEqual(TEXT("a long line shows its end"),
        HudDrawList::Build(S, FHudPresentation{}, Layout, 0, Glyphs).Num() - WithChat.Num(), 1 + 2 * 105);
    S.ChatInput = TEXT("/give");
    TestTrue(TEXT("the input bar"),
        Open.ContainsByPredicate(
            [](const FHudDraw& Draw)
            {
                return Draw.Sprite.IsNone() && Draw.Rect == FIntRect(2, 360 - 14, 638, 360 - 2) && Draw.Color.A == .5f;
            }));
    // 6 px glyphs: 53 fit in 320 px.
    const FString Words = FString::ChrN(40, 'a') + TEXT(" ") + FString::ChrN(30, 'b');
    TArray<TArray<FHudChatRun>> Rows = HudDrawList::WrapChat({{{{Words, FoundationChat::White()}}, 1.f}}, Glyphs);
    if (TestEqual(TEXT("wraps at the space"), Rows.Num(), 2))
    {
        TestEqual(TEXT("first row"), Rows[0][0].Text, FString::ChrN(40, 'a') + TEXT(" "));
        TestEqual(TEXT("second row"), Rows[1][0].Text, FString::ChrN(30, 'b'));
    }
    Rows = HudDrawList::WrapChat({{{{FString::ChrN(60, 'a'), FoundationChat::White()}}, 1.f}}, Glyphs);
    if (TestEqual(TEXT("breaks a long word"), Rows.Num(), 2))
        TestEqual(TEXT("as much as fits"), Rows[0][0].Text, FString::ChrN(53, 'a'));
    Rows = HudDrawList::WrapChat({{{{FString::ChrN(53, 'a') + TEXT(" b"), FoundationChat::White()}}, 1.f}}, Glyphs);
    if (TestEqual(TEXT("the breaking space"), Rows.Num(), 2))
        TestEqual(TEXT("is dropped"), Rows[1][0].Text, FString(TEXT("b")));
    Rows = HudDrawList::WrapChat(
        {{{{TEXT("ab"), FoundationChat::Gray()}, {TEXT("cd"), FoundationChat::Red()}}, 1.f}}, Glyphs);
    if (TestEqual(TEXT("one row"), Rows.Num(), 1) && TestEqual(TEXT("two runs"), Rows[0].Num(), 2))
        TestEqual(TEXT("colours kept"), Rows[0][1].Color, FoundationChat::Red());

    // The snapshot's == (the presenter's redraw guard) sees a change of letter case in its strings.
    FHudSnapshot A, B;
    A.ChatInput = TEXT("/time");
    B.ChatInput = TEXT("/TIME");
    TestFalse(TEXT("typed line: case counts"), A == B);
    B.ChatInput = A.ChatInput;
    TestTrue(TEXT("equal snapshots"), A == B);
    A.Chat = {{{{TEXT("hi"), FoundationChat::White()}}, 1.f}};
    B.Chat = {{{{TEXT("HI"), FoundationChat::White()}}, 1.f}};
    TestFalse(TEXT("chat text: case counts"), A == B);
    B.Chat = A.Chat;
    A.ItemName = TEXT("Apple");
    B.ItemName = TEXT("apple");
    TestFalse(TEXT("item name: case counts"), A == B);
    B.ItemName = A.ItemName;
    B.Level = 1;
    TestFalse(TEXT("other members still compared"), A == B);
    return true;
}
#endif
