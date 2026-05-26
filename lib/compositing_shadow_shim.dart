import "dart:math" as math;

import "package:flutter/material.dart";

/// Minimal shadow-only corner paint that avoids FVP/Linux chroma corruption
/// when composited over a [VideoPlayer] texture (see HANDOFF.md).
class CompositingShadowShimOverlay extends StatelessWidget {
  const CompositingShadowShimOverlay({
    super.key,
    required this.enabled,
    this.cornerOffset = 1,
    this.stripHeight = 1,
    this.shadowColor = const Color(0x01000000),
    this.shadowBlur = 1,
  });

  final bool enabled;
  final double cornerOffset;
  final double stripHeight;
  final Color shadowColor;
  final double shadowBlur;

  @override
  Widget build(BuildContext context) {
    if (!enabled) {
      return const SizedBox.shrink();
    }
    final Size size = MediaQuery.sizeOf(context);
    return IgnorePointer(
      child: CustomPaint(
        foregroundPainter: _CornerShadowPainter(
          layoutDirection: Directionality.of(context),
          cornerOffset: cornerOffset,
          stripHeight: stripHeight,
          shadowColor: shadowColor,
          shadowBlur: shadowBlur,
        ),
        child: SizedBox(width: size.width, height: size.height),
      ),
    );
  }
}

class _CornerShadowPainter extends CustomPainter {
  _CornerShadowPainter({
    required this.layoutDirection,
    required this.cornerOffset,
    required this.stripHeight,
    required this.shadowColor,
    required this.shadowBlur,
  });

  final TextDirection layoutDirection;
  final double cornerOffset;
  final double stripHeight;
  final Color shadowColor;
  final double shadowBlur;

  @override
  void paint(Canvas canvas, Size size) {
    final Rect stripRect = Rect.fromLTWH(
      -cornerOffset,
      cornerOffset - stripHeight,
      cornerOffset * 2,
      stripHeight,
    );

    canvas.save();
    canvas.translate(
      layoutDirection == TextDirection.ltr ? size.width : 0,
      0,
    );
    canvas.rotate(
      layoutDirection == TextDirection.ltr ? math.pi / 4 : -math.pi / 4,
    );
    canvas.drawRect(
      stripRect,
      Paint()
        ..color = shadowColor
        ..maskFilter = MaskFilter.blur(BlurStyle.normal, shadowBlur),
    );
    canvas.restore();
  }

  @override
  bool shouldRepaint(covariant _CornerShadowPainter oldDelegate) {
    return oldDelegate.layoutDirection != layoutDirection ||
        oldDelegate.cornerOffset != cornerOffset ||
        oldDelegate.stripHeight != stripHeight ||
        oldDelegate.shadowColor != shadowColor ||
        oldDelegate.shadowBlur != shadowBlur;
  }
}
