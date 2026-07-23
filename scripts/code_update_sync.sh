#!/usr/bin/env bash
set -euo pipefail

echo "============================================================"
echo "  CODE UPDATE SYNC AGENT (config/update_policy.txt)"
echo "============================================================"

REPO_ROOT="$(git rev-parse --show-toplevel)"
POLICY_FILE="${REPO_ROOT}/config/update_policy.txt"
COMMIT_MSG="${1:-"chore: automated policy update sync"}"

if [[ ! -f "$POLICY_FILE" ]]; then
    echo "❌ ERROR: config/update_policy.txt not found!"
    exit 1
fi

echo "🔍 Checking Git status and remotes..."
git status

# Stage changes
echo "📦 Staging changes..."
git add -A

if git diff-index --quiet HEAD --; then
    echo "ℹ️ No uncommitted changes detected."
else
    echo "💾 Committing changes..."
    git commit -m "$COMMIT_MSG"
fi

# Push to GitHub via gh CLI / git
echo "🚀 Pushing to GitHub (origin master)..."
if command -v gh &> /dev/null && gh auth status &> /dev/null; then
    echo "  [gh CLI] Authenticated with GitHub. Syncing origin..."
    git push origin master
else
    echo "  [git CLI] Pushing to GitHub origin master..."
    git push origin master
fi

# Push to Gitea via git CLI
echo "🚀 Pushing to Gitea (gitea master)..."
if git remote | grep -q "gitea"; then
    git push gitea master
    echo "  [Gitea] Pushed to Gitea remote master cleanly."
else
    echo "⚠️ Warning: 'gitea' remote not configured. Skipping Gitea push."
fi

echo "============================================================"
echo "✅ CODE UPDATE SYNC COMPLETED SUCCESSFULLY."
echo "============================================================"
