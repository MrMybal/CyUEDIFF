# GitHub publishing

Remote: **https://github.com/MrMybal/CyUEDIFF**. This is the existing private repository; publishing must not change its visibility or create another repository.

## Repository description

Use this English text in the repository's **About → Description** field:

> Reusable C++17 engine for inspecting and diffing Unreal Engine assets without Unreal Editor, with local Git/LFS support and a separate multilingual CyUI desktop interface.

The same text is saved in [`.github/description.txt`](../.github/description.txt). Git does not update the About field when pushing that file. A repository owner can also set it through authenticated GitHub CLI:

```powershell
gh repo edit MrMybal/CyUEDIFF --description "Reusable C++17 engine for inspecting and diffing Unreal Engine assets without Unreal Editor, with local Git/LFS support and a separate multilingual CyUI desktop interface."
```

See the [official `gh repo edit` reference](https://cli.github.com/manual/gh_repo_edit). This command changes the description only.

## Source selection and secret scanning

The repository tracks source code, tests, vendored open-source headers/licenses and documentation. Build directories, generated executables/libraries, local credentials/configuration, Unreal packages and payloads, local Oodle artifacts, reports and native captures are ignored. No Git LFS tracking is needed for these source files; the application's Git/LFS adapter is a separate runtime capability.

Gitleaks **8.30.1** was used for the initial checks. The project configuration extends its default rules with no custom exclusions or baseline. The helper exports tracked working-tree files and non-ignored untracked files into a temporary scan directory. Third-party source files are included. The full-history scan covers all local refs. Inline allow comments and an automatic ignore file are not used to suppress findings.

```powershell
# Before the first commit:
python scripts/scan_secrets.py --working-tree-only

# After committing, or for subsequent checks:
python scripts/scan_secrets.py
# Explicit installation if it is not on PATH:
python scripts/scan_secrets.py --gitleaks C:/path/to/gitleaks.exe
```

Reports are redacted and written under the ignored `build-native/security/` directory, with a source manifest and summary. A successful scan applies to that exact manifest/history; repeat it after source changes. It is a secret-pattern check, not a guarantee that every sensitive value or licensing issue has been detected. See [Gitleaks scanning modes](https://github.com/gitleaks/gitleaks#commands).

## Initial import

The local source directory initially had no Git metadata. The existing remote was reachable using Git HTTPS with the configured credential manager, and advertised no refs. The connected GitHub app did not have metadata access to this newly private repository. No token needs to be pasted into a file, a URL or a chat; use the existing credential manager or an authenticated GitHub client.

The standard import uses a local `main` branch and the existing remote, following [GitHub's command-line import instructions](https://docs.github.com/en/migrations/importing-source-code/using-the-command-line-to-import-source-code/adding-locally-hosted-code-to-github?platform=windows):

```powershell
# Once, if the folder has no Git repository yet:
git init -b main
git remote add origin https://github.com/MrMybal/CyUEDIFF.git

# Inspect the intended source files before committing:
git status --short
git add --all
python scripts/scan_secrets.py --working-tree-only
git diff --cached --stat
git commit -m "Initial CyUEDiff source import"
python scripts/scan_secrets.py

# Recheck the remote before the first push; inspect any existing refs:
git ls-remote --symref origin HEAD
git ls-remote origin
git push --dry-run origin main

# When publication is authorized:
git push -u origin main
```

Do not repeat `init` or `remote add` when already configured. If the remote has acquired commits, fetch and reconcile its history instead of overwriting it. Use a normal push; no force push is required. GitHub permissions or branch rules may require a branch/PR workflow instead.

The README is English by default, with [a French version](../README.fr.md). Runtime/API/JSON diagnostics use English; UI controls support English first and French second. Detailed implementation documents currently remain in French and are labeled accordingly from the English README.
