import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const repoRoot = process.cwd();
const apiSource = readFileSync(join(repoRoot, 'forward_part/static/services/api.ts'), 'utf8');
const typeSource = readFileSync(join(repoRoot, 'forward_part/static/types.ts'), 'utf8');
const appSource = readFileSync(join(repoRoot, 'forward_part/static/App.tsx'), 'utf8');
const authPageSource = readFileSync(join(repoRoot, 'forward_part/static/components/AuthPage.tsx'), 'utf8');
const uploadSource = readFileSync(join(repoRoot, 'forward_part/static/components/UploadManager.tsx'), 'utf8');
const accountRoutes = readFileSync(join(repoRoot, 'forward_part/gateway/controllers/AccountController.h'), 'utf8');
const fileRoutes = readFileSync(join(repoRoot, 'forward_part/gateway/controllers/FileController.h'), 'utf8');
const frontendContractSource = [apiSource, typeSource, appSource, authPageSource, uploadSource].join('\n');

const activeFrontendEndpoints = [
  { name: 'signup', method: 'POST', path: '/user/signup', auth: false, body: ['username', 'password', 'email'] },
  { name: 'signin', method: 'POST', path: '/user/signin', auth: false, body: ['username', 'password'], response: ['token'] },
  { name: 'sendCode', method: 'POST', path: '/user/sendcode', auth: false, body: ['email'] },
  { name: 'verifyCode', method: 'POST', path: '/user/code', auth: false, body: ['email', 'code'] },
  { name: 'getUserInfo', method: 'GET', path: '/user/info', auth: true },
  { name: 'logout', method: 'POST', path: '/user/token/blacklist', auth: true, body: ['token'] },
  { name: 'queryFiles', method: 'POST', path: '/file/query', auth: true },
  { name: 'getDownloadUrl', method: 'POST', path: '/file/download', auth: true, body: ['filename', 'filehash', 'file_size'], response: ['download_url'] },
  { name: 'deleteFile', method: 'POST', path: '/file/delete', auth: true, body: ['filename', 'filehash'] },
  { name: 'getPreviewUrl', method: 'POST', path: '/file/showfile', auth: true, body: ['filename', 'filehash', 'file_size'] },
  { name: 'initMultipart', method: 'POST', path: '/file/initupload', auth: true, body: ['file_name', 'file_hash', 'file_size', 'content_type'], response: ['upload_id', 'object_key', 'part_size', 'total_parts', 'status', 'message'] },
  { name: 'presignParts', method: 'POST', path: '/file/PresignParts', auth: true, body: ['upload_id', 'part_numbers'], response: ['parts'] },
  { name: 'completeMultipart', method: 'POST', path: '/file/CompleteMultipart', auth: true, body: ['upload_id'], response: ['upload_id', 'object_key', 'etag', 'status'] },
  { name: 'abortMultipart', method: 'POST', path: '/file/AbortMultipart', auth: true, body: ['upload_id'] },
  { name: 'getMultipartStatus', method: 'POST', path: '/file/Status', auth: true, body: ['upload_id'], response: ['total_parts', 'uploaded_parts'] },
];

const compatibilityRoutes = [
  { name: 'smallUploadRemoved', path: '/file/upload', auth: true, marker: 'small_upload_proxy_removed' },
  { name: 'uploadPresignedPartProxyRemoved', path: '/file/uploadpart', auth: true, marker: 'part_proxy_removed' },
];

const failures = [];

for (const endpoint of activeFrontendEndpoints) {
  if (!apiSource.includes(endpoint.path)) {
    failures.push(`${endpoint.name}: frontend API path ${endpoint.path} is missing`);
  }
  if (!apiSource.includes(`method: '${endpoint.method}'`) && !apiSource.includes(`method: "${endpoint.method}"`)) {
    failures.push(`${endpoint.name}: frontend API method ${endpoint.method} is missing`);
  }
  for (const field of endpoint.body ?? []) {
    if (!frontendContractSource.includes(field)) {
      failures.push(`${endpoint.name}: request field ${field} is missing from frontend contract`);
    }
  }
  for (const header of endpoint.headers ?? []) {
    if (!frontendContractSource.includes(header)) {
      failures.push(`${endpoint.name}: request header ${header} is missing from frontend contract`);
    }
  }
  for (const responseField of endpoint.response ?? []) {
    if (!frontendContractSource.includes(responseField)) {
      failures.push(`${endpoint.name}: response field ${responseField} is not referenced by frontend contract`);
    }
  }
}

const routeSources = `${accountRoutes}\n${fileRoutes}`;
for (const endpoint of [...activeFrontendEndpoints, ...compatibilityRoutes]) {
  if (!routeSources.includes(`"${endpoint.path}"`)) {
    failures.push(`${endpoint.name}: Drogon route ${endpoint.path} is missing`);
  }
  if (endpoint.auth) {
    const routeLine = routeSources.split('\n').find((line) => line.includes(`"${endpoint.path}"`)) ?? '';
    if (!routeLine.includes('jwt_decode')) {
      failures.push(`${endpoint.name}: Drogon route ${endpoint.path} must keep jwt_decode auth filter`);
    }
  }
}

for (const route of compatibilityRoutes) {
  if (!fileRoutes.includes(route.marker)) {
    const controllerSource = readFileSync(join(repoRoot, 'forward_part/gateway/controllers/FileController.cc'), 'utf8');
    if (!controllerSource.includes(route.marker)) {
      failures.push(`${route.name}: compatibility route must return ${route.marker}`);
    }
  }
}

if (failures.length > 0) {
  console.error('Core HTTP contract verification failed:');
  for (const failure of failures) {
    console.error(`- ${failure}`);
  }
  process.exit(1);
}

console.log(`Core HTTP contract verification passed for ${activeFrontendEndpoints.length} active endpoints and ${compatibilityRoutes.length} compatibility routes.`);
