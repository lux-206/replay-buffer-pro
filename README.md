# Replay Buffer Pro

[![GitHub Release](https://img.shields.io/github/v/release/joshuapotter/replay-buffer-pro)
![GitHub Release Date](https://img.shields.io/github/release-date/joshuapotter/replay-buffer-pro?display_date=published_at)](https://github.com/JoshuaPotter/replay-buffer-pro/releases/latest/download/replay-buffer-pro-windows-x64.zip)
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/JoshuaPotter/replay-buffer-pro)

This OBS Studio plugin expands upon the built-in Replay Buffer, allowing users to save recent footage at different lengths with customizable save buttons, similar to how PlayStation/Xbox's "Save Recent Gameplay" functionality.

**Note:** Windows builds are 64-bit only, as OBS Studio 29.0.0+ dropped 32-bit support. macOS builds are universal binaries (arm64 + x86_64).

## How It Works
OBS keeps a rolling buffer of the last few seconds or minutes of footage in memory using the built-in replay buffer. The length of this footage is defined in settings. If the amount of footage exceeds the length in settings, old footage is overwritten as new footage is recorded.

Unlike the default Replay Buffer, which saves a fixed duration, this OBS Studio plugin allows users to save different lengths on demand. Set the replay buffer length, then clip custom lengths of footage automatically. Example: Set your replay buffer to 10 minutes. Save the last 30 seconds, 2 minutes, or 5 minutes instantly with UI buttons or hotkeys.

The project website is currently hosted via GitHub Pages.

## Usage

### Saving Clips
1. Start the Replay Buffer in OBS
2. Click any save clip button (customizable durations) or use the assigned hotkey
3. Use the Customize button to set your preferred clip lengths
4. The plugin will:
   - Save the full replay buffer
   - Automatically trim to the selected duration (Without re-encoding)
   - Replace the original file with the trimmed version

### Buffer Length
- Quickly adjust built-in replay buffer length (1s to 6h) without digging through the settings

### Hotkeys
- Assign hotkeys to each save duration button in OBS Settings > Hotkeys

## Google Drive Cloud Clips

The Windows build can upload verified, trimmed clips to Google Drive without blocking OBS. Cloud upload is disabled by default, so the original behavior is unchanged until it is enabled.

### Google Cloud setup

1. In [Google Cloud Console](https://console.cloud.google.com/), create or select a project and enable **Google Drive API**.
2. Configure the OAuth consent screen and add your Google account as a test user while the app is in testing.
3. Create an OAuth 2.0 Client ID with application type **Desktop app**.
4. In the Replay Buffer Pro dock, open **Cloud Upload**, enter the client ID and client secret, and select **Connect Google Account**.
5. Approve access in the browser. The callback uses a temporary random localhost port and closes after authorization.
6. Enable cloud upload and choose the destination root (default: `OBS Clips`).

Tokens and OAuth client credentials are encrypted with Windows DPAPI and tied to the current Windows user. They are never written to settings JSON or logs. The requested Drive scope is `drive.file`.

Clips are staged under `%LOCALAPPDATA%\OBSCloudClips\temp`, persisted in `%LOCALAPPDATA%\OBSCloudClips\queue.json`, and uploaded by one background worker using the Drive API v3 resumable protocol. Files are organized as `OBS Clips/YYYY-MM-DD/`. The plugin fetches remote metadata and compares sizes before deleting the temporary copy. With **Delete local file after successful upload** disabled, the finished clip remains in the OBS recordings directory and only its staging copy is removed.

If the network or API is unavailable, the staged file and job are retained. Retries use bounded backoff (5s, 15s, 30s, 60s, then 5min). On restart, interrupted jobs return to pending. Shutdown stops accepting jobs, persists the queue, and uses a bounded timeout for an active request.

Known v1 limitations: cloud upload and secure credential storage are Windows-only; one upload runs at a time; OAuth credentials must be supplied by the user; interrupted uploads create a new resumable session after restart.

## Installation

### From Release

**Windows:**
1. Download the latest `.zip` release
2. Extract the ZIP file
3. Copy the `replay-buffer-pro` folder to `%ALLUSERSPROFILE%\obs-studio\plugins\` (typically `C:\ProgramData\obs-studio\plugins\`)

Final file structure should look like this:
```
obs-studio/
└── plugins/
    └── replay-buffer-pro/
        ├── bin/
        │   └── 64bit/
        │       └── replay-buffer-pro.dll
        └── data/
            └── locale/
                └── en-US.ini
```

**Note:** The `%ALLUSERSPROFILE%` environment variable typically resolves to `C:\ProgramData`. You can type this directly into File Explorer's address bar.

**macOS:**
1. Download the latest `.pkg` release (universal: arm64 + x86_64)
2. Open the downloaded `.pkg` and follow the installer
3. The installer places the plugin at `~/Library/Application Support/obs-studio/plugins/replay-buffer-pro.plugin` automatically

**Note:** If macOS blocks the installer because it's from an unidentified developer, right-click (or Control-click) the `.pkg` and choose **Open**, then confirm in the dialog.

## Building from Source

The build system follows the [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) pattern. All dependencies (OBS Studio source, prebuilt obs-deps, Qt6) are **automatically downloaded** at configure time.

### Requirements

**Windows:**
- Windows 10/11 64-bit
- Visual Studio 2022+ with "Desktop development with C++"
- CMake 3.28+

**macOS:**
- macOS 12.0+ (builds target macOS 12.0+; universal binary: arm64 + x86_64)
- Xcode 26.5+ with macOS SDK 26.5+
- CMake 3.28+

No manual OBS clone, Qt6 install, or FFmpeg setup is needed on either platform — everything is fetched automatically.

### Build (Windows)

```bash
git clone https://github.com/joshuapotter/replay-buffer-pro.git
cd replay-buffer-pro

# Configure (first run downloads deps and builds OBS — takes a few minutes)
cmake --preset windows-x64

# Build the plugin
cmake --build --preset windows-x64

# Install (close OBS first)
cmake --install build_x64 --config RelWithDebInfo
```

The install target places the plugin in `%ALLUSERSPROFILE%/obs-studio/plugins/`. After building, a rundir is also available at `build_x64/rundir/RelWithDebInfo/` for quick testing.

### Build (macOS)

```bash
git clone https://github.com/joshuapotter/replay-buffer-pro.git
cd replay-buffer-pro

# Configure (first run downloads deps and builds OBS — takes a few minutes)
cmake --preset macos

# Build the plugin (universal binary)
cmake --build --preset macos

# Install (close OBS first)
cmake --install build_macos --config RelWithDebInfo
```

The install target places the `.plugin` bundle in `~/Library/Application Support/obs-studio/plugins/`. After building, a rundir is also available at `build_macos/rundir/RelWithDebInfo/` for quick testing.

### Release (Windows)

Update the version in `buildspec.json`, then run:
```bash
cmake --preset windows-x64
cmake --build build_x64 --config RelWithDebInfo --target prepare_release
```
This creates `build_x64/releases/<version>/replay-buffer-pro-windows-x64.zip`.

### Release (macOS)

There is no local one-command release target for macOS. Packaging (codesigning, notarization, and `.pkg` creation via `.github/scripts/package-macos`) requires CI credentials and only runs in GitHub Actions — see CI / GitHub Actions below.

### CI / GitHub Actions

Pushing a semver tag (e.g., `1.4.0`) to `main`/`master` triggers the GitHub Actions workflow, which builds the plugin for both Windows and macOS and creates a draft GitHub release with all artifacts attached (Windows `.zip` and macOS `.pkg`).

### Project Structure

```
replay-buffer-pro/
├── buildspec.json       # Plugin metadata + dependency versions (Windows + macOS)
├── CMakePresets.json    # Build presets (windows-x64, macos)
├── CMakeLists.txt       # Main build configuration
├── cmake/               # CMake modules (common + windows + macos)
├── data/               
│   └── locale/          # Translations
├── src/                 # Source files (fully cross-platform)
│   ├── config/          # Config constants
│   ├── managers/        # Core functionality managers
│   ├── plugin/          # Main plugin implementation
│   ├── ui/              # User interface components
│   └── utils/           # Utility classes (including video-trimmer)
├── .github/             # CI workflows, actions, scripts (Windows + macOS)
├── docs/                # Project website source
├── reference/           # Developer documentation
└── README.md
```

## Troubleshooting

- Verify plugin file location (`.dll` on Windows, `.plugin` bundle on macOS)
- Check OBS logs for errors

### A clip wasn't trimmed

Every replay save writes one `TRIM VERDICT` line to the OBS log (**Help → Log Files → Show Log Files**). Search the log for `TRIM VERDICT` and find the line matching the clip:

- `skipped reason=no-pending-request` — the save was triggered outside this plugin, so it was saved at full buffer length. OBS's own **Save Replay** hotkey, the tray menu item, and Stream Deck buttons using the official *OBS Studio* plugin's "Save Replay Buffer" action all take this path. Use one of Replay Buffer Pro's own **Save Clip** hotkeys (Settings → Hotkeys → *Replay Buffer Pro: Save ...*) so the plugin knows which duration you wanted.
- `skipped reason=save-full-buffer` — this was a **Save Replay Buffer** click, which is intentionally untrimmed.
- `failed reason=output-too-long` — your encoder's keyframe interval is too long for the clip length you asked for, so the cut could not land near the right place. Set **Settings → Output → Keyframe Interval** to 2 seconds.
- `failed reason=open-input-failed` — something else was holding the file. If **Settings → Advanced → Automatically remux to mp4** is enabled, try turning it off; antivirus and cloud-sync folders can do the same.
- Any other `failed reason=...` — check disk space and write permissions in the output directory, and include the line when reporting an issue.

A failed trim always leaves your original full-length clip in place, so nothing is lost.
- When building from source:
  - **Windows**: Ensure Visual Studio 2022+ and CMake 3.28+ are installed
  - **macOS**: Ensure Xcode 26.5+ (with macOS SDK 26.5+) and CMake 3.28+ are installed; run `xcode-select --install` if needed
  - First configure run downloads ~500MB of dependencies — ensure network access
  - **Windows**: Run install command in a terminal with admin privileges if installing to a protected directory

## Third-Party Software

This plugin uses OBS Studio's built-in FFmpeg libraries (libavformat) for video trimming functionality. FFmpeg is licensed under the LGPL v2.1+ license.

Because the plugin links against OBS's bundled FFmpeg libraries by soname, each release is built against a specific OBS version's FFmpeg major version and is not binary-compatible with OBS versions that ship a different FFmpeg major. Always use the plugin release matching your OBS Studio version's minimum requirement.

## License

GPL v2 or later. See LICENSE file for details. 
