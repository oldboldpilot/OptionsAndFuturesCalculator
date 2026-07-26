#!/bin/bash
set -e

NAS_TARGET_DIR="/home/muyiwa/PrimaryNAS/DataFolder/PycharmProjects/OptionsAndFuturesCalculator"

echo "=== Starting Backup to NAS at $NAS_TARGET_DIR ==="

mkdir -p "$NAS_TARGET_DIR"

rsync -avz --delete \
  --exclude="node_modules" \
  --exclude=".next" \
  --exclude="build" \
  --exclude=".git" \
  --exclude=".wrangler" \
  /home/muyiwa/Development/OptionsAndFuturesCalculator/ "$NAS_TARGET_DIR/"

echo "=== NAS Backup Completed Successfully! ==="
