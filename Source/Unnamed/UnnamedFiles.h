#pragma once

#include "CoreMinimal.h"

namespace UnnamedFiles
{
    enum class ESaveResult : uint8
    {
        Saved,
        WriteFailed, // Temporary file could not be written; the saved file is untouched.
        BackupFailed, // Previous file could not be backed up; the saved file is untouched.
        ReplaceFailed, // Temporary file could not replace the saved file; the backup remains.
    };

    // Crash-safe replace: writes Filename.tmp, copies the current file to Filename.bak, then moves
    // the temporary file over Filename. An interrupted save leaves the file or its backup readable.
    UNNAMED_API ESaveResult SaveTextAtomically(const FString& Filename, const FString& Text);

    // Filename, or its .bak when a save stopped after backing the file up.
    UNNAMED_API FString ReadablePath(const FString& Filename);
}
