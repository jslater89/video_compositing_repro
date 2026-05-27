# Linux compositing repro

Minimal Flutter **Linux** app: external **GL texture** video + small corner PNG overlay in the video stack. Reproduces **chroma corruption** / **vertical band freeze** with **FVP** unless a root-overlay **blurred shadow** (or Flutter `Banner`) is present.

The same overlay stack is used for every backend; only the video texture source changes.

## Quick start

```bash
cd compositing_repro
# Add assets/video.mp4 and assets/overlay.png (see assets/README.md)
flutter pub get
flutter run -d linux --release
```

Window opens **1280×720** with a **hidden title bar** (matches production playout chrome).

Console should log `video backend: FVP`.

## Toggles (keyboard)

On-screen controls are disabled — they interfere with the repro. Use keys instead:

| Key | Effect |
|-----|--------|
| **1** / **O** | Corner overlay (trigger) |
| **2** / **S** | Shadow shim — minimal `MaskFilter.blur` via root `OverlayEntry` (fix) |
| **3** / **B** | Debug `Banner` via root `OverlayEntry` (control / also fixes) |

**Expected (FVP backend):** overlay ON, shims OFF → broken video for ≥15s. Enable shadow shim OR Banner → correct colors/motion.

## Video backends

Backend is selected at **compile time** via `--dart-define=VIDEO_BACKEND=…`. VS Code launch configs are in `.vscode/launch.json`.

| `VIDEO_BACKEND` | Description | Overlay bug |
|-----------------|-------------|-------------|
| *(default)* / `fvp` | `video_player` + **fvp** / mdk (`renderVideo()` → FBO texture) | **Yes** |
| `synthetic` | SMPTE color bars → `FlTextureGL` via `glTexSubImage2D` | No |
| `synthetic_fbo` | Same bars → FBO color attachment + `glTexSubImage2D` | No |
| `synthetic_yuv` | SMPTE bars as **planar YUV420** → GLES BT.709 shader → FBO | *(test)* |
| `offline_rgba` | Pre-decoded RGBA frames from `video.mp4` → `glTexSubImage2D` | No |
| `offline_rgba_fbo` | Same RGBA frames → FBO attachment + `glTexSubImage2D` | No |

### Commands

```bash
# FVP (default)
flutter run -d linux --release

# Synthetic SMPTE bars
flutter run -d linux --release --dart-define=VIDEO_BACKEND=synthetic

# Synthetic bars, FBO-backed texture
flutter run -d linux --release --dart-define=VIDEO_BACKEND=synthetic_fbo

# Planar YUV420 SMPTE bars → BT.709 shader → FBO (tests live YUV→RGB path)
flutter run -d linux --release --dart-define=VIDEO_BACKEND=synthetic_yuv

# Offline RGBA frames (requires extract step below)
flutter run -d linux --release --dart-define=VIDEO_BACKEND=offline_rgba

# Offline RGBA, FBO-backed texture
flutter run -d linux --release --dart-define=VIDEO_BACKEND=offline_rgba_fbo
```

Log line to confirm backend: `video backend: <name>`.

### Offline RGBA setup

Extract a loopable RGBA sequence from `assets/video.mp4` (default **90 frames**, ~712 MiB @ 1080p):

```bash
./tool/extract_rgba_frames.sh        # 90 frames
./tool/extract_rgba_frames.sh 180    # optional: more frames
```

Writes `assets/video_rgba.bin` (gitignored). Required for `offline_rgba` and `offline_rgba_fbo`.

### FVP software decode

FVP defaults to **software FFmpeg decode** (`video.decoders: [FFmpeg]`, mdk equivalent of `hwdec=no`). To restore hardware decoders:

```bash
flutter run -d linux --release --dart-define=FVP_HWDEC=auto
```

Console logs `fvp hwdec: no` or `fvp hwdec: auto`.

## Bisect findings

All backends share the same widget tree: `Texture` inside a letterboxed `Stack`, corner `Image.asset` overlay, optional root `OverlayEntry` shims.

| Ruled out | Evidence |
|-----------|----------|
| Generic **Flutter `FlTextureGL` + overlay** | `synthetic` backend clean |
| **Real video pixel content alone** | `offline_rgba` clean (same clip, ffmpeg → RGBA) |
| **Hardware decode** | FVP + `FVP_HWDEC=no` still broken |
| **FBO-backed texture alone** | `offline_rgba_fbo` clean |

**Still reproduces:** FVP/mdk live path only — decode + **`renderVideo()` into an FBO** (YUV→RGB inside mdk's GL renderer, Y-flip, async frame delivery). The minimal plugin paths upload **pre-formed RGBA** with `glTexSubImage2D`; they do not exercise mdk's renderer.

**Conclusion:** Not a clean Flutter-only repro. Strongest filing target is **[fvp / mdk Linux GL renderer](https://github.com/wang-bin/fvp)** (possibly interacting with Flutter compositing when a semi-transparent overlay is present). Flutter may be CC'd, but vanilla external-texture upload does not trigger the bug.

## Project layout

```
lib/main.dart                          # overlay stack, keyboard toggles
lib/video/backend.dart                 # VIDEO_BACKEND selection
lib/video/fvp_video.dart               # FVP + optional FVP_HWDEC
lib/video/synthetic_video.dart         # synthetic / synthetic_fbo
lib/video/synthetic_yuv_video.dart     # synthetic_yuv (planar YUV420)
lib/video/offline_rgba_video.dart      # offline_rgba / offline_rgba_fbo
lib/compositing_shadow_shim.dart       # blur corner fix
packages/synthetic_texture/            # local FlTextureGL plugin (bisect)
tool/extract_rgba_frames.sh            # ffmpeg → assets/video_rgba.bin
```

## Upstream filing

See `HANDOFF.md` for symptom details, production context, and validation checklist.

Suggested issue title:

> Linux: FVP/mdk GL video texture + overlay compositing corrupts chroma unless root overlay uses MaskFilter.blur

Include:

- Screen recording (FVP broken vs shadow shim fixed)
- Bisect table above (`synthetic` / `offline_rgba*` clean; FVP broken)
- `flutter doctor -v`, this repo, repro steps
