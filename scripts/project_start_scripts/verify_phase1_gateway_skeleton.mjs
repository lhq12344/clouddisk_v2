import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

const repoRoot = process.cwd();
const failures = [];

function requireFile(path) {
  const fullPath = join(repoRoot, path);
  if (!existsSync(fullPath)) {
    failures.push(`missing required file: ${path}`);
    return '';
  }
  return readFileSync(fullPath, 'utf8');
}

function requireContains(path, source, needle) {
  if (!source.includes(needle)) {
    failures.push(`${path} must contain ${needle}`);
  }
}

function requireMissing(path) {
  const fullPath = join(repoRoot, path);
  if (existsSync(fullPath)) {
    failures.push(`${path} should be removed after Phase 7 source cutover`);
  }
}

const files = {
  requestContext: requireFile('forward_part/gateway/domain/common/RequestContext.h'),
  result: requireFile('forward_part/gateway/domain/common/Result.h'),
  featureFlags: requireFile('forward_part/gateway/application/common/FeatureFlags.h'),
  featureFlagsCc: requireFile('forward_part/gateway/application/common/FeatureFlags.cc'),
  txRunner: requireFile('forward_part/gateway/application/common/TransactionRunner.h'),
  accountPorts: requireFile('forward_part/gateway/application/account/AccountPorts.h'),
  filePorts: requireFile('forward_part/gateway/application/file/FilePorts.h'),
  fileReadService: requireFile('forward_part/gateway/application/file/FileReadService.h'),
  fileDeleteService: requireFile('forward_part/gateway/application/file/FileDeleteService.h'),
  uploadState: requireFile('forward_part/gateway/application/upload/UploadStateMachine.h'),
  uploadPorts: requireFile('forward_part/gateway/application/upload/UploadPorts.h'),
  multipartUploadService: requireFile('forward_part/gateway/application/upload/MultipartUploadService.h'),
  runtime: requireFile('forward_part/gateway/infrastructure/runtime/CoreRuntime.h'),
  runtimeCc: requireFile('forward_part/gateway/infrastructure/runtime/CoreRuntime.cc'),
  mysqlRunner: requireFile('forward_part/gateway/infrastructure/mysql/MysqlTransactionRunner.h'),
  mysqlAccountRepository: requireFile('forward_part/gateway/infrastructure/mysql/MysqlAccountRepository.h'),
  mysqlAccountRepositoryCc: requireFile('forward_part/gateway/infrastructure/mysql/MysqlAccountRepository.cc'),
  userInfoService: requireFile('forward_part/gateway/application/account/UserInfoService.h'),
  signinService: requireFile('forward_part/gateway/application/account/SigninService.h'),
  legacyJwtIssuer: requireFile('forward_part/gateway/infrastructure/jwt/LegacyJwtIssuer.cc'),
  legacyPasswordVerifier: requireFile('forward_part/gateway/infrastructure/jwt/LegacyPasswordVerifier.cc'),
  registrationService: requireFile('forward_part/gateway/application/account/RegistrationService.h'),
  emailVerificationService: requireFile('forward_part/gateway/application/account/EmailVerificationCommandService.h'),
  outboxPort: requireFile('forward_part/gateway/application/common/OutboxPort.h'),
  mysqlOutboxRepository: requireFile('forward_part/gateway/infrastructure/outbox/MysqlOutboxRepository.cc'),
  redisPendingRegistrationRepository: requireFile('forward_part/gateway/infrastructure/redis/RedisPendingRegistrationRepository.cc'),
  mysqlFileRepository: requireFile('forward_part/gateway/infrastructure/mysql/MysqlFileRepository.cc'),
  mysqlUploadSessionRepository: requireFile('forward_part/gateway/infrastructure/mysql/MysqlUploadSessionRepository.cc'),
  grpcStorageControlAdapter: requireFile('forward_part/gateway/infrastructure/storage_control/GrpcStorageControlAdapter.cc'),
  storageControlGrpcCpp: requireFile('proto/storage_control/storage_control.grpc.pb.cc'),
  storageControlGrpcCppHeader: requireFile('proto/storage_control/storage_control.grpc.pb.h'),
  uploadSessionModel: requireFile('backword_part/model/upload_session.go'),
  uploadSessionMigration: requireFile('scripts/sql/upload_sessions_migration.sql'),
  storageControlProto: requireFile('proto/storage_control/storage_control.proto'),
  storageControlCpp: requireFile('proto/storage_control/storage_control.pb.cc'),
  storageControlCppHeader: requireFile('proto/storage_control/storage_control.pb.h'),
  storageControlPb: requireFile('clouddisk_v2/storage_control/protobuf/storage_control.pb.go'),
  storageControlGrpcPb: requireFile('clouddisk_v2/storage_control/protobuf/storage_control_grpc.pb.go'),
  storageControlService: requireFile('clouddisk_v2/storage_control/protobuf/storage_control.go'),
  storageControlMain: requireFile('backword_part/storage_control/main.go'),
  storageControlReadme: requireFile('backword_part/storage_control/README.md'),
  generateStorageControlProtoSh: requireFile('scripts/project_start_scripts/generate_storage_control_proto.sh'),
  generateStorageControlProtoPs1: requireFile('scripts/project_start_scripts/generate_storage_control_proto.ps1'),
  goDb: requireFile('internal/db.go'),
  outboxRelayDispatcher: requireFile('internal/outboxrelay/dispatcher.go'),
  outboxRelayMain: requireFile('backword_part/outbox_relay/main.go'),
  startAll: requireFile('scripts/project_start_scripts/start_all.sh'),
  status: requireFile('scripts/project_start_scripts/status.sh'),
  stopAll: requireFile('scripts/project_start_scripts/stop_all.sh'),
  emailKafkaConsumer: requireFile('other_srv/email_srv/KafkaConsumer.h'),
  storageWorkerMain: requireFile('other_srv/store_srv/main.go'),
  storageWorkerConsumer: requireFile('other_srv/store_srv/kafka/kafkacunsumer.go'),
  reconciliation: requireFile('other_srv/store_srv/reconciliation/reconciler.go'),
  reconciliationTests: requireFile('other_srv/store_srv/reconciliation/reconciler_test.go'),
  appData: requireFile('forward_part/gateway/MyAppData.h'),
  accountController: requireFile('forward_part/gateway/controllers/AccountController.cc'),
  fileController: requireFile('forward_part/gateway/controllers/FileController.cc'),
  healthHeader: requireFile('forward_part/gateway/controllers/HealthController.h'),
  healthCc: requireFile('forward_part/gateway/controllers/HealthController.cc'),
  main: requireFile('forward_part/gateway/main.cc'),
  cmake: requireFile('forward_part/gateway/CMakeLists.txt'),
  internalHeader: requireFile('forward_part/internal/internal.h'),
  internalCc: requireFile('forward_part/internal/internal.cpp'),
  goConsul: requireFile('internal/concul.go'),
  gatewayConfig: requireFile('forward_part/gateway/config.json'),
  nacosConfig: requireFile('scripts/nacos-config/clouddisk.json'),
  nginxConfig: requireFile('forward_part/config/nginx/nginx.conf'),
  openrestyRouteVerifier: requireFile('scripts/project_start_scripts/verify_openresty_core_routes.mjs'),
  mcpServer: requireFile('backword_part/mcp_server/main.go'),
  mcpTools: requireFile('backword_part/mcp_server/Tools.go'),
};

requireContains('RequestContext.h', files.requestContext, 'struct RequestContext');
requireContains('RequestContext.h', files.requestContext, 'requestId');
requireContains('RequestContext.h', files.requestContext, 'userId');
requireContains('RequestContext.h', files.requestContext, 'username');

requireContains('FeatureFlags.h', files.featureFlags, 'CoreFeatureFlags');
for (const envName of [
  'CORE_ACCOUNT_READS',
  'CORE_ACCOUNT_LOGIN',
  'CORE_ACCOUNT_REGISTRATION',
  'CORE_FILE_READS',
  'CORE_FILE_ACCESS',
  'CORE_UPLOAD_CONTROL',
  'CORE_UPLOAD_COMPLETE',
  'CORE_FILE_DELETE',
  'CORE_SMALL_UPLOAD_DIRECT',
  'CORE_OUTBOX_RELAY',
]) {
  requireContains('FeatureFlags.cc', files.featureFlagsCc, envName);
}

requireContains('TransactionRunner.h', files.txRunner, 'class TransactionRunner');
requireContains('AccountPorts.h', files.accountPorts, 'class AccountReadPort');
requireContains('UserInfoService.h', files.userInfoService, 'shouldUseCoreReadPath');
requireContains('SigninService.h', files.signinService, 'shouldUseCoreLoginPath');
requireContains('SigninService.h', files.signinService, 'verifyLegacyPassword');
requireContains('RegistrationService.h', files.registrationService, 'shouldUseCoreRegistrationPath');
requireContains('RegistrationService.h', files.registrationService, 'verifyCode');
requireContains('RegistrationService.h', files.registrationService, 'deletePendingRegistrationAndCode');
requireContains('EmailVerificationCommandService.h', files.emailVerificationService, 'EMAIL_VERIFICATION_REQUESTED');
requireContains('OutboxPort.h', files.outboxPort, 'struct OutboxMessage');
requireContains('FilePorts.h', files.filePorts, 'class FileReadPort');
requireContains('FilePorts.h', files.filePorts, 'FileAuthorizationPolicy');
requireContains('FilePorts.h', files.filePorts, 'class StorageControlPort');
requireContains('FileReadService.h', files.fileReadService, 'shouldUseCoreListPath');
requireContains('FileReadService.h', files.fileReadService, 'shouldUseCoreAccessPath');
requireContains('FileReadService.h', files.fileReadService, 'canReadObject');
requireContains('FileDeleteService.h', files.fileDeleteService, 'shouldUseCoreDeletePath');
requireContains('UploadStateMachine.h', files.uploadState, 'canTransition');
requireContains('UploadStateMachine.h', files.uploadState, 'uploadStateFromString');
requireContains('UploadPorts.h', files.uploadPorts, 'struct UploadSession');
requireContains('UploadPorts.h', files.uploadPorts, 'class UploadSessionPort');
requireContains('UploadPorts.h', files.uploadPorts, 'class MultipartStorageControlPort');
requireContains('MultipartUploadService.h', files.multipartUploadService, 'shouldUseCoreUploadControlPath');
requireContains('MultipartUploadService.h', files.multipartUploadService, 'saveInitiatedSession');
requireContains('MultipartUploadService.h', files.multipartUploadService, 'storageControl_.abort(ctx, *session');
requireContains('MultipartUploadService.h', files.multipartUploadService, 'loadOwnedSession');
requireContains('MultipartUploadService.h', files.multipartUploadService, 'complete(');
requireContains('MultipartUploadService.h', files.multipartUploadService, 'finalizeCompletedUpload');

requireContains('CoreRuntime.h', files.runtime, 'RuntimeSnapshot');
requireContains('CoreRuntime.cc', files.runtimeCc, 'DependencyStatus');
requireContains('CoreRuntime.cc', files.runtimeCc, 'mysql');
requireContains('CoreRuntime.cc', files.runtimeCc, 'redis');
requireContains('CoreRuntime.cc', files.runtimeCc, 'consul');
requireContains('CoreRuntime.cc', files.runtimeCc, 'storage_control');
requireContains('CoreRuntime.cc', files.runtimeCc, 'kafka');
requireContains('CoreRuntime.cc', files.runtimeCc, 'jwt');
requireContains('MysqlAccountRepository.h', files.mysqlAccountRepository, 'class MysqlAccountRepository');
requireContains('MysqlAccountRepository.cc', files.mysqlAccountRepositoryCc, 'select id, name, email, mobile, gender from accounts');
requireContains('MysqlAccountRepository.cc', files.mysqlAccountRepositoryCc, 'select id, name, password, salt from accounts');
requireContains('MysqlAccountRepository.cc', files.mysqlAccountRepositoryCc, 'insert into accounts');
requireContains('MysqlAccountRepository.cc', files.mysqlAccountRepositoryCc, 'execSqlAsync');
requireContains('LegacyJwtIssuer.cc', files.legacyJwtIssuer, 'set_issuer("Signin")');
requireContains('LegacyJwtIssuer.cc', files.legacyJwtIssuer, 'set_payload_claim("ID"');
requireContains('LegacyJwtIssuer.cc', files.legacyJwtIssuer, 'set_payload_claim("Name"');
requireContains('LegacyPasswordVerifier.cc', files.legacyPasswordVerifier, 'PKCS5_PBKDF2_HMAC');
requireContains('LegacyPasswordVerifier.cc', files.legacyPasswordVerifier, 'EVP_md5()');
requireContains('LegacyPasswordVerifier.cc', files.legacyPasswordVerifier, 'RAND_bytes');
requireContains('RedisPendingRegistrationRepository.cc', files.redisPendingRegistrationRepository, 'pending_reg:');
requireContains('RedisPendingRegistrationRepository.cc', files.redisPendingRegistrationRepository, 'SET %s %s EX 600');
requireContains('RedisPendingRegistrationRepository.cc', files.redisPendingRegistrationRepository, 'DEL %s %s');
requireContains('MysqlOutboxRepository.cc', files.mysqlOutboxRepository, 'insert into outboxes');
requireContains('KafkaConsumer.h', files.emailKafkaConsumer, 'extract_email_task_payload');
requireContains('MysqlFileRepository.cc', files.mysqlFileRepository, 'from user_files uf join files f');
requireContains('MysqlFileRepository.cc', files.mysqlFileRepository, 'where uf.account_id = ? and uf.deleted_at is null order by uf.id desc');
requireContains('MysqlFileRepository.cc', files.mysqlFileRepository, 'OBJECT_DELETE_REQUESTED');
requireContains('MysqlFileRepository.cc', files.mysqlFileRepository, 'delete from user_files');
requireContains('MysqlFileRepository.cc', files.mysqlFileRepository, 'insert into outboxes');
requireContains('store_srv kafkacunsumer.go', files.storageWorkerConsumer, 'processObjectDeleteRequested');
requireContains('store_srv kafkacunsumer.go', files.storageWorkerConsumer, 'ObjectDeleteRequested');
requireContains('store_srv kafkacunsumer.go', files.storageWorkerConsumer, 'MinIODeleteObject');
requireContains('reconciler.go', files.reconciliation, 'type Reconciler struct');
requireContains('reconciler.go', files.reconciliation, 'type Report struct');
requireContains('reconciler.go', files.reconciliation, 'PendingScanOlderThanWindow');
requireContains('reconciler.go', files.reconciliation, 'ExpiredOutboxLeases');
requireContains('reconciler.go', files.reconciliation, 'ExpiredInboxLeases');
requireContains('reconciler.go', files.reconciliation, 'MissingMetadataObjects');
requireContains('reconciler.go', files.reconciliation, 'OrphanStorageObjects');
requireContains('reconciler.go', files.reconciliation, 'ObjectStore interface');
requireContains('reconciler.go', files.reconciliation, 'ListObjectKeys');
requireContains('reconciler.go', files.reconciliation, 'RECONCILIATION_INTERVAL_SECONDS');
requireContains('reconciler_test.go', files.reconciliationTests, 'TestDefaultConfigUsesSafeDefaults');
requireContains('GrpcStorageControlAdapter.cc', files.grpcStorageControlAdapter, 'PresignGet');
requireContains('GrpcStorageControlAdapter.cc', files.grpcStorageControlAdapter, 'InitiateMultipart');
requireContains('GrpcStorageControlAdapter.cc', files.grpcStorageControlAdapter, 'CompleteMultipart');
requireContains('GrpcStorageControlAdapter.cc', files.grpcStorageControlAdapter, 'buildObjectKey');
requireContains('GrpcStorageControlAdapter.cc', files.grpcStorageControlAdapter, 'invalid part_number');
requireContains('storage_control.grpc.pb.cc', files.storageControlGrpcCpp, 'BlockingUnaryCall');
requireContains('storage_control.grpc.pb.h', files.storageControlGrpcCppHeader, 'class StorageControl final');
requireContains('MysqlUploadSessionRepository.cc', files.mysqlUploadSessionRepository, 'insert into upload_sessions');
requireContains('MysqlUploadSessionRepository.cc', files.mysqlUploadSessionRepository, 'loadOwnedSession');
requireContains('MysqlUploadSessionRepository.cc', files.mysqlUploadSessionRepository, 'owner_account_id=?');
requireContains('MysqlUploadSessionRepository.cc', files.mysqlUploadSessionRepository, 'finalizeCompletedUpload');
requireContains('MysqlUploadSessionRepository.cc', files.mysqlUploadSessionRepository, 'insert into files');
requireContains('MysqlUploadSessionRepository.cc', files.mysqlUploadSessionRepository, 'insert into user_files');
requireContains('MysqlUploadSessionRepository.cc', files.mysqlUploadSessionRepository, 'insert into outboxes');
requireContains('MysqlUploadSessionRepository.cc', files.mysqlUploadSessionRepository, 'scanOutboxPayload(ctx, session, fileID)');
requireContains('upload_session.go', files.uploadSessionModel, 'type UploadSession struct');
requireContains('upload_sessions_migration.sql', files.uploadSessionMigration, 'CREATE TABLE IF NOT EXISTS `upload_sessions`');
requireContains('storage_control.proto', files.storageControlProto, 'service StorageControl');
requireContains('storage_control.proto', files.storageControlProto, 'rpc CompleteMultipart');
requireContains('storage_control.proto', files.storageControlProto, 'rpc PresignGet');
requireContains('storage_control.proto', files.storageControlProto, 'storage_upload_id');
requireContains('storage_control.pb.cc', files.storageControlCpp, 'CompleteMultipartReq');
requireContains('storage_control.pb.h', files.storageControlCppHeader, 'class CompleteMultipartReq');
requireContains('storage_control.pb.go', files.storageControlPb, 'type CompleteMultipartReq struct');
requireContains('storage_control_grpc.pb.go', files.storageControlGrpcPb, 'RegisterStorageControlServer');
requireContains('storage_control.go', files.storageControlService, 'type StorageControlService struct');
requireContains('storage_control.go', files.storageControlService, 'requireMinIOClient');
requireContains('storage_control main.go', files.storageControlMain, 'RegisterStorageControlServer');
requireContains('storage_control main.go', files.storageControlMain, 'storage_control');
requireContains('storage_control README', files.storageControlReadme, 'must not decide user identity');
requireContains('generate_storage_control_proto.sh', files.generateStorageControlProtoSh, 'protoc --go_out=. --go-grpc_out=.');
requireContains('generate_storage_control_proto.ps1', files.generateStorageControlProtoPs1, 'protoc --go_out=. --go-grpc_out=.');
requireContains('internal/db.go', files.goDb, 'model.UploadSession{}');
requireContains('internal/outboxrelay/dispatcher.go', files.outboxRelayDispatcher, 'SKIP LOCKED');
requireContains('internal/outboxrelay/dispatcher.go', files.outboxRelayDispatcher, 'markSent');
requireContains('internal/outboxrelay/dispatcher.go', files.outboxRelayDispatcher, 'markFailed');
requireContains('outbox_relay main.go', files.outboxRelayMain, 'outboxrelay.NewDispatcher');
requireContains('start_all.sh', files.startAll, 'go run ./backword_part/storage_control/');
requireContains('start_all.sh', files.startAll, 'go run ./backword_part/outbox_relay/');
requireContains('status.sh', files.status, 'check_service "storage_control"');
requireContains('status.sh', files.status, 'check_service "outbox_relay"');
requireContains('stop_all.sh', files.stopAll, 'stop_service "storage_control"');
requireContains('stop_all.sh', files.stopAll, 'stop_service "outbox_relay"');

requireMissing('forward_part/gateway/infrastructure/legacy/LegacyAdapters.h');

requireContains('MyAppData.h', files.appData, 'mysqlClient');
requireContains('AccountController.cc', files.accountController, 'runtimeSnapshot.flags.accountReads');
requireContains('AccountController.cc', files.accountController, 'runtimeSnapshot.flags.accountLogin');
requireContains('AccountController.cc', files.accountController, 'runtimeSnapshot.flags.accountRegistration');
requireContains('AccountController.cc', files.accountController, 'legacy account adapter removed');
requireContains('AccountController.cc', files.accountController, 'SigninService');
requireContains('AccountController.cc', files.accountController, 'RegistrationService');
requireContains('AccountController.cc', files.accountController, 'EmailVerificationCommandService');
requireContains('AccountController.cc', files.accountController, 'MysqlOutboxRepository');
requireContains('AccountController.cc', files.accountController, 'RedisPendingRegistrationRepository');
requireContains('AccountController.cc', files.accountController, 'MysqlAccountRepository');
requireContains('AccountController.cc', files.accountController, 'MyAppData::instance().mysqlClient');
requireContains('FileController.cc', files.fileController, 'runtimeSnapshot.flags.uploadControl');
requireContains('FileController.cc', files.fileController, 'runtimeSnapshot.flags.uploadComplete');
requireContains('FileController.cc', files.fileController, 'runtimeSnapshot.flags.fileDelete');
requireContains('FileController.cc', files.fileController, 'legacy_file_adapter_removed');
requireContains('FileController.cc', files.fileController, 'small_upload_proxy_removed');
requireContains('FileController.cc', files.fileController, 'part_proxy_removed');
requireContains('FileController.cc', files.fileController, 'FileDeleteService');
requireContains('FileController.cc', files.fileController, 'MultipartUploadService');
requireContains('FileController.cc', files.fileController, 'MysqlUploadSessionRepository');
requireContains('FileController.cc', files.fileController, 'FindStorageControl("storage_control")');
requireContains('FileController.cc', files.fileController, 'GrpcStorageControlAdapter');
requireContains('store_srv main.go', files.storageWorkerMain, 'reconciliation.New');
requireContains('store_srv main.go', files.storageWorkerMain, 'ListObjects');

requireContains('HealthController.h', files.healthHeader, '"/ready"');
requireContains('HealthController.cc', files.healthCc, 'CoreRuntime::instance().snapshot()');
requireContains('HealthController.cc', files.healthCc, 'k503ServiceUnavailable');
requireContains('main.cc', files.main, 'CoreRuntime::instance().initialize');
requireContains('main.cc', files.main, 'DbClient::newMysqlClient');
requireContains('main.cc', files.main, 'MyAppData::instance().mysqlClient');

requireContains('CMakeLists.txt', files.cmake, 'application/common APP_COMMON_SRC');
requireContains('CMakeLists.txt', files.cmake, 'infrastructure/runtime INFRA_RUNTIME_SRC');
requireContains('CMakeLists.txt', files.cmake, 'infrastructure/mysql INFRA_MYSQL_SRC');
requireContains('CMakeLists.txt', files.cmake, 'infrastructure/jwt INFRA_JWT_SRC');
requireContains('CMakeLists.txt', files.cmake, 'infrastructure/redis INFRA_REDIS_SRC');
requireContains('CMakeLists.txt', files.cmake, 'infrastructure/outbox INFRA_OUTBOX_SRC');
requireContains('CMakeLists.txt', files.cmake, 'infrastructure/storage_control INFRA_STORAGE_CONTROL_SRC');
requireContains('CMakeLists.txt', files.cmake, 'STORAGE_CONTROL_PROTO_SRCS');
requireContains('CMakeLists.txt', files.cmake, 'OpenSSL::Crypto');

requireContains('internal.h', files.internalHeader, 'configSource');
requireContains('internal.h', files.internalHeader, 'configVersion');
requireContains('internal.h', files.internalHeader, 'database');
requireContains('internal.h', files.internalHeader, 'storage_control');
requireContains('internal.cpp', files.internalCc, 'loadedPath');
requireContains('internal.cpp', files.internalCc, 'storage_control');
requireContains('internal/concul.go', files.goConsul, 'StorageControl');
requireContains('mcp_server main.go', files.mcpServer, 'storage_control');
requireContains('mcp_server Tools.go', files.mcpTools, 'storageControl.PresignGet');
requireContains('gateway config.json', files.gatewayConfig, '"storage_control"');
requireContains('nacos clouddisk.json', files.nacosConfig, '"storage_control"');
requireContains('nginx.conf', files.nginxConfig, '^/(user/(signin|signup|info|token/blacklist|sendcode|code)|file/');
requireContains('verify_openresty_core_routes.mjs', files.openrestyRouteVerifier, 'OpenResty Core route verification passed');

if (failures.length > 0) {
  console.error('Phase 1 gateway skeleton verification failed:');
  for (const failure of failures) {
    console.error(`- ${failure}`);
  }
  process.exit(1);
}

console.log('Phase 1 gateway skeleton verification passed.');
