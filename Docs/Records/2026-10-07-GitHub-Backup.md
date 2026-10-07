# GitHub Backup - October 7, 2026

## Scope

Private repository: [jherediaramirez/unnamed](https://github.com/jherediaramirez/unnamed).
Local project remains `C:\dev\projects\Unnamed` on branch `main`.

Source, configuration, reference assets, documentation, and test evidence are tracked. Unreal maps/assets use Git LFS. Builds, packages, caches, personal saves, dependencies, local credentials, and recovery bundles are excluded. GitHub is a source checkpoint, not a full-machine or save-game backup. Nothing is scheduled or automatically uploaded.

## Problem and Fix

The four original commits contained an unused Android File Server debugging token. With explicit owner approval, the original repository, dirty work, index, and local settings were preserved outside the project before changes. The token was removed from every uploadable commit using pinned `git-filter-repo` 2.47.0 in an isolated clone. Original commit messages/order remain; commit IDs change. Historical source references to original IDs remain historical evidence, not current branch IDs.

The unused Android file server and network access are disabled in current config, with an explicit empty token. Unreal only auto-generates that token for `[AUTO]`, so an empty value does not regenerate it. Windows menu behavior is unchanged. Enabling Android development later requires a separate credential/setup decision.

Recovery backup: `C:\dev\backups\Unnamed\github-setup-20261007-112245`.
It contains the original `.git`, `OriginalLocalHistory.bundle`, a hash-verified file snapshot, and `PreservationManifest.json`. `PreSanitizationCheckpoint.bundle` additionally preserves today's complete checkpoint before history cleanup; `SanitizedCommitMap.txt` maps the old and new commit IDs. It deliberately retains private historical data: keep it local and never upload it. Builds/packages remain in their existing local locations and are not copied to GitHub.

## Future Checkpoints

Git, Git LFS, Node 24, and authenticated GitHub CLI are needed. This checkout uses repository-local identity `Jesus Heredia <jrh06121998@gmail.com>` and `core.hooksPath=.githooks`. Hooks check staged blobs before commit and all reachable blobs before push, then run the normal LFS upload hook. On another clone, enable them explicitly:

```powershell
git lfs install --local
git config --local core.hooksPath .githooks
git config --local user.name 'Jesus Heredia'
git config --local user.email 'jrh06121998@gmail.com'
```

For an intentional project checkpoint, review the changes and then run:

```powershell
Set-Location 'C:\dev\projects\Unnamed'
git status --short
git add --all -- .
node Scripts/TestRepositorySafety.mjs --staged
git diff --cached --stat
git commit -m 'Describe the completed change'
git push origin main
```

Do not bypass hooks or force-push. A rejected credential check does not publish anything; remove/redact the credential before committing. If it is already in local history, stop and arrange a preserved history cleanup. Never push the original recovery bundle or old refs.

## Verification and Limits

The file-byte policy disables automatic line-ending conversion so the accepted source/assets/evidence can be restored exactly. LFS changes Git's storage representation of `.umap`/`.uasset`, not their checked-out bytes. Previous acceptance manifests remain immutable; this repository setup changes config and Git metadata only, not the accepted menu implementation.

The credential check catches the Android token and common GitHub/OpenAI/AWS/Google/private-key patterns. It is a focused safeguard, not an exhaustive security audit; compressed/encrypted archives need separate inspection. Hooks are local configuration and do not install themselves on another clone. Saves and packaged executables need a separate backup if desired.

## Completed Checks

- GitHub authentication completed as `jherediaramirez`. Repository is private, default branch is `main`, and the local branch tracks `origin/main`.
- First uploaded sanitized checkpoint: `b4fdae5ac71df96a926aa97d1c713d67f0492310`. The latest tree was identical before and after history cleanup. Only sanitized `main` was uploaded; no force-push or working-file reset was used.
- Fresh authenticated clone at `C:\dev\backups\Unnamed\github-setup-20261007-112245\GitHubReadback`: all 3,157 tracked files restored byte-for-byte, including the LFS map. Generated builds, personal saves, and dependencies were absent.
- Six repository-safety tests, all 25 existing menu unit tests, and the 938-asset/reference/syntax check passed. Commit and push hooks executed successfully. Reachable-history scan passed with zero findings; an exact-value check also found no original credential across all 1,245 reachable objects. The preserved review ZIP's 111 entries were separately checked for the original token.
- Preservation comparison: 3,147 existing project files unchanged, with five approved existing-file edits for config/repository policy/documentation. One live editor `Saved` configuration changed while Unreal was open; it was retained, not restored over. Original settings remain in the recovery snapshot.

Machine-readable restore and preservation evidence remains local in `GitHubRestoreVerification.json` and `SetupPreservationResults.json` inside the recovery folder. This is repository/restore acceptance, not a new native build, Shipping test, or gameplay acceptance run. The earlier menu runtime evidence and its known gaps are unchanged.

References: [GitHub sensitive-data guidance](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository), [git-filter-repo](https://github.com/newren/git-filter-repo).
