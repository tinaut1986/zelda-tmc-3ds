#!/bin/sh
# Prints the path of a Windows makerom that keeps the title version's minor number. devkitPro's makerom.exe 0.19.0
# drops it (3DSGuy/Project_CTR#169: 0.4.8 becomes 0.0.8, with -major/-minor/-micro and with -ver alike). The fixed
# build comes from the owner's fork (tinaut1986/Project_CTR, release makerom-latest) and is downloaded once per machine
# into a cache every 3DS repo shares. MAKEROM_FORK_URL changes the source; delete the cached file to fetch it again.
# The same file is in each of the owner's 3DS repos: keep the copies equal.
set -e
url=${MAKEROM_FORK_URL:-https://github.com/tinaut1986/Project_CTR/releases/download/makerom-latest/makerom-win_x86_64.exe}
# AppData\Local from Windows itself (cygpath -F 28): devkitPro's MSYS2 shell gets no LOCALAPPDATA.
if local=$(cygpath -F 28 2>/dev/null) && [ -n "$local" ]; then
    dir=$local/3ds-tools
else
    dir=$HOME/.cache/3ds-tools
fi
exe=$dir/makerom-fixed.exe
if [ ! -s "$exe" ]; then
    mkdir -p "$dir"
    echo "fetch-makerom: downloading $url" >&2
    curl -fsSL -o "$exe.part" "$url" || { echo "fetch-makerom: download failed" >&2; exit 1; }
    mv "$exe.part" "$exe"
    chmod +x "$exe"
fi
printf '%s\n' "$exe"
