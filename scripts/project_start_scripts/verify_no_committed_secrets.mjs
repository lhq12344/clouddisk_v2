import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { join, relative } from 'node:path';

const repoRoot = process.cwd();
const failures = [];

const skippedDirs = new Set([
  '.git',
  '.cache',
  'build',
  'node_modules',
  'dist',
  'baseline-results',
]);

const textExtensions = new Set([
  '.cc', '.cpp', '.c', '.h', '.hpp', '.go', '.js', '.mjs', '.ts', '.tsx', '.json', '.md', '.txt', '.sh', '.ps1', '.yaml', '.yml', '.sql', '.conf', '.toml', '.cmake', '.gitignore', '.proto'
]);
const removedSmtpAppPassword = ['romcmq', 'sisbxg', 'jjef'].join('');

function extensionOf(path) {
  const name = path.split(/[\\/]/).pop() ?? '';
  const dot = name.lastIndexOf('.');
  return dot >= 0 ? name.slice(dot) : name;
}

function walk(dir, files = []) {
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    if (entry.isDirectory()) {
      if (skippedDirs.has(entry.name)) {
        continue;
      }
      walk(join(dir, entry.name), files);
      continue;
    }
    if (!entry.isFile()) {
      continue;
    }
    const full = join(dir, entry.name);
    const rel = relative(repoRoot, full).replaceAll('\\', '/');
    if (textExtensions.has(extensionOf(rel))) {
      files.push({ rel, full });
    }
  }
  return files;
}

function readJson(path) {
  const full = join(repoRoot, path);
  if (!existsSync(full)) {
    failures.push(`${path}: missing file`);
    return null;
  }
  try {
    return JSON.parse(readFileSync(full, 'utf8'));
  } catch (err) {
    failures.push(`${path}: invalid JSON: ${err.message}`);
    return null;
  }
}

for (const { rel, full } of walk(repoRoot)) {
  const source = readFileSync(full, 'utf8');
  if (source.includes(removedSmtpAppPassword)) {
    failures.push(`${rel}: contains removed SMTP app password`);
  }
  if (/-----BEGIN (RSA |OPENSSH |EC |DSA |)?PRIVATE KEY-----/.test(source)) {
    failures.push(`${rel}: contains a private key block`);
  }
  if (/\bAKIA[0-9A-Z]{16}\b/.test(source)) {
    failures.push(`${rel}: contains AWS-style access key id`);
  }
  if (/LOG_\w+\([^\n]*(password|pass|secret|access_key_secret|signing_key)/i.test(source)) {
    failures.push(`${rel}: appears to log a secret-bearing value`);
  }
}

const emailConfig = readJson('other_srv/email_srv/email_config.json');
if (emailConfig?.smtp?.pass !== '') {
  failures.push('other_srv/email_srv/email_config.json: smtp.pass must stay empty; use EMAIL_SMTP_PASS at runtime');
}

const nacosConfig = readJson('scripts/nacos-config/clouddisk.json');
if (nacosConfig?.alioss?.access_key_secret && nacosConfig.alioss.access_key_secret !== 'your_access_key_secret') {
  failures.push('scripts/nacos-config/clouddisk.json: alioss.access_key_secret must be a placeholder in committed config');
}

if (failures.length > 0) {
  console.error('Committed secret verification failed:');
  for (const failure of failures) {
    console.error(`- ${failure}`);
  }
  process.exit(1);
}

console.log('Committed secret verification passed.');
