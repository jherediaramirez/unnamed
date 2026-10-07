#include "Menu/MenuPreferences.h"
#include "UnnamedFiles.h"
#include "UnnamedSession.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    FString Encode(const TSharedPtr<FJsonObject>& Object)
    {
        FString Json;
        FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
        return Json;
    }

    TSharedPtr<FJsonObject> Copy(const TSharedPtr<FJsonObject>& Object)
    {
        TSharedPtr<FJsonObject> Result;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Encode(Object)), Result);
        return Result;
    }

    bool Equal(const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
    {
        if (!A || !B || A->Type != B->Type) return false;
        if (A->Type == EJson::Boolean) return A->AsBool() == B->AsBool();
        if (A->Type == EJson::String) return A->AsString() == B->AsString();
        if (A->Type == EJson::Number) return A->AsNumber() == B->AsNumber();
        return false;
    }
}

void UMenuPreferences::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    FString Path = FPaths::ProjectSavedDir() / TEXT("Config/MenuPreferences.json");
    FString Override;
    if (FParse::Value(FCommandLine::Get(), TEXT("MenuPreferencesFile="), Override))
        Path = FPaths::ConvertRelativePathToFull(Override);
    Load(Path, UnnamedSession::CanSaveSettings());
}

#if WITH_DEV_AUTOMATION_TESTS
void UMenuPreferences::InitializeForTesting(const FString& Path, bool bSave)
{
    Load(Path, bSave);
}
#endif

TSharedPtr<FJsonObject> UMenuPreferences::Defaults() const
{
    const auto Result = MakeShared<FJsonObject>();
    const auto Settings = MakeShared<FJsonObject>();
    for (const auto& Entry : Schema->GetObjectField(TEXT("settings"))->Values)
        Settings->SetField(Entry.Key, Entry.Value->AsObject()->Values[TEXT("default")]);
    Result->SetObjectField(TEXT("settings"), Settings);
    Result->SetStringField(TEXT("language"), TEXT("en_us"));
    Result->SetArrayField(TEXT("packs"), {});
    Result->SetBoolField(TEXT("skipWarning"), false);
    Result->SetBoolField(TEXT("telemetryOptInExtra"), false);
    return Result;
}

void UMenuPreferences::Load(const FString& Path, bool bSave)
{
    Filename = Path;
    bPersistent = bSave;
    bReady = bProtectedFile = false;
    Revision = 0;
    LastError.Empty();
    FString Json;
    if (!FFileHelper::LoadFileToString(
            Json, *(FPaths::ProjectContentDir() / TEXT("Menu/Web/preferences-schema.menuprefs"))) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Schema) || !Schema ||
        !Schema->HasTypedField<EJson::Object>(TEXT("settings")) ||
        !Schema->HasTypedField<EJson::Number>(TEXT("version")) || Schema->GetNumberField(TEXT("version")) != 1)
    {
        LastError = TEXT("Preference definitions are unavailable. Rebuild this menu.");
        return;
    }
    State = Defaults();
    bReady = true;
    if (!bPersistent) return;
    // Recover the last good document if replacement was interrupted after the
    // original was backed up. Never replace an existing invalid/future document.
    const FString ReadPath = UnnamedFiles::ReadablePath(Filename);
    if (!IFileManager::Get().FileExists(*ReadPath)) return;
    TSharedPtr<FJsonObject> Saved;
    if (IFileManager::Get().FileSize(*ReadPath) > 262144 || !FFileHelper::LoadFileToString(Json, *ReadPath) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Saved) || !Saved ||
        !Saved->HasTypedField<EJson::Number>(TEXT("version")) || Saved->GetNumberField(TEXT("version")) != 1)
    {
        bProtectedFile = true;
        LastError = TEXT("Saved preferences are unreadable or from a newer version. The original file is protected; "
                         "Hard Reset can restore defaults.");
        return;
    }
    Saved->RemoveField(TEXT("version"));
    auto Candidate = Defaults();
    if (!ApplyPatch(Saved, Candidate, true))
    {
        bProtectedFile = true;
        LastError = TEXT("Saved preferences contain unsupported values. The original file is protected; Hard Reset can "
                         "restore defaults.");
        return;
    }
    State = Candidate;
}

bool UMenuPreferences::ValidateSetting(const FString& Id, const TSharedPtr<FJsonValue>& Value) const
{
    const auto Definitions = Schema->GetObjectField(TEXT("settings"));
    const auto Definition = Definitions->TryGetField(Id);
    if (!Definition || !Value) return false;
    const auto D = Definition->AsObject();
    const TArray<TSharedPtr<FJsonValue>>* Choices;
    if (D->TryGetArrayField(TEXT("values"), Choices))
        return Choices->ContainsByPredicate([&](const auto& Choice) { return Equal(Value, Choice); });
    if (Value->Type != EJson::Number) return false;
    const double Number = Value->AsNumber();
    if (!FMath::IsFinite(Number) || Number < D->GetNumberField(TEXT("min")) || Number > D->GetNumberField(TEXT("max")))
        return false;
    // Reference defaults can predate the slider grid (unfocused chat height).
    if (Equal(Value, D->Values[TEXT("default")])) return true;
    const double Step = D->GetNumberField(TEXT("step"));
    const double Units = (Number - D->GetNumberField(TEXT("min"))) / Step;
    return FMath::IsNearlyEqual(Units, FMath::RoundToDouble(Units), 0.00001);
}

bool UMenuPreferences::ApplyPatch(const TSharedPtr<FJsonObject>& Patch, TSharedPtr<FJsonObject>& Target, bool bLoading)
{
    const TSet<FString> Fields = {
        TEXT("settings"), TEXT("language"), TEXT("packs"), TEXT("skipWarning"), TEXT("telemetryOptInExtra")};
    for (const auto& Field : Patch->Values)
        if (!Fields.Contains(FString(*Field.Key))) return false;
    const auto Settings = Target->GetObjectField(TEXT("settings"));
    const TSharedPtr<FJsonObject>* Changes = nullptr;
    if (Patch->HasField(TEXT("settings")))
    {
        if (!Patch->TryGetObjectField(TEXT("settings"), Changes)) return false;
        for (const auto& Entry : (*Changes)->Values)
        {
            if (!ValidateSetting(FString(*Entry.Key), Entry.Value)) return false;
            Settings->SetField(Entry.Key, Entry.Value);
        }
        if (!bLoading)
        {
            FString Preset;
            if ((*Changes)->TryGetStringField(TEXT("graphicsPreset"), Preset))
            {
                const TSharedPtr<FJsonObject>* Values;
                if (Schema->GetObjectField(TEXT("presets"))->TryGetObjectField(Preset, Values))
                    for (const auto& Entry : (*Values)->Values)
                        Settings->SetField(Entry.Key, Entry.Value);
            }
            else
            {
                for (const auto& Id : Schema->GetArrayField(TEXT("quality")))
                    if ((*Changes)->HasField(Id->AsString()))
                    {
                        Settings->SetStringField(TEXT("graphicsPreset"), TEXT("Custom"));
                        break;
                    }
            }
        }
    }
    if (Patch->HasField(TEXT("language")))
    {
        FString Language;
        if (!Patch->HasTypedField<EJson::String>(TEXT("language")) ||
            !Patch->TryGetStringField(TEXT("language"), Language) ||
            !Schema->GetArrayField(TEXT("languages"))
                .ContainsByPredicate([&](const auto& Value) { return Value->AsString() == Language; }))
            return false;
        Target->SetStringField(TEXT("language"), Language);
    }
    for (const TCHAR* Flag : {TEXT("skipWarning"), TEXT("telemetryOptInExtra")})
    {
        bool Value;
        if (Patch->HasField(Flag))
        {
            if (!Patch->HasTypedField<EJson::Boolean>(Flag) || !Patch->TryGetBoolField(Flag, Value)) return false;
            Target->SetBoolField(Flag, Value);
        }
    }
    if (Patch->HasField(TEXT("packs")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Packs;
        if (!Patch->TryGetArrayField(TEXT("packs"), Packs) || Packs->Num() > 2) return false;
        TSet<FString> Unique;
        for (const auto& Pack : *Packs)
        {
            if (Pack->Type != EJson::String ||
                (Pack->AsString() != TEXT("high_contrast") && Pack->AsString() != TEXT("programmer_art")) ||
                Unique.Contains(Pack->AsString()))
                return false;
            Unique.Add(Pack->AsString());
        }
        const bool HighContrast = Unique.Contains(TEXT("high_contrast"));
        if (Changes && (*Changes)->HasField(TEXT("highContrast")) &&
            Settings->GetBoolField(TEXT("highContrast")) != HighContrast)
            return false;
        Target->SetArrayField(TEXT("packs"), *Packs);
        Settings->SetBoolField(TEXT("highContrast"), HighContrast);
    }
    else if (Changes && (*Changes)->HasField(TEXT("highContrast")))
    {
        auto Packs = Target->GetArrayField(TEXT("packs"));
        Packs.RemoveAll([](const auto& Value) { return Value->AsString() == TEXT("high_contrast"); });
        if (Settings->GetBoolField(TEXT("highContrast")))
            Packs.Add(MakeShared<FJsonValueString>(TEXT("high_contrast")));
        Target->SetArrayField(TEXT("packs"), Packs);
    }
    return true;
}

bool UMenuPreferences::Save(const TSharedPtr<FJsonObject>& Value)
{
    if (!bPersistent) return true;
    if (bProtectedFile)
    {
        LastError = TEXT("The existing preference file is protected. Use the confirmed Hard Reset to replace it.");
        return false;
    }
    const auto Document = Copy(Value);
    Document->SetNumberField(TEXT("version"), 1);
    switch (UnnamedFiles::SaveTextAtomically(Filename, Encode(Document)))
    {
    case UnnamedFiles::ESaveResult::Saved:
        return true;
    case UnnamedFiles::ESaveResult::WriteFailed:
        LastError = TEXT("Preferences could not be saved. Your previous saved choices are unchanged.");
        return false;
    case UnnamedFiles::ESaveResult::BackupFailed:
        LastError = TEXT("The previous preferences could not be backed up. Please try again.");
        return false;
    default:
        LastError = TEXT("Preferences could not be replaced. A backup of the previous choices was retained.");
        return false;
    }
}

FString UMenuPreferences::Snapshot(bool bSuccess) const
{
    auto Result = State ? Copy(State) : MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), bSuccess && LastError.IsEmpty());
    Result->SetBoolField(TEXT("ready"), bReady);
    Result->SetBoolField(TEXT("editable"), bReady && !bProtectedFile);
    Result->SetBoolField(TEXT("persistent"), bPersistent);
    Result->SetStringField(TEXT("error"), LastError);
    Result->SetStringField(TEXT("build"), TEXT("Preferences v1"));
    Result->SetNumberField(TEXT("revision"), Revision);
    return Encode(Result);
}

FString UMenuPreferences::GetState()
{
    return Snapshot();
}

FString UMenuPreferences::ApplyChanges(const FString& ChangesJson)
{
    if (!bReady || bProtectedFile) return Snapshot(false);
    LastError.Empty();
    TSharedPtr<FJsonObject> Patch;
    if (ChangesJson.Len() > 32768 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ChangesJson), Patch) ||
        !Patch)
    {
        LastError = TEXT("Invalid preference request.");
        return Snapshot(false);
    }
    auto Candidate = Copy(State);
    if (!ApplyPatch(Patch, Candidate, false))
    {
        LastError = TEXT("Unsupported preference or invalid value.");
        return Snapshot(false);
    }
    if (!Save(Candidate)) return Snapshot(false);
    State = Candidate;
    ++Revision;
    return Snapshot();
}

FString UMenuPreferences::ResetPreferences()
{
    if (!bReady) return Snapshot(false);
    LastError.Empty();
    if (bProtectedFile && bPersistent)
    {
        const FString Backup = Filename + TEXT(".protected-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString Existing = UnnamedFiles::ReadablePath(Filename);
        if (IFileManager::Get().Copy(*Backup, *Existing) != COPY_OK)
        {
            LastError = TEXT("The protected preferences could not be backed up. Reset was cancelled.");
            return Snapshot(false);
        }
    }
    const bool WasProtected = bProtectedFile;
    bProtectedFile = false;
    auto Candidate = Defaults();
    if (!Save(Candidate))
    {
        bProtectedFile = WasProtected;
        return Snapshot(false);
    }
    State = Candidate;
    ++Revision;
    return Snapshot();
}

bool UMenuPreferences::FlushForExit(FString& OutError)
{
    // A file rejected on load was never edited; allow normal exit without touching it.
    if (bProtectedFile) return true;
    LastError.Empty();
    if (!bReady || !Save(State))
    {
        OutError = LastError.IsEmpty() ? TEXT("Preferences are unavailable.") : LastError;
        return false;
    }
    return true;
}

TSharedPtr<const FJsonValue> UMenuPreferences::FindPreference(const FString& Id) const
{
    if (!State) return nullptr;
    return State->GetObjectField(TEXT("settings"))->TryGetField(Id);
}
