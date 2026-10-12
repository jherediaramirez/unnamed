#include "UnnamedFiles.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
    // The second half of a crash-safe replace, once Filename.tmp is written: back the current file up, then move.
    UnnamedFiles::ESaveResult ReplaceWithTemporary(const FString& Filename, const FString& Temporary)
    {
        IFileManager& Files = IFileManager::Get();
        if (Files.FileExists(*Filename) && Files.Copy(*(Filename + TEXT(".bak")), *Filename) != COPY_OK)
            return UnnamedFiles::ESaveResult::BackupFailed;
        if (!Files.Move(*Filename, *Temporary, true)) return UnnamedFiles::ESaveResult::ReplaceFailed;
        return UnnamedFiles::ESaveResult::Saved;
    }
}

UnnamedFiles::ESaveResult UnnamedFiles::SaveTextAtomically(const FString& Filename, const FString& Text)
{
    const FString Temporary = Filename + TEXT(".tmp");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    if (!FFileHelper::SaveStringToFile(Text, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        return ESaveResult::WriteFailed;
    return ReplaceWithTemporary(Filename, Temporary);
}

UnnamedFiles::ESaveResult UnnamedFiles::SaveBytesAtomically(const FString& Filename, TConstArrayView<uint8> Bytes)
{
    const FString Temporary = Filename + TEXT(".tmp");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    if (!FFileHelper::SaveArrayToFile(Bytes, *Temporary)) return ESaveResult::WriteFailed;
    return ReplaceWithTemporary(Filename, Temporary);
}

FString UnnamedFiles::ReadablePath(const FString& Filename)
{
    return IFileManager::Get().FileExists(*Filename) ? Filename : Filename + TEXT(".bak");
}

bool UnnamedFiles::RestoreFromBackup(const FString& Filename)
{
    IFileManager& Files = IFileManager::Get();
    const FString Backup = Filename + TEXT(".bak"), Temporary = Filename + TEXT(".tmp");
    if (Files.FileSize(*Filename) > 0 || Files.FileSize(*Backup) <= 0) return false;
    // Through a temporary file, so a restore that stops leaves the file missing or empty again, never half-written.
    return Files.Copy(*Temporary, *Backup) == COPY_OK && Files.Move(*Filename, *Temporary, true);
}
