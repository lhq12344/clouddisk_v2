# Kubernetes 清单迁移说明

`clouddisk_v2` 的 K8s YAML 已迁移到统一目录：

```bash
/home/lihaoqian/project/k8s
```

统一目录按组件名组织，不再按项目建目录。

统一脚本：

```bash
/home/lihaoqian/project/k8s/bin/k8s-stack.sh deploy mysql
/home/lihaoqian/project/k8s/bin/k8s-stack.sh start redis
/home/lihaoqian/project/k8s/bin/k8s-stack.sh stop nacos
/home/lihaoqian/project/k8s/bin/k8s-stack.sh status clouddisk_v2
```
