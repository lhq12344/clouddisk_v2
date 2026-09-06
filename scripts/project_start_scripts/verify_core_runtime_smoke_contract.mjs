import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const repoRoot = process.cwd();
const smoke = readFileSync(join(repoRoot, 'scripts/project_start_scripts/verify_core_runtime_smoke.ps1'), 'utf8');
const readme = readFileSync(join(repoRoot, 'scripts/project_start_scripts/SCRIPTS_README.md'), 'utf8');
const audit = readFileSync(join(repoRoot, 'docs/contracts/core-migration-completion-audit.md'), 'utf8');

const failures = [];

function requireContains(path, source, needle, detail = `missing ${needle}`) {
  if (!source.includes(needle)) {
    failures.push(`${path}: ${detail}`);
  }
}

function requireRegex(path, source, pattern, detail = `missing pattern ${pattern}`) {
  if (!pattern.test(source)) {
    failures.push(`${path}: ${detail}`);
  }
}

const requiredSmokeEndpoints = [
  { name: 'health', path: '/health', method: 'GET' },
  { name: 'ready', path: '/ready', method: 'GET' },
  { name: 'signin', path: '/user/signin', method: 'POST' },
  { name: 'file query', path: '/file/query', method: 'POST' },
  { name: 'init multipart', path: '/file/initupload', method: 'POST' },
  { name: 'presign parts', path: '/file/PresignParts', method: 'POST' },
  { name: 'multipart status', path: '/file/Status', method: 'POST' },
  { name: 'complete multipart', path: '/file/CompleteMultipart', method: 'POST' },
  { name: 'download', path: '/file/download', method: 'POST' },
  { name: 'preview', path: '/file/showfile', method: 'POST' },
  { name: 'delete', path: '/file/delete', method: 'POST' },
];

for (const endpoint of requiredSmokeEndpoints) {
  requireContains('verify_core_runtime_smoke.ps1', smoke, `-Path '${endpoint.path}'`, `${endpoint.name} smoke path ${endpoint.path} is missing`);
  requireContains('verify_core_runtime_smoke.ps1', smoke, `-Method ${endpoint.method}`, `${endpoint.name} smoke method ${endpoint.method} is missing`);
}

for (const removedProxy of [
  { path: '/file/upload', marker: 'small_upload_proxy_removed' },
  { path: '/file/uploadpart', marker: 'part_proxy_removed' },
]) {
  requireContains('verify_core_runtime_smoke.ps1', smoke, `-Path '${removedProxy.path}'`, `fail-closed check for ${removedProxy.path} is missing`);
  requireContains('verify_core_runtime_smoke.ps1', smoke, "-ExpectedStatus @(503)", 'legacy upload proxy checks must expect 503');
  requireContains('verify_core_runtime_smoke.ps1', smoke, removedProxy.marker, `fail-closed marker ${removedProxy.marker} is missing`);
}

for (const field of [
  'upload_id',
  'part_size',
  'total_parts',
  'uploaded_parts',
  'object_key',
  'pending_scan',
  'ready',
  'success',
]) {
  requireContains('verify_core_runtime_smoke.ps1', smoke, field, `runtime smoke must validate/record ${field}`);
}

for (const switchName of ['RequireJwt', 'RequireOtherJwt', 'WaitForScanSuccess', 'NoCleanup']) {
  requireContains('verify_core_runtime_smoke.ps1', smoke, `[switch]$${switchName}`, `runtime smoke must keep -${switchName}`);
}

for (const authToken of ['OTHER_JWT', 'SMOKE_OTHER_SIGNIN_USERNAME', 'SMOKE_OTHER_SIGNIN_PASSWORD']) {
  requireContains('verify_core_runtime_smoke.ps1', smoke, authToken, `runtime smoke must support ${authToken} for cross-user authorization checks`);
}

for (const deniedStep of [
  'other user upload status denied',
  'other user presign parts denied',
  'other user complete upload denied',
  'other user abort upload denied',
  'other-user-upload-session-denied',
  'other user download denied',
  'other user preview denied',
  'other user delete denied',
  'other-user-authorization-denied',
]) {
  requireContains('verify_core_runtime_smoke.ps1', smoke, deniedStep, `runtime smoke must include ${deniedStep}`);
}

requireContains('verify_core_runtime_smoke.ps1', smoke, 'Upload-PartBytes', 'runtime smoke must exercise direct presigned PUT data plane');
requireContains('verify_core_runtime_smoke.ps1', smoke, 'summary.json', 'runtime smoke must write summary.json evidence');
requireContains('verify_core_runtime_smoke.ps1', smoke, 'Find-FileInQuery', 'runtime smoke must verify uploaded file appears in query results');
requireContains('verify_core_runtime_smoke.ps1', smoke, 'if ($RequireJwt) { throw $message }', 'runtime smoke must fail instead of skipping authenticated checks when -RequireJwt is set');
requireContains('verify_core_runtime_smoke.ps1', smoke, 'if (-not $OtherJwt -and $RequireOtherJwt)', 'runtime smoke must fail instead of skipping cross-user checks when -RequireOtherJwt is set');
requireContains('verify_core_runtime_smoke.ps1', smoke, "step = 'authenticated-smoke'; status = 'skipped'", 'runtime smoke must explicitly record skipped authenticated checks when JWT is optional');
requireRegex('verify_core_runtime_smoke.ps1', smoke, /if \(\$WaitForScanSuccess\)[\s\S]*download presign after scan[\s\S]*preview presign after scan/, 'scan-success branch must verify download and preview presigns');
requireRegex('verify_core_runtime_smoke.ps1', smoke, /if \(-not \$NoCleanup[\s\S]*delete uploaded file relation/, 'cleanup branch must delete the smoke file relation unless disabled or pending scan');

requireContains('SCRIPTS_README.md', readme, 'verify_core_runtime_smoke.ps1', 'README must document runtime smoke usage');
requireContains('SCRIPTS_README.md', readme, '-WaitForScanSuccess', 'README must document scan-success smoke mode');
requireContains('core-migration-completion-audit.md', audit, 'verify_core_runtime_smoke.ps1', 'completion audit must cite runtime smoke gate');

if (failures.length > 0) {
  console.error('Core runtime smoke contract verification failed:');
  for (const failure of failures) {
    console.error(`- ${failure}`);
  }
  process.exit(1);
}

console.log(`Core runtime smoke contract verification passed for ${requiredSmokeEndpoints.length} runtime endpoints and 2 fail-closed compatibility routes.`);
