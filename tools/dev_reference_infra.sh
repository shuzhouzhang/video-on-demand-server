#!/usr/bin/env bash
set -euo pipefail

RUNTIME_ROOT="${VOD_RUNTIME_ROOT:-${HOME}/.local/opt/vod}"
DOWNLOAD_DIR="${RUNTIME_ROOT}/downloads"
RUN_DIR="${RUNTIME_ROOT}/run"
LOG_DIR="${RUNTIME_ROOT}/logs"
DATA_DIR="${RUNTIME_ROOT}/data"
SECRET_DIR="${RUNTIME_ROOT}/secrets"

ETCD_VERSION="3.7.1"
ERLANG_VERSION="27.3"
RABBITMQ_VERSION="4.3.4"
ELASTICSEARCH_VERSION="9.4.2"

ETCD_HOME="${RUNTIME_ROOT}/etcd-${ETCD_VERSION}"
ERLANG_HOME="${RUNTIME_ROOT}/erlang-${ERLANG_VERSION}"
RABBITMQ_HOME="${RUNTIME_ROOT}/rabbitmq-${RABBITMQ_VERSION}"
ELASTICSEARCH_HOME="${RUNTIME_ROOT}/elasticsearch-${ELASTICSEARCH_VERSION}"
FASTDFS_HOME="${RUNTIME_ROOT}/fastdfs"

mkdirs() {
    mkdir -p "${DOWNLOAD_DIR}" "${RUN_DIR}" "${LOG_DIR}" \
        "${DATA_DIR}" "${SECRET_DIR}"
    chmod 700 "${SECRET_DIR}"
}

download() {
    local url="$1"
    local destination="$2"
    if [[ -s "${destination}" ]]; then
        return
    fi
    echo "downloading ${url}"
    curl --fail --location --retry 3 --output "${destination}.part" "${url}"
    mv "${destination}.part" "${destination}"
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "missing required command: $1" >&2
        return 1
    }
}

bootstrap_etcd() {
    if [[ -x "${ETCD_HOME}/etcd" ]]; then
        return
    fi
    local archive="${DOWNLOAD_DIR}/etcd-v${ETCD_VERSION}-linux-amd64.tar.gz"
    download "https://storage.googleapis.com/etcd/v${ETCD_VERSION}/etcd-v${ETCD_VERSION}-linux-amd64.tar.gz" "${archive}"
    rm -rf "${ETCD_HOME}.tmp"
    mkdir -p "${ETCD_HOME}.tmp"
    tar -xzf "${archive}" -C "${ETCD_HOME}.tmp" --strip-components=1
    mv "${ETCD_HOME}.tmp" "${ETCD_HOME}"
}

bootstrap_erlang() {
    if command -v erl >/dev/null 2>&1 || [[ -x "${ERLANG_HOME}/bin/erl" ]]; then
        return
    fi
    require_command make
    require_command gcc
    local archive="${DOWNLOAD_DIR}/otp_src_${ERLANG_VERSION}.tar.gz"
    local source="${RUNTIME_ROOT}/src/otp-${ERLANG_VERSION}"
    download "https://github.com/erlang/otp/releases/download/OTP-${ERLANG_VERSION}/otp_src_${ERLANG_VERSION}.tar.gz" "${archive}"
    rm -rf "${source}"
    mkdir -p "${source}"
    tar -xzf "${archive}" -C "${source}" --strip-components=1
    (
        cd "${source}"
        ./configure --prefix="${ERLANG_HOME}" --without-javac --without-wx \
            --without-odbc --without-debugger --without-observer \
            --without-et --without-megaco --without-termcap
        make -j"${VOD_BUILD_JOBS:-2}"
        make install
    )
}

bootstrap_rabbitmq() {
    if [[ -x "${RABBITMQ_HOME}/sbin/rabbitmq-server" ]]; then
        return
    fi
    bootstrap_erlang
    local archive="${DOWNLOAD_DIR}/rabbitmq-server-generic-unix-${RABBITMQ_VERSION}.tar.xz"
    download "https://github.com/rabbitmq/rabbitmq-server/releases/download/v${RABBITMQ_VERSION}/rabbitmq-server-generic-unix-${RABBITMQ_VERSION}.tar.xz" "${archive}"
    rm -rf "${RABBITMQ_HOME}.tmp"
    mkdir -p "${RABBITMQ_HOME}.tmp"
    tar -xJf "${archive}" -C "${RABBITMQ_HOME}.tmp" --strip-components=1
    mv "${RABBITMQ_HOME}.tmp" "${RABBITMQ_HOME}"
}

bootstrap_elasticsearch() {
    if [[ -x "${ELASTICSEARCH_HOME}/bin/elasticsearch" ]]; then
        return
    fi
    local archive="${DOWNLOAD_DIR}/elasticsearch-${ELASTICSEARCH_VERSION}-linux-x86_64.tar.gz"
    download "https://artifacts.elastic.co/downloads/elasticsearch/elasticsearch-${ELASTICSEARCH_VERSION}-linux-x86_64.tar.gz" "${archive}"
    rm -rf "${ELASTICSEARCH_HOME}.tmp"
    mkdir -p "${ELASTICSEARCH_HOME}.tmp"
    tar -xzf "${archive}" -C "${ELASTICSEARCH_HOME}.tmp" --strip-components=1
    mv "${ELASTICSEARCH_HOME}.tmp" "${ELASTICSEARCH_HOME}"
}

bootstrap_fastdfs() {
    require_command fdfs_trackerd
    require_command fdfs_storaged
    require_command fdfs_test
    [[ -r /etc/fdfs/tracker.conf ]] || { echo "/etc/fdfs/tracker.conf is missing" >&2; return 1; }
    [[ -r /etc/fdfs/storage.conf ]] || { echo "/etc/fdfs/storage.conf is missing" >&2; return 1; }
    [[ -r /etc/fdfs/client.conf ]] || { echo "/etc/fdfs/client.conf is missing" >&2; return 1; }

    mkdir -p "${FASTDFS_HOME}/conf" "${DATA_DIR}/fastdfs/tracker" \
        "${DATA_DIR}/fastdfs/storage" "${DATA_DIR}/fastdfs/files"
    cp /etc/fdfs/tracker.conf "${FASTDFS_HOME}/conf/tracker.conf"
    cp /etc/fdfs/storage.conf "${FASTDFS_HOME}/conf/storage.conf"
    cp /etc/fdfs/client.conf "${FASTDFS_HOME}/conf/client.conf"
    sed -i "s#^base_path=.*#base_path=${DATA_DIR}/fastdfs/tracker#" \
        "${FASTDFS_HOME}/conf/tracker.conf"
    sed -i "s#^base_path=.*#base_path=${DATA_DIR}/fastdfs/storage#" \
        "${FASTDFS_HOME}/conf/storage.conf"
    sed -i "s#^store_path0=.*#store_path0=${DATA_DIR}/fastdfs/files#" \
        "${FASTDFS_HOME}/conf/storage.conf"
    sed -i "s#^tracker_server=.*#tracker_server=127.0.0.1:22122#" \
        "${FASTDFS_HOME}/conf/storage.conf" "${FASTDFS_HOME}/conf/client.conf"
}

bootstrap() {
    mkdirs
    require_command curl
    require_command tar
    bootstrap_etcd
    bootstrap_rabbitmq
    bootstrap_elasticsearch
    bootstrap_fastdfs
    echo "reference infrastructure installed under ${RUNTIME_ROOT}"
}

erlang_path() {
    if [[ -x "${ERLANG_HOME}/bin/erl" ]]; then
        printf '%s' "${ERLANG_HOME}/bin"
    else
        dirname "$(command -v erl)"
    fi
}

start_etcd() {
    if [[ -s "${RUN_DIR}/etcd.pid" ]] && kill -0 "$(cat "${RUN_DIR}/etcd.pid")" 2>/dev/null; then
        return
    fi
    nohup "${ETCD_HOME}/etcd" --name vod-dev \
        --data-dir "${DATA_DIR}/etcd" \
        --listen-client-urls http://127.0.0.1:2379 \
        --advertise-client-urls http://127.0.0.1:2379 \
        --listen-peer-urls http://127.0.0.1:2380 \
        --initial-advertise-peer-urls http://127.0.0.1:2380 \
        --initial-cluster vod-dev=http://127.0.0.1:2380 \
        >"${LOG_DIR}/etcd.log" 2>&1 &
    echo "$!" >"${RUN_DIR}/etcd.pid"
}

rabbitmq_env() {
    export PATH="$(erlang_path):${RABBITMQ_HOME}/sbin:${PATH}"
    export RABBITMQ_MNESIA_BASE="${DATA_DIR}/rabbitmq/mnesia"
    export RABBITMQ_LOG_BASE="${LOG_DIR}/rabbitmq"
    export RABBITMQ_PID_FILE="${RUN_DIR}/rabbitmq.pid"
    export RABBITMQ_NODENAME="vod-dev@localhost"
    mkdir -p "${RABBITMQ_MNESIA_BASE}" "${RABBITMQ_LOG_BASE}"
}

start_rabbitmq() {
    rabbitmq_env
    if rabbitmq-diagnostics -q ping >/dev/null 2>&1; then
        return
    fi
    rabbitmq-server -detached
    for _ in $(seq 1 60); do
        rabbitmq-diagnostics -q ping >/dev/null 2>&1 && break
        sleep 1
    done
    rabbitmq-diagnostics -q ping >/dev/null

    local password_file="${SECRET_DIR}/rabbitmq_password"
    if [[ ! -s "${password_file}" ]]; then
        umask 077
        openssl rand -hex 24 >"${password_file}"
    fi
    local password
    password="$(cat "${password_file}")"
    rabbitmqctl add_vhost /vod >/dev/null 2>&1 || true
    rabbitmqctl add_user video_app "${password}" >/dev/null 2>&1 || \
        rabbitmqctl change_password video_app "${password}" >/dev/null
    rabbitmqctl set_permissions -p /vod video_app '.*' '.*' '.*' >/dev/null
}

start_elasticsearch() {
    if [[ -s "${RUN_DIR}/elasticsearch.pid" ]] && \
       kill -0 "$(cat "${RUN_DIR}/elasticsearch.pid")" 2>/dev/null; then
        return
    fi
    mkdir -p "${DATA_DIR}/elasticsearch"
    ES_JAVA_OPTS="-Xms512m -Xmx512m" \
        "${ELASTICSEARCH_HOME}/bin/elasticsearch" -d \
        -p "${RUN_DIR}/elasticsearch.pid" \
        -Ecluster.name=vod-dev -Enode.name=vod-dev-1 \
        -Ediscovery.type=single-node -Enetwork.host=127.0.0.1 \
        -Ehttp.port=9200 -Expack.security.enabled=false \
        -Epath.data="${DATA_DIR}/elasticsearch" \
        -Epath.logs="${LOG_DIR}/elasticsearch"
}

start_fastdfs() {
    fdfs_trackerd "${FASTDFS_HOME}/conf/tracker.conf" start
    fdfs_storaged "${FASTDFS_HOME}/conf/storage.conf" start
}

wait_http() {
    local name="$1"
    local url="$2"
    for _ in $(seq 1 60); do
        curl --fail --silent "${url}" >/dev/null 2>&1 && return
        sleep 1
    done
    echo "${name} did not become healthy: ${url}" >&2
    return 1
}

start() {
    mkdirs
    [[ -x "${ETCD_HOME}/etcd" ]] || bootstrap
    start_etcd
    start_rabbitmq
    start_elasticsearch
    start_fastdfs
    wait_http etcd http://127.0.0.1:2379/health
    wait_http elasticsearch http://127.0.0.1:9200/
    status
}

stop_pid() {
    local file="$1"
    if [[ -s "${file}" ]]; then
        local pid
        pid="$(cat "${file}")"
        kill "${pid}" 2>/dev/null || true
        for _ in $(seq 1 20); do
            kill -0 "${pid}" 2>/dev/null || break
            sleep 0.25
        done
        rm -f "${file}"
    fi
}

stop() {
    if [[ -d "${FASTDFS_HOME}/conf" ]]; then
        fdfs_storaged "${FASTDFS_HOME}/conf/storage.conf" stop || true
        fdfs_trackerd "${FASTDFS_HOME}/conf/tracker.conf" stop || true
    fi
    if [[ -x "${RABBITMQ_HOME}/sbin/rabbitmqctl" ]]; then
        rabbitmq_env
        rabbitmqctl shutdown >/dev/null 2>&1 || true
    fi
    stop_pid "${RUN_DIR}/elasticsearch.pid"
    stop_pid "${RUN_DIR}/etcd.pid"
}

show_pid_status() {
    local name="$1"
    local file="$2"
    if [[ -s "${file}" ]] && kill -0 "$(cat "${file}")" 2>/dev/null; then
        echo "${name}: UP pid=$(cat "${file}")"
    else
        echo "${name}: DOWN"
        return 1
    fi
}

status() {
    local failed=0
    show_pid_status etcd "${RUN_DIR}/etcd.pid" || failed=1
    if [[ -x "${RABBITMQ_HOME}/sbin/rabbitmq-diagnostics" ]]; then
        rabbitmq_env
        rabbitmq-diagnostics -q ping >/dev/null 2>&1 && \
            echo "rabbitmq: UP" || { echo "rabbitmq: DOWN"; failed=1; }
    else
        echo "rabbitmq: NOT INSTALLED"
        failed=1
    fi
    show_pid_status elasticsearch "${RUN_DIR}/elasticsearch.pid" || failed=1
    pgrep -af 'fdfs_trackerd.*fastdfs/conf/tracker.conf' >/dev/null && \
        echo "fastdfs tracker: UP" || { echo "fastdfs tracker: DOWN"; failed=1; }
    pgrep -af 'fdfs_storaged.*fastdfs/conf/storage.conf' >/dev/null && \
        echo "fastdfs storage: UP" || { echo "fastdfs storage: DOWN"; failed=1; }
    return "${failed}"
}

usage() {
    echo "usage: $0 {bootstrap|start|stop|status}" >&2
    exit 2
}

case "${1:-}" in
    bootstrap) bootstrap ;;
    start) start ;;
    stop) stop ;;
    status) status ;;
    *) usage ;;
esac
