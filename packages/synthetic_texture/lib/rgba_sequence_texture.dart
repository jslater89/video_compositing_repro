import "dart:async";
import "dart:io";

import "package:flutter/scheduler.dart";
import "package:flutter/services.dart";
import "package:path/path.dart" as p;

export "synthetic_texture.dart" show SyntheticTextureController;

/// Pre-decoded RGBA frames from [assets/video_rgba.bin] uploaded via FlTextureGL.
class RgbaSequenceTextureController {
  RgbaSequenceTextureController({
    this.assetPath = "assets/video_rgba.bin",
    this.useFbo = false,
  });

  static const MethodChannel _channel = MethodChannel("synthetic_texture");

  final String assetPath;
  final bool useFbo;

  int? textureId;
  int width = 0;
  int height = 0;
  int frameCount = 0;
  Ticker? _ticker;

  Future<void> initialize() async {
    if (!Platform.isLinux) {
      throw UnsupportedError("RgbaSequenceTextureController is Linux-only.");
    }

    final String path = await _resolveAssetFile(assetPath);
    final Map<Object?, Object?> result =
        await _channel.invokeMethod<Map<Object?, Object?>>(
      "createFromFile",
      <String, Object>{
        "path": path,
        "useFbo": useFbo,
      },
    ) ?? <Object?, Object?>{};

    textureId = result["textureId"] as int?;
    width = result["width"] as int? ?? 0;
    height = result["height"] as int? ?? 0;
    frameCount = result["frameCount"] as int? ?? 0;

    if (textureId == null || width <= 0 || height <= 0 || frameCount <= 0) {
      throw StateError(
        "Invalid RGBA sequence from plugin. "
        "Run tool/extract_rgba_frames.sh to generate $assetPath.",
      );
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

  static Future<String> _resolveAssetFile(String assetKey) async {
    final String bundled = p.normalize(
      p.join(
        File(Platform.resolvedExecutable).parent.path,
        "data",
        "flutter_assets",
        assetKey,
      ),
    );
    if (await File(bundled).exists()) {
      return bundled;
    }

    final ByteData data = await rootBundle.load(assetKey);
    final File temp = File(
      p.join(Directory.systemTemp.path, "compositing_repro_${p.basename(assetKey)}"),
    );
    await temp.writeAsBytes(
      data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes),
      flush: true,
    );
    return temp.path;
  }
}
