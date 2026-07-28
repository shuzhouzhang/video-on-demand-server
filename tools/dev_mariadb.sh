#!/usr/bin/env bash
set -euo pipefail

MARIADB_VERSION="${MARIADB_VERSION:-11.4.10}"
MARIADB_PORT="${MARIADB_PORT:-3306}"
MARIADB_ARCHIVE_NAME="mariadb-${MARIADB_VERSION}-linux-systemd-x86_64"
MARIADB_DOWNLOAD_URL="${MARIADB_DOWNLOAD_URL:-https://dlm.mariadb.com/4574296/MariaDB/mariadb-${MARIADB_VERSION}/bintar-linux-systemd-x86_64/${MARIADB_ARCHIVE_NAME}.tar.gz}"
MARIADB_OPT_ROOT="${MARIADB_OPT_ROOT:-${HOME}/.local/opt}"
MARIADB_INSTALL_ROOT="${MARIADB_INSTALL_ROOT:-${MARIADB_OPT_ROOT}/${MARIADB_ARCHIVE_NAME}}"
MARIADB_LINK="${MARIADB_LINK:-${MARIADB_OPT_ROOT}/mariadb}"
MARIADB_DATA_DIR="${MARIADB_DATA_DIR:-${HOME}/.local/var/vod-mariadb}"
MARIADB_RUN_DIR="${MARIADB_RUN_DIR:-${HOME}/.local/var/vod-mariadb-run}"
MARIADB_SOCKET="${MARIADB_RUN_DIR}/mysql.sock"

case "${MARIADB_PORT}" in
    ''|*[!0-9]*) echo "MARIADB_PORT must be numeric" >&2; exit 1 ;;
esac
if [ "${MARIADB_PORT}" -lt 1 ] || [ "${MARIADB_PORT}" -gt 65535 ]; then
    echo "MARIADB_PORT must be between 1 and 65535" >&2
    exit 1
fi

server_bin() { printf '%s/bin/mariadbd' "${MARIADB_LINK}"; }
admin_bin() { printf '%s/bin/mariadb-admin' "${MARIADB_LINK}"; }
client_bin() { printf '%s/bin/mariadb' "${MARIADB_LINK}"; }

cleanup_download_dir() {
    local resolved_work_dir
    local resolved_temp_root
    resolved_work_dir="$(realpath "${download_work_dir}")"
    resolved_temp_root="$(realpath "${download_temp_root}")"
    case "${resolved_work_dir}" in
        "${resolved_temp_root}"/vod-mariadb-download.*)
            rm -rf -- "${resolved_work_dir}"
            ;;
        *)
            echo "refusing to remove unexpected download path: ${resolved_work_dir}" >&2
            ;;
    esac
}

bootstrap() {
    for command_name in curl tar; do
        command -v "${command_name}" >/dev/null 2>&1 || {
            echo "missing install dependency: ${command_name}" >&2
            exit 1
        }
    done
    mkdir -p "${MARIADB_OPT_ROOT}"

    if [ -x "${MARIADB_INSTALL_ROOT}/bin/mariadbd" ]; then
        echo "MariaDB ${MARIADB_VERSION} is already installed in ${MARIADB_INSTALL_ROOT}"
    else
        download_temp_root="${TMPDIR:-/tmp}"
        download_work_dir="$(mktemp -d "${download_temp_root}/vod-mariadb-download.XXXXXX")"
        trap cleanup_download_dir EXIT
        local archive="${download_work_dir}/mariadb.tar.gz"
        echo "Downloading MariaDB ${MARIADB_VERSION}"
        curl -fL --retry 3 --connect-timeout 15 \
            "${MARIADB_DOWNLOAD_URL}" -o "${archive}"
        tar -xzf "${archive}" -C "${MARIADB_OPT_ROOT}"
        cleanup_download_dir
        trap - EXIT
    fi

    ln -sfn "${MARIADB_INSTALL_ROOT}" "${MARIADB_LINK}"
    "$(server_bin)" --version
}

ping() {
    [ -x "$(admin_bin)" ] &&
        "$(admin_bin)" --no-defaults --socket="${MARIADB_SOCKET}" ping \
            >/dev/null 2>&1
}

initialize_data() {
    mkdir -p "${MARIADB_DATA_DIR}" "${MARIADB_RUN_DIR}"
    if [ ! -d "${MARIADB_DATA_DIR}/mysql" ]; then
        "${MARIADB_LINK}/scripts/mariadb-install-db" \
            --no-defaults \
            --basedir="${MARIADB_LINK}" \
            --datadir="${MARIADB_DATA_DIR}" \
            --auth-root-authentication-method=normal \
            >"${MARIADB_RUN_DIR}/install.log"
    fi
}

ensure_application_database() {
    "$(client_bin)" --no-defaults --socket="${MARIADB_SOCKET}" -uroot <<'SQL'
CREATE DATABASE IF NOT EXISTS video_on_demand
    CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS 'video_app'@'127.0.0.1' IDENTIFIED BY '';
GRANT ALL PRIVILEGES ON video_on_demand.* TO 'video_app'@'127.0.0.1';
FLUSH PRIVILEGES;
SQL
}

start() {
    if ping; then
        echo "MariaDB is already running on 127.0.0.1:${MARIADB_PORT}"
        return
    fi
    if [ ! -x "$(server_bin)" ]; then
        echo "MariaDB is not installed; run '$0 bootstrap' first" >&2
        exit 1
    fi
    initialize_data
    nohup "$(server_bin)" \
        --no-defaults \
        --basedir="${MARIADB_LINK}" \
        --datadir="${MARIADB_DATA_DIR}" \
        --socket="${MARIADB_SOCKET}" \
        --pid-file="${MARIADB_RUN_DIR}/mysql.pid" \
        --port="${MARIADB_PORT}" \
        --bind-address=127.0.0.1 \
        --log-error="${MARIADB_RUN_DIR}/mysql.log" \
        --skip-name-resolve \
        >"${MARIADB_RUN_DIR}/stdout.log" 2>&1 &

    for _ in $(seq 1 30); do
        if ping; then
            ensure_application_database
            echo "MariaDB is running on 127.0.0.1:${MARIADB_PORT}"
            return
        fi
        sleep 1
    done
    echo "MariaDB failed to start; inspect ${MARIADB_RUN_DIR}/mysql.log" >&2
    exit 1
}

stop() {
    if ! ping; then
        echo "MariaDB is not running"
        return
    fi
    "$(admin_bin)" --no-defaults --socket="${MARIADB_SOCKET}" -uroot shutdown
    echo "MariaDB stopped"
}

status() {
    if ! ping; then
        echo "MariaDB is not running on 127.0.0.1:${MARIADB_PORT}" >&2
        exit 1
    fi
    local version
    version="$("$(client_bin)" --no-defaults --socket="${MARIADB_SOCKET}" \
        -uroot -Nse 'SELECT VERSION()')"
    echo "MariaDB ${version} is healthy on 127.0.0.1:${MARIADB_PORT}"
}

case "${1:-status}" in
    bootstrap) bootstrap ;;
    start) start ;;
    stop) stop ;;
    restart) stop; start ;;
    status) status ;;
    *) echo "usage: $0 {bootstrap|start|stop|restart|status}" >&2; exit 2 ;;
esac
