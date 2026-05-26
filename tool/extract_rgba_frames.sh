#!/usr/bin/env bash
# Extract a loopable RGBA frame sequence from video.mp4 for offline-RGBA bisect.
#
# Output format (assets/video_rgba.bin):
#   magic "RGBQ" (4 bytes)
#   width, height, frame_count (uint32 LE each)
#   frame_count * width * height * 4 bytes RGBA
#
# Usage:
#   ./tool/extract_rgba_frames.sh [max_frames]
#
# Default: 90 frames (~1.5s @ 60fps, ~750 MiB at 1080p). Increase for longer loops.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INPUT="${ROOT}/assets/video.mp4"
OUTPUT="${ROOT}/assets/video_rgba.bin"
MAX_FRAMES="${1:-90}"

if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "ffmpeg is required." >&2
  exit 1
fi

if [[ ! -f "$INPUT" ]]; then
  echo "Missing $INPUT" >&2
  exit 1
fi

IFS=, read -r WIDTH HEIGHT <<< "$(ffprobe -v error -select_streams v:0 \
  -show_entries stream=width,height -of csv=p=0 "$INPUT")"

TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT

echo "Extracting ${MAX_FRAMES} frames (${WIDTH}x${HEIGHT}) from $(basename "$INPUT")..."

ffmpeg -nostdin -hide_banner -loglevel error -y -i "$INPUT" \
  -frames:v "$MAX_FRAMES" -f rawvideo -pix_fmt rgba "$TMP"

PAYLOAD_BYTES="$(stat -c%s "$TMP")"
EXPECTED=$((MAX_FRAMES * WIDTH * HEIGHT * 4))
if [[ "$PAYLOAD_BYTES" -ne "$EXPECTED" ]]; then
  echo "Unexpected payload size: got $PAYLOAD_BYTES, expected $EXPECTED" >&2
  exit 1
fi

{
  printf 'RGBQ'
  python3 - "$WIDTH" "$HEIGHT" "$MAX_FRAMES" <<'PY'
import struct, sys
w, h, n = map(int, sys.argv[1:])
sys.stdout.buffer.write(struct.pack("<III", w, h, n))
PY
  cat "$TMP"
} >"$OUTPUT"

echo "Wrote $OUTPUT ($(numfmt --to=iec-i --suffix=B "$(stat -c%s "$OUTPUT")"))"
