import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

const repoRoot = process.cwd();
const findings = [];

function exists(path) {
  return existsSync(join(repoRoot, path));
}

function read(path) {
  const fullPath = join(repoRoot, path);
  if (!existsSync(fullPath)) {
    return '';
  }
  return readFileSync(fullPath, 'utf8');
}

function pending(path, detail) {
  findings.push({ path, detail });
}

const legacyDirs = [
  'backword_part/account_server/account_srv',
  'backword_part/file_server/file_srv',
  'proto/account_srv',
  'proto/file_srv',
];
for (const dir of legacyDirs) {
  if (exists(dir)) {
    pending(dir, 'legacy business service directory still exists');
  }
}

const sourceChecks = [
  ['forward_part/gateway/controllers/AccountController.h', /account\.grpc\.pb\.h/g],
  ['forward_part/gateway/controllers/AccountController.cc', /FindService\("account_srv"\)/g],
  ['forward_part/gateway/controllers/FileController.h', /file\.grpc\.pb\.h/g],
  ['forward_part/gateway/controllers/FileController.cc', /FindService\("file_srv"\)/g],
  ['forward_part/gateway/CMakeLists.txt', /ACCOUNT_PROTO_SRCS|FILE_PROTO_SRCS|account\.grpc\.pb\.cc|file\.grpc\.pb\.cc/g],
  ['forward_part/internal/internal.h', /ServerConfig\s+(account_srv|file_srv)\b/g],
  ['forward_part/internal/internal.cpp', /j\["consul"\]\["(account_srv|file_srv)"\]|cfg\.consul\.(account_srv|file_srv)/g],
  ['scripts/project_start_scripts/start_all.sh', /go run \.\/backword_part\/(account_server\/account_srv|file_server\/file_srv)/g],
  ['scripts/project_start_scripts/status.sh', /check_service "(account_srv|file_srv)"/g],
  ['scripts/project_start_scripts/stop_all.sh', /stop_service "(account_srv|file_srv)"/g],
  ['forward_part/gateway/config.json', /"account_srv"|"file_srv"/g],
  ['scripts/nacos-config/clouddisk.json', /"account_srv"|"file_srv"/g],
];

for (const [path, pattern] of sourceChecks) {
  const source = read(path);
  const count = [...source.matchAll(pattern)].length;
  if (count > 0) {
    pending(path, `${count} legacy reference(s) remain`);
  }
}

if (findings.length > 0) {
  console.log('Legacy service removal readiness: NOT READY');
  for (const finding of findings) {
    console.log(`- ${finding.path}: ${finding.detail}`);
  }
  process.exit(1);
}

console.log('Legacy service removal readiness: READY');
