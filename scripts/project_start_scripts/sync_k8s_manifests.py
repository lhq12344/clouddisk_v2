#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path


OPENRESTY_DEPLOYMENT = """apiVersion: apps/v1
kind: Deployment
metadata:
  name: openresty
  namespace: infra
spec:
  replicas: 1
  strategy:
    type: Recreate
  selector:
    matchLabels:
      app: openresty
  template:
    metadata:
      labels:
        app: openresty
    spec:
      hostNetwork: true
      dnsPolicy: ClusterFirstWithHostNet
      containers:
        - name: openresty
          image: swr.cn-north-4.myhuaweicloud.com/ddn-k8s/docker.io/uusec/openresty-manager:latest
          imagePullPolicy: IfNotPresent
          command:
            - openresty
          args:
            - -c
            - /home/lihaoqian/project/clouddisk_v2/forward_part/config/nginx/nginx.conf
            - -p
            - /home/lihaoqian/project/clouddisk_v2/forward_part/config/nginx/
            - -g
            - daemon off;
          ports:
            - name: app
              containerPort: 2024
            - name: preview
              containerPort: 2025
          readinessProbe:
            httpGet:
              host: 127.0.0.1
              path: /
              port: 2024
            initialDelaySeconds: 5
            periodSeconds: 5
          livenessProbe:
            httpGet:
              host: 127.0.0.1
              path: /
              port: 2024
            initialDelaySeconds: 10
            periodSeconds: 10
          volumeMounts:
            - name: project-root
              mountPath: /home/lihaoqian/project/clouddisk_v2
      volumes:
        - name: project-root
          hostPath:
            path: /home/lihaoqian/project/clouddisk_v2
            type: Directory
"""


KAFKA_DEPLOYMENT = """apiVersion: apps/v1
kind: Deployment
metadata:
  name: kafka
  namespace: infra
spec:
  replicas: 1
  selector:
    matchLabels:
      app: kafka
  template:
    metadata:
      labels:
        app: kafka
    spec:
      # 禁用服务环境变量注入
      enableServiceLinks: false
      containers:
      - name: kafka
        image: swr.cn-north-4.myhuaweicloud.com/ddn-k8s/docker.io/wurstmeister/kafka:latest
        imagePullPolicy: IfNotPresent
        ports:
        - containerPort: 9092
        - containerPort: 29092
        env:
        - name: KAFKA_ZOOKEEPER_CONNECT
          value: zookeeper:2181
        - name: NODE_IP
          valueFrom:
            fieldRef:
              fieldPath: status.hostIP
        - name: KAFKA_ADVERTISED_LISTENERS
          value: INTERNAL://kafka:9092,EXTERNAL://$(NODE_IP):31092
        - name: KAFKA_LISTENERS
          value: INTERNAL://0.0.0.0:9092,EXTERNAL://0.0.0.0:29092
        - name: KAFKA_LISTENER_SECURITY_PROTOCOL_MAP
          value: INTERNAL:PLAINTEXT,EXTERNAL:PLAINTEXT
        - name: KAFKA_INTER_BROKER_LISTENER_NAME
          value: INTERNAL
        - name: KAFKA_BROKER_ID
          value: "1"
        - name: KAFKA_OFFSETS_TOPIC_REPLICATION_FACTOR
          value: "1"
        - name: KAFKA_LOG_RETENTION_HOURS
          value: "168"
        - name: KAFKA_AUTO_CREATE_TOPICS_ENABLE
          value: "true"
"""


def write_if_needed(path: Path, content: str, apply: bool) -> bool:
    current = path.read_text() if path.exists() else ""
    normalized = content.rstrip() + "\n"
    if current == normalized:
        return False
    if apply:
        path.write_text(normalized)
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--k8s-root", default="/home/lihaoqian/project/k8s")
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    k8s_root = Path(args.k8s_root)
    openresty = k8s_root / "manifests/openresty/workloads/deployment.yaml"
    kafka = k8s_root / "manifests/kafka/workloads/deployment.yaml"

    changed = False
    changed |= write_if_needed(openresty, OPENRESTY_DEPLOYMENT, args.apply)
    changed |= write_if_needed(kafka, KAFKA_DEPLOYMENT, args.apply)

    print("changed" if changed else "ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
