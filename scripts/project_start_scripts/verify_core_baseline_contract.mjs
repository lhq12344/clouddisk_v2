import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const repoRoot = process.cwd();
const psBaseline = readFileSync(join(repoRoot, 'scripts/project_start_scripts/core_api_baseline.ps1'), 'utf8');
const shBaseline = readFileSync(join(repoRoot, 'scripts/project_start_scripts/core_api_baseline.sh'), 'utf8');
const runbook = readFileSync(join(repoRoot, 'docs/contracts/core-baseline-runbook.md'), 'utf8');
const audit = readFileSync(join(repoRoot, 'docs/contracts/core-migration-completion-audit.md'), 'utf8');

const failures = [];

function requireContains(path, source, needle, detail = `missing ${needle}`) {
  if (!source.includes(needle)) {
    failures.push(`${path}: ${detail}`);
  }
}

for (const [path, source] of [
  ['core_api_baseline.ps1', psBaseline],
  ['core_api_baseline.sh', shBaseline],
]) {
  requireContains(path, source, '/health', 'baseline must probe health');
  requireContains(path, source, '/user/info', 'baseline must cover authenticated account metadata endpoint');
  requireContains(path, source, '/file/query', 'baseline must cover file metadata listing endpoint');
  requireContains(path, source, '/file/initupload', 'baseline must cover upload control init endpoint');
  requireContains(path, source, '/file/PresignParts', 'baseline must cover presign control endpoint');
  requireContains(path, source, '/file/CompleteMultipart', 'baseline must cover complete control endpoint');
  requireContains(path, source, 'JWT', 'baseline must support authenticated probes with JWT');
  requireContains(path, source, 'SMOKE_SIGNIN_USERNAME', 'baseline must support auto-login username');
  requireContains(path, source, 'SMOKE_SIGNIN_PASSWORD', 'baseline must support auto-login password');
}

for (const metric of ['p50', 'p90', 'p99']) {
  requireContains('core_api_baseline.ps1', psBaseline, metric, `PowerShell baseline must emit ${metric}`);
  requireContains('core-baseline-runbook.md', runbook, metric, `baseline runbook must document ${metric}`);
}

requireContains('core_api_baseline.ps1', psBaseline, 'RESULT_DIR', 'PowerShell baseline must support RESULT_DIR');
requireContains('core_api_baseline.sh', shBaseline, 'RESULT_DIR', 'Bash baseline must support RESULT_DIR');
requireContains('core-baseline-runbook.md', runbook, 'core_api_baseline.ps1', 'runbook must document PowerShell baseline');
requireContains('core-baseline-runbook.md', runbook, 'core_api_baseline.sh', 'runbook must document Bash baseline');
requireContains('core-migration-completion-audit.md', audit, 'core_api_baseline.ps1', 'completion audit must cite baseline runners');

if (failures.length > 0) {
  console.error('Core baseline contract verification failed:');
  for (const failure of failures) {
    console.error(`- ${failure}`);
  }
  process.exit(1);
}

console.log('Core baseline contract verification passed for PowerShell and Bash baseline runners.');
