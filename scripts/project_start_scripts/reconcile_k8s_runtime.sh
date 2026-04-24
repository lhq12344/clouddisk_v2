#!/bin/bash

set -euo pipefail

PROJECT_ROOT="/home/lihaoqian/project/clouddisk_v2"
K8S_ROOT="${K8S_ROOT:-/home/lihaoqian/project/k8s}"
K8S_NAMESPACE="${K8S_NAMESPACE:-infra}"
SYNC_SCRIPT="$PROJECT_ROOT/scripts/project_start_scripts/sync_k8s_manifests.py"

BLUE='\033[0;34m'
YELLOW='\033[1;33m'
GREEN='\033[0;32m'
NC='\033[0m'

log_ok() {
    echo -e "${GREEN}$1${NC}"
}

log_warn() {
    echo -e "${YELLOW}$1${NC}"
}

if ! command -v kubectl > /dev/null 2>&1; then
    exit 0
fi

if ! kubectl get namespace "$K8S_NAMESPACE" > /dev/null 2>&1; then
    exit 0
fi

if [ -f "$SYNC_SCRIPT" ] && command -v python3 > /dev/null 2>&1; then
    if python3 "$SYNC_SCRIPT" --apply --k8s-root "$K8S_ROOT" > /dev/null 2>&1; then
        log_ok "  ✓ 已同步 $K8S_ROOT 中的 openresty/kafka manifest"
    else
        log_warn "  ⚠ 外部 K8s manifest 未能自动同步"
    fi
fi

NODE_NAME=$(kubectl get nodes -o jsonpath='{.items[0].metadata.name}' 2>/dev/null || true)
NODE_IP=$(kubectl get node "$NODE_NAME" -o jsonpath='{range .status.addresses[?(@.type=="InternalIP")]}{.address}{"\n"}{end}' 2>/dev/null | grep -E '^[0-9]+\.' | head -1 || true)
HOST_IP=$(ip -4 route get 1.1.1.1 2>/dev/null | awk '/src/ {print $7; exit}' || true)
TARGET_IP="$NODE_IP"

if [ -n "$HOST_IP" ]; then
    TARGET_IP="$HOST_IP"
fi

if [ -n "$HOST_IP" ] && [ -n "$NODE_IP" ] && [ "$HOST_IP" != "$NODE_IP" ]; then
    log_warn "  ⚠ 当前宿主机 IP($HOST_IP) 与 K8s Node IP($NODE_IP) 不一致，请检查 /etc/rancher/k3s/config.yaml"
fi

if [ -n "$TARGET_IP" ]; then
    CURRENT_ENDPOINTS=$(kubectl get endpoints -n default kubernetes -o jsonpath='{range .subsets[*].addresses[*]}{.ip}{" "}{end}' 2>/dev/null || true)
    if [ -n "$CURRENT_ENDPOINTS" ] && [ "$CURRENT_ENDPOINTS" != "$TARGET_IP " ]; then
        kubectl patch endpoints -n default kubernetes --type merge -p "{\"subsets\":[{\"addresses\":[{\"ip\":\"$TARGET_IP\"}],\"ports\":[{\"name\":\"https\",\"port\":6443,\"protocol\":\"TCP\"}]}]}" > /dev/null

        ENDPOINTSLICE_NAME=$(kubectl get endpointslice -n default -l kubernetes.io/service-name=kubernetes -o jsonpath='{.items[0].metadata.name}' 2>/dev/null || true)
        if [ -n "$ENDPOINTSLICE_NAME" ]; then
            kubectl patch endpointslice -n default "$ENDPOINTSLICE_NAME" --type merge -p "{\"endpoints\":[{\"addresses\":[\"$TARGET_IP\"],\"conditions\":{\"ready\":true}}],\"ports\":[{\"name\":\"https\",\"port\":6443,\"protocol\":\"TCP\"}]}" > /dev/null
        fi

        log_ok "  ✓ 已校正 kubernetes Service 后端到 $TARGET_IP:6443"
    fi
fi

if kubectl get deployment -n "$K8S_NAMESPACE" openresty > /dev/null 2>&1; then
    kubectl patch deployment -n "$K8S_NAMESPACE" openresty --type strategic -p '{"spec":{"template":{"spec":{"containers":[{"name":"openresty","readinessProbe":{"httpGet":{"host":"127.0.0.1","path":"/","port":2024},"initialDelaySeconds":5,"periodSeconds":5},"livenessProbe":{"httpGet":{"host":"127.0.0.1","path":"/","port":2024},"initialDelaySeconds":10,"periodSeconds":10}}]}}}}' > /dev/null
    kubectl rollout status -n "$K8S_NAMESPACE" deployment/openresty --timeout=180s > /dev/null || true
    log_ok "  ✓ 已校正 openresty 探针"
fi

if kubectl get deployment -n "$K8S_NAMESPACE" kafka > /dev/null 2>&1 && [ -n "$TARGET_IP" ]; then
    kubectl scale -n "$K8S_NAMESPACE" deployment/kafka --replicas=1 > /dev/null || true
    kubectl set env -n "$K8S_NAMESPACE" deployment/kafka \
        KAFKA_ZOOKEEPER_CONNECT=zookeeper:2181 \
        KAFKA_ADVERTISED_LISTENERS="INTERNAL://kafka:9092,EXTERNAL://$TARGET_IP:31092" \
        KAFKA_LISTENERS="INTERNAL://0.0.0.0:9092,EXTERNAL://0.0.0.0:29092" \
        KAFKA_LISTENER_SECURITY_PROTOCOL_MAP="INTERNAL:PLAINTEXT,EXTERNAL:PLAINTEXT" \
        KAFKA_INTER_BROKER_LISTENER_NAME=INTERNAL \
        KAFKA_BROKER_ID=1 \
        KAFKA_OFFSETS_TOPIC_REPLICATION_FACTOR=1 \
        KAFKA_LOG_RETENTION_HOURS=168 \
        KAFKA_AUTO_CREATE_TOPICS_ENABLE=true > /dev/null
    kubectl rollout status -n "$K8S_NAMESPACE" deployment/kafka --timeout=180s > /dev/null || true
    log_ok "  ✓ 已校正 kafka listeners/advertised.listeners"
fi
