import "dart:async";

import "package:flutter/material.dart";
import "package:flutter/services.dart";
import "package:window_manager/window_manager.dart";

import "compositing_shadow_shim.dart";
import "video/backend.dart";
import "video/repro_video.dart";

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await windowManager.ensureInitialized();
  await ensureVideoBackendInitialized();
  await windowManager.setTitleBarStyle(TitleBarStyle.hidden);
  await windowManager.setSize(const Size(1280, 720));
  await windowManager.center();
  runApp(CompositingReproApp(backendLabel: videoBackendLabel));
}

class CompositingReproApp extends StatefulWidget {
  const CompositingReproApp({super.key, required this.backendLabel});

  final String backendLabel;

  @override
  State<CompositingReproApp> createState() => _CompositingReproAppState();
}

class _CompositingReproAppState extends State<CompositingReproApp> {
  final GlobalKey<NavigatorState> _navigatorKey = GlobalKey<NavigatorState>();

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      navigatorKey: _navigatorKey,
      debugShowCheckedModeBanner: false,
      title: "${widget.backendLabel} Compositing Repro",
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.blueGrey,
          brightness: Brightness.dark,
        ),
        brightness: Brightness.dark,
      ),
      home: ReproScreen(
        navigatorKey: _navigatorKey,
        backendLabel: widget.backendLabel,
      ),
    );
  }
}

class ReproScreen extends StatefulWidget {
  const ReproScreen({
    super.key,
    required this.navigatorKey,
    required this.backendLabel,
  });

  final GlobalKey<NavigatorState> navigatorKey;
  final String backendLabel;

  @override
  State<ReproScreen> createState() => _ReproScreenState();
}

class _ReproScreenState extends State<ReproScreen> {
  static const String _videoAsset = "assets/video.mp4";
  static const String _overlayAsset = "assets/overlay.png";

  ReproVideo? _video;
  String? _initError;
  bool _showCornerOverlay = false;
  bool _showShadowShim = false;
  bool _showDebugBanner = false;
  OverlayEntry? _shadowEntry;
  OverlayEntry? _bannerEntry;

  @override
  void initState() {
    super.initState();
    debugPrint("video backend: $videoBackendLabel");
    unawaited(_initVideo());
  }

  Future<void> _initVideo() async {
    final ReproVideo video = createReproVideo();
    try {
      await video.initializeAsset(_videoAsset);
      if (!mounted) {
        await video.dispose();
        return;
      }
      setState(() {
        _video = video;
        _initError = null;
      });
    } on Object catch (error) {
      await video.dispose();
      if (!mounted) {
        return;
      }
      setState(() {
        _initError = error.toString();
      });
    }
  }

  @override
  void dispose() {
    _removeOverlays();
    unawaited(_video?.dispose());
    super.dispose();
  }

  void _syncOverlays() {
    _removeOverlays();
    final OverlayState? overlay = widget.navigatorKey.currentState?.overlay;
    if (overlay == null) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) {
          _syncOverlays();
        }
      });
      return;
    }

    if (_showShadowShim) {
      _shadowEntry = OverlayEntry(
        opaque: false,
        builder: (BuildContext context) {
          final Size size = MediaQuery.sizeOf(context);
          return Positioned(
            left: 0,
            top: 0,
            width: size.width,
            height: size.height,
            child: const CompositingShadowShimOverlay(enabled: true),
          );
        },
      );
      overlay.insert(_shadowEntry!);
    }

    if (_showDebugBanner) {
      _bannerEntry = OverlayEntry(
        opaque: false,
        builder: (BuildContext context) {
          final Size size = MediaQuery.sizeOf(context);
          return Positioned(
            left: 0,
            top: 0,
            width: size.width,
            height: size.height,
            child: IgnorePointer(
              child: Material(
                type: MaterialType.transparency,
                child: Banner(
                  message: "DEBUG",
                  location: BannerLocation.topEnd,
                  child: SizedBox(width: size.width, height: size.height),
                ),
              ),
            ),
          );
        },
      );
      overlay.insert(_bannerEntry!);
    }
  }

  void _removeOverlays() {
    _shadowEntry?.remove();
    _shadowEntry?.dispose();
    _shadowEntry = null;
    _bannerEntry?.remove();
    _bannerEntry?.dispose();
    _bannerEntry = null;
  }

  void _toggleCornerOverlay() {
    setState(() => _showCornerOverlay = !_showCornerOverlay);
  }

  void _toggleShadowShim() {
    setState(() => _showShadowShim = !_showShadowShim);
    _syncOverlays();
  }

  void _toggleDebugBanner() {
    setState(() => _showDebugBanner = !_showDebugBanner);
    _syncOverlays();
  }

  @override
  Widget build(BuildContext context) {
    return CallbackShortcuts(
      bindings: <ShortcutActivator, VoidCallback>{
        const SingleActivator(LogicalKeyboardKey.digit1): _toggleCornerOverlay,
        const SingleActivator(LogicalKeyboardKey.keyO): _toggleCornerOverlay,
        const SingleActivator(LogicalKeyboardKey.digit2): _toggleShadowShim,
        const SingleActivator(LogicalKeyboardKey.keyS): _toggleShadowShim,
        const SingleActivator(LogicalKeyboardKey.digit3): _toggleDebugBanner,
        const SingleActivator(LogicalKeyboardKey.keyB): _toggleDebugBanner,
      },
      child: Focus(
        autofocus: true,
        child: Scaffold(
          backgroundColor: Colors.black,
          body: Stack(
            fit: StackFit.expand,
            children: <Widget>[
              _buildVideoStack(),
              // _buildControls() — on-screen controls break the repro; use keys above.
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildVideoStack() {
    if (_initError != null) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Text(
            "Failed to load $_videoAsset.\n\n"
            "Add assets/video.mp4 (see assets/README.md).\n\n"
            "Backend: ${widget.backendLabel}\n\n"
            "Error: $_initError",
            textAlign: TextAlign.center,
          ),
        ),
      );
    }
    final ReproVideo? video = _video;
    if (video == null || !video.isInitialized) {
      return const Center(child: CircularProgressIndicator());
    }

    return ListenableBuilder(
      listenable: video.listenable,
      builder: (BuildContext context, Widget? child) {
        final Size intrinsic = video.intrinsicSize;
        if (intrinsic.width <= 0 || intrinsic.height <= 0) {
          return const Center(child: CircularProgressIndicator());
        }
        return LayoutBuilder(
          builder: (BuildContext context, BoxConstraints constraints) {
            final Size container = Size(
              constraints.maxWidth,
              constraints.maxHeight,
            );
            final Rect videoRect = _videoDestinationRect(
              container: container,
              intrinsic: intrinsic,
            );
            return Stack(
              fit: StackFit.expand,
              clipBehavior: Clip.none,
              children: <Widget>[
                Positioned.fromRect(
                  rect: videoRect,
                  child: Stack(
                    fit: StackFit.expand,
                    clipBehavior: Clip.none,
                    children: <Widget>[
                      FittedBox(
                        fit: BoxFit.contain,
                        child: SizedBox(
                          width: intrinsic.width,
                          height: intrinsic.height,
                          child: video.buildPlayerWidget(),
                        ),
                      ),
                      if (_showCornerOverlay) _buildCornerOverlay(videoRect.size),
                    ],
                  ),
                ),
              ],
            );
          },
        );
      },
    );
  }

  Widget _buildCornerOverlay(Size videoSize) {
    const double overlayNorm = 0.22;
    final double side = videoSize.shortestSide * overlayNorm;
    return Positioned(
      left: 16,
      top: 16,
      width: side,
      height: side,
      child: Image.asset(
        _overlayAsset,
        fit: BoxFit.contain,
        errorBuilder: (BuildContext context, Object error, StackTrace? stack) {
          return Container(
            color: Colors.white24,
            alignment: Alignment.center,
            child: const Text(
              "overlay.png?",
              style: TextStyle(fontSize: 12),
            ),
          );
        },
      ),
    );
  }

  // ignore: unused_element — kept for reference; on-screen controls break the repro.
  Widget _buildControls() {
    return Align(
      alignment: Alignment.bottomLeft,
      child: Material(
        color: Colors.black54,
        child: Padding(
          padding: const EdgeInsets.all(12),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: <Widget>[
              Text(
                "${widget.backendLabel} + corner overlay compositing repro (Linux)",
                style: const TextStyle(fontWeight: FontWeight.bold),
              ),
              const SizedBox(height: 8),
              SwitchListTile(
                contentPadding: EdgeInsets.zero,
                title: const Text("Corner overlay (videoAreaOverlay)"),
                value: _showCornerOverlay,
                onChanged: (bool value) {
                  setState(() => _showCornerOverlay = value);
                },
              ),
              SwitchListTile(
                contentPadding: EdgeInsets.zero,
                title: const Text("Shadow shim (root OverlayEntry)"),
                value: _showShadowShim,
                onChanged: (bool value) {
                  setState(() => _showShadowShim = value);
                  _syncOverlays();
                },
              ),
              SwitchListTile(
                contentPadding: EdgeInsets.zero,
                title: const Text("Flutter Banner overlay (control)"),
                value: _showDebugBanner,
                onChanged: (bool value) {
                  setState(() => _showDebugBanner = value);
                  _syncOverlays();
                },
              ),
              const SizedBox(height: 8),
              const Text(
                "Keyboard: 1/O corner overlay, 2/S shadow shim, 3/B debug banner.\n"
                "Broken: overlay ON, shims OFF → green/blue chroma loss or "
                "vertical band freeze.\n"
                "Fixed: enable shadow shim OR Banner overlay.",
                style: TextStyle(fontSize: 12, color: Colors.white70),
              ),
            ],
          ),
        ),
      ),
    );
  }

  static Rect _videoDestinationRect({
    required Size container,
    required Size intrinsic,
  }) {
    if (intrinsic.width <= 0 ||
        intrinsic.height <= 0 ||
        container.width <= 0 ||
        container.height <= 0) {
      return Rect.zero;
    }
    final FittedSizes fitted = applyBoxFit(
      BoxFit.contain,
      intrinsic,
      container,
    );
    final Size destination = fitted.destination;
    final double left = (container.width - destination.width) / 2;
    final double top = (container.height - destination.height) / 2;
    return Rect.fromLTWH(left, top, destination.width, destination.height);
  }
}
