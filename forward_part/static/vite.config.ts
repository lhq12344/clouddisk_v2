import path from 'path';
import { defineConfig, loadEnv } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig(({ mode }) => {
    const env = loadEnv(mode, '.', '');
    const apiProxyTarget = env.VITE_API_PROXY_TARGET || 'http://127.0.0.1:38080';
    return {
      server: {
        port: 3000,
        host: '0.0.0.0',
        proxy: {
          '/user': {
            target: apiProxyTarget,
            changeOrigin: true,
          },
          '/file': {
            target: apiProxyTarget,
            changeOrigin: true,
          },
          '/AI': {
            target: apiProxyTarget,
            changeOrigin: true,
          },
          '/mcp': {
            target: apiProxyTarget,
            changeOrigin: true,
          },
          // Proxy MinIO presigned upload URLs through Vite dev server
          // to avoid system proxy (127.0.0.1:7890) intercepting NodePort traffic.
          // changeOrigin: false preserves the original Host header for MinIO signature validation.
          '/clouddisk': {
            target: 'http://127.0.0.1:30900',
            changeOrigin: true,
          },
        },
      },
      plugins: [react()],
      define: {
        'process.env.API_KEY': JSON.stringify(env.GEMINI_API_KEY),
        'process.env.GEMINI_API_KEY': JSON.stringify(env.GEMINI_API_KEY)
      },
      resolve: {
        alias: {
          '@': path.resolve(__dirname, '.'),
        }
      }
    };
});
