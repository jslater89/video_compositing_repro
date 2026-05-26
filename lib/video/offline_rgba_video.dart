import "package:flutter/material.dart";
import "package:synthetic_texture/rgba_sequence_texture.dart";

import "repro_video.dart";

class OfflineRgbaReproVideo extends ChangeNotifier implements ReproVideo {
  OfflineRgbaReproVideo({this.useFbo = false});

  final bool useFbo;
  RgbaSequenceTextureController? _controller;
  bool _initialized = false;
  Size _intrinsicSize = Size.zero;

  @override
  bool get isInitialized => _initialized;

  @override
  Size get intrinsicSize => _intrinsicSize;

  @override
  Listenable get listenable => this;

  @override
  Future<void> initializeAsset(String assetPath) async {
    // Uses pre-extracted assets/video_rgba.bin (see tool/extract_rgba_frames.sh).
    final RgbaSequenceTextureController controller =
        RgbaSequenceTextureController(useFbo: useFbo);
    await controller.initialize();
    _controller = controller;
    _intrinsicSize = Size(
      controller.width.toDouble(),
      controller.height.toDouble(),
    );
    _initialized = true;
    notifyListeners();

    await Future<void>.delayed(Duration.zero);
    WidgetsBinding.instance.addPostFrameCallback((_) {
      controller.start();
    });
  }

  @override
  Future<void> dispose() async {
    await _controller?.dispose();
    _controller = null;
    _initialized = false;
    _intrinsicSize = Size.zero;
    super.dispose();
  }

  @override
  Widget buildPlayerWidget() {
    final int? textureId = _controller?.textureId;
    if (textureId == null) {
      return const SizedBox.shrink();
    }
    return Texture(textureId: textureId);
  }
}
