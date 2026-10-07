#include "UnnamedFiles.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

UnnamedFiles::ESaveResult UnnamedFiles::SaveTextAtomically(const FString& Filename, const FString& Text)
{
    const FString Temporary = Filename + TEXT(".tmp");
    IFileManager& Files = IFileManager::Get();
    Files.MakeDirectory(*FPaths::GetPath(Filename), true);
    if (!FFileHelper::SaveStringToFile(Text, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        return ESaveResult::WriteFailed;
    if (Files.FileExists(*Filename) && Files.Copy(*(Filename + TEXT(".bak")), *Filename) != COPY_OK)
        return ESaveResult::BackupFailed;
    if (!Files.Move(*Filename, *Temporary, true)) return ESaveResult::ReplaceFailed;
    return ESaveResult::Saved;
}

FString UnnamedFiles::ReadablePath(const FString& Filename)
{
    return IFileManager::Get().FileExists(*Filename) ? Filename : Filename + TEXT(".bak");
}
