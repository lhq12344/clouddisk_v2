
import React, { useState, useRef } from 'react';
import { api } from '../services/api';
import { UploadTask } from '../types';

interface UploadManagerProps {
  onComplete: () => void;
}

export const UploadManager: React.FC<UploadManagerProps> = ({ onComplete }) => {
  const [tasks, setTasks] = useState<UploadTask[]>([]);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const calculateHash = async (data: ArrayBuffer): Promise<string> => {
    const hashBuffer = await crypto.subtle.digest('SHA-256', data);
    const hashArray = Array.from(new Uint8Array(hashBuffer));
    return hashArray.map(b => b.toString(16).padStart(2, '0')).join('');
  };

  const formatSize = (bytes: number) => {
    if (!bytes || bytes <= 0) return '0 B';
    return (bytes / 1024 / 1024).toFixed(2) + ' MB';
  };

  const handleFileUpload = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const files = e.target.files;
    if (!files) return;

    for (let i = 0; i < files.length; i++) {
      const file = files[i];
      const taskId = Math.random().toString(36).substr(2, 9);
      const newTask: UploadTask = { id: taskId, filename: file.name, size: file.size, progress: 0, status: 'pending' };
      setTasks(prev => [newTask, ...prev]);
      processUpload(file, taskId);
    }
    if (fileInputRef.current) fileInputRef.current.value = '';
  };

  const processUpload = async (file: File, taskId: string) => {
    try {
      updateTask(taskId, { status: 'uploading' });
      const fullFileBuffer = await file.arrayBuffer();
      const fileHash = await calculateHash(fullFileBuffer);
      
      if (file.size < 5 * 1024 * 1024) {
        const text = await file.text().catch(() => "BINARY_STREAM");
        await api.simpleUpload(file.name, text);
        updateTask(taskId, { progress: 100, status: 'completed' });
      } else {
        const init = await api.initMultipart(file.name, fileHash, file.size, file.type);
        const { upload_id, part_size, total_parts } = init;

        for (let p = 1; p <= total_parts; p++) {
          const start = (p - 1) * part_size;
          const end = Math.min(start + part_size, file.size);
          const chunk = file.slice(start, end);
          const buffer = await chunk.arrayBuffer();
          const chunkHash = await calculateHash(buffer);
          await api.uploadPart(upload_id, p, buffer, chunkHash);
          updateTask(taskId, { progress: Math.round((p / total_parts) * 100) });
        }
        await api.completeMultipart(upload_id);
        updateTask(taskId, { progress: 100, status: 'completed' });
      }
      onComplete();
    } catch (err: any) {
      updateTask(taskId, { status: 'error', error: err.message });
    }
  };

  const updateTask = (id: string, updates: Partial<UploadTask>) => {
    setTasks(prev => prev.map(t => t.id === id ? { ...t, ...updates } : t));
  };

  return (
    <div className="fixed bottom-8 right-8 w-80 glass-panel rounded-2xl shadow-3xl border-white/10 z-50 overflow-hidden">
      <div className="bg-white/5 px-4 py-3 flex justify-between items-center border-b border-white/5">
        <h3 className="text-[10px] font-black uppercase tracking-widest text-blue-400">Task_Manager ({tasks.length})</h3>
        <button onClick={() => fileInputRef.current?.click()} className="text-[9px] bg-blue-600 px-3 py-1 rounded-lg hover:bg-blue-500 font-bold uppercase tracking-tighter transition">
          New Node
        </button>
      </div>

      <input type="file" multiple ref={fileInputRef} onChange={handleFileUpload} className="hidden" />

      <div className="max-h-64 overflow-y-auto p-2 space-y-2">
        {tasks.length === 0 && (
          <div className="py-8 text-center text-[10px] font-mono text-slate-600">IDLE_STATE: AWAITING_INPUT</div>
        )}
        {tasks.map(task => (
          <div key={task.id} className="p-3 bg-white/5 rounded-xl border border-white/5">
            <div className="flex justify-between items-center mb-2">
              <span className="text-[11px] font-bold truncate w-32 uppercase" title={task.filename}>{task.filename}</span>
              <span className={`text-[8px] font-black px-2 py-0.5 rounded-full border ${
                task.status === 'completed' ? 'border-green-500/30 text-green-500 bg-green-500/10' :
                task.status === 'error' ? 'border-red-500/30 text-red-500 bg-red-500/10' :
                'border-blue-500/30 text-blue-500 animate-pulse bg-blue-500/10'
              }`}>
                {task.status}
              </span>
            </div>
            
            <div className="w-full bg-black/20 rounded-full h-1 mb-1.5 overflow-hidden">
              <div 
                className={`h-full transition-all duration-500 ease-out ${
                  task.status === 'completed' ? 'bg-green-500 shadow-[0_0_5px_green]' :
                  task.status === 'error' ? 'bg-red-500' :
                  'bg-blue-500 shadow-[0_0_5px_rgba(59,130,246,0.5)]'
                }`}
                style={{ width: `${task.progress}%` }}
              ></div>
            </div>
            <div className="flex justify-between text-[9px] font-mono text-slate-500">
              <span>{task.progress}% SYNC</span>
              <span>{formatSize(task.size)}</span>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
};
