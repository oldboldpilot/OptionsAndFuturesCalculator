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
# NO --delete, AND THAT IS NOT A STYLE CHOICE. Measured with --dry-run on
# 2026-10-05: it would have removed 636 files from the share, 612 of them
# .remember/logs/autonomous/save-*.log, which exist ON THE NAS AND NOT IN THIS
# CHECKOUT -- the NAS is their only copy. The other 24 are genuinely stale
# (pre-bump nanobind headers, superseded frontend/out chunks) and are not worth
# a mechanism that can destroy history to collect them. The NAS is a FILE STORE,
# not a mirror of this working tree; mirroring a checkout onto it is how a sensen
# backup took a share from 12G to 432M on 2026-09-29 and removed a config/.env
# that lived nowhere else.
#
# --exclude='build*/' RATHER THAN 'build': the bare form matches only a directory
# named exactly `build` and misses build-sched/, build-gateway/ and build-lane/.
# A run with the narrow pattern read 26 GB of build artifacts before it was
# stopped -- object files and BMIs that are reproducible from source and have no
# business on a backup share.
#
# -c (checksum) RATHER THAN the default size+mtime quick check: SMB will not let
# the client make an mtime stick, so every file compares as changed on every run
# and the whole tree is rewritten -- slow, and it hides which files really moved.
# --size-only would also stop the churn and would SKIP a same-size edit, which is
# the silent-staleness failure this whole arrangement exists to prevent.
rsync -a -c --no-links --info=stats2 \
  --exclude="node_modules" \
  --exclude=".next" \
  --exclude="build*/" \
  --exclude=".venv/" \
  --exclude=".git" \
  --exclude=".wrangler" \
  /home/muyiwa/Development/OptionsAndFuturesCalculator/ "$NAS_TARGET_DIR/"

echo "=== NAS Backup Completed Successfully! ==="
