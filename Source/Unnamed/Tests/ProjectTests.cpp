#if WITH_DEV_AUTOMATION_TESTS
#include "UnnamedFiles.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtomicSaveTest, "Unnamed.Project.AtomicSave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAtomicSaveTest::RunTest(const FString& Parameters)
{
    const FString Folder =
        FPaths::ProjectSavedDir() / TEXT("Automation/AtomicSave-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString File = Folder / TEXT("settings.ini");
    auto Read = [](const FString& Path)
    {
        FString Text;
        FFileHelper::LoadFileToString(Text, *Path);
        return Text;
    };
    TestTrue(TEXT("First save writes the file"),
        UnnamedFiles::SaveTextAtomically(File, TEXT("first")) == UnnamedFiles::ESaveResult::Saved);
    TestEqual(TEXT("First contents"), Read(File), FString(TEXT("first")));
    TestTrue(TEXT("Second save succeeds"),
        UnnamedFiles::SaveTextAtomically(File, TEXT("second")) == UnnamedFiles::ESaveResult::Saved);
    TestEqual(TEXT("Second contents"), Read(File), FString(TEXT("second")));
    TestEqual(TEXT("Previous contents kept as backup"), Read(File + TEXT(".bak")), FString(TEXT("first")));
    TestFalse(TEXT("No temporary file left"), IFileManager::Get().FileExists(*(File + TEXT(".tmp"))));
    TestEqual(TEXT("Existing file is read"), UnnamedFiles::ReadablePath(File), File);
    IFileManager::Get().Delete(*File);
    TestEqual(TEXT("Interrupted replace falls back to backup"), UnnamedFiles::ReadablePath(File), File + TEXT(".bak"));
    TestTrue(TEXT("A missing file is restored from its backup"), UnnamedFiles::RestoreFromBackup(File));
    TestEqual(TEXT("Restored contents"), Read(File), FString(TEXT("first")));
    TestFalse(TEXT("A present file is never restored over"), UnnamedFiles::RestoreFromBackup(File));

    // The bytes variant (the input settings' slot) replaces the same way.
    const FString Bytes = Folder / TEXT("slot.sav");
    TestTrue(TEXT("First bytes save"),
        UnnamedFiles::SaveBytesAtomically(Bytes, TArray<uint8>{1, 2, 3}) == UnnamedFiles::ESaveResult::Saved);
    TestTrue(TEXT("Second bytes save"),
        UnnamedFiles::SaveBytesAtomically(Bytes, TArray<uint8>{4, 5}) == UnnamedFiles::ESaveResult::Saved);
    TArray<uint8> LoadedBytes, BackupBytes;
    FFileHelper::LoadFileToArray(LoadedBytes, *Bytes);
    FFileHelper::LoadFileToArray(BackupBytes, *(Bytes + TEXT(".bak")));
    TestTrue(TEXT("Second bytes saved, first kept as backup"),
        LoadedBytes == TArray<uint8>{4, 5} && BackupBytes == TArray<uint8>{1, 2, 3});
    TestFalse(TEXT("No temporary bytes file left"), IFileManager::Get().FileExists(*(Bytes + TEXT(".tmp"))));
    IFileManager::Get().MakeDirectory(*(Bytes + TEXT(".tmp")), true);
    TestTrue(TEXT("A blocked temporary file fails before the saved file is touched"),
        UnnamedFiles::SaveBytesAtomically(Bytes, TArray<uint8>{6}) == UnnamedFiles::ESaveResult::WriteFailed);
    FFileHelper::LoadFileToArray(LoadedBytes, *Bytes);
    TestTrue(TEXT("The saved bytes are unchanged"), LoadedBytes == TArray<uint8>{4, 5});

    // Video settings save FConfigFile text through this path and read it back with FConfigFile::Read.
    FConfigFile Written;
    Written.SetString(TEXT("MenuVideoSettings"), TEXT("Version"), TEXT("1"));
    Written.SetBool(TEXT("MenuVideoSettings"), TEXT("VSync"), false);
    FString Text;
    Written.WriteToString(Text, File);
    TestTrue(
        TEXT("Config text saves"), UnnamedFiles::SaveTextAtomically(File, Text) == UnnamedFiles::ESaveResult::Saved);
    FConfigFile Loaded;
    Loaded.Read(UnnamedFiles::ReadablePath(File));
    int32 Version = 0;
    bool bVSync = true;
    TestTrue(TEXT("Config round trip keeps values"),
        Loaded.GetInt(TEXT("MenuVideoSettings"), TEXT("Version"), Version) && Version == 1 &&
            Loaded.GetBool(TEXT("MenuVideoSettings"), TEXT("VSync"), bVSync) && !bVSync);
    IFileManager::Get().DeleteDirectory(*Folder, false, true);
    return true;
}
#endif
