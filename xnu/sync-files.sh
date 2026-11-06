#!/bin/bash
# Copy the files/ overlays onto the existing source tree.
#
# apply.sh only ever runs when the tree is made: an existing tree keeps whatever files/ held
# then. The drivers have sync-drivers.sh for the same reason; the iokit and osfmk files that
# live under files/ need the same courtesy -- without it, editing them is editing a file the
# build never sees. Idempotent: this is the same cp apply.sh does, run again.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
DIR=${1:-${XNU_SRC:-$HERE/../xnu-src/xnu-8796.121.2}}
[ -d "$DIR" ] || { echo "sync-files.sh: no tree at $DIR (run apply.sh)" >&2; exit 1; }
cp -a "$HERE/files/." "$DIR/"
echo "sync-files.sh: overlays copied onto $DIR"
