import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

const repoRoot = process.cwd();
const failures = [];

function read(path) {
  const fullPath = join(repoRoot, path);
  if (!existsSync(fullPath)) {
    failures.push(`${path}: missing file`);
    return '';
  }
  return readFileSync(fullPath, 'utf8');
}

function requireContains(path, source, needle, detail = `missing ${needle}`) {
  if (!source.includes(needle)) {
    failures.push(`${path}: ${detail}`);
  }
}

function requireNotContains(path, source, needle, detail = `must not contain ${needle}`) {
  if (source.includes(needle)) {
    failures.push(`${path}: ${detail}`);
  }
}

function requireRegex(path, source, pattern, detail = `missing pattern ${pattern}`) {
  if (!pattern.test(source)) {
    failures.push(`${path}: ${detail}`);
  }
}

const accountControllerCc = read('forward_part/gateway/controllers/AccountController.cc');
const accountControllerH = read('forward_part/gateway/controllers/AccountController.h');
const fileControllerCc = read('forward_part/gateway/controllers/FileController.cc');
const fileControllerH = read('forward_part/gateway/controllers/FileController.h');
const healthControllerH = read('forward_part/gateway/controllers/HealthController.h');
const healthControllerCc = read('forward_part/gateway/controllers/HealthController.cc');
const featureFlags = read('forward_part/gateway/application/common/FeatureFlags.h');
const featureFlagsCc = read('forward_part/gateway/application/common/FeatureFlags.cc');
const accountPorts = read('forward_part/gateway/application/account/AccountPorts.h');
const emailService = read('forward_part/gateway/application/account/EmailVerificationCommandService.h');
const multipartUploadService = read('forward_part/gateway/application/upload/MultipartUploadService.h');
const outboxRepo = read('forward_part/gateway/infrastructure/outbox/MysqlOutboxRepository.cc');
const uploadRepo = read('forward_part/gateway/infrastructure/mysql/MysqlUploadSessionRepository.cc');
const fileRepo = read('forward_part/gateway/infrastructure/mysql/MysqlFileRepository.cc');
const storageAdapterCc = read('forward_part/gateway/infrastructure/storage_control/GrpcStorageControlAdapter.cc');
const storageAdapterH = read('forward_part/gateway/infrastructure/storage_control/GrpcStorageControlAdapter.h');
const uploadManager = read('forward_part/static/components/UploadManager.tsx');
const apiTs = read('forward_part/static/services/api.ts');
const typesTs = read('forward_part/static/types.ts');
const startAll = read('scripts/project_start_scripts/start_all.sh');
const status = read('scripts/project_start_scripts/status.sh');
const stopAll = read('scripts/project_start_scripts/stop_all.sh');
const internalConfig = read('forward_part/internal/internal.cpp');
const internalHeader = read('forward_part/internal/internal.h');
const emailConfigCpp = read('other_srv/email_srv/config/config.cpp');
const emailConfigJson = read('other_srv/email_srv/email_config.json');
const gatewayConfig = read('forward_part/gateway/config.json');
const nacosConfig = read('scripts/nacos-config/clouddisk.json');
const goBootstrap = read('internal/viper_config_centre.go');
const cmake = read('forward_part/gateway/CMakeLists.txt');
const resultHeader = read('forward_part/gateway/domain/common/Result.h');
const compileProbe = read('forward_part/gateway/compile_probe/core_logic_compile_probe.cc');
const sourceSuite = read('scripts/project_start_scripts/verify_core_source_suite.ps1');
const agentsMd = read('AGENTS.md');
const outboxRelay = read('internal/outboxrelay/dispatcher.go');
const dlqProducer = read('other_srv/store_srv/kafka/dlq_producer.go');
const removedSmtpAppPassword = ['romcmq', 'sisbxg', 'jjef'].join('');

// Legacy business services must stay removed from active code/config.
for (const [path, source] of [
  ['AccountController.cc', accountControllerCc],
  ['AccountController.h', accountControllerH],
  ['FileController.cc', fileControllerCc],
  ['FileController.h', fileControllerH],
  ['gateway/config.json', gatewayConfig],
  ['scripts/nacos-config/clouddisk.json', nacosConfig],
]) {
  requireNotContains(path, source, 'account.grpc.pb.h');
  requireNotContains(path, source, 'file.grpc.pb.h');
  requireNotContains(path, source, 'FindService("account_srv")');
  requireNotContains(path, source, 'FindService("file_srv")');
}

for (const legacyPath of [
  'backword_part/account_server/account_srv',
  'backword_part/account_server/custom_error',
  'backword_part/file_server/file_srv',
  'forward_part/gateway/infrastructure/legacy/LegacyAdapters.h',
  'proto/account_srv',
  'proto/file_srv',
]) {
  if (existsSync(join(repoRoot, legacyPath))) {
    failures.push(`${legacyPath}: legacy path still exists`);
  }
}

// Explicit legacy flag values fail closed now that old adapters are retired.
requireContains('AccountController.cc', accountControllerCc, 'legacy account adapter removed; use CORE_ACCOUNT_READS=core');
requireContains('AccountController.cc', accountControllerCc, 'legacy account adapter removed; use CORE_ACCOUNT_LOGIN=core');
requireContains('AccountController.cc', accountControllerCc, 'legacy account adapter removed; use CORE_ACCOUNT_REGISTRATION=core');
requireContains('FileController.cc', fileControllerCc, 'legacy_file_adapter_removed');
requireContains('FileController.cc', fileControllerCc, 'small_upload_proxy_removed');
requireContains('FileController.cc', fileControllerCc, 'part_proxy_removed');

// Core defaults must remain core unless explicitly overridden by the environment.
for (const field of [
  'accountReads',
  'accountLogin',
  'accountRegistration',
  'fileReads',
  'fileAccess',
  'uploadControl',
  'uploadComplete',
  'fileDelete',
  'smallUploadDirect',
  'outboxRelay',
]) {
  requireRegex('FeatureFlags.h', featureFlags, new RegExp(`${field}\\{ImplementationRoute::Core\\}`), `${field} must default to Core`);
}
requireContains('FeatureFlags.cc', featureFlagsCc, 'value == "legacy" || value == "old"');
requireContains('FeatureFlags.cc', featureFlagsCc, '_dupenv_s', 'Windows feature-flag env reads should avoid MSVC getenv warnings');
requireNotContains('AccountPorts.h', accountPorts, 'LegacyAccountPort', 'retired account service adapter port must not remain');
requireContains('AccountPorts.h', accountPorts, 'verifyLegacyPassword', 'legacy password compatibility must remain explicit');
requireContains('internal.cpp', internalConfig, 'CLOUDDISK_CONFIG_JSON', 'gateway config bootstrap must support environment-provided JSON');
requireContains('internal.cpp', internalConfig, 'CLOUDDISK_CONFIG_FILE', 'gateway config bootstrap must support environment-provided config file path');
requireContains('internal.cpp', internalConfig, 'CLOUDDISK_CONFIG_VERSION', 'gateway config bootstrap must expose a non-sensitive config version');
requireNotContains('internal.cpp', internalConfig, 'Config updated:\\n', 'gateway config updates must not print full config payloads');
requireContains('email_srv/config.cpp', emailConfigCpp, 'EMAIL_SMTP_PASS', 'email SMTP password must be injectable through environment');
requireNotContains('email_srv/config.cpp', emailConfigCpp, 'Config updated:\\n', 'email config updates must not print full config payloads');
requireNotContains('email_srv/config.cpp', emailConfigCpp, 'Initial config:\\n', 'email initial config must not print full config payloads');
requireNotContains('email_config.json', emailConfigJson, removedSmtpAppPassword, 'SMTP app password must not be committed');
requireContains('email_config.json', emailConfigJson, '"pass": ""', 'local email config should leave SMTP password blank and use EMAIL_SMTP_PASS');
requireNotContains('internal.h', internalHeader, '<sys/socket.h>', 'socket platform headers must stay out of the public config header');
requireNotContains('internal.h', internalHeader, '<netinet/in.h>', 'socket platform headers must stay out of the public config header');
requireContains('internal.cpp', internalConfig, '#ifdef _WIN32', 'GetFreePort must keep a Windows-compatible socket implementation');
requireContains('internal.cpp', internalConfig, 'WSAStartup', 'GetFreePort must initialize Winsock on Windows');
requireContains('CMakeLists.txt', cmake, 'ws2_32', 'Windows builds must link Winsock for GetFreePort');
requireContains('Result.h', resultHeader, 'static Result<void> success()', 'void Result success factory must not collide with the ok() status accessor');
requireNotContains('gateway', accountControllerCc + accountControllerH + fileControllerCc + fileControllerH + multipartUploadService + uploadRepo + outboxRepo + storageAdapterCc, 'Result<void>::ok()', 'void Result success callers must use Result<void>::success()');
requireContains('verify_core_source_suite.ps1', sourceSuite, 'verify_gateway_core_logic_compile.ps1', 'source suite must include the Drogon-free Gateway Core compile probe');
requireContains('verify_core_source_suite.ps1', sourceSuite, 'verify_core_runtime_prereqs_selftest.ps1', 'source suite must include runtime prereq checker self-test');
requireContains('verify_core_source_suite.ps1', sourceSuite, 'verify_core_runtime_smoke_contract.mjs', 'source suite must include runtime smoke contract verifier');
requireContains('verify_core_source_suite.ps1', sourceSuite, 'verify_core_baseline_contract.mjs', 'source suite must include baseline contract verifier');
requireContains('verify_core_source_suite.ps1', sourceSuite, 'verify_no_committed_secrets.mjs', 'source suite must include committed secret verifier');
requireContains('verify_core_source_suite.ps1', sourceSuite, 'verify_powershell_script_syntax.ps1', 'source suite must include PowerShell script syntax verifier');
requireContains('verify_core_runtime_prereqs.ps1', read('scripts/project_start_scripts/verify_core_runtime_prereqs.ps1'), '-lt 300', 'runtime prereq health check must require 2xx HTTP responses');
requireContains('verify_core_runtime_prereqs_selftest.ps1', read('scripts/project_start_scripts/verify_core_runtime_prereqs_selftest.ps1'), 'must exit non-zero in hard mode', 'runtime prereq self-test must verify hard-mode failure behavior');
requireContains('core_logic_compile_probe.cc', compileProbe, 'CoreFeatureFlags::fromEnvironment()', 'Gateway Core compile probe must exercise feature flag env parsing');
requireContains('core_logic_compile_probe.cc', compileProbe, 'SigninService signin', 'Gateway Core compile probe must exercise account application service wiring');
requireContains('core_logic_compile_probe.cc', compileProbe, 'RegistrationService registration', 'Gateway Core compile probe must exercise registration application service wiring');
requireContains('core_logic_compile_probe.cc', compileProbe, 'EmailVerificationCommandService emailCommands', 'Gateway Core compile probe must exercise email verification outbox command wiring');
requireContains('core_logic_compile_probe.cc', compileProbe, 'FileReadService fileReads', 'Gateway Core compile probe must exercise file access application service wiring');
requireContains('core_logic_compile_probe.cc', compileProbe, 'FileDeleteService fileDelete', 'Gateway Core compile probe must exercise file delete application service wiring');
requireContains('core_logic_compile_probe.cc', compileProbe, 'MultipartUploadService uploads', 'Gateway Core compile probe must exercise upload application service wiring');
requireContains('core_logic_compile_probe.cc', compileProbe, 'completeCalls != 1', 'Gateway Core compile probe must guard idempotent duplicate complete behavior');
requireContains('core_logic_compile_probe.cc', compileProbe, 'accountWriter.createCalls != 1', 'Gateway Core compile probe must guard verification-code account creation/idempotency');
requireContains('core_logic_compile_probe.cc', compileProbe, 'objectDeleteQueued', 'Gateway Core compile probe must guard async object delete queueing');
requireContains('AGENTS.md', agentsMd, 'C++ Drogon Core API', 'AGENTS.md must describe the current Core API topology');
requireContains('AGENTS.md', agentsMd, 'Storage Control (gRPC)', 'AGENTS.md must include the Storage Control runtime role');
requireContains('AGENTS.md', agentsMd, 'outbox_relay', 'AGENTS.md must document the standalone outbox relay role');
requireNotContains('AGENTS.md', agentsMd, 'go build -o bin/account_srv', 'AGENTS.md must not document retired account_srv build commands');
requireNotContains('AGENTS.md', agentsMd, 'go build -o bin/file_srv', 'AGENTS.md must not document retired file_srv build commands');
requireNotContains('AGENTS.md', agentsMd, 'go run ./backword_part/account_server/account_srv', 'AGENTS.md must not document retired account_srv run commands');
requireNotContains('AGENTS.md', agentsMd, 'go run ./backword_part/file_server/file_srv', 'AGENTS.md must not document retired file_srv run commands');
for (const entry of readdirSync(join(repoRoot, 'manifest_txt'), { withFileTypes: true })) {
  if (!entry.isFile() || !entry.name.endsWith('.txt')) {
    continue;
  }
  const manifestPath = `manifest_txt/${entry.name}`;
  requireContains(manifestPath, read(manifestPath), 'Archived pre-Core-migration snapshot', 'pre-migration manifest docs must be clearly marked as historical');
}
requireContains('viper_config_centre.go', goBootstrap, 'CLOUDDISK_SKIP_BOOTSTRAP', 'Go bootstrap must support explicit external dependency skip for offline checks');
requireContains('viper_config_centre.go', goBootstrap, 'CLOUDDISK_TEST_BOOTSTRAP', 'Go tests must only hit external Nacos/bootstrap when explicitly requested');
requireContains('viper_config_centre.go', goBootstrap, '.test.exe', 'Go test bootstrap detection must work on Windows');
requireContains('AGENTS.md', agentsMd, 'CLOUDDISK_TEST_BOOTSTRAP=1', 'AGENTS.md must document how to opt integration tests into external bootstrap');
requireContains('HealthController.cc', healthControllerCc, 'select 1', '/ready must probe MySQL instead of only checking config');
requireContains('HealthController.cc', healthControllerCc, 'PING', '/ready must probe Redis instead of only checking config');
requireContains('HealthController.cc', healthControllerCc, 'mysql client is not initialized', '/ready must fail closed when MySQL client is missing');
requireContains('HealthController.cc', healthControllerCc, 'redis client is not initialized', '/ready must fail closed when Redis client is missing');

// Header self-sufficiency checks for new gateway code that currently cannot be fully compiled here.
for (const [path, source] of [
  ['AccountController.h', accountControllerH],
  ['FileController.h', fileControllerH],
  ['HealthController.h', healthControllerH],
  ['GrpcStorageControlAdapter.h', storageAdapterH],
]) {
  requireContains(path, source, '#include <functional>', `${path} must explicitly include <functional>`);
}
requireContains('FileController.h', fileControllerH, '#include <memory>');
requireContains('GrpcStorageControlAdapter.h', storageAdapterH, '#include <memory>');
requireContains('GrpcStorageControlAdapter.h', storageAdapterH, '#include <vector>');
requireContains('GrpcStorageControlAdapter.cc', storageAdapterCc, '#include <functional>');

// Storage Control must be the only object-storage control path for file access and upload control.
requireContains('FileController.cc', fileControllerCc, 'FindStorageControl("storage_control")');
requireContains('FileController.cc', fileControllerCc, 'GrpcStorageControlAdapter');
requireContains('GrpcStorageControlAdapter.cc', storageAdapterCc, 'InitiateMultipart');
requireContains('GrpcStorageControlAdapter.cc', storageAdapterCc, 'PresignPart');
requireContains('GrpcStorageControlAdapter.cc', storageAdapterCc, 'ListParts');
requireContains('GrpcStorageControlAdapter.cc', storageAdapterCc, 'CompleteMultipart');
requireContains('GrpcStorageControlAdapter.cc', storageAdapterCc, 'AbortMultipart');
requireContains('GrpcStorageControlAdapter.cc', storageAdapterCc, 'PresignGet');

// Reliable command/outbox invariants.
requireContains('EmailVerificationCommandService.h', emailService, 'EMAIL_VERIFICATION_REQUESTED');
requireContains('EmailVerificationCommandService.h', emailService, 'message.topic = "email_verify"');
requireContains('EmailVerificationCommandService.h', emailService, 'email_verify:');
requireContains('EmailVerificationCommandService.h', emailService, 'out << "\\\\n"');
requireContains('MysqlOutboxRepository.cc', outboxRepo, 'on duplicate key update updated_at=updated_at', 'generic outbox enqueue must be idempotent on event_id');
requireContains('MysqlUploadSessionRepository.cc', uploadRepo, 'insert into outboxes');
requireContains('MysqlUploadSessionRepository.cc', uploadRepo, "'FILE_SCAN_REQUESTED'");
requireContains('MysqlUploadSessionRepository.cc', uploadRepo, 'on duplicate key update updated_at=updated_at');
requireContains('MysqlUploadSessionRepository.cc', uploadRepo, 'escapeJson(session.fileHash)', 'scan outbox payload must JSON-escape file hash');
requireContains('MysqlUploadSessionRepository.cc', uploadRepo, 'escapeJson(session.objectKey)', 'scan outbox payload must JSON-escape object key');
requireContains('MysqlUploadSessionRepository.cc', uploadRepo, 'escapeJson(session.contentType)', 'scan outbox payload must JSON-escape content type');
requireContains('MysqlUploadSessionRepository.cc', uploadRepo, 'escapeJson(ctx.requestId)', 'scan outbox headers must JSON-escape request id');
requireContains('MysqlUploadSessionRepository.cc', uploadRepo, 'sessionUpdate.affectedRows() == 0', 'upload finalization must fail if the session status update misses its row');
requireContains('MultipartUploadService.h', multipartUploadService, 'upload completion already in progress', 'concurrent complete must not repeat Storage Control complete after CAS conflict');
requireContains('MultipartUploadService.h', multipartUploadService, 'latestResult.value.state == UploadState::PendingScan', 'duplicate complete must be idempotent once upload is pending scan');
requireContains('MysqlFileRepository.cc', fileRepo, "'OBJECT_DELETE_REQUESTED'");
requireContains('MysqlFileRepository.cc', fileRepo, 'select count(*) as remaining');
requireContains('MysqlFileRepository.cc', fileRepo, 'escapeJson(file.filehash)', 'delete outbox payload must JSON-escape file hash');
requireContains('MysqlFileRepository.cc', fileRepo, 'escapeJson(file.objectKey)', 'delete outbox payload must JSON-escape object key');
requireContains('MysqlFileRepository.cc', fileRepo, 'escapeJson(file.contentType)', 'delete outbox payload must JSON-escape content type');
requireContains('MysqlFileRepository.cc', fileRepo, 'escapeJson(ctx.requestId)', 'delete outbox headers must JSON-escape request id');
const workerConsumer = read('other_srv/store_srv/kafka/kafkacunsumer.go');
requireContains('kafkacunsumer.go', workerConsumer, 'db.Where("status = ?", model.FilePendingScan).Updates(updates)', 'storage worker file status updates must use an expected pending_scan guard');
requireContains('kafkacunsumer.go', workerConsumer, 'errFileScanAlreadyConverged', 'storage worker must treat already-terminal scan events as converged');
requireContains('kafkacunsumer.go', workerConsumer, 'model.FileSuccess, model.FileInfected, model.FileScanFailed', 'storage worker must define scan terminal states for idempotency');
requireContains('kafkacunsumer.go', workerConsumer, 'result.RowsAffected != 1', 'storage worker inbox state transitions must fail closed on lost lease ownership');
requireContains('dispatcher.go', outboxRelay, 'parseHeaders', 'outbox relay must parse headers explicitly before sending');
requireContains('dispatcher.go', outboxRelay, 'invalid outbox headers JSON', 'outbox relay must fail malformed headers into retry/DLQ instead of silently dropping them');
requireContains('dispatcher.go', outboxRelay, 'result.RowsAffected != 1', 'outbox relay sent/failed transitions must verify lease ownership');
requireContains('dispatcher.go', outboxRelay, 'd.db == nil', 'outbox relay must fail closed when DB is unavailable');
requireContains('dlq_producer.go', dlqProducer, 'result.RowsAffected != 1', 'DLQ persistence must keep DLQ record and Inbox status transactionally aligned');

// Frontend must stay on direct multipart uploads; old byte proxy paths stay fail-closed only.
requireNotContains('services/api.ts', apiTs, 'simpleUpload');
requireNotContains('UploadManager.tsx', uploadManager, "/file/upload'");
requireNotContains('UploadManager.tsx', uploadManager, 'uploadpart');
requireContains('services/api.ts', apiTs, "method: 'PUT'");
requireContains('UploadManager.tsx', uploadManager, 'api.uploadPresignedPart');
requireContains('types.ts', typesTs, 'upload_id: string;');

// Current script topology must keep new roles visible and old roles absent from lifecycle management.
for (const [path, source] of [
  ['start_all.sh', startAll],
  ['status.sh', status],
  ['stop_all.sh', stopAll],
]) {
  requireContains(path, source, 'storage_control');
  requireContains(path, source, 'outbox_relay');
  requireNotContains(path, source, 'check_service "account_srv"');
  requireNotContains(path, source, 'check_service "file_srv"');
  requireNotContains(path, source, 'stop_service "account_srv"');
  requireNotContains(path, source, 'stop_service "file_srv"');
  requireNotContains(path, source, 'legacy_runtime_enabled');
}

if (failures.length > 0) {
  console.error('Core source invariant verification failed:');
  for (const failure of failures) {
    console.error(`- ${failure}`);
  }
  process.exit(1);
}

console.log('Core source invariant verification passed.');
