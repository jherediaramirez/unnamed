#include "Voxel/VoxelSkyReference.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sky/VoxelTimeline.h"
#include "Voxel/VoxelWorldgenJson.h"

namespace
{
    // The engine's TryGet*Field converts strings and numbers into each other; the reference's fields must hold their
    // own type.
    bool SkyNumberField(const FJsonObject& Object, const TCHAR* Name, double& Out)
    {
        const TSharedPtr<FJsonValue> Value = Object.TryGetField(Name);
        return Value && Value->Type == EJson::Number && Value->TryGetNumber(Out);
    }

    bool SkyStringField(const FJsonObject& Object, const TCHAR* Name, FString& Out)
    {
        const TSharedPtr<FJsonValue> Value = Object.TryGetField(Name);
        return Value && Value->Type == EJson::String && Value->TryGetString(Out);
    }

    bool SkyBoolField(const FJsonObject& Object, const TCHAR* Name, bool& Out)
    {
        const TSharedPtr<FJsonValue> Value = Object.TryGetField(Name);
        return Value && Value->Type == EJson::Boolean && Value->TryGetBool(Out);
    }

    // A whole tick that fits an int32.
    bool SkyWholeTick(double Ticks)
    {
        return Ticks == FMath::FloorToDouble(Ticks) && Ticks >= 0 && Ticks <= MAX_int32;
    }

    // A keyframe value's type and its timeline value; false for a type a track can't hold.
    bool SkyReadKeyValue(
        const FJsonValue& Json, FVoxelTimelineTrack& Track, EVoxelTimelineValue& OutType, FVector4f& OutValue)
    {
        switch (Json.Type)
        {
        case EJson::Number:
            OutType = EVoxelTimelineValue::Float;
            OutValue = FVector4f(float(Json.AsNumber()), 0.f, 0.f, 0.f);
            return true;
        case EJson::Boolean:
            OutType = EVoxelTimelineValue::Bool;
            OutValue = FVector4f(Json.AsBool() ? 1.f : 0.f, 0.f, 0.f, 0.f);
            return true;
        case EJson::String:
        {
            const FString Text = Json.AsString();
            if (Text.StartsWith(TEXT("#")))
            {
                OutType = EVoxelTimelineValue::Color;
                return VoxelSky::ParseColor(Text, OutValue);
            }
            OutType = EVoxelTimelineValue::Name;
            // Names compare case-sensitively, as the keys do (FString's own comparisons ignore case).
            int32 Index = Track.Names.IndexOfByPredicate(
                [&Text](const FString& Name) { return Name.Equals(Text, ESearchCase::CaseSensitive); });
            if (Index == INDEX_NONE) Index = Track.Names.Add(Text);
            OutValue = FVector4f(float(Index), 0.f, 0.f, 0.f);
            return true;
        }
        default:
            return false;
        }
    }

    bool SkyReadEase(const FJsonValue& Json, FVoxelTimelineTrack& Track)
    {
        if (Json.Type == EJson::String)
        {
            const FString Name = Json.AsString();
            if (VoxelWorldgenJson::IsWorldgenKey(Name, TEXT("linear")) ||
                VoxelWorldgenJson::IsWorldgenKey(Name, TEXT("constant")))
            {
                Track.Ease = Name == TEXT("linear") ? EVoxelTimelineEase::Linear : EVoxelTimelineEase::Constant;
                return true;
            }
            return false;
        }
        const TSharedPtr<FJsonObject>* Object = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
        if (!Json.TryGetObject(Object) || !VoxelWorldgenJson::HasOnlyWorldgenKeys(**Object, {TEXT("cubic_bezier")}) ||
            !(*Object)->TryGetArrayField(TEXT("cubic_bezier"), Points) || Points->Num() != 4)
        {
            return false;
        }
        for (int32 Index = 0; Index < 4; ++Index)
        {
            if ((*Points)[Index]->Type != EJson::Number) return false;
            Track.Bezier[Index] = float((*Points)[Index]->AsNumber());
        }
        // x must stay within 0..1 for the curve to be a function of the progress.
        if (Track.Bezier.X < 0.f || Track.Bezier.X > 1.f || Track.Bezier.Z < 0.f || Track.Bezier.Z > 1.f) return false;
        Track.Ease = EVoxelTimelineEase::CubicBezier;
        return true;
    }

    bool SkyReadModifier(const FString& Name, EVoxelTimelineModifier& Out)
    {
        static const TPair<const TCHAR*, EVoxelTimelineModifier> Modifiers[] = {
            {TEXT("override"), EVoxelTimelineModifier::Override}, {TEXT("multiply"), EVoxelTimelineModifier::Multiply},
            {TEXT("maximum"), EVoxelTimelineModifier::Maximum}, {TEXT("or"), EVoxelTimelineModifier::Or},
            {TEXT("and"), EVoxelTimelineModifier::And}};
        for (const TPair<const TCHAR*, EVoxelTimelineModifier>& Modifier : Modifiers)
        {
            if (VoxelWorldgenJson::IsWorldgenKey(Name, Modifier.Key))
            {
                Out = Modifier.Value;
                return true;
            }
        }
        return false;
    }

    bool SkyReadTrack(const FString& Id, const FString& Attribute, const FJsonValue& Json, FVoxelTimelineTrack& Track,
        FString& OutError)
    {
        const FString Where = FString::Printf(TEXT("%s: track %s"), *Id, *Attribute);
        const TSharedPtr<FJsonObject>* Object = nullptr;
        FString Unexpected;
        if (!Json.TryGetObject(Object))
        {
            OutError = Where + TEXT(" is not an object");
            return false;
        }
        if (VoxelWorldgenJson::FindUnexpectedWorldgenKey(
                **Object, {TEXT("keyframes"), TEXT("ease"), TEXT("modifier")}, Unexpected))
        {
            OutError = FString::Printf(TEXT("%s: unexpected field \"%s\""), *Where, *Unexpected);
            return false;
        }
        Track.Attribute = Attribute;
        if (const TSharedPtr<FJsonValue> Ease = (*Object)->TryGetField(TEXT("ease"));
            Ease && !SkyReadEase(*Ease, Track))
        {
            OutError = Where + TEXT(": unknown ease");
            return false;
        }
        FString Modifier;
        if ((*Object)->Values.Contains(TEXT("modifier")) &&
            (!SkyStringField(**Object, TEXT("modifier"), Modifier) || !SkyReadModifier(Modifier, Track.Modifier)))
        {
            OutError = FString::Printf(
                TEXT("%s: modifier \"%s\" is not override, multiply, maximum, or or and"), *Where, *Modifier);
            return false;
        }
        const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
        if (!(*Object)->TryGetArrayField(TEXT("keyframes"), Keys) || Keys->IsEmpty())
        {
            OutError = Where + TEXT(": no keyframes");
            return false;
        }
        for (int32 Index = 0; Index < Keys->Num(); ++Index)
        {
            const TSharedPtr<FJsonObject>* Key = nullptr;
            double Ticks = 0;
            EVoxelTimelineValue Type = EVoxelTimelineValue::Float;
            FVoxelTimelineKey Out;
            if (!(*Keys)[Index]->TryGetObject(Key) ||
                !VoxelWorldgenJson::HasOnlyWorldgenKeys(**Key, {TEXT("ticks"), TEXT("value")}) ||
                !SkyNumberField(**Key, TEXT("ticks"), Ticks) || !SkyWholeTick(Ticks) ||
                !(*Key)->Values.Contains(TEXT("value")) ||
                !SkyReadKeyValue(*(*Key)->Values[TEXT("value")], Track, Type, Out.Value))
            {
                OutError = FString::Printf(TEXT("%s: keyframe %d is not {ticks, value} with a whole tick and a number, "
                                                "colour, boolean or name"),
                    *Where, Index);
                return false;
            }
            if (Index > 0 && Type != Track.Type)
            {
                OutError = FString::Printf(TEXT("%s: keyframe %d's value type differs from the first"), *Where, Index);
                return false;
            }
            Track.Type = Type;
            Out.Ticks = int32(Ticks);
            Track.Keys.Add(Out);
        }
        Track.SortKeys();
        return true;
    }
}

FString VoxelSkyReference::GetReferenceRoot()
{
    return FPaths::ConvertRelativePathToFull(
        FPaths::Combine(FPaths::ProjectDir(), TEXT("Reference/minecraft-sky-26.3")));
}

bool VoxelSkyReference::ParseTimeline(const FString& Text, const FString& Id, FVoxelTimeline& Out, FString& OutError)
{
    Out = FVoxelTimeline();
    Out.Id = Id;
    const TSharedPtr<FJsonValue> Json = VoxelWorldgenJson::ParseWorldgenJson(Text);
    const TSharedPtr<FJsonObject>* Object = nullptr;
    FString Unexpected;
    if (!Json || !Json->TryGetObject(Object))
    {
        OutError = Id + TEXT(": not a JSON object");
        return false;
    }
    if (VoxelWorldgenJson::FindUnexpectedWorldgenKey(
            **Object, {TEXT("clock"), TEXT("period_ticks"), TEXT("time_markers"), TEXT("tracks")}, Unexpected))
    {
        OutError = FString::Printf(TEXT("%s: unexpected field \"%s\""), *Id, *Unexpected);
        return false;
    }
    // period_ticks is optional: a timeline without one doesn't repeat (minecraft.wiki, Timeline).
    double Period = 0;
    if (!SkyStringField(**Object, TEXT("clock"), Out.Clock) ||
        ((*Object)->HasField(TEXT("period_ticks")) &&
            (!SkyNumberField(**Object, TEXT("period_ticks"), Period) || Period != FMath::FloorToDouble(Period) ||
                Period < 1 || Period > MAX_int32)))
    {
        OutError = Id + TEXT(": needs a clock, and a period_ticks that is a whole number of ticks");
        return false;
    }
    Out.PeriodTicks = int32(Period);
    if (const TSharedPtr<FJsonValue> Markers = (*Object)->TryGetField(TEXT("time_markers")))
    {
        const TSharedPtr<FJsonObject>* MarkerObject = nullptr;
        if (!Markers->TryGetObject(MarkerObject))
        {
            OutError = Id + TEXT(": time_markers is not an object");
            return false;
        }
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*MarkerObject)->Values)
        {
            // A marker is its tick, or {ticks, show_in_commands} (minecraft.wiki, Timeline).
            FVoxelTimelineMarker Marker;
            Marker.Name = Field.Key;
            double Ticks = -1;
            const TSharedPtr<FJsonObject>* Full = nullptr;
            bool bRead = Field.Value->Type == EJson::Number && Field.Value->TryGetNumber(Ticks);
            if (!bRead && Field.Value->TryGetObject(Full))
            {
                bRead = VoxelWorldgenJson::HasOnlyWorldgenKeys(**Full, {TEXT("ticks"), TEXT("show_in_commands")}) &&
                    SkyNumberField(**Full, TEXT("ticks"), Ticks) &&
                    (!(*Full)->HasField(TEXT("show_in_commands")) ||
                        SkyBoolField(**Full, TEXT("show_in_commands"), Marker.bShowInCommands));
            }
            if (!bRead || !SkyWholeTick(Ticks) || (Out.PeriodTicks > 0 && Ticks > Out.PeriodTicks))
            {
                OutError = FString::Printf(TEXT("%s: time marker %s is not a tick within the period"), *Id, *Field.Key);
                return false;
            }
            Marker.Ticks = int32(Ticks);
            Out.Markers.Add(Marker);
        }
    }
    const TSharedPtr<FJsonObject>* Tracks = nullptr;
    if (!(*Object)->TryGetObjectField(TEXT("tracks"), Tracks))
    {
        OutError = Id + TEXT(": no tracks");
        return false;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*Tracks)->Values)
    {
        if (!SkyReadTrack(Id, Field.Key, *Field.Value, Out.Tracks.AddDefaulted_GetRef(), OutError)) return false;
    }
    return Out.Validate(OutError);
}

bool VoxelSkyReference::ReadTimeline(const FString& Root, const FString& Id, FVoxelTimeline& Out, FString& OutError)
{
    FString Path;
    FString Text;
    if (!VoxelWorldgenJson::WorldgenIdToPath(Id, TEXT("timeline"), Path) ||
        !FFileHelper::LoadFileToString(Text, *FPaths::Combine(Root, TEXT("data/minecraft"), Path)))
    {
        OutError = FString::Printf(TEXT("%s: no timeline file under %s"), *Id, *Root);
        return false;
    }
    return ParseTimeline(Text, Id, Out, OutError);
}

bool VoxelSkyReference::ReadOverworldBase(
    const FString& Root, const FString& Attribute, FVector4f& OutValue, FString& OutError)
{
    FString Text;
    const TSharedPtr<FJsonValue> Json =
        FFileHelper::LoadFileToString(Text, *FPaths::Combine(Root, TEXT("attributes.json")))
        ? VoxelWorldgenJson::ParseWorldgenJson(Text)
        : nullptr;
    const TSharedPtr<FJsonObject>* Object = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Attributes = nullptr;
    if (!Json || !Json->TryGetObject(Object) || !(*Object)->TryGetArrayField(TEXT("attributes"), Attributes))
    {
        OutError = TEXT("attributes.json: missing or without an attributes list");
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Entry : *Attributes)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr;
        FString Id;
        if (!Entry->TryGetObject(Row) || !SkyStringField(**Row, TEXT("id"), Id) ||
            !VoxelWorldgenJson::IsWorldgenKey(Id, *Attribute))
            continue;
        TSharedPtr<FJsonValue> Value = (*Row)->TryGetField(TEXT("overworld"));
        if (!Value || Value->IsNull()) Value = (*Row)->TryGetField(TEXT("wiki_default"));
        FVoxelTimelineTrack Scratch;
        EVoxelTimelineValue Type = EVoxelTimelineValue::Float;
        if (Value && !Value->IsNull() && SkyReadKeyValue(*Value, Scratch, Type, OutValue) &&
            (Type == EVoxelTimelineValue::Float || Type == EVoxelTimelineValue::Color))
            return true;
        OutError = Attribute + TEXT(": no number or colour beneath it in attributes.json");
        return false;
    }
    OutError = Attribute + TEXT(": not in attributes.json");
    return false;
}
