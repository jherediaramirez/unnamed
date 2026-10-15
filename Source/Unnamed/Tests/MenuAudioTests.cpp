#include "Misc/AutomationTest.h"
#include "Menu/MenuAudioSettings.h"
#include "Menu/MenuBassShelfPreset.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags MenuAudioTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr float AudioRate = 48000;

    // A 1 s sine through Render (interleaved, Channels wide, every channel the same tone), in blocks of 512 frames;
    // the gain in dB is the RMS out over in after the first 0.25 s (the shelf has settled).
    template <typename FRender> double MeasureGainDb(double Hz, int32 Channels, FRender&& Render)
    {
        constexpr int32 Block = 512, Frames = int32(AudioRate);
        TArray<float> In, Out;
        In.SetNumZeroed(Block * Channels);
        Out.SetNumZeroed(Block * Channels);
        double SumIn = 0, SumOut = 0;
        for (int32 Start = 0; Start < Frames; Start += Block)
        {
            for (int32 Frame = 0; Frame < Block; ++Frame)
                for (int32 Channel = 0; Channel < Channels; ++Channel)
                    In[Frame * Channels + Channel] =
                        .5f * float(FMath::Sin(2 * UE_DOUBLE_PI * Hz * (Start + Frame) / AudioRate));
            Render(In, Out, Block);
            if (Start < Frames / 4) continue;
            for (int32 Index = 0; Index < Block * Channels; ++Index)
            {
                SumIn += double(In[Index]) * In[Index];
                SumOut += double(Out[Index]) * Out[Index];
            }
        }
        return 10 * FMath::LogX(10., SumOut / SumIn);
    }

    // The engine's RBJ low shelf at 175 Hz, +5 dB, 48 kHz (S = 1): its response at these frequencies.
    struct FShelfPoint
    {
        double Hz, GainDb;
    };
    const FShelfPoint ShelfPoints[] = {{50, 4.965}, {100, 4.499}, {200, 1.864}, {1000, .005}, {5000, 0}};
} // namespace

// The Bass shelf rendered offline (no device, no recording): sines through MenuBass::FShelf at +5 dB match the shelf's
// response, less the -1.5 dB makeup; at 0 dB it passes everything unchanged.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuAudioBassShelfTest, "Unnamed.Menu.Audio.BassShelf", MenuAudioTestFlags)

bool FMenuAudioBassShelfTest::RunTest(const FString& Parameters)
{
    const double MakeupDb = MenuBass::MakeupPerDb * MenuBass::MaxGainDb;
    TestEqual(TEXT("makeup is -1.5 dB at +5"), MakeupDb, -1.5, 1e-6);
    for (const FShelfPoint& Point : ShelfPoints)
    {
        MenuBass::FShelf Shelf;
        Shelf.Init(AudioRate, 1, MenuBass::MaxGainDb);
        const double Gain =
            MeasureGainDb(Point.Hz, 1, [&Shelf](const TArray<float>& In, TArray<float>& Out, int32 Frames)
                { Shelf.Process(In.GetData(), Out.GetData(), Frames); });
        AddInfo(FString::Printf(
            TEXT("%.0f Hz: %+.3f dB (shelf %+.3f, makeup %+.2f)"), Point.Hz, Gain, Point.GainDb, MakeupDb));
        TestEqual(FString::Printf(TEXT("%.0f Hz gain"), Point.Hz), Gain, Point.GainDb + MakeupDb, .1);
    }
    MenuBass::FShelf Flat;
    Flat.Init(AudioRate, 1, 0);
    for (const double Hz : {50., 1000.})
        TestEqual(FString::Printf(TEXT("0 dB passes %.0f Hz"), Hz),
            MeasureGainDb(Hz, 1, [&Flat](const TArray<float>& In, TArray<float>& Out, int32 Frames)
                { Flat.Process(In.GetData(), Out.GetData(), Frames); }),
            0., .01);
    return true;
}

// The submix effect on hand-built stereo buffers (no preset: the default +5 dB): each channel keeps its own filter
// state, so a 50 Hz left and a 1 kHz right get their own gains.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuAudioBassEffectTest, "Unnamed.Menu.Audio.BassEffect", MenuAudioTestFlags)

bool FMenuAudioBassEffectTest::RunTest(const FString& Parameters)
{
    FMenuBassShelf Effect;
    FSoundEffectSubmixInitData Init;
    Init.SampleRate = AudioRate;
    Effect.Init(Init);
    constexpr int32 Block = 512, Frames = int32(AudioRate);
    Audio::FAlignedFloatBuffer In, Out;
    In.SetNumZeroed(Block * 2);
    Out.SetNumZeroed(Block * 2);
    double Sum[2][2] = {};
    for (int32 Start = 0; Start < Frames; Start += Block)
    {
        for (int32 Frame = 0; Frame < Block; ++Frame)
        {
            const double T = double(Start + Frame) / AudioRate;
            In[Frame * 2] = .5f * float(FMath::Sin(2 * UE_DOUBLE_PI * 50 * T));
            In[Frame * 2 + 1] = .5f * float(FMath::Sin(2 * UE_DOUBLE_PI * 1000 * T));
        }
        FSoundEffectSubmixInputData InData;
        InData.NumFrames = Block;
        InData.NumChannels = 2;
        InData.AudioBuffer = &In;
        FSoundEffectSubmixOutputData OutData;
        OutData.AudioBuffer = &Out;
        OutData.NumChannels = 2;
        Effect.OnProcessAudio(InData, OutData);
        if (Start < Frames / 4) continue;
        for (int32 Index = 0; Index < Block * 2; ++Index)
        {
            Sum[Index % 2][0] += double(In[Index]) * In[Index];
            Sum[Index % 2][1] += double(Out[Index]) * Out[Index];
        }
    }
    const double Left = 10 * FMath::LogX(10., Sum[0][1] / Sum[0][0]);
    const double Right = 10 * FMath::LogX(10., Sum[1][1] / Sum[1][0]);
    AddInfo(FString::Printf(TEXT("stereo: 50 Hz left %+.3f dB, 1 kHz right %+.3f dB"), Left, Right));
    TestEqual(TEXT("50 Hz left: +4.97 - 1.5"), Left, 4.965 - 1.5, .1);
    TestEqual(TEXT("1 kHz right: makeup only"), Right, -1.5, .1);
    TestEqual(TEXT("every block went through"), Effect.GetProcessedBlocks(), (Frames + Block - 1) / Block);
    return true;
}

// The Bass preference: its schema row, and the gain the settings attach (0 removes the effect: vanilla).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuAudioBassPreferenceTest, "Unnamed.Menu.Audio.BassPreference", MenuAudioTestFlags)

bool FMenuAudioBassPreferenceTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("slider 1 is +5 dB"), UMenuAudioSettings::GetBassGainDb(1, true), 5.f);
    TestEqual(TEXT("slider 0.5 is +2.5 dB"), UMenuAudioSettings::GetBassGainDb(.5, true), 2.5f);
    TestEqual(TEXT("slider 0 is no shelf"), UMenuAudioSettings::GetBassGainDb(0, true), 0.f);
    TestEqual(TEXT("switch 0 is no shelf"), UMenuAudioSettings::GetBassGainDb(1, false), 0.f);
    TestEqual(TEXT("clamped above 1"), UMenuAudioSettings::GetBassGainDb(3, true), 5.f);

    FString Text;
    const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Menu/Web/preferences-schema.menuprefs"));
    if (!TestTrue(TEXT("schema readable"), FFileHelper::LoadFileToString(Text, *Path))) return false;
    TSharedPtr<FJsonObject> Schema;
    if (!TestTrue(TEXT("schema parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Schema)))
        return false;
    const TSharedPtr<FJsonObject>* Settings = nullptr;
    const TSharedPtr<FJsonObject>* Bass = nullptr;
    if (!TestTrue(TEXT("settings"), Schema->TryGetObjectField(TEXT("settings"), Settings)) ||
        !TestTrue(TEXT("sound_bass is a preference"), (*Settings)->TryGetObjectField(TEXT("sound_bass"), Bass)))
        return false;
    TestEqual(TEXT("102 preferences"), (*Settings)->Values.Num(), 102);
    TestEqual(TEXT("min"), (*Bass)->GetNumberField(TEXT("min")), 0.);
    TestEqual(TEXT("max"), (*Bass)->GetNumberField(TEXT("max")), 1.);
    TestEqual(TEXT("step"), (*Bass)->GetNumberField(TEXT("step")), .01);
    TestEqual(TEXT("on by default (+5 dB)"), (*Bass)->GetNumberField(TEXT("default")), 1.);
    return true;
}
#endif
