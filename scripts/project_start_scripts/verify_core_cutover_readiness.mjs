import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

const repoRoot = process.cwd();
const findings = [];

function read(path) {
  const fullPath = join(repoRoot, path);
  if (!existsSync(fullPath)) {
    findings.push({ level: 'blocker', path, detail: 'missing file' });
    return '';
  }
  return readFileSync(fullPath, 'utf8');
}

function countMatches(path, source, pattern) {
  const matches = [...source.matchAll(pattern)];
  if (matches.length > 0) {
    findings.push({ level: 'pending', path, detail: `${matches.length} match(es) for ${pattern}` });
  }
}

function requireContains(path, source, needle) {
  if (!source.includes(needle)) {
    findings.push({ level: 'blocker', path, detail: `missing ${needle}` });
  }
}

const files = {
  accountControllerH: read('forward_part/gateway/controllers/AccountController.h'),
  accountControllerCc: read('forward_part/gateway/controllers/AccountController.cc'),
  fileControllerH: read('forward_part/gateway/controllers/FileController.h'),
  fileControllerCc: read('forward_part/gateway/controllers/FileController.cc'),
  grpcStorageControlAdapter: read('forward_part/gateway/infrastructure/storage_control/GrpcStorageControlAdapter.cc'),
  storageControlGrpcCpp: read('proto/storage_control/storage_control.grpc.pb.cc'),
  cmake: read('forward_part/gateway/CMakeLists.txt'),
  gatewayConfig: read('forward_part/gateway/config.json'),
  nacosConfig: read('scripts/nacos-config/clouddisk.json'),
  internalConfigH: read('forward_part/internal/internal.h'),
  internalConfigCpp: read('forward_part/internal/internal.cpp'),
  startAll: read('scripts/project_start_scripts/start_all.sh'),
  status: read('scripts/project_start_scripts/status.sh'),
  stopAll: read('scripts/project_start_scripts/stop_all.sh'),
};

countMatches('AccountController.h', files.accountControllerH, /account\.grpc\.pb\.h/g);
countMatches('AccountController.cc', files.accountControllerCc, /FindService\("account_srv"\)/g);
countMatches('FileController.h', files.fileControllerH, /file\.grpc\.pb\.h/g);
countMatches('FileController.cc', files.fileControllerCc, /FindService\("file_srv"\)/g);
countMatches('CMakeLists.txt', files.cmake, /ACCOUNT_PROTO_SRCS|FILE_PROTO_SRCS|account\.grpc\.pb\.cc|file\.grpc\.pb\.cc/g);
countMatches('gateway/config.json', files.gatewayConfig, /"account_srv"|"file_srv"/g);
countMatches('scripts/nacos-config/clouddisk.json', files.nacosConfig, /"account_srv"|"file_srv"/g);
countMatches('forward_part/internal/internal.h', files.internalConfigH, /ServerConfig\s+(account_srv|file_srv)\b/g);
countMatches('forward_part/internal/internal.cpp', files.internalConfigCpp, /j\["consul"\]\["(account_srv|file_srv)"\]|cfg\.consul\.(account_srv|file_srv)/g);

requireContains('GrpcStorageControlAdapter.cc', files.grpcStorageControlAdapter, 'CompleteMultipart');
requireContains('GrpcStorageControlAdapter.cc', files.grpcStorageControlAdapter, 'PresignGet');
requireContains('storage_control.grpc.pb.cc', files.storageControlGrpcCpp, '/storage_control.StorageControl/CompleteMultipart');
requireContains('FileController.cc', files.fileControllerCc, 'FindStorageControl("storage_control")');
requireContains('FileController.cc', files.fileControllerCc, 'GrpcStorageControlAdapter');

requireContains('start_all.sh', files.startAll, 'CORE_RUNTIME_PROFILE="${CORE_RUNTIME_PROFILE:-core}"');
requireContains('start_all.sh', files.startAll, 'configure_core_feature_flags');
requireContains('start_all.sh', files.startAll, 'CORE_ACCOUNT_LOGIN:=core');
requireContains('start_all.sh', files.startAll, 'CORE_UPLOAD_COMPLETE:=core');
requireContains('status.sh', files.status, 'CORE_RUNTIME_PROFILE="${CORE_RUNTIME_PROFILE:-core}"');
requireContains('stop_all.sh', files.stopAll, 'CORE_RUNTIME_PROFILE="${CORE_RUNTIME_PROFILE:-core}"');

const requiredNewRoles = [
  ['start_all.sh', files.startAll, 'outbox_relay'],
  ['start_all.sh', files.startAll, 'storage_control'],
  ['status.sh', files.status, 'outbox_relay'],
  ['status.sh', files.status, 'storage_control'],
  ['stop_all.sh', files.stopAll, 'outbox_relay'],
  ['stop_all.sh', files.stopAll, 'storage_control'],
];
for (const [path, source, needle] of requiredNewRoles) {
  if (!source.includes(needle)) {
    findings.push({ level: 'blocker', path, detail: `missing new role marker ${needle}` });
  }
}

if (findings.length > 0) {
  console.log('Core cutover readiness: NOT READY');
  for (const finding of findings) {
    console.log(`- [${finding.level}] ${finding.path}: ${finding.detail}`);
  }
  process.exit(findings.some((finding) => finding.level === 'blocker') ? 2 : 1);
}

console.log('Core cutover readiness: READY');
