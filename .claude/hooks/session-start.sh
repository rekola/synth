#!/bin/bash
# Cloud sessions start from a fresh clone: enable the repository's git hooks,
# install the build dependencies and configure the build, so the pre-commit
# hook (clang-format, clang-tidy against build/compile_commands.json), the
# build and the tests all work from the first commit.
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

cd "$CLAUDE_PROJECT_DIR"

git config core.hooksPath .githooks

packages="cmake pkg-config clang-format clang-tidy libnotcurses-dev libnotcurses++-dev libfmt-dev libsndfile1-dev libasound2-dev libunistring-dev libsoundtouch-dev libcairo2-dev"
missing=""
for p in $packages; do
  dpkg -s "$p" >/dev/null 2>&1 || missing="$missing $p"
done
if [ -n "$missing" ]; then
  sudo=""
  [ "$(id -u)" -eq 0 ] || sudo="sudo"
  $sudo apt-get update -qq
  # shellcheck disable=SC2086
  DEBIAN_FRONTEND=noninteractive $sudo apt-get install -y -qq $missing
fi

if [ ! -f build/compile_commands.json ]; then
  cmake -B build >/dev/null
fi
