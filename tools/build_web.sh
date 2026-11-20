#!/bin/sh
# PCDEMOS 64k - the emscripten build.
#
#     sh tools/build_web.sh [demo] [outdir]
#     sh tools/build_web.sh ukko build/web
#
# RUN THIS EVEN WHEN YOU ARE NOT SHIPPING A WEB BUILD. emcc is clang
# with -std=c89 -pedantic, which is far stricter than MSVC - and MSVC
# has no C89 switch at all, so this is THE ONLY REAL C89 CHECK.
#
# THIRD-PARTY SOURCES COMPILE WITH OURS, which is not the usual
# arrangement and is only true because of what was picked: ibxm/ac
# builds with -ansi -pedantic -Wall in its own Makefile, so it
# survives the C89 gate unchanged. Vendor something written in C99
# style later and it will have to be compiled separately with its own
# flags, or the gate has to be loosened for our code too - which
# would throw away the thing that makes running this worthwhile.

# The packed EXE is EMBEDDED at the same relative path the native
# build opens, so nothing under src/ knows which host it is under.
#
# THE PAGE'S TEXT IS tools/web/<demo>.html and the page itself is
# tools/web_shell.html. The two are composed into a throwaway copy
# under build/. SUBSTITUTING INTO THE TEMPLATE IN PLACE WOULD CONSUME
# ITS MARKERS, so the first build would work and every build after it
# would ship the first demo's text.

set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEMO=${1:-ukko}
OUT=${2:-$ROOT/build/web}

cd "$ROOT"

# The Windows emsdk ships emcc.bat and no extensionless script, and
# a POSIX shell will not resolve one from the other. WORSE, THE .bat
# IS NOT MARKED EXECUTABLE, so command -v will not report it however
# good PATH is - bash runs it anyway through its Windows layer, sh
# honours the mode bits and refuses. Calling it by its full path
# works in both, so the last resort is to go and look.
#
# EMCC=/path/to/emcc overrides the lot.
if [ -z "$EMCC" ]; then
    if command -v emcc >/dev/null 2>&1; then
        EMCC=emcc
    elif command -v emcc.bat >/dev/null 2>&1; then
        EMCC=emcc.bat
    else
        OLDIFS=$IFS
        IFS=:
        for d in $PATH; do
            if [ -f "$d/emcc.bat" ]; then EMCC="$d/emcc.bat"; break; fi
            if [ -f "$d/emcc" ]; then EMCC="$d/emcc"; break; fi
        done
        IFS=$OLDIFS
    fi
fi
if [ -z "$EMCC" ]; then
    echo "emcc not found - install emsdk and run its env script" >&2
    echo "  https://emscripten.org/docs/getting_started/downloads.html" >&2
    exit 1
fi

if [ ! -d "src/$DEMO" ]; then
    echo "no such demo: src/$DEMO" >&2
    exit 1
fi

EXE="reverse/ghidra/$(echo "$DEMO" | tr 'a-z' 'A-Z').EXE"
if [ ! -f "$EXE" ]; then
    echo "missing $EXE - the port depacks the original at runtime" >&2
    exit 1
fi

mkdir -p "$OUT"
mkdir -p "$ROOT/build"

# The per-demo text, and the title out of its first line. A MISSING
# FRAGMENT IS A WARNING, NOT AN ERROR: this script is also the only
# C89 gate we have, and it has to stay runnable before a new demo has
# any prose written for it.
FRAG="tools/web/$DEMO.html"
SHELL_OUT="$ROOT/build/shell_$DEMO.html"
UPPER=$(echo "$DEMO" | tr 'a-z' 'A-Z')
TITLE=""

if [ -f "$FRAG" ]; then
    TITLE=$(sed -n 's/^<!-- *title: *\(.*\)-->.*$/\1/p' "$FRAG" | head -1 | sed 's/ *$//')
else
    echo "warning: no $FRAG - the page will have no text" >&2
    FRAG="$ROOT/build/frag_$DEMO.html"
    printf '%s\n' "<p>$UPPER</p>" > "$FRAG"
fi
[ -n "$TITLE" ] || TITLE="PCDEMOS 64k - $UPPER"

# r reads the fragment in after the marker line and d then drops the
# marker itself. Doing it this way means the fragment is never run
# through sed's replacement parser, so & and | in the prose are safe.
sed -e "s|<!--TITLE-->|$TITLE|" \
    -e "/<!--CONTENT-->/r $FRAG" \
    -e '/<!--CONTENT-->/d' \
    tools/web_shell.html > "$SHELL_OUT"

# main_ppm.c is the other host and must not be linked in. NEITHER CAN
# agent.c: it is the debug agent, it is a named-pipe server on
# windows.h, and it belongs to the PPM host alone - the web build
# fails to FIND windows.h rather than failing to link, so the error
# reads like a missing include path and is not one.
SRCS="src/main_sdl.c"
for f in src/*.c; do
    case "$f" in
        src/main_sdl.c|src/main_ppm.c|src/agent.c) ;;
        *) SRCS="$SRCS $f" ;;
    esac
done
SRCS="$SRCS $(ls src/$DEMO/*.c)"

# Third party, if present. See the note at the top: these go through
# the same strict flags as everything else.
TP=""
if [ -d src/third_party/ibxm ]; then
    TP="src/third_party/ibxm/ibxm.c"
fi

# -s USE_SDL=2 is emscripten's own SDL2. The vendored
# src/third_party/SDL is the DESKTOP one and is deliberately not on
# the include path here.
$EMCC $SRCS $TP \
    -std=c89 -pedantic -Wall -Wextra \
    -O2 \
    -I src \
    -s USE_SDL=2 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXIT_RUNTIME=0 \
    -s EXPORTED_FUNCTIONS='["_main","_Host_Start"]' \
    --embed-file "$EXE@$EXE" \
    --shell-file "$SHELL_OUT" \
    -o "$OUT/$DEMO.html"

echo "wrote $OUT/$DEMO.html  (text from $FRAG)"
echo "serve it - a file:// page cannot read the embedded data:"
echo "  cd $OUT && py -3.10 -m http.server 8000"
