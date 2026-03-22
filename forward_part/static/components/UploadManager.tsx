import React, { useRef, useState, useEffect } from 'react';
import { api } from '../services/api';
import { MultipartInitResponse, UploadTask, UploadResponse } from '../types';

interface UploadManagerProps {
  onComplete: () => void;
}

interface MultipartSession {
  upload_id: string;
  part_size: number;
  total_parts: number;
}

const SIMPLE_UPLOAD_THRESHOLD = 5 * 1024 * 1024;
const PART_PARALLELISM = 2;
const MULTIPART_SESSION_KEY = 'multipart_upload_sessions';

const multipartSessionStorageKey = () => {
  const token = localStorage.getItem('oss_token') || 'anonymous';
  return `${MULTIPART_SESSION_KEY}:${token.slice(0, 16)}`;
};

const multipartSessionStorage = () => sessionStorage;

const calculateHash = async (data: ArrayBuffer): Promise<string> => {
  const hashBuffer = await crypto.subtle.digest('SHA-256', data);
  const hashArray = Array.from(new Uint8Array(hashBuffer));
  return hashArray.map((b) => b.toString(16).padStart(2, '0')).join('');
};

const loadMultipartSessions = (): Record<string, MultipartSession> => {
  const key = multipartSessionStorageKey();
  try {
    const raw = multipartSessionStorage().getItem(key);
    if (raw) return JSON.parse(raw);
  } catch {
    return {};
  }

  // 向前兼容旧版本 localStorage 中的断点会话，并在读取后迁移到 sessionStorage。
  try {
    const legacyRaw = localStorage.getItem(key);
    if (!legacyRaw) return {};
    const parsed = JSON.parse(legacyRaw);
    multipartSessionStorage().setItem(key, legacyRaw);
    localStorage.removeItem(key);
    return parsed;
  } catch {
    return {};
  }
};

const getMultipartSession = (fileHash: string): MultipartSession | null => {
  const sessions = loadMultipartSessions();
  return sessions[fileHash] || null;
};

const saveMultipartSession = (fileHash: string, session: MultipartSession) => {
  const sessions = loadMultipartSessions();
  sessions[fileHash] = session;
  multipartSessionStorage().setItem(multipartSessionStorageKey(), JSON.stringify(sessions));
  localStorage.removeItem(multipartSessionStorageKey());
};

const clearMultipartSession = (fileHash: string) => {
  const sessions = loadMultipartSessions();
  delete sessions[fileHash];
  const key = multipartSessionStorageKey();
  multipartSessionStorage().setItem(key, JSON.stringify(sessions));
  localStorage.removeItem(key);
};

const chunkNumbers = (items: number[], size: number) => {
  const batches: number[][] = [];
  for (let i = 0; i < items.length; i += size) {
    batches.push(items.slice(i, i + size));
  }
  return batches;
};

const toTaskStatus = (serverStatus?: string) => {
  switch (serverStatus) {
    case 'success':
      return 'success';
    case 'pending_scan':
      return 'pending_scan';
    case 'infected':
      return 'infected';
    case 'scan_failed':
      return 'scan_failed';
    default:
      return serverStatus || 'success';
  }
};

const uploadResponseFromError = (err: any): UploadResponse | MultipartInitResponse | null => {
  if (err?.uploadResponse) {
    return err.uploadResponse;
  }
  if (err?.body && typeof err.body === 'object' && typeof err.body.status === 'string') {
    return {
      status: err.body.status,
      message: err.body.message || err.body.details,
      object_key: err.body.object_key || '',
      file_hash: err.body.file_hash || ''
    } as UploadResponse;
  }
  return null;
};

export const UploadManager: React.FC<UploadManagerProps> = ({ onComplete }) => {
  const [tasks, setTasks] = useState<UploadTask[]>([]);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const formatSize = (bytes: number) => {
    if (!bytes || bytes <= 0) return '0 B';
    return bytes < 1024 * 1024
      ? `${(bytes / 1024).toFixed(2)} KB`
      : `${(bytes / 1024 / 1024).toFixed(2)} MB`;
  };

  const updateTask = (id: string, updates: Partial<UploadTask>) => {
    setTasks((prev) => prev.map((t) => (t.id === id ? { ...t, ...updates } : t)));
  };

  const updateTaskFromUploadResponse = (taskId: string, response: UploadResponse | MultipartInitResponse, progress = 100) => {
    const status = toTaskStatus(response.status);
    updateTask(taskId, {
      progress,
      status,
      detail: response.message || response.status || 'uploaded'
    });
  };

  const buildMultipartSession = async (file: File, fileHash: string): Promise<MultipartSession> => {
    let init: MultipartInitResponse;
    try {
      init = await api.initMultipart(file.name, fileHash, file.size, file.type);
    } catch (err: any) {
      const uploadResponse = uploadResponseFromError(err);
      if (uploadResponse) {
        throw Object.assign(new Error(uploadResponse.message || uploadResponse.status), { uploadResponse });
      }
      throw err;
    }
    if (init.status && init.status !== 'init') {
      throw Object.assign(new Error(init.message || init.status), { uploadResponse: init });
    }
    if (!init.upload_id || !init.part_size || !init.total_parts) {
      throw new Error('Multipart init response missing upload session data');
    }
    const session = {
      upload_id: init.upload_id,
      part_size: init.part_size,
      total_parts: init.total_parts
    };
    saveMultipartSession(fileHash, session);
    return session;
  };

  const uploadMultipart = async (file: File, fileHash: string, taskId: string) => {
    let session = getMultipartSession(fileHash);
    if (!session) {
      session = await buildMultipartSession(file, fileHash);
    }

    let statusResp;
    try {
      statusResp = await api.getMultipartStatus(session.upload_id);
    } catch {
      clearMultipartSession(fileHash);
      session = await buildMultipartSession(file, fileHash);
      statusResp = await api.getMultipartStatus(session.upload_id);
    }

    const uploadedParts = new Set<number>(statusResp.uploaded_parts || []);
    const refreshProgress = () => {
      updateTask(taskId, {
        progress: Math.round((uploadedParts.size / session.total_parts) * 100)
      });
    };
    refreshProgress();

    const missingParts: number[] = [];
    for (let partNumber = 1; partNumber <= session.total_parts; partNumber++) {
      if (!uploadedParts.has(partNumber)) {
        missingParts.push(partNumber);
      }
    }

    for (const batch of chunkNumbers(missingParts, PART_PARALLELISM)) {
      const presigned = await api.presignParts(session.upload_id, batch);
      const partMap = new Map(presigned.parts.map((part) => [part.part_number, part.url]));

      await Promise.all(
        batch.map(async (partNumber) => {
          const start = (partNumber - 1) * session.part_size;
          const end = Math.min(start + session.part_size, file.size);
          const url = partMap.get(partNumber);
          if (!url) {
            throw new Error(`Missing presigned URL for part ${partNumber}`);
          }
          await api.uploadPresignedPart(url, file.slice(start, end));
          uploadedParts.add(partNumber);
          refreshProgress();
        })
      );
    }

    const complete = await api.completeMultipart(session.upload_id);
    clearMultipartSession(fileHash);
    updateTask(taskId, {
      progress: 100,
      status: toTaskStatus(complete.status),
      detail: complete.status
    });
  };

  const processUpload = async (file: File, taskId: string) => {
    try {
      updateTask(taskId, { status: 'uploading', detail: 'hashing' });
      const fullFileBuffer = await file.arrayBuffer();
      const fileHash = await calculateHash(fullFileBuffer);
      updateTask(taskId, { fileHash });

      if (file.size < SIMPLE_UPLOAD_THRESHOLD) {
        try {
          const response = await api.simpleUpload(file, file.name, fileHash, file.type);
          updateTaskFromUploadResponse(taskId, response);
        } catch (err: any) {
          const uploadResponse = uploadResponseFromError(err);
          if (uploadResponse) {
            updateTaskFromUploadResponse(taskId, uploadResponse);
            return;
          }
          throw err;
        }
      } else {
        try {
          await uploadMultipart(file, fileHash, taskId);
        } catch (err: any) {
          if (err?.uploadResponse) {
            updateTaskFromUploadResponse(taskId, err.uploadResponse, 100);
            return;
          }
          throw err;
        }
      }

      onComplete();
    } catch (err: any) {
      updateTask(taskId, {
        status: 'error',
        error: err.message,
        detail: err.message
      });
    }
  };

  const handleFileUpload = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const files = e.target.files;
    if (!files) return;

    for (let i = 0; i < files.length; i++) {
      const file = files[i];
      const taskId = Math.random().toString(36).slice(2, 11);
      const newTask: UploadTask = {
        id: taskId,
        filename: file.name,
        size: file.size,
        progress: 0,
        status: 'pending'
      };
      setTasks((prev) => [newTask, ...prev]);
      void processUpload(file, taskId);
    }

    if (fileInputRef.current) fileInputRef.current.value = '';
  };

  useEffect(() => {
    const pendingTasks = tasks.filter((t) => t.status === 'pending_scan' && t.fileHash);
    if (pendingTasks.length === 0) return;

    const poll = setInterval(async () => {
      try {
        const res = await api.queryFiles();
        const fileMap = new Map((res.filelist || []).map((f: any) => [f.filehash, f.status]));
        setTasks((prev) =>
          prev.map((t) => {
            if (t.status !== 'pending_scan' || !t.fileHash) return t;
            const updatedStatus = fileMap.get(t.fileHash);
            if (updatedStatus && updatedStatus !== 'pending_scan') {
              return { ...t, status: updatedStatus };
            }
            return t;
          })
        );
      } catch {
        // ignore poll errors
      }
    }, 3000);

    return () => clearInterval(poll);
  }, [tasks]);

  const getStatusClass = (status: string) => {
    if (status === 'success') return 'border-green-500/30 text-green-500 bg-green-500/10';
    if (status === 'pending_scan') return 'border-amber-500/30 text-amber-400 bg-amber-500/10';
    if (status === 'infected' || status === 'scan_failed' || status === 'error') return 'border-red-500/30 text-red-500 bg-red-500/10';
    return 'border-blue-500/30 text-blue-500 bg-blue-500/10';
  };

  const getProgressClass = (status: string) => {
    if (status === 'success') return 'bg-green-500 shadow-[0_0_5px_green]';
    if (status === 'pending_scan') return 'bg-amber-500 shadow-[0_0_5px_rgba(245,158,11,0.5)]';
    if (status === 'infected' || status === 'scan_failed' || status === 'error') return 'bg-red-500';
    return 'bg-blue-500 shadow-[0_0_5px_rgba(59,130,246,0.5)]';
  };

  return (
    <div className="fixed bottom-8 right-8 w-80 glass-panel rounded-2xl shadow-3xl border-white/10 z-50 overflow-hidden">
      <div className="bg-white/5 px-4 py-3 flex justify-between items-center border-b border-white/5">
        <h3 className="text-[10px] font-black uppercase tracking-widest text-blue-400">Task_Manager ({tasks.length})</h3>
        <button
          onClick={() => fileInputRef.current?.click()}
          className="text-[9px] bg-blue-600 px-3 py-1 rounded-lg hover:bg-blue-500 font-bold uppercase tracking-tighter transition"
        >
          New Node
        </button>
      </div>

      <input type="file" multiple ref={fileInputRef} onChange={handleFileUpload} className="hidden" />

      <div className="max-h-64 overflow-y-auto p-2 space-y-2">
        {tasks.length === 0 && (
          <div className="py-8 text-center text-[10px] font-mono text-slate-600">IDLE_STATE: AWAITING_INPUT</div>
        )}
        {tasks.map((task) => (
          <div key={task.id} className="p-3 bg-white/5 rounded-xl border border-white/5">
            <div className="flex justify-between items-center mb-2 gap-3">
              <span className="text-[11px] font-bold truncate w-32 uppercase" title={task.filename}>
                {task.filename}
              </span>
              <span className={`text-[8px] font-black px-2 py-0.5 rounded-full border ${getStatusClass(task.status)}`}>
                {task.status}
              </span>
            </div>

            <div className="w-full bg-black/20 rounded-full h-1 mb-1.5 overflow-hidden">
              <div className={`h-full transition-all duration-500 ease-out ${getProgressClass(task.status)}`} style={{ width: `${task.progress}%` }} />
            </div>

            <div className="flex justify-between text-[9px] font-mono text-slate-500">
              <span>{task.progress}% SYNC</span>
              <span>{formatSize(task.size)}</span>
            </div>

            {(task.detail || task.error) && (
              <div className="mt-2 text-[9px] font-mono text-slate-400 break-words">
                {task.error || task.detail}
              </div>
            )}
          </div>
        ))}
      </div>
    </div>
  );
};
