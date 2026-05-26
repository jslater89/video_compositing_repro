import "package:flutter/material.dart";
import "package:synthetic_texture/synthetic_texture.dart";

import "repro_video.dart";

class SyntheticReproVideo extends ChangeNotifier implements ReproVideo {
  SyntheticReproVideo({this.useFbo = false});

  final bool useFbo;
  SyntheticTextureController? _controller;
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
    final SyntheticTextureController controller =
        SyntheticTextureController(useFbo: useFbo);
    await controller.initialize();
    _controller = controller;
    _intrinsicSize = Size(
      controller.width.toDouble(),
      controller.height.toDouble(),
    );
    _initialized = true;
    notifyListeners();

    // Wait for Texture widget to mount before driving frame updates.
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
