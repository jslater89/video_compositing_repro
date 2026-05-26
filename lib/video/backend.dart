import "fvp_video.dart";
import "offline_rgba_video.dart";
import "repro_video.dart";
import "synthetic_video.dart";

// Compile-time backend selection via --dart-define:
//
//   FVP (default):          flutter run -d linux
//   synthetic bars:         --dart-define=VIDEO_BACKEND=synthetic
//   synthetic bars (FBO):   --dart-define=VIDEO_BACKEND=synthetic_fbo
//   offline RGBA frames:    --dart-define=VIDEO_BACKEND=offline_rgba
//   offline RGBA (FBO):     --dart-define=VIDEO_BACKEND=offline_rgba_fbo
//
// offline_rgba* requires: ./tool/extract_rgba_frames.sh

const String _videoBackend = String.fromEnvironment(
  "VIDEO_BACKEND",
  defaultValue: "fvp",
);

const String videoBackendLabel = _videoBackend == "synthetic"
    ? "synthetic"
    : _videoBackend == "synthetic_fbo"
        ? "synthetic_fbo"
        : _videoBackend == "offline_rgba"
            ? "offline_rgba"
            : _videoBackend == "offline_rgba_fbo"
                ? "offline_rgba_fbo"
                : "FVP";

Future<void> ensureVideoBackendInitialized() async {
  if (_videoBackend == "fvp") {
    await ensureFvpInitialized();
  }
}

ReproVideo createReproVideo() {
  switch (_videoBackend) {
    case "synthetic":
      return SyntheticReproVideo();
    case "synthetic_fbo":
      return SyntheticReproVideo(useFbo: true);
    case "offline_rgba":
      return OfflineRgbaReproVideo();
    case "offline_rgba_fbo":
      return OfflineRgbaReproVideo(useFbo: true);
    default:
      return FvpReproVideo();
  }
}
