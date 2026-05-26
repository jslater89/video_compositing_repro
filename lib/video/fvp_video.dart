import "dart:io";

import "package:flutter/material.dart";
import "package:fvp/fvp.dart" as fvp;
import "package:video_player/video_player.dart";

import "repro_video.dart";

Future<void> ensureFvpInitialized() async {
  if (Platform.isLinux) {
    // mdk equivalent of hwdec=no: skip VAAPI/CUDA/VDPAU, use software FFmpeg only.
    const String hwdec = String.fromEnvironment("FVP_HWDEC", defaultValue: "no");
    final Map<String, Object> options = <String, Object>{
      "platforms": <String>["linux"],
    };
    if (hwdec == "no") {
      options["video.decoders"] = <String>["FFmpeg"];
    }
    debugPrint("fvp hwdec: $hwdec");
    fvp.registerWith(options: options);
  }
}

class FvpReproVideo implements ReproVideo {
  VideoPlayerController? _controller;

  @override
  bool get isInitialized =>
      _controller?.value.isInitialized ?? false;

  @override
  Size get intrinsicSize => _controller?.value.size ?? Size.zero;

  @override
  Listenable get listenable => _controller!;

  @override
  Future<void> initializeAsset(String assetPath) async {
    final VideoPlayerController controller =
        VideoPlayerController.asset(assetPath);
    await controller.initialize();
    await controller.setLooping(true);
    await controller.play();
    _controller = controller;
  }

  @override
  Future<void> dispose() async {
    await _controller?.dispose();
    _controller = null;
  }

  @override
  Widget buildPlayerWidget() {
    return VideoPlayer(_controller!);
  }
}
