# Releasing a version

How a numbered version of GigChain Keys is built and published on GitHub
Releases, where the README's **Download** links point.

## Version numbers

`MAJOR.MINOR.PATCH`, set in one place: `PRODUCT_VERSION` in
[`branding.cmake`](../branding.cmake). A fix-only release raises PATCH
(0.1.0 → 0.1.1); new features raise MINOR (0.1 → 0.2).

The stage goes in the release's title, not the number (the installer and the
Mac app need plain numbers): **alpha** now (open testing, features still
changing), then **beta** (feature-complete for 1.0), then **1.0**. Until 1.0
every release is marked *pre-release* on GitHub.

The tag is the version with a `v`: `v0.1.0`.

## Steps

1. **Finish the changelog.** In [`CHANGELOG.md`](../CHANGELOG.md), rename
   **Unreleased** to the version and the date, `## [0.1.0] - 2026-10-20`, and
   start a new empty **Unreleased** above it. Write it for players: what they
   will notice, not how it was done.
2. **Set the version** in `branding.cmake` (`PRODUCT_VERSION`) and commit both
   files: `release: 0.1.0`.
3. **Build the Windows downloads** from that commit:

   ```powershell
   .\tools\package.ps1
   ```

   It builds Release, runs every test, checks the staged files and writes
   `dist\GigChainKeys-<version>-x64-setup.exe` and
   `dist\GigChainKeys-<version>-x64-portable.zip`.
4. **Try the installer** on a clean Windows account or machine: install,
   start, play a note through an instrument, open a setlist, uninstall.
5. **Tag and push:**

   ```powershell
   git tag -a v0.1.0 -m "GigChain Keys 0.1.0"
   git push origin main v0.1.0
   ```

   The tag starts the Mac build (`.github/workflows/mac.yml`) and the Linux
   build (`.github/workflows/linux.yml`). When they finish, download their
   artifacts (`gh run download <run id>`), and rename
   `GigChain Keys-<version>-arm64.dmg` to `GigChainKeys-<version>-arm64.dmg`
   (GitHub turns spaces in release file names into dots). The Linux ones
   (`GigChainKeys-<version>-x86_64.AppImage`, `gigchain-keys_<version>_amd64.deb`)
   keep their names.
6. **Checksums:** put the five files in one folder and write
   `SHA256SUMS.txt` (one `<sha256>  <file name>` line each, as `shasum -a 256`
   writes it):

   ```powershell
   Get-ChildItem release\* -Exclude SHA256SUMS.txt | ForEach-Object {
       "$((Get-FileHash $_ -Algorithm SHA256).Hash.ToLower())  $($_.Name)" } |
       Set-Content -Encoding ascii release\SHA256SUMS.txt
   ```

7. **Publish the release** with the notes from the changelog, the five
   files and the checksums:

   ```powershell
   gh release create v0.1.0 --prerelease --title "GigChain Keys 0.1.0 (alpha)" `
       --notes-file notes.md `
       release\GigChainKeys-0.1.0-x64-setup.exe `
       release\GigChainKeys-0.1.0-x64-portable.zip `
       release\GigChainKeys-0.1.0-arm64.dmg `
       release\GigChainKeys-0.1.0-x86_64.AppImage `
       release\gigchain-keys_0.1.0_amd64.deb `
       release\SHA256SUMS.txt
   ```

   `notes.md` is that version's section of the changelog, with the
   *About code signing* and *First start* notes from the README under it.
   Drop `--prerelease` from 1.0 on.
8. **Check the README links:** the **Download** links open the release page,
   and the badge shows the new version.

## Signing

The installer and the Mac app are not signed yet, so Windows SmartScreen and
macOS Gatekeeper warn on first start (the README says how to get past them,
and the checksums let people check their download). Signing needs a
code-signing certificate (Windows) and an Apple Developer ID (macOS), both
yearly costs; they come when interest and sponsorship allow. When they exist, signing goes
into `tools\package.ps1` and the Mac workflow, before the files are uploaded.
