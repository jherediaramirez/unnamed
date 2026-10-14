#include "Misc/AutomationTest.h"
#include "Foundation/FoundationBreakAssets.h"
#include "Foundation/FoundationBreakEffects.h"
#include "Foundation/FoundationSoundAssets.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"
#include "UnnamedProjectSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags SoundTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // Reference/minecraft-sounds-26.3's named events, in its order; the step and fall events follow (86 each), then the
    // appended named events of B0's batch (133), then the step and fall events of the sound types survival B1 added (3
    // each; Reference/minecraft-sounds-26.3/README.md, "Events").
    const TCHAR* const SoundNamedEvents[] = {TEXT("entity.item.pickup"), TEXT("entity.item.break"),
        TEXT("entity.generic.eat"), TEXT("entity.player.burp"), TEXT("entity.player.hurt"),
        TEXT("entity.player.hurt_drown"), TEXT("entity.player.hurt_freeze"), TEXT("entity.player.hurt_on_fire"),
        TEXT("entity.player.hurt_sweet_berry_bush"), TEXT("entity.player.death"), TEXT("entity.player.small_fall"),
        TEXT("entity.player.big_fall"), TEXT("item.axe.strip"), TEXT("block.furnace.fire_crackle"),
        TEXT("block.barrel.open"), TEXT("block.barrel.close"), TEXT("block.chest.open"), TEXT("block.chest.close"),
        TEXT("entity.player.splash"), TEXT("entity.player.splash.high_speed"), TEXT("entity.player.swim")};
    constexpr int32 SoundStepEvents = 86, SoundFallEvents = 86, SoundAppendedEvents = 133;
    constexpr int32 SoundAppendedStepEvents = 3, SoundAppendedFallEvents = 3;

    FString SoundPackageOf(const UObject* Object)
    {
        return Object ? Object->GetOutermost()->GetName() : FString();
    }

    const FFoundationSoundEvent* SoundFindEvent(const UFoundationSoundAssets& Assets, const TCHAR* Name)
    {
        return Assets.Events.FindByPredicate([Name](const FFoundationSoundEvent& E) { return E.Name == FName(Name); });
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationSoundPickTest, "Unnamed.Foundation.Sound.Pick", SoundTestFlags)

bool FFoundationSoundPickTest::RunTest(const FString&)
{
    using namespace FoundationBreak;
    FFoundationSoundEvent Event;
    Event.Name = TEXT("item.axe.strip");
    for (int32 Index = 0; Index < 4; ++Index)
    {
        FFoundationBreakSoundFile File;
        File.Name = *FString::Printf(TEXT("item/axe/strip%d"), Index + 1);
        File.Volume = 0.9f;
        File.Pitch = Index == 3 ? 0.85f : 1.f;
        Event.Files.Add(File);
    }
    FRandomStream Random(7);
    TSet<int32> Used;
    for (int32 Play = 0; Play < 200; ++Play)
    {
        const FSoundPlay Picked = PickEventSound(Event, 0.5f, 1.2f, Random);
        if (!Event.Files.IsValidIndex(Picked.File)) return TestTrue(TEXT("file in range"), false);
        Used.Add(Picked.File);
        const FFoundationBreakSoundFile& File = Event.Files[Picked.File];
        TestTrue(
            TEXT("volume is the caller's times the file's"), FMath::IsNearlyEqual(Picked.Volume, 0.5f * File.Volume));
        TestTrue(TEXT("pitch is the caller's times the file's"), FMath::IsNearlyEqual(Picked.Pitch, 1.2f * File.Pitch));
    }
    TestEqual(TEXT("every file is reachable"), Used.Num(), 4);
    // One draw per play: the file.
    FRandomStream Played(3), Mirror(3);
    TestEqual(TEXT("file draw"), PickEventSound(Event, 1, 1, Played).File, Mirror.RandRange(0, 3));
    TestEqual(TEXT("empty event"), PickEventSound(FFoundationSoundEvent(), 1, 1, Random).File, int32(INDEX_NONE));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationSoundAssetsTest, "Unnamed.Foundation.Sound.Assets", SoundTestFlags)

bool FFoundationSoundAssetsTest::RunTest(const FString&)
{
    const UFoundationSoundAssets* Assets = LoadObject<UFoundationSoundAssets>(
        nullptr, *UUnnamedProjectSettings::GetSoundAssetPath(TEXT("DA_Sounds")).ToString());
    if (!Assets)
    {
        AddError(TEXT("DA_Sounds is missing under SoundRoot: run Scripts/Editor/ImportSounds.py"));
        return false;
    }
    const FString SoundRoot = UUnnamedProjectSettings::Get().SoundRoot;
    const FString BreakRoot = UUnnamedProjectSettings::Get().BreakRoot;
    TestFalse(TEXT("source digest recorded"), Assets->SourceSha256.IsEmpty());

    // Every event resolves: named first in the reference's order, then the step and fall events; every file is an
    // imported wave with a length, under SoundRoot or (the block reference's files) BreakRoot; every event has its
    // attenuation, 12 blocks for the chest and 16 otherwise.
    const int32 Named = UE_ARRAY_COUNT(SoundNamedEvents);
    const int32 Appended = Named + SoundStepEvents + SoundFallEvents;
    const int32 AppendedTypes = Appended + SoundAppendedEvents;
    TestEqual(
        TEXT("event count"), Assets->Events.Num(), AppendedTypes + SoundAppendedStepEvents + SoundAppendedFallEvents);
    for (int32 Index = 0; Index < Assets->Events.Num(); ++Index)
    {
        const FFoundationSoundEvent& Event = Assets->Events[Index];
        const FString Name = Event.Name.ToString();
        if (Index < Named)
            TestEqual(TEXT("named event order"), Name, FString(SoundNamedEvents[Index]));
        else if (Index < Appended)
            TestTrue(FString::Printf(TEXT("%s is a step or fall event"), *Name),
                Name.EndsWith(Index < Named + SoundStepEvents ? TEXT(".step") : TEXT(".fall")));
        else if (Index >= AppendedTypes)
            TestTrue(FString::Printf(TEXT("%s is an appended step or fall event"), *Name),
                Name.EndsWith(Index < AppendedTypes + SoundAppendedStepEvents ? TEXT(".step") : TEXT(".fall")));
        TestTrue(FString::Printf(TEXT("%s has files"), *Name), !Event.Files.IsEmpty());
        for (const FFoundationBreakSoundFile& File : Event.Files)
        {
            const USoundWave* Wave = File.Wave.Get();
            const FString Package = SoundPackageOf(Wave);
            const FString Expected = File.Name.ToString();
            const bool bOwn = Package == SoundRoot / TEXT("Sounds") / Expected;
            const bool bBlock = Package == BreakRoot / TEXT("Sounds") / Expected;
            if (!TestTrue(FString::Printf(TEXT("%s: %s is its wave (%s)"), *Name, *Expected, *Package),
                    Wave && (bOwn || bBlock)))
                continue;
            TestTrue(FString::Printf(TEXT("%s: %s has a length"), *Name, *Expected), Wave->GetDuration() > 0);
        }
        const USoundAttenuation* Attenuation = Event.Attenuation.Get();
        if (!TestNotNull(*FString::Printf(TEXT("%s attenuation"), *Name), Attenuation)) continue;
        const float Blocks = Name.StartsWith(TEXT("block.chest.")) ? 12 : 16;
        TestTrue(FString::Printf(TEXT("%s falls off over %g blocks"), *Name, Blocks),
            FMath::IsNearlyEqual(Attenuation->Attenuation.FalloffDistance, Blocks * 100));
    }

    // The appended events start with T5's buttons and end with the batch's last line.
    auto EventAt = [&](int32 Index)
    { return Assets->Events.IsValidIndex(Index) ? Assets->Events[Index].Name.ToString() : FString(TEXT("(none)")); };
    TestEqual(TEXT("first appended event"), EventAt(Appended), FString(TEXT("block.bamboo_wood_button.click_off")));
    TestEqual(TEXT("last appended event"), EventAt(Appended + SoundAppendedEvents - 1),
        FString(TEXT("entity.generic.swim")));
    TestEqual(TEXT("first appended step event"), EventAt(AppendedTypes), FString(TEXT("block.cherry_sapling.step")));
    TestEqual(TEXT("last appended fall event"),
        EventAt(AppendedTypes + SoundAppendedStepEvents + SoundAppendedFallEvents - 1),
        FString(TEXT("block.weeping_vines.fall")));

    // Spot values from sounds.json.
    if (const FFoundationSoundEvent* Pickup = SoundFindEvent(*Assets, TEXT("entity.item.pickup")))
    {
        if (TestEqual(TEXT("pickup files"), Pickup->Files.Num(), 1))
        {
            TestEqual(TEXT("pickup file"), Pickup->Files[0].Name.ToString(), FString(TEXT("random/pop")));
            TestEqual(TEXT("pickup wave"), SoundPackageOf(Pickup->Files[0].Wave.Get()),
                SoundRoot / TEXT("Sounds/random/pop"));
        }
    }
    else
        AddError(TEXT("entity.item.pickup is missing"));
    if (const FFoundationSoundEvent* Strip = SoundFindEvent(*Assets, TEXT("item.axe.strip")))
        for (const FFoundationBreakSoundFile& File : Strip->Files)
            TestTrue(TEXT("strip volume 0.9"), FMath::IsNearlyEqual(File.Volume, 0.9f));
    const FFoundationSoundEvent* Hurt = SoundFindEvent(*Assets, TEXT("entity.player.hurt"));
    const FFoundationSoundEvent* Death = SoundFindEvent(*Assets, TEXT("entity.player.death"));
    if (TestNotNull(TEXT("hurt"), Hurt) && TestNotNull(TEXT("death"), Death) &&
        TestEqual(TEXT("death files"), Death->Files.Num(), Hurt->Files.Num()))
        for (int32 Index = 0; Index < Hurt->Files.Num(); ++Index)
            TestTrue(TEXT("death plays the hurt waves"), Death->Files[Index].Wave == Hurt->Files[Index].Wave);

    // The blocks: DA_BreakAssets' order; stone steps on stone, water and lava are silent, and amethyst's step mixes
    // the block reference's files with the sound reference's.
    const UFoundationBreakAssets* Break = LoadObject<UFoundationBreakAssets>(
        nullptr, *UUnnamedProjectSettings::GetBreakAssetPath(TEXT("DA_BreakAssets")).ToString());
    if (!TestNotNull(TEXT("DA_BreakAssets"), Break)) return false;
    if (TestEqual(TEXT("block count"), Assets->Blocks.Num(), Break->Blocks.Num()))
        for (int32 Index = 0; Index < Assets->Blocks.Num(); ++Index)
            if (Assets->Blocks[Index].Id != Break->Blocks[Index].Id)
            {
                AddError(FString::Printf(TEXT("block %d is %s, DA_BreakAssets has %s"), Index,
                    *Assets->Blocks[Index].Id.ToString(), *Break->Blocks[Index].Id.ToString()));
                break;
            }
    auto Block = [&](const TCHAR* Id)
    { return Assets->Blocks.FindByPredicate([Id](const FFoundationSoundBlock& B) { return B.Id == FName(Id); }); };
    auto EventName = [&](int32 Index)
    { return Assets->Events.IsValidIndex(Index) ? Assets->Events[Index].Name.ToString() : FString(TEXT("(none)")); };
    if (const FFoundationSoundBlock* Stone = Block(TEXT("stone")))
    {
        TestEqual(TEXT("stone step"), EventName(Stone->StepEvent), FString(TEXT("block.stone.step")));
        TestEqual(TEXT("stone fall"), EventName(Stone->FallEvent), FString(TEXT("block.stone.fall")));
    }
    else
        AddError(TEXT("stone is not in DA_Sounds"));
    for (const TCHAR* Fluid : {TEXT("water"), TEXT("lava")})
        if (const FFoundationSoundBlock* Entry = Block(Fluid))
            TestTrue(FString::Printf(TEXT("%s is silent"), Fluid),
                Entry->StepEvent == INDEX_NONE && Entry->FallEvent == INDEX_NONE);
    if (const FFoundationSoundBlock* Amethyst = Block(TEXT("amethyst_block")))
    {
        TestEqual(TEXT("amethyst step"), EventName(Amethyst->StepEvent), FString(TEXT("block.amethyst_block.step")));
        int32 Own = 0, Shared = 0;
        if (Assets->Events.IsValidIndex(Amethyst->StepEvent))
            for (const FFoundationBreakSoundFile& File : Assets->Events[Amethyst->StepEvent].Files)
                (SoundPackageOf(File.Wave.Get()).StartsWith(SoundRoot + TEXT("/")) ? Own : Shared) += 1;
        TestEqual(TEXT("amethyst step files from the sound reference"), Own, 2);
        TestEqual(TEXT("amethyst step files from the block reference"), Shared, 12);
    }
    else
        AddError(TEXT("amethyst_block is not in DA_Sounds"));
    return true;
}
#endif
