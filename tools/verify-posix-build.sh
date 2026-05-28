#!/usr/bin/env bash
# verify-posix-build.sh — POSIX-side build smoke for the windows-port branch.
#
# This script is intentionally minimal and self-contained. It is designed to
# be run from anywhere; it `cd`s into the directory of this script and walks
# up to the repo root, then runs autoreconf / configure / make and captures
# the result in a JSON-ish summary plus a build log.
#
# Why this script exists
# ----------------------
# The windows-port-release-candidate branch makes large changes to compat/,
# tmux core C, configure.ac and tmux-protocol.h. Several reviewers pointed
# out that POSIX-side regressions were possible (AC_PROG_CXX, MSG_RESIZE
# ordering, %token ERROR rename, KEYC_BREAK position, PROTOCOL_VERSION
# rollback, ...). All of those should be invisible to a Linux/BSD build.
# This script is the cheapest way to confirm that.
#
# Usage
# -----
#   bash tools/verify-posix-build.sh [--clean] [--jobs N] [--prefix DIR]
#
# Typical invocation on a fresh Linux box (e.g. via `ioa-ssh-cli ssh`):
#   git clone <repo> /tmp/tmux-windows-port
#   cd /tmp/tmux-windows-port
#   git checkout windows-port-release-candidate
#   bash tools/verify-posix-build.sh --clean --jobs $(nproc)
#
# Exit codes:
#   0  build & link succeeded
#   1  configure failed (missing libevent / ncurses / yacc / etc.)
#   2  make failed (compile error)
#   3  link failed
#   4  binary did not start (-V smoke failed)

set -u
set -o pipefail

CLEAN=0
JOBS=${JOBS:-1}
PREFIX=${PREFIX:-}
LOG_DIR="${LOG_DIR:-build-logs}"

while [ $# -gt 0 ]; do
    case "$1" in
        --clean)  CLEAN=1; shift ;;
        --jobs)   JOBS="$2"; shift 2 ;;
        --prefix) PREFIX="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 64 ;;
    esac
done

# Resolve repo root (parent of tools/).
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

mkdir -p "$LOG_DIR"
SUMMARY="$LOG_DIR/posix-verify-summary.json"
BUILD_LOG="$LOG_DIR/build.log"
CFG_LOG="$LOG_DIR/configure.log"

emit_summary() {
    local stage="$1"
    local status="$2"
    local extra="${3:-}"
    cat >"$SUMMARY" <<EOF
{
  "stage": "$stage",
  "status": "$status",
  "host": "$(uname -srm 2>/dev/null || true)",
  "compiler": "$(${CC:-cc} --version 2>/dev/null | head -1 || true)",
  "git_head": "$(git rev-parse --short HEAD 2>/dev/null || echo unknown)",
  "git_branch": "$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)",
  "uncommitted": "$(git diff --quiet 2>/dev/null && echo no || echo yes)",
  "jobs": $JOBS,
  "extra": "$extra"
}
EOF
    echo "[verify] summary -> $SUMMARY"
    cat "$SUMMARY"
}

step() { echo; echo "==[ $* ]=="; }

step "Environment"
echo "  cwd=$REPO_ROOT"
echo "  branch=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo ?)"
echo "  head=$(git rev-parse --short HEAD 2>/dev/null || echo ?)"
echo "  uncommitted=$(git diff --quiet 2>/dev/null && echo no || echo yes)"
echo "  jobs=$JOBS"
echo "  ${CC:-cc} --version: $(${CC:-cc} --version 2>/dev/null | head -1 || echo missing)"
echo "  pkg-config libevent: $(pkg-config --modversion libevent 2>/dev/null || echo missing)"
echo "  ncursesw: $(pkg-config --modversion ncursesw 2>/dev/null || pkg-config --modversion ncurses 2>/dev/null || echo missing)"
echo "  yacc: $(command -v yacc 2>/dev/null || command -v bison 2>/dev/null || echo missing)"

# Cross-platform safety net: if the working tree was checked out on Windows
# with core.autocrlf=true (which is git for Windows' default), every .ac /
# .am / .y / .c / .h ends up with CRLF. autoconf 2.72 then mis-parses the
# `\<CR>` line continuation in AC_CHECK_HEADERS / AC_CHECK_FUNCS / etc, and
# the generated configure script dies in `for ac_header in \ <CR>...`.
# Strip CRs in-place from every Unix-toolchain input file before we touch
# autoreconf. On a tree that's already pure LF this is a no-op.
step "Normalise CRLF -> LF for Unix toolchain inputs"
NORMALISED=0
while IFS= read -r f; do
    if grep -qP '\r$' "$f" 2>/dev/null; then
        sed -i 's/\r$//' "$f"
        NORMALISED=$((NORMALISED + 1))
    fi
done < <(
    find . -type f \
        \( -name '*.c' -o -name '*.cc' -o -name '*.h' -o -name '*.hh' \
        -o -name '*.ac' -o -name '*.am' -o -name '*.in' -o -name '*.m4' \
        -o -name '*.y' -o -name '*.l' -o -name '*.sh' -o -name '*.awk' \
        -o -name 'Makefile' -o -name 'Makefile.*' \) \
        -not -path './.git/*' -not -path './build-logs/*' \
        -not -path './.codex-tmp/*'
)
echo "  files normalised: $NORMALISED"

if [ "$CLEAN" -eq 1 ]; then
    step "make distclean (best effort)"
    make distclean 2>/dev/null || true
    rm -rf autom4te.cache aclocal.m4 configure config.* compat/.deps Makefile.in compat/Makefile.in
fi

step "autoreconf -fi"
if ! autoreconf -fi 2>&1 | tee "$CFG_LOG"; then
    emit_summary autoreconf failed "autoreconf -fi failed; see $CFG_LOG"
    exit 1
fi

step "./configure"
CONFIGURE_ARGS=()
if [ -n "$PREFIX" ]; then
    CONFIGURE_ARGS+=(--prefix="$PREFIX")
fi
if ! ./configure "${CONFIGURE_ARGS[@]}" 2>&1 | tee -a "$CFG_LOG"; then
    emit_summary configure failed "./configure failed; see $CFG_LOG"
    exit 1
fi

step "make -j$JOBS"
if ! make -j"$JOBS" 2>&1 | tee "$BUILD_LOG"; then
    if grep -qE 'error: |Error 1' "$BUILD_LOG"; then
        emit_summary make failed "compile error; see $BUILD_LOG"
        exit 2
    fi
    emit_summary make failed "make returned non-zero; see $BUILD_LOG"
    exit 3
fi

step "binary smoke"
if [ ! -x ./tmux ]; then
    emit_summary binary missing "./tmux not produced"
    exit 3
fi
if ! ./tmux -V 2>&1; then
    emit_summary binary nostart "./tmux -V failed"
    exit 4
fi

step "PROTOCOL_VERSION sanity"
PV="$(grep -E '^#define[[:space:]]+PROTOCOL_VERSION' tmux-protocol.h | awk '{print $3}')"
echo "  tmux-protocol.h PROTOCOL_VERSION = $PV (expected: 8 after rollback)"
if [ "$PV" != "8" ]; then
    emit_summary protocol unexpected "PROTOCOL_VERSION=$PV (expected 8)"
    # Not a fatal error if you intentionally bumped it; do not exit.
fi

emit_summary all ok "tmux $(./tmux -V | awk '{print $2}') built successfully"
echo
echo "[verify] OK — POSIX build & link succeeded."
exit 0
