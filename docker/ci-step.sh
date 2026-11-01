#!/usr/bin/env bash
#
# Run one of the build tasks and, if it fails, say why in a way that can
# be read without a log server.
#
#     bash docker/ci-step.sh 3dsx
#
# The output of the build is written to build/<task>.log. When the task
# fails, the lines that say what broke are turned into GitHub annotations.
# An annotation is attached to the run itself and is handed out by the
# API, so the reason for a failure does not need anybody to go looking
# for a log that not every environment can download.

set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
TASK="${1:-all}"
LOG="$ROOT/build/$TASK.log"

mkdir -p "$ROOT/build"

echo "::group::The build output"
set +e
bash "$HERE/ci-build.sh" "$TASK" 2>&1 | tee "$LOG"
status=${PIPESTATUS[0]}
set -e
echo "::endgroup::"

if [ "$status" -ne 0 ]; then
    echo "The $TASK build failed with status $status."

    # Only a handful of annotations survive per job, so spend them on the
    # lines that say what broke rather than on the whole tail.
    grep -E 'rror|Error [0-9]|No such file|not found|No rule|undefined reference|cannot find' "$LOG" \
        | head -n 8 | while IFS= read -r line; do
            line="${line//$'\r'/}"
            echo "::error::$line"
        done

    # The tail is in the log file, and the log file is uploaded as an
    # artifact, so the whole of it is still there for anybody who can
    # reach the artifacts.
    echo
    echo "The last lines of the build:"
    tail -n 25 "$LOG"

    exit "$status"
fi

echo "The $TASK build passed."
