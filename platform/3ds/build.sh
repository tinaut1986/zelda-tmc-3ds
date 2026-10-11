#!/usr/bin/env bash
# Builds the 3DSX and the CIA into build-3ds/game/.
#
# Environment:
#   TMC3DS_DEBUG_TOOLS=1   compile in the DEBUG tab and its tools (docs/debug-tools.md)
#   TMC3DS_CHANNEL=beta|release   set by the release workflow; empty for local builds
#   TMC3DS_VERSION=X.Y.Z   override the version derived from git (tools/version.sh)
#   TMC3DS_JOBS=N          parallel jobs (default: all cores)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="$(sh "${ROOT}/platform/3ds/tools/version.sh")"
UPSTREAM_VERSION="$(tr -d '\r\n' < "${ROOT}/platform/3ds/upstream_version.txt")"
CHANNEL="${TMC3DS_CHANNEL:-}"
DEBUG_TOOLS="${TMC3DS_DEBUG_TOOLS:-0}"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
BUILD="${ROOT}/build-3ds/game"
# makerom and bannertool are committed (copied from ../mzm, like ../sm-3ds).
TOOLS_ROOT="${TMC3DS_TOOLS_ROOT:-${ROOT}/platform/3ds/tools/bin}"
MAKEROM="${MAKEROM:-${TOOLS_ROOT}/makerom}"
BANNERTOOL="${BANNERTOOL:-${TOOLS_ROOT}/bannertool}"

if [[ ! -x "${MAKEROM}" ]] && command -v makerom >/dev/null 2>&1; then
  MAKEROM="$(command -v makerom)"
fi
if [[ ! -x "${BANNERTOOL}" ]] && command -v bannertool >/dev/null 2>&1; then
  BANNERTOOL="$(command -v bannertool)"
fi

LABEL="${VERSION}"
[[ "${CHANNEL}" == "beta" ]] && LABEL="${VERSION} BETA"
printf 'Minish Cap 3DS v%s (upstream %s), debug tools %s\n' "${LABEL}" "${UPSTREAM_VERSION}" "${DEBUG_TOOLS}"

export DEVKITPRO
if [[ -z "${UPDATE_DEPS_ROOT:-}" ]]; then
  UPDATE_DEPS_ROOT="${DEVKITPRO}/portlibs/3ds"
  if [[ ! -f "${UPDATE_DEPS_ROOT}/lib/libcurl.a" || ! -f "${UPDATE_DEPS_ROOT}/lib/libjansson.a" ]]; then
    UPDATE_DEPS_ROOT="${ROOT}/build-3ds/update-deps/prefix"
    if [[ ! -f "${UPDATE_DEPS_ROOT}/lib/libcurl.a" || ! -f "${UPDATE_DEPS_ROOT}/lib/libjansson.a" ]]; then
      python3 "${ROOT}/platform/3ds/tools/build_update_deps.py" "${ROOT}/build-3ds/update-deps"
    fi
  fi
fi
if [[ "${DEBUG_TOOLS}" == "1" ]]; then DEBUG_TOOLS_OPT=ON; else DEBUG_TOOLS_OPT=OFF; fi
cmake -S "${ROOT}/platform/3ds" -B "${BUILD}" \
  -DCMAKE_TOOLCHAIN_FILE="${DEVKITPRO}/cmake/3DS.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DTMC_3DS_VERSION="${VERSION}" \
  -DTMC_3DS_CHANNEL="${CHANNEL}" \
  -DTMC_3DS_DEBUG_TOOLS="${DEBUG_TOOLS_OPT}" \
  -DUPDATE_DEPS_ROOT="${UPDATE_DEPS_ROOT:-${DEVKITPRO}/portlibs/3ds}"
cmake --build "${BUILD}" --parallel "${TMC3DS_JOBS:-$(nproc 2>/dev/null || echo 4)}"

if [[ ! -x "${MAKEROM}" || ! -x "${BANNERTOOL}" ]]; then
  printf '3DSX ready; makerom/bannertool are unavailable for CIA packaging.\n'
  exit 0
fi

"${BANNERTOOL}" makesmdh \
  -s "Minish Cap 3DS" \
  -l "Minish Cap 3DS v${LABEL}" \
  -p "tinaut1986 / Esteban PDN / Project Picori" \
  -i "${ROOT}/platform/3ds/assets/icon-48.png" \
  -f visible,allow3d,extendedbanner,nosavebackups \
  -o "${BUILD}/tmc-3ds.icn"

"${BANNERTOOL}" makebanner \
  -ci "${ROOT}/platform/3ds/assets/banner.cgfx" \
  -a "${ROOT}/platform/3ds/assets/banner.wav" \
  -o "${BUILD}/tmc-3ds.bnr"

# The CIA header takes numeric major.minor.micro.
IFS=. read -r V_MAJOR V_MINOR V_MICRO <<< "$(printf '%s' "${VERSION}" | sed -E 's/^([0-9]+)\.([0-9]+)\.([0-9]+).*/\1.\2.\3/')"
(
cd "${ROOT}"
"${MAKEROM}" -f cia -o "${BUILD}/tmc-3ds-v${VERSION}.cia" \
  -DAPP_ROMFS="${BUILD#"${ROOT}/"}/romfs" \
  -rsf "${ROOT}/platform/3ds/cia/tmc3ds.rsf" -target t -exefslogo \
  -major "${V_MAJOR:-0}" -minor "${V_MINOR:-0}" -micro "${V_MICRO:-0}" \
  -elf "${BUILD}/tmc-3ds.elf" -icon "${BUILD}/tmc-3ds.icn" \
  -banner "${BUILD}/tmc-3ds.bnr"
)

printf 'Ready:\n  %s\n  %s\n' \
  "${BUILD}/tmc-3ds-v${VERSION}.3dsx" "${BUILD}/tmc-3ds-v${VERSION}.cia"
