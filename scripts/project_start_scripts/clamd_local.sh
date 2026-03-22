#!/bin/bash

set -euo pipefail

PROJECT_ROOT="/home/lihaoqian/project/clouddisk_v2"
PID_DIR="$PROJECT_ROOT/.pids"
LOG_DIR="$PROJECT_ROOT/log"
RUN_DIR="$PROJECT_ROOT/run/clamav"
PID_FILE="$PID_DIR/clamd_local.pid"
LOG_FILE="$LOG_DIR/clamd_local.log"
CONF_FILE="$RUN_DIR/clamd-local.conf"
SOCKET_FILE="$RUN_DIR/clamd-local.ctl"
DAEMON_PID_FILE="$RUN_DIR/clamd-local.pid"

CLAMD_LIMIT_MB="${CLAMD_LIMIT_MB:-1024}"
CLAMD_TIMEOUT_SECONDS="${CLAMD_TIMEOUT_SECONDS:-120}"
CLAMD_DATABASE_DIR="${CLAMD_DATABASE_DIR:-/var/lib/clamav}"

mkdir -p "$PID_DIR" "$LOG_DIR" "$RUN_DIR"

is_running() {
    find_running_pid > /dev/null 2>&1
}

find_running_pid() {
    local pid=""

    for candidate in "$PID_FILE" "$DAEMON_PID_FILE"; do
        if [ -f "$candidate" ]; then
            pid=$(cat "$candidate" 2>/dev/null || true)
            if [ -n "$pid" ] && ps -p "$pid" > /dev/null 2>&1; then
                echo "$pid" > "$PID_FILE"
                echo "$pid"
                return 0
            fi
        fi
    done

    pid=$(pgrep -f "clamd --config-file=${CONF_FILE}" | head -1 || true)
    if [ -n "$pid" ] && ps -p "$pid" > /dev/null 2>&1; then
        echo "$pid" > "$PID_FILE"
        echo "$pid"
        return 0
    fi

    return 1
}

write_config() {
    cat > "$CONF_FILE" <<EOF
LocalSocket ${SOCKET_FILE}
FixStaleSocket true
LocalSocketMode 666
PidFile ${DAEMON_PID_FILE}
LogFile ${LOG_FILE}
LogTime true
DatabaseDirectory ${CLAMD_DATABASE_DIR}
TemporaryDirectory /tmp
ReadTimeout 180
CommandReadTimeout ${CLAMD_TIMEOUT_SECONDS}
SendBufTimeout 500
MaxThreads 4
MaxConnectionQueueLength 8
MaxQueue 32
MaxScanTime 300000
MaxScanSize ${CLAMD_LIMIT_MB}M
MaxFileSize ${CLAMD_LIMIT_MB}M
PCREMaxFileSize ${CLAMD_LIMIT_MB}M
StreamMaxLength ${CLAMD_LIMIT_MB}M
ExtendedDetectionInfo true
EOF
}

refresh_pid_file() {
    find_running_pid > /dev/null 2>&1 || true
}

start_clamd() {
    if ! command -v clamd > /dev/null 2>&1; then
        echo "✗ 未找到 clamd，可执行文件不在 PATH 中"
        return 1
    fi

    if [ ! -d "$CLAMD_DATABASE_DIR" ]; then
        echo "✗ 未找到 ClamAV 病毒库目录: $CLAMD_DATABASE_DIR"
        return 1
    fi

    if is_running && [ -S "$SOCKET_FILE" ]; then
        echo "✓ clamd_local 已在运行"
        echo "  PID: $(cat "$PID_FILE")"
        echo "  Socket: $SOCKET_FILE"
        return 0
    fi

    rm -f "$PID_FILE" "$DAEMON_PID_FILE" "$SOCKET_FILE"
    write_config

    : > "$LOG_FILE"
    if ! clamd -c "$CONF_FILE"; then
        echo "✗ clamd_local 启动失败"
        tail -n 40 "$LOG_FILE" 2>/dev/null || true
        return 1
    fi

    for _ in $(seq 1 10); do
        refresh_pid_file
        if is_running && [ -S "$SOCKET_FILE" ]; then
            local pid
            pid=$(cat "$PID_FILE")
            echo "✓ clamd_local 已启动"
            echo "  PID: $pid"
            echo "  Socket: $SOCKET_FILE"
            echo "  Limit: ${CLAMD_LIMIT_MB} MiB"
            return 0
        fi
        sleep 1
    done

    echo "✗ clamd_local 启动失败"
    tail -n 40 "$LOG_FILE" 2>/dev/null || true
    return 1
}

stop_clamd() {
    if ! is_running; then
        rm -f "$PID_FILE" "$DAEMON_PID_FILE" "$SOCKET_FILE"
        echo "○ clamd_local 未运行"
        return 0
    fi

    local pid
    pid=$(find_running_pid)
    kill "$pid" 2>/dev/null || true
    sleep 1
    if ps -p "$pid" > /dev/null 2>&1; then
        kill -9 "$pid" 2>/dev/null || true
    fi

    rm -f "$PID_FILE" "$DAEMON_PID_FILE" "$SOCKET_FILE"
    echo "✓ clamd_local 已停止"
}

status_clamd() {
    refresh_pid_file
    if is_running && [ -S "$SOCKET_FILE" ]; then
        local pid
        pid=$(find_running_pid)
        echo "✓ clamd_local 运行中"
        echo "  PID: $pid"
        echo "  Socket: $SOCKET_FILE"
        echo "  Config: $CONF_FILE"
        echo "  Log: $LOG_FILE"
        return 0
    fi

    echo "○ clamd_local 未运行"
    return 1
}

case "${1:-start}" in
    start)
        start_clamd
        ;;
    stop)
        stop_clamd
        ;;
    status)
        status_clamd
        ;;
    *)
        echo "用法: $0 {start|stop|status}"
        exit 1
        ;;
esac
