#!/bin/bash
set -e

NAS_TARGET_DIR="/home/muyiwa/PrimaryNAS/DataFolder/PycharmProjects/OptionsAndFuturesCalculator"

echo "=== Starting Backup to NAS at $NAS_TARGET_DIR ==="

mkdir -p "$NAS_TARGET_DIR"

# --no-links: the NAS is mounted over CIFS, which cannot store a POSIX symlink
# unless the share is mounted with `mfsymlinks`. Without it every symlink fails
# with "Operation not supported (95)", rsync exits 23, and `set -e` then aborts
# the script before it can report success — so the backup has been failing on
# its last step on every run. The tree currently holds exactly one symlink,
# backend/sensen/external/CosyVoice/third_party/Matcha-TTS/data, which is
# vendored third-party code pointing at an absolute path on an upstream author's
# machine and is already dangling here, so nothing is lost by skipping it.
# rsync still prints a "skipping non-regular file" line for each one, so a
# symlink that does matter later will be visible rather than silently dropped.
rsync -avz --delete --no-links \
  --exclude="node_modules" \
  --exclude=".next" \
  --exclude="build" \
  --exclude=".git" \
  --exclude=".wrangler" \
  /home/muyiwa/Development/OptionsAndFuturesCalculator/ "$NAS_TARGET_DIR/"

echo "=== NAS Backup Completed Successfully! ==="
