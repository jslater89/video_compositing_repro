import "package:flutter/material.dart";

/// Shared surface for FVP and synthetic FlTextureGL backends.
abstract class ReproVideo {
  bool get isInitialized;

  Size get intrinsicSize;

  Listenable get listenable;

  Future<void> initializeAsset(String assetPath);

  Future<void> dispose();

  Widget buildPlayerWidget();
}
