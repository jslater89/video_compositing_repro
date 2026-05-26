# Assets (required before running)

Copy your test media here:

| File | Purpose |
|------|---------|
| `video.mp4` | H.264 (or other FVP-supported) clip, 1920×1080 recommended |
| `overlay.png` | Small corner logo with alpha (~470×470 px source; displayed ~22% of video short side) |
| `video_rgba.bin` | **offline_rgba backend only.** Regenerate with `./tool/extract_rgba_frames.sh` (~750 MiB for 90 frames @ 1080p). Gitignored by default. |

These paths are declared in `pubspec.yaml`. `flutter run` fails asset bundling if either file is missing.

## Suggested sources from obs_clipshow playout repro

Use the same clip and OSG template PNG that triggered chroma loss in production playout (corner logo preset, lower-third solid preset).
