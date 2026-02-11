
export interface UserInfo {
  username: string;
  email: string;
  message?: string;
}

export interface FileItem {
  filename: string;
  filehash: string;
  filesize: number; // Changed from file_size to match backend 'filesize'
  updated_at?: string;
  content_type?: string;
}

export interface QueryResponse {
  status: string;
  message: string;
  filelist: FileItem[];
}

export interface UploadStatusResponse {
  total_parts: number;
  uploaded_parts: number[];
}

export interface MultipartInitResponse {
  upload_id: string;
  object_key: string;
  part_size: number;
  total_parts: number;
}

export type AuthMode = 'signin' | 'signup' | 'verify';

export interface UploadTask {
  id: string;
  filename: string;
  size: number;
  progress: number;
  status: 'pending' | 'uploading' | 'completed' | 'error';
  error?: string;
}
