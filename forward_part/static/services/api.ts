
import { 
  QueryResponse, 
  MultipartInitResponse, 
  UploadStatusResponse, 
  FileItem 
} from '../types';


const BASE_URL = '';  // 使用相对路径，自动适配当前访问地址


class ApiService {
  private getToken(): string | null {
    return localStorage.getItem('oss_token');
  }

  private async request(endpoint: string, options: RequestInit = {}) {
    const token = this.getToken();
    const headers = new Headers(options.headers || {});
    
    if (token) {
      headers.set('Authorization', `Bearer ${token}`);
    }

    if (options.body && typeof options.body === 'string' && !headers.has('Content-Type')) {
      headers.set('Content-Type', 'application/json');
    }

    // 生成 X-Request-Id 用于全链路追踪
    const requestId = crypto.randomUUID();
    headers.set('X-Request-Id', requestId);

    const fullUrl = `${BASE_URL}${endpoint}`;
    console.debug(`[NETWORK_REQUEST][${requestId}]: ${options.method || 'GET'} -> ${fullUrl}`);

    try {
      const response = await fetch(fullUrl, {
        ...options,
        headers
      });

      if (!response.ok) {
        const errorText = await response.text();
        throw new Error(`[STATUS_${response.status}]: ${errorText || 'Endpoint unreachable'}`);
      }

      const responseText = await response.text();
      return responseText ? JSON.parse(responseText) : null;
    } catch (e: any) {
      console.error(`[COMMUNICATION_FAILURE]: Failed to sync with node ${endpoint}`, e);
      throw e;
    }
  }

  async signup(data: any) {
    return this.request('/user/signup', { method: 'POST', body: JSON.stringify(data) });
  }

  async signin(data: any) {
    const res = await this.request('/user/signin', { method: 'POST', body: JSON.stringify(data) });
    if (res && res.token) localStorage.setItem('oss_token', res.token);
    return res;
  }

  async sendCode(email: string) {
    return this.request('/user/sendcode', { method: 'POST', body: JSON.stringify({ email }) });
  }

  async verifyCode(email: string, code: string) {
    return this.request('/user/code', { method: 'POST', body: JSON.stringify({ email, code }) });
  }

  async getUserInfo() {
    return this.request('/user/info', { method: 'GET' });
  }

  async logout() {
    const token = this.getToken();
    if (token) {
      try {
        await this.request('/user/token/blacklist', { method: 'POST', body: JSON.stringify({ token }) });
      } catch (e) {
        console.warn('Logout blacklist failed', e);
      }
    }
    localStorage.removeItem('oss_token');
  }

  async queryFiles(): Promise<QueryResponse> {
    return this.request('/file/query', { method: 'POST' });
  }

  async downloadFile(file: FileItem) {
    const res = await this.request('/file/download', { 
      method: 'POST', 
      body: JSON.stringify({ 
        filename: file.filename, 
        filehash: file.filehash, 
        file_size: file.filesize 
      }) 
    });
    if (res && res.download_url) {
      window.location.href = res.download_url;
    }
    return res;
  }

  // 确认已绑定 /file/delete 路径
  async deleteFile(file: FileItem) {
    return this.request('/file/delete', {
      method: 'POST',
      body: JSON.stringify({
        filename: file.filename,
        filehash: file.filehash
      })
    });
  }

  async getPreviewUrl(file: FileItem) {
    return this.request('/file/showfile', { 
      method: 'POST', 
      body: JSON.stringify({ 
        filename: file.filename, 
        filehash: file.filehash, 
        file_size: file.filesize 
      }) 
    });
  }

  async simpleUpload(filename: string, content: string) {
    return this.request('/file/upload', { 
      method: 'POST', 
      body: JSON.stringify({ filename, content }) 
    });
  }

  async initMultipart(file_name: string, file_hash: string, file_size: number, content_type?: string): Promise<MultipartInitResponse> {
    return this.request('/file/initupload', {
      method: 'POST',
      body: JSON.stringify({ file_name, file_hash, file_size, content_type })
    });
  }

  async uploadPart(uploadId: string, partNumber: number, body: ArrayBuffer, chunkHash: string) {
    return this.request('/file/uploadpart', {
      method: 'POST',
      headers: {
        'X-Upload-Id': uploadId,
        'X-Part-Number': partNumber.toString(),
        'X-Chunk-Hash': chunkHash,
        'X-Part-Size': body.byteLength.toString(),
        'Content-Type': 'application/octet-stream'
      },
      body
    });
  }

  async completeMultipart(upload_id: string) {
    return this.request('/file/CompleteMultipart', {
      method: 'POST',
      body: JSON.stringify({ upload_id })
    });
  }

  async abortMultipart(upload_id: string) {
    return this.request('/file/AbortMultipart', {
      method: 'POST',
      body: JSON.stringify({ upload_id })
    });
  }

  async getMultipartStatus(upload_id: string): Promise<UploadStatusResponse> {
    return this.request('/file/Status', {
      method: 'POST',
      body: JSON.stringify({ upload_id })
    });
  }
}

export const api = new ApiService();
