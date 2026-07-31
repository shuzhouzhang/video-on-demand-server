#!/usr/bin/env bash
set -euo pipefail

RUNTIME_ROOT="${VOD_RUNTIME_ROOT:-${HOME}/.local/opt/vod}"
DOWNLOAD_DIR="${RUNTIME_ROOT}/downloads"
FFMPEG_HOME="${RUNTIME_ROOT}/ffmpeg-release"
BIN_DIR="${HOME}/.local/bin"
ARCHIVE_NAME="ffmpeg-release-amd64-static.tar.xz"
DOWNLOAD_URL="https://johnvansickle.com/ffmpeg/releases/${ARCHIVE_NAME}"

bootstrap() {
    if [[ -x "${FFMPEG_HOME}/ffmpeg" && -x "${FFMPEG_HOME}/ffprobe" ]]; then
        link_binaries
        return
    fi
    command -v curl >/dev/null || { echo "curl is required" >&2; return 1; }
    command -v md5sum >/dev/null || { echo "md5sum is required" >&2; return 1; }
    command -v tar >/dev/null || { echo "tar is required" >&2; return 1; }

    mkdir -p "${DOWNLOAD_DIR}" "${BIN_DIR}"
    local archive="${DOWNLOAD_DIR}/${ARCHIVE_NAME}"
    local checksum="${archive}.md5"
    curl --fail --location --retry 3 --output "${archive}.part" "${DOWNLOAD_URL}"
    mv "${archive}.part" "${archive}"
    curl --fail --location --retry 3 --output "${checksum}.part" "${DOWNLOAD_URL}.md5"
    mv "${checksum}.part" "${checksum}"
    (cd "${DOWNLOAD_DIR}" && md5sum --check "${ARCHIVE_NAME}.md5")

    local extract_root
    extract_root="$(mktemp -d "${RUNTIME_ROOT}/ffmpeg-extract.XXXXXX")"
    tar -xJf "${archive}" -C "${extract_root}"
    local extracted
    extracted="$(find "${extract_root}" -mindepth 1 -maxdepth 1 -type d -print -quit)"
    if [[ -z "${extracted}" || ! -x "${extracted}/ffmpeg" ||
          ! -x "${extracted}/ffprobe" ]]; then
        echo "downloaded FFmpeg archive has an unexpected layout" >&2
        return 1
    fi
    if [[ -e "${FFMPEG_HOME}" ]]; then
        echo "${FFMPEG_HOME} exists but is incomplete; move it aside and retry" >&2
        return 1
    fi
    mv "${extracted}" "${FFMPEG_HOME}"
    rmdir "${extract_root}"
    link_binaries
}

link_binaries() {
    mkdir -p "${BIN_DIR}"
    for name in ffmpeg ffprobe; do
        local destination="${BIN_DIR}/${name}"
        if [[ -e "${destination}" && ! -L "${destination}" ]]; then
            echo "refusing to replace non-symlink ${destination}" >&2
            return 1
        fi
        ln -sfn "${FFMPEG_HOME}/${name}" "${destination}"
    done
}

status() {
    [[ -x "${FFMPEG_HOME}/ffmpeg" ]] || {
        echo "FFmpeg is not installed at ${FFMPEG_HOME}" >&2
        return 1
    }
    "${FFMPEG_HOME}/ffmpeg" -version | head -1
    "${FFMPEG_HOME}/ffprobe" -version | head -1
}

case "${1:-status}" in
    bootstrap) bootstrap ;;
    status) status ;;
    *) echo "usage: $0 {bootstrap|status}" >&2; exit 2 ;;
esac
