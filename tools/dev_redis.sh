#!/usr/bin/env bash
set -euo pipefail

REDIS_VERSION="${REDIS_VERSION:-7.2.15}"
REDIS_PORT="${REDIS_PORT:-6379}"
REDIS_INSTALL_PREFIX="${REDIS_INSTALL_PREFIX:-${HOME}/.local/opt/redis-${REDIS_VERSION}}"
REDIS_BIN_DIR="${REDIS_BIN_DIR:-${HOME}/.local/bin}"
REDIS_STATE_DIR="${REDIS_STATE_DIR:-${HOME}/.local/var/redis}"
REDIS_SERVER_BIN="${REDIS_SERVER_BIN:-${REDIS_BIN_DIR}/redis-server}"
REDIS_CLI_BIN="${REDIS_CLI_BIN:-${REDIS_BIN_DIR}/redis-cli}"
REDIS_CONFIG="${REDIS_STATE_DIR}/redis.conf"

case "${REDIS_PORT}" in
    ''|*[!0-9]*) echo "REDIS_PORT must be numeric" >&2; exit 1 ;;
esac
if [ "${REDIS_PORT}" -lt 1 ] || [ "${REDIS_PORT}" -gt 65535 ]; then
    echo "REDIS_PORT must be between 1 and 65535" >&2
    exit 1
fi

bootstrap() {
    for command_name in curl tar make cc; do
        command -v "${command_name}" >/dev/null 2>&1 || {
            echo "missing build dependency: ${command_name}" >&2
            exit 1
        }
    done

    if [ -x "${REDIS_INSTALL_PREFIX}/bin/redis-server" ] &&
       [ -x "${REDIS_INSTALL_PREFIX}/bin/redis-cli" ]; then
        echo "Redis ${REDIS_VERSION} is already installed in ${REDIS_INSTALL_PREFIX}"
    else
        local temp_root="${TMPDIR:-/tmp}"
        local work_dir
        work_dir="$(mktemp -d "${temp_root}/vod-redis-build.XXXXXX")"
        cleanup_build_dir() {
            local resolved_work_dir
            local resolved_temp_root
            resolved_work_dir="$(realpath "${work_dir}")"
            resolved_temp_root="$(realpath "${temp_root}")"
            case "${resolved_work_dir}" in
                "${resolved_temp_root}"/vod-redis-build.*)
                    rm -rf -- "${resolved_work_dir}"
                    ;;
                *)
                    echo "refusing to remove unexpected build path: ${resolved_work_dir}" >&2
                    ;;
            esac
        }
        trap cleanup_build_dir EXIT
        local archive="${work_dir}/redis.tar.gz"
        echo "Downloading Redis ${REDIS_VERSION} from download.redis.io"
        curl -fL --retry 3 --connect-timeout 10 \
            "https://download.redis.io/releases/redis-${REDIS_VERSION}.tar.gz" \
            -o "${archive}"
        tar -xzf "${archive}" -C "${work_dir}"
        make -C "${work_dir}/redis-${REDIS_VERSION}" -j2 BUILD_TLS=no
        make -C "${work_dir}/redis-${REDIS_VERSION}" \
            PREFIX="${REDIS_INSTALL_PREFIX}" install
        cleanup_build_dir
        trap - EXIT
    fi

    mkdir -p "${REDIS_BIN_DIR}"
    ln -sfn "${REDIS_INSTALL_PREFIX}/bin/redis-server" \
        "${REDIS_BIN_DIR}/redis-server"
    ln -sfn "${REDIS_INSTALL_PREFIX}/bin/redis-cli" \
        "${REDIS_BIN_DIR}/redis-cli"
    echo "Redis binaries are available in ${REDIS_BIN_DIR}"
}

write_config() {
    mkdir -p "${REDIS_STATE_DIR}"
    cat >"${REDIS_CONFIG}" <<EOF
bind 127.0.0.1
protected-mode yes
port ${REDIS_PORT}
daemonize yes
supervised no
pidfile ${REDIS_STATE_DIR}/redis.pid
logfile ${REDIS_STATE_DIR}/redis.log
dir ${REDIS_STATE_DIR}
dbfilename dump.rdb
appendonly yes
appenddirname appendonlydir
appendfsync everysec
EOF
}

ping() {
    [ -x "${REDIS_CLI_BIN}" ] &&
        [ "$("${REDIS_CLI_BIN}" -h 127.0.0.1 -p "${REDIS_PORT}" ping 2>/dev/null || true)" = "PONG" ]
}

start() {
    if ping; then
        echo "Redis is already running on 127.0.0.1:${REDIS_PORT}"
        return
    fi
    if [ ! -x "${REDIS_SERVER_BIN}" ]; then
        echo "Redis is not installed; run '$0 bootstrap' first" >&2
        exit 1
    fi
    write_config
    "${REDIS_SERVER_BIN}" "${REDIS_CONFIG}"
    for _ in 1 2 3 4 5; do
        if ping; then
            echo "Redis is running on 127.0.0.1:${REDIS_PORT}"
            return
        fi
        sleep 1
    done
    echo "Redis failed to start; inspect ${REDIS_STATE_DIR}/redis.log" >&2
    exit 1
}

stop() {
    if ! ping; then
        echo "Redis is not running"
        return
    fi
    "${REDIS_CLI_BIN}" -h 127.0.0.1 -p "${REDIS_PORT}" shutdown
    echo "Redis stopped"
}

status() {
    if ping; then
        local version
        version="$("${REDIS_CLI_BIN}" -h 127.0.0.1 -p "${REDIS_PORT}" INFO server | awk -F: '/^redis_version:/{gsub(/\r/, "", $2); print $2}')"
        echo "Redis ${version} is healthy on 127.0.0.1:${REDIS_PORT}"
        return
    fi
    echo "Redis is not running on 127.0.0.1:${REDIS_PORT}" >&2
    exit 1
}

case "${1:-status}" in
    bootstrap) bootstrap ;;
    start) start ;;
    stop) stop ;;
    restart) stop; start ;;
    status) status ;;
    *) echo "usage: $0 {bootstrap|start|stop|restart|status}" >&2; exit 2 ;;
esac
