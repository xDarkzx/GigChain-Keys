# Security Policy

## Reporting a Vulnerability

**Please do not open a public GitHub issue for a security vulnerability.**
That publishes the details before a fix exists.

Instead, use GitHub's private reporting: open the repository's **Security**
tab and choose **Report a vulnerability**. This starts a private conversation
that only you and the maintainer can see.

## What to Expect

This is a solo-maintained project, so there is no fixed response time. A
genuine security report is still handled before regular feature work. You'll
get an acknowledgement, then a fix, or an explanation if it turns out not to
be exploitable.

## Scope

OpenStage runs locally: it loads plugins from your VST3 folder, reads and
writes setlist files, and talks to your audio and MIDI devices. It has no
server and makes no network connections. Relevant reports include:

- A crafted setlist file (`.openstage.json`) that causes a crash, memory
  corruption, or file access outside what the user chose
- Memory-safety bugs in OpenStage's own code (the host side of plugin
  loading, the audio and MIDI paths, file parsing)
- Anything that makes OpenStage load or run code the user did not install

Bugs inside a third-party plugin belong with that plugin's vendor, and bugs in
Qt, the VST3 SDK, RtAudio or RtMidi belong with those projects. Do tell us if
OpenStage could reasonably protect against them.

## Supported Versions

Only the latest code on `main` is supported.
