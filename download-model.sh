#!/usr/bin/env bash
# ----------------------------------------------------------------------------
# download-model.sh — fetch a whisper.cpp GGML model into ./models/
#
#   ./download-model.sh            # default: ggml-large-v3-turbo.bin
#   ./download-model.sh small      # ggml-small.bin
#   ./download-model.sh base       # ggml-base.bin
#   ./download-model.sh tiny       # ggml-tiny.bin
#   ./download-model.sh medium     # ggml-medium.bin
#   ./download-model.sh large-v3   # ggml-large-v3.bin
#   ./download-model.sh large-v3-turbo   # (default)
# ----------------------------------------------------------------------------
set -euo pipefail

# Map a friendly name to the exact GGML filename on Hugging Face.
#   https://huggingface.co/ggerganov/whisper.cpp/resolve/main/<file>
declare -A NAMES=(
  [tiny]="ggml-tiny.bin"
  [base]="ggml-base.bin"
  [small]="ggml-small.bin"
  [medium]="ggml-medium.bin"
  [large-v3]="ggml-large-v3.bin"
  [large-v3-turbo]="ggml-large-v3-turbo.bin"
)

NAME="${1:-large-v3-turbo}"
FILE="${NAMES[$NAME]:-}"
if [ -z "$FILE" ]; then
  echo "Unknown model: '$NAME'" >&2
  echo "Available: ${!NAMES[*]}" >&2
  exit 1
fi

mkdir -p models
DEST="models/$FILE"
URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$FILE"

if [ -f "$DEST" ]; then
  echo "Already downloaded: $DEST"
else
  echo "Downloading $FILE ..."
  if command -v curl >/dev/null 2>&1; then
    curl -L --fail -o "$DEST" "$URL"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$DEST" "$URL"
  else
    echo "Need curl or wget to download the model." >&2
    exit 1
  fi
fi
echo "Saved to $DEST"
echo "Run: ./whisperpipe -m $DEST"
