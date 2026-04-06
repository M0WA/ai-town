#!/usr/bin/env bash
# test-sonarcloud.sh — local dry-run of the SonarCloud pipeline steps.
#
# Reproduces exactly what sonarcloud.yml does, using artifacts from an
# existing CI run so you don't need to push to main.
#
# Prerequisites:
#   - gh CLI authenticated (gh auth login)
#   - SONAR_TOKEN set in environment
#   - Java 17+ on PATH (for sonar-scanner)
#   - python3 on PATH
#
# Usage:
#   export SONAR_TOKEN=<your-token>
#   ./tools/test-sonarcloud.sh <ci_run_id> <head_sha>
#
# Finding the values:
#   ci_run_id — the number in the CI run URL, e.g.
#               https://github.com/M0WA/ai-town/actions/runs/24021747675
#               → 24021747675
#   head_sha  — shown on the run page, or: gh run view <run_id> --json headSha -q .headSha

set -euo pipefail

CI_RUN_ID="${1:-}"
HEAD_SHA="${2:-}"

if [[ -z "$CI_RUN_ID" || -z "$HEAD_SHA" ]]; then
  echo "Usage: $0 <ci_run_id> <head_sha>"
  echo ""
  echo "Example:"
  echo "  $0 24021747675 44b748e495b965fe7611e2d35865f824a0e1bd92"
  echo ""
  echo "Tip: get values for the latest CI run on main:"
  echo "  gh run list --repo M0WA/ai-town --workflow ci.yml --limit 1 --json databaseId,headSha"
  exit 1
fi

if [[ -z "${SONAR_TOKEN:-}" ]]; then
  echo "ERROR: SONAR_TOKEN environment variable is not set."
  exit 1
fi

WORK_DIR="$(mktemp -d /tmp/sonar-test-XXXXXX)"
trap 'rm -rf "$WORK_DIR"' EXIT

SONAR_SCANNER_VERSION="7.0.2.4839"
SCANNER_DIR="$WORK_DIR/sonar-scanner-cli-${SONAR_SCANNER_VERSION}-Linux-X64"

echo "==> Working directory: $WORK_DIR"
echo "==> CI run: $CI_RUN_ID  SHA: $HEAD_SHA"
echo ""

# Step 1: Download artifacts
echo "==> Downloading coverage-sonar-linux-${HEAD_SHA} ..."
gh run download "$CI_RUN_ID" \
  --repo M0WA/ai-town \
  --name "coverage-sonar-linux-${HEAD_SHA}" \
  --dir "$WORK_DIR"

echo "==> Downloading compile-commands-linux-${HEAD_SHA} ..."
gh run download "$CI_RUN_ID" \
  --repo M0WA/ai-town \
  --name "compile-commands-linux-${HEAD_SHA}" \
  --dir "$WORK_DIR"

ls -lh "$WORK_DIR"
echo ""

# Step 2: Download sonar-scanner if not cached
SCANNER_CACHE="$HOME/.cache/sonar-scanner-cli-${SONAR_SCANNER_VERSION}"
if [[ ! -d "$SCANNER_CACHE" ]]; then
  echo "==> Downloading sonar-scanner CLI ${SONAR_SCANNER_VERSION} ..."
  BINARIES_URL="https://binaries.sonarsource.com/Distribution/sonar-scanner-cli"
  SCANNER_FILE="sonar-scanner-cli-${SONAR_SCANNER_VERSION}-linux-x64.zip"
  curl --fail --silent --show-error --location \
       --user-agent sonarqube-scan-action \
       --output "/tmp/$SCANNER_FILE" \
       "${BINARIES_URL}/${SCANNER_FILE}"
  mkdir -p "$SCANNER_CACHE"
  unzip -q -o "/tmp/$SCANNER_FILE" -d "$SCANNER_CACHE"
  rm "/tmp/$SCANNER_FILE"
fi
SCANNER_BIN="$SCANNER_CACHE/sonar-scanner-${SONAR_SCANNER_VERSION}-linux-x64/bin/sonar-scanner"

# Step 3: Run sonar-scanner from repo root (matches projectBaseDir=.)
echo "==> Running sonar-scanner ..."
echo "    coverage.xml: $WORK_DIR/coverage.xml"
echo "    compile_commands.json: $WORK_DIR/compile_commands.json"
echo ""

REPO_ROOT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"

"$SCANNER_BIN" \
  -Dsonar.projectKey=M0WA_ai-town \
  -Dsonar.organization=m0wa \
  -Dsonar.sources=src \
  -Dsonar.tests=tests \
  -Dsonar.coverageReportPaths="$WORK_DIR/coverage.xml" \
  -Dsonar.exclusions="assets/**,build/**,tools/**" \
  -Dsonar.cpd.exclusions="tests/**" \
  -Dsonar.scm.provider=git \
  -Dsonar.projectBaseDir="$REPO_ROOT" \
  -Dsonar.cfamily.compile-commands="$WORK_DIR/compile_commands.json"
