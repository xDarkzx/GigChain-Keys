# Releasing a version

How a numbered version of GigChain Keys is built and published on GitHub
Releases, where the README's **Download** links point.

## Version numbers

`MAJOR.MINOR.PATCH`, set in one place: `PRODUCT_VERSION` in
[`branding.cmake`](../branding.cmake). A fix-only release raises PATCH
(0.1.0 → 0.1.1); new features raise MINOR (0.1 → 0.2).

The stage goes in the release's title, not the number (the installer and the
Mac app need plain numbers): **alpha** now (open testing, features still
changing), then **beta** (feature-complete for 1.0), then **1.0**. Each
release is published as GitHub's **Latest** (not as a pre-release): the
README's **Download** links open the latest release, which skips pre-releases.

The tag is the version with a `v`: `v0.1.0`.

## The weekly release

A version ships **every Monday** (New Zealand time). The week before it:

- **A milestone per version** (`0.12`): every issue and pull request meant
  for it goes in, so one page shows what is in and what is left. Bug reports
  that come in during the week are fixed for that Monday's version.
- **Through the week:** each change goes through its own pull request from
  `main` (a branch on top of another only when it truly needs the other),
  CI (Windows debug, release and dev; Linux), the review comments resolved,
  then a squash merge.
- **Freeze: Sunday at noon.** Nothing new merges after it unless it fixes
  something the release candidate broke.
- **Sunday afternoon: the release candidate.** First the release commit
  (steps 1 and 2 below, through its own pull request), then from that
  commit:
  - every test, the heavy sound tests included (`tools\verify.ps1 -Force
    -Full`; everyday runs leave the tests labelled `heavy` out);
  - the Windows downloads (step 3; `package.ps1` runs the tests on the
    release build it packs), a soak of that release build
    (`tools/soak.ps1`, the `release` preset) and the installer tried
    (step 4);
  - every live check (`tests/live`, see
    [testing/live-test-control.md](testing/live-test-control.md)) with
    `tools/live-checks.ps1`. The live test control is left out of release
    builds, so the checks drive the `dev` build of the same commit: the
    same code with the control compiled in;
  - the Linux and Mac downloads, started by hand on that commit (Actions →
    Linux / Mac → Run workflow; ask before the Mac run, it is paid), their
    artifacts kept.

  Write down the candidate's commit (`git rev-parse HEAD`): Monday's tag
  goes on it. A failure is fixed before Monday, or its change is taken out. Either way
  the candidate is built and tested again from the new commit: only a
  candidate that passed is published.
- **Monday:** tag the candidate's commit and publish exactly the files
  tested on Sunday (steps 5 to 8). What merged after the freeze waits for
  the next Monday.
- **A patch release** (0.12.1) only for what hurts players: a crash, lost
  work, no sound, an app that will not start.

## Steps

1. **Finish the changelog.** In [`CHANGELOG.md`](../CHANGELOG.md), rename
   **Unreleased** to the version and the date, `## [0.1.0] - 2026-10-20`, and
   start a new empty **Unreleased** above it. Write it for players: what they
   will notice, not how it was done.
2. **Set the version** in `branding.cmake` (`PRODUCT_VERSION`), and in
   [`CITATION.cff`](../CITATION.cff) set `version` to the same and
   `date-released` to the release's date (the `citation_matches_version` test
   fails until they match). Commit the three files: `release: 0.1.0`.
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
   git tag -a v0.1.0 <candidate commit> -m "GigChain Keys 0.1.0"
   git push origin v0.1.0
   ```

   `<candidate commit>` is the commit the candidate was built from, written
   down on Sunday (`git rev-parse HEAD` when building it): the tag names it,
   never whatever is checked out on Monday. Pushing the tag also starts the
   Mac and Linux workflows again (their `v*` trigger); that Mac run is paid,
   so ask before pushing the tag, and cancel it (Actions → Mac → Cancel)
   when Sunday's Mac files are being published. The Mac and Linux
   files published are the ones from Sunday's runs on that commit
   (`.github/workflows/mac.yml`, `.github/workflows/linux.yml`), not new
   builds: download their artifacts (`gh run download <run id>`), and rename
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
   gh release create v0.1.0 --latest --title "GigChain Keys 0.1.0 (alpha)" `
       --notes-file notes.md `
       release\GigChainKeys-0.1.0-x64-setup.exe `
       release\GigChainKeys-0.1.0-x64-portable.zip `
       release\GigChainKeys-0.1.0-arm64.dmg `
       release\GigChainKeys-0.1.0-x86_64.AppImage `
       release\gigchain-keys_0.1.0_amd64.deb `
       release\SHA256SUMS.txt
   ```

   `notes.md` is that version's section of the changelog, with the
   *About code signing* and *First start* notes from the README under it,
   and at the very end the ask:

   ```markdown
   **Free, and new every Monday.** If GigChain Keys helps your gigs, a coffee
   keeps it going: ☕ [Donate](https://buymeacoffee.com/ddazomnike)
   (card, Apple Pay or Google Pay, no account) · ♥ [Sponsor](https://github.com/sponsors/xDarkzx)
   · [all the ways to support it](https://xdarkzx.github.io/GigChain-Keys/support.html).
   First goal: code signing (and Apple's notarisation), so Windows and Mac warn less on installing.
   ```
8. **Check the README links:** the **Download** links open the release page,
   and the badge shows the new version.

## Signing

The installer and the Mac app are not signed yet, so Windows SmartScreen and
macOS Gatekeeper warn on first start (the README says how to get past them,
and the checksums let people check their download). Signing needs a
code-signing certificate (Windows) and an Apple Developer ID (macOS), both
yearly costs; they come when interest and donations allow. When they exist, signing goes
into `tools\package.ps1` and the Mac workflow, before the files are uploaded.
