# Releasing a version

How a numbered version of GigChain Keys is built and published on GitHub
Releases, where the README's **Download** links point.

## Version numbers

`MAJOR.MINOR.PATCH`, set in one place: `PRODUCT_VERSION` in
[`branding.cmake`](../branding.cmake). While the version is **0.x**, every
release is a **beta** (marked *pre-release* on GitHub). A fix-only release
raises PATCH (0.1.0 → 0.1.1); new features raise MINOR (0.1 → 0.2).

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

   The tag starts the Mac build (`.github/workflows/mac.yml`). When it
   finishes, download its artifact, `GigChain Keys-<version>-arm64.dmg`, from
   the run's page (Actions → Mac → the run).
6. **Publish the release** with the notes from the changelog and the three
   files:

   ```powershell
   gh release create v0.1.0 --prerelease --title "GigChain Keys 0.1.0 (beta)" `
       --notes-file notes.md `
       "dist\GigChainKeys-0.1.0-x64-setup.exe" `
       "dist\GigChainKeys-0.1.0-x64-portable.zip" `
       "GigChain Keys-0.1.0-arm64.dmg"
   ```

   `notes.md` is that version's section of the changelog, with the
   *First start* notes from the README (SmartScreen, Open Anyway) under it.
   Drop `--prerelease` from 1.0 on.
7. **Check the README links:** the **Download** links open the release page,
   and the badge shows the new version.

## Signing

The installer and the Mac app are not signed yet, so Windows SmartScreen and
macOS Gatekeeper warn on first start (the README says how to get past them).
Signing needs a code-signing certificate (Windows) and an Apple Developer ID
(macOS); donations are meant to pay for them. When they exist, signing goes
into `tools\package.ps1` and the Mac workflow, before the files are uploaded.
