import "dart:async";
import "dart:io";

import "package:flutter/scheduler.dart";
import "package:flutter/services.dart";

/// Planar YUV420 SMPTE bars rendered via GLES YUV→RGB shader into FlTextureGL.
class YuvPatternTextureController {
  YuvPatternTextureController({
    this.width = 1920,
    this.height = 1080,
  });

  static const MethodChannel _channel = MethodChannel("synthetic_texture");

  final int width;
  final int height;

  int? textureId;
  Ticker? _ticker;

  Future<void> initialize() async {
    if (!Platform.isLinux) {
      throw UnsupportedError("YuvPatternTextureController is Linux-only.");
    }
    final Map<Object?, Object?> result =
        await _channel.invokeMethod<Map<Object?, Object?>>(
      "createYuv",
      <String, int>{
        "width": width,
        "height": height,
      },
    ) ?? <Object?, Object?>{};
    textureId = result["textureId"] as int?;
    if (textureId == null) {
      throw StateError("YUV synthetic texture plugin did not return textureId.");
    }
  }

  void start() {
    if (textureId == null) {
      throw StateError("Call initialize() before start().");
    }
    _ticker?.dispose();
    _ticker = Ticker((Duration elapsed) {
      final int? id = textureId;
      if (id == null) {
        return;
      }
      unawaited(
        _channel.invokeMethod<void>(
          "markFrameAvailable",
          <String, int>{"textureId": id},
        ),
      );
    })
      ..start();
  }

  Future<void> dispose() async {
    _ticker?.dispose();
    _ticker = null;
    final int? id = textureId;
    textureId = null;
    if (id != null) {
      await _channel.invokeMethod<void>(
        "dispose",
        <String, int>{"textureId": id},
      );
    }
  }
}
