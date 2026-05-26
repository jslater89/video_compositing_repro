# Handoff: FVP / Flutter Linux video + overlay compositing bug

**Audience:** Another AI agent validating, minimizing, and filing an upstream issue.  
**Origin:** [obs_clipshow](https://github.com/) playout investigation (2026).  
**Repro project:** This directory (`compositing_repro/`).

---

## One-sentence summary

On **Linux**, **fvp** + **video_player** (`FlTextureGL`) shows **chroma corruption** or **partial frame freeze** when a **small semi-transparent overlay** is composited in the video stack; a **root-overlay `MaskFilter.blur` shadow** (or Flutter **`Banner`**) fixes it—even at **1px / alpha 1**.

---

## Symptoms (production + repro)

| Trigger | Broken behavior | Fixed by |
|---------|-----------------|----------|
| Small corner PNG/SVG OSG over video | Green/blue channels lost (red/luma remain); may flicker | Shadow shim, Banner, telestrator stroke, help overlay, `debugShowCheckedModeBanner` |
| Large lower-third OSG (wide bar) | Center band freezes / mirrors; top keeps moving | Same |
| Large OSG (~570px+) or half-screen OSG | Often **no** bug | N/A |

Preview panel (embedded player, normal window chrome) does **not** reproduce. Playout (hidden title bar, dedicated window, 1:1 output size) **does**.

---

## What we ruled out

- Video not repainting (progress `setState` + paint probes fire ~500ms)
- Unstable video destination rect
- `RepaintBoundary` around video (harmful)
- Moving OSG to `videoAreaOverlay` vs external stack (both broken in playout)
- Full-screen static veil (5% or 65%) between video and OSG
- 1px **opaque** `ColoredBox` at playout stack top, `MaterialApp.builder`, or root `OverlayEntry`
- Full-canvas low-alpha repaint shim in overlay
- Corner triangles + full-width 1px lines in video overlay (partial improvement only)

---

## What fixes it (bisected)

1. **`MaterialApp.debugShowCheckedModeBanner: true`** (framework `Banner` wrapping app)
2. **Root `OverlayEntry` + Flutter `Banner`** (empty message OK)
3. **Root `OverlayEntry` + custom corner paint:** **`MaskFilter.blur` shadow only** — fill optional  
   - Minimal working values: **cornerOffset=1, stripHeight=1, blur=1, shadow alpha=1** (`Color(0x01000000)`)
4. **Opaque telestrator** strokes (any size; tiny dot fixes lower-third case)
5. **Help overlay** (65% black) **above** entire player stack—not between video and OSG

**Key insight:** Not “dirty region size” alone. A **blurred shadow** in a **foreground painter** on a **root overlay** changes compositing; a **1px opaque line** in the same overlay slot does **not**.

Reference: Flutter `BannerPainter` (`packages/flutter/lib/src/widgets/banner.dart`) draws **shadow rect with blur**, then semi-transparent fill. Shadow appears necessary; fill is not.

Production shim: `obs_clipshow/lib/src/features/playout/playout_framework_compositing_strip.dart` (mount via `NavigatorState.overlay` in `app.dart`).

---

## Repro app structure

```
compositing_repro/
  lib/main.dart                 # FVP init, hidden title bar, toggles
  lib/compositing_shadow_shim.dart
  assets/video.mp4              # user-supplied (placeholder may exist)
  assets/overlay.png            # corner overlay PNG
```

**Stack layout (broken path):**

```
Scaffold
  Stack
    Positioned.fromRect(videoRect)
      Stack
        FittedBox → VideoPlayer (fvp GL texture)
        Positioned corner → Image.asset(overlay.png)
    Controls (switches)
```

**Fix path:** insert root `OverlayEntry` with `CompositingShadowShimOverlay` (see `compositing_shadow_shim.dart`).

---

## Validation checklist (agent)

1. **Environment:** Linux, Flutter stable, `fvp` 0.36.x, `video_player` 2.11.x. Record `flutter doctor -v`.
2. **Assets:** Replace placeholders with 1080p H.264 clip + ~470px corner PNG with alpha (same as production if possible).
3. **Baseline broken:** Corner overlay **ON**, shadow shim **OFF**, Banner **OFF** → observe chroma loss for ≥15s playback.
4. **Shadow fix:** Shadow shim **ON** only → colors normal.
5. **Banner control:** Banner **ON**, shadow **OFF** → colors normal.
6. **Opaque line control:** Replace shadow painter with 1px opaque `ColoredBox` in same `OverlayEntry` → expect **still broken** (confirms shadow-specific).
7. **Optional:** Toggle `debugShowCheckedModeBanner: true` in `CompositingReproApp` → broken case fixes without shim.
8. **Minimize:** Reduce shadow blur/alpha/size until broken returns; document threshold.

---

## Suggested upstream filing

| Repo | Rationale |
|------|-----------|
| **[flutter/flutter](https://github.com/flutter/flutter)** | Linux Skia/Impeller compositing of **GL texture layers** with **blurred overlay** vs opaque; `Banner`/`MaskFilter.blur` changes behavior |
| **[wang-bin/fvp](https://github.com/wang-bin/fvp)** | Linux `FlTextureGL` / `fvp_plugin.cc` FBO → texture; may interact with Flutter texture layer invalidation |
| **flutter/packages `video_player`** | Unlikely root cause (thin wrapper); mention only as API surface |

**Title idea:** “Linux: VideoPlayer (FVP GL texture) + overlay compositing corrupts chroma unless root overlay uses MaskFilter.blur”

**Include:** screen recording (broken vs shadow shim), `flutter doctor`, this repro zip, steps above.

---

## Related production files (obs_clipshow)

| File | Role |
|------|------|
| `lib/main.dart` | `fvp.registerWith` Linux decoders |
| `lib/src/app/app.dart` | Playout window hidden title bar; overlay mount |
| `lib/src/features/playout/playout_framework_compositing_strip.dart` | Production shadow shim |
| `lib/src/features/playout/clip_player_view.dart` | Video + `videoAreaOverlay` stack |
| `lib/src/features/playout/osg_playout_layer.dart` | OSG `Opacity` + `CustomPaint` templates |

---

## Copy to new repository

Copy entire `tmp/compositing_repro/` to a fresh git repo. Ensure `assets/video.mp4` and `assets/overlay.png` are present (or LFS). Do **not** commit obs_clipshow workspace paths.

---

## Open questions for upstream

1. Does **`Opacity` widget / `saveLayer`** on a small overlay bounds trigger partial texture composite on Linux?
2. Why does **`MaskFilter.blur`** on a **separate overlay layer** affect **underlying video texture** sampling?
3. Does **hidden title bar** / **client size = video size** affect EGL surface behavior (preview never breaks)?

---

## Agent success criteria

- [ ] Reproduces broken state on Linux with documented assets
- [ ] Confirms shadow shim fixes at minimal constants
- [ ] Confirms 1px opaque overlay entry does **not** fix
- [ ] Produces screen capture + minimized test case for issue body
- [ ] Files issue (or draft PR) with clear component label: `platform-linux`, `engine`, `video`
