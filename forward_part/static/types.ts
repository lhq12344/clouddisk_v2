
export interface UserInfo {
  username: string;
  email?: string;
  name?: string;
  mobile?: string;
  gender?: string;
  createdAt?: string;
  updatedAt?: string;
  message?: string;
}

export interface FileItem {
  filename: string;
  filehash: string;
  filesize: number;
  updated_at?: string;
  status?: string;
  scan_detail?: string;
  content_type?: string;
}

export interface QueryResponse {
  status: number | string;
  message: string;
  filelist: FileItem[];
}

export interface UploadStatusResponse {
  total_parts: number;
  uploaded_parts: number[];
}

export interface MultipartInitResponse {
  upload_id?: string;
  object_key: string;
  part_size?: number;
  total_parts?: number;
  status?: string;
  message?: string;
}

export interface PresignedPart {
  part_number: number;
  url: string;
  expires_at: string;
}

export interface PresignPartsResponse {
  upload_id: string;
  parts: PresignedPart[];
}

export interface UploadResponse {
  file_hash: string;
  object_key: string;
  status: string;
  message?: string;
}

export interface CompleteMultipartResponse {
  upload_id: string;
  object_key: string;
  etag: string;
  status: string;
}

export type AuthMode = 'signin' | 'signup' | 'verify';

export interface UploadTask {
  id: string;
  filename: string;
  fileHash?: string;
  size: number;
  progress: number;
  status: string;
  error?: string;
  detail?: string;
}

export interface ApiErrorBody {
  error?: string;
  details?: string;
  message?: string;
  status?: string;
  object_key?: string;
  file_hash?: string;
}
