import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const repoRoot = process.cwd();
const nginxConf = readFileSync(join(repoRoot, 'forward_part/config/nginx/nginx.conf'), 'utf8');
const failures = [];

const requiredFragments = [
  '^/file/(upload|uploadpart)$',
  'proxy_request_buffering off;',
  'proxy_buffering off;',
  'user/(signin|signup|info|token/blacklist|sendcode|code)',
  'file/(query|download|showfile|delete|initupload|PresignParts|CompleteMultipart|AbortMultipart|Status)',
  '|AI)',
  'proxy_set_header Host $host;',
  'proxy_set_header X-Real-IP $remote_addr;',
  'proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;',
];

for (const fragment of requiredFragments) {
  if (!nginxConf.includes(fragment)) {
    failures.push(`nginx.conf missing route/proxy fragment: ${fragment}`);
  }
}

const proxyPassCount = (nginxConf.match(/proxy_pass http:\/\/cloudisk_backend;/g) || []).length;
if (proxyPassCount !== 2) {
  failures.push(`expected exactly 2 cloudisk_backend proxy_pass blocks after route consolidation, found ${proxyPassCount}`);
}

for (const removedExactRoute of [
  'location = /user/signin',
  'location = /file/query',
  'location = /file/CompleteMultipart',
]) {
  if (nginxConf.includes(removedExactRoute)) {
    failures.push(`nginx.conf still contains duplicated exact route: ${removedExactRoute}`);
  }
}

if (failures.length > 0) {
  console.error('OpenResty Core route verification failed:');
  for (const failure of failures) {
    console.error(`- ${failure}`);
  }
  process.exit(1);
}

console.log('OpenResty Core route verification passed.');
