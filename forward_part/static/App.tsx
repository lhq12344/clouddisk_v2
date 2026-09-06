
import React, { useState, useEffect } from 'react';
import { api } from './services/api';
import { AuthPage } from './components/AuthPage';
import { UploadManager } from './components/UploadManager';
import { FileItem, UserInfo } from './types';

const FILE_TYPES = [
  { label: 'Markdown', ext: '.md', protocol: 'PROTO_MD' },
  { label: 'Text', ext: '.txt', protocol: 'PROTO_TXT' },
  { label: 'JSON', ext: '.json', protocol: 'PROTO_JSON' },
  { label: 'JavaScript', ext: '.js', protocol: 'PROTO_JS' },
];

const TEXT_PREVIEW_EXTENSIONS = new Set([
  '.md',
  '.txt',
  '.json',
  '.js',
  '.ts',
  '.tsx',
  '.jsx',
  '.css',
  '.html',
  '.xml',
  '.yaml',
  '.yml',
  '.csv',
  '.log',
  '.sql',
  '.sh',
  '.py',
  '.go',
  '.cpp',
  '.cc',
  '.c',
  '.h',
  '.hpp',
]);

const getFileExtension = (filename: string) => {
  const ext = filename.lastIndexOf('.');
  if (ext < 0) return '';
  return filename.slice(ext).toLowerCase();
};

const isTextContentType = (contentType?: string) => {
  const normalized = (contentType || '').toLowerCase();
  return normalized.startsWith('text/')
    || normalized.includes('json')
    || normalized.includes('javascript')
    || normalized.includes('xml')
    || normalized.includes('yaml')
    || normalized.includes('csv')
    || normalized.includes('svg');
};

const isTextPreviewable = (file: FileItem) => {
  return TEXT_PREVIEW_EXTENSIONS.has(getFileExtension(file.filename))
    || isTextContentType(file.content_type);
};

const rewriteMinioUrl = (url: string) => {
  try {
    const u = new URL(url);
    if (u.host === '127.0.0.1:30900' || u.host === 'localhost:30900') {
      u.host = 'localhost:3000';
      u.protocol = 'http:';
    }
    return u.toString();
  } catch {
    return url;
  }
};

const openExternalUrl = (url: string) => {
  const rewritten = rewriteMinioUrl(url);
  const popup = window.open(rewritten, '_blank', 'noopener,noreferrer');
  if (!popup) {
    window.location.assign(rewritten);
  }
};

const isInitiatedUploadStatus = (status?: string) => !status || status === 'init' || status === 'initiated';

const uploadBlobViaMultipart = async (blob: Blob, filename: string, fileHash: string) => {
  const init = await api.initMultipart(filename, fileHash, blob.size, blob.type || textContentType(filename));
  if (!isInitiatedUploadStatus(init.status)) {
    throw new Error(init.message || init.status || 'Upload init failed');
  }
  if (!init.upload_id || !init.part_size || !init.total_parts) {
    throw new Error('Multipart init response missing upload session data');
  }

  const status = await api.getMultipartStatus(init.upload_id);
  const uploadedParts = new Set(status.uploaded_parts || []);
  for (let partNumber = 1; partNumber <= init.total_parts; partNumber++) {
    if (uploadedParts.has(partNumber)) continue;
    const presigned = await api.presignParts(init.upload_id, [partNumber]);
    const url = presigned.parts.find((part) => part.part_number === partNumber)?.url;
    if (!url) {
      throw new Error(`Missing presigned URL for part ${partNumber}`);
    }
    const start = (partNumber - 1) * init.part_size;
    const end = Math.min(start + init.part_size, blob.size);
    await api.uploadPresignedPart(url, blob.slice(start, end));
  }

  return api.completeMultipart(init.upload_id);
};

const triggerBrowserDownload = (url: string) => {
  const rewritten = rewriteMinioUrl(url);
  const link = document.createElement('a');
  link.href = rewritten;
  link.rel = 'noopener noreferrer';
  document.body.appendChild(link);
  link.click();
  link.remove();
};

const textContentType = (filename: string) => {
  if (filename.endsWith('.md')) return 'text/markdown;charset=utf-8';
  if (filename.endsWith('.json')) return 'application/json;charset=utf-8';
  if (filename.endsWith('.js')) return 'application/javascript;charset=utf-8';
  return 'text/plain;charset=utf-8';
};

const sha256Hex = async (content: string) => {
  const encoded = new TextEncoder().encode(content);
  const digest = await crypto.subtle.digest('SHA-256', encoded);
  return Array.from(new Uint8Array(digest))
    .map((b) => b.toString(16).padStart(2, '0'))
    .join('');
};

const normalizeUserInfo = (payload: any): UserInfo | null => {
  if (!payload) return null;
  let source = payload;
  if (!payload.username && typeof payload.message === 'string') {
    try {
      source = JSON.parse(payload.message);
    } catch {
      source = payload;
    }
  }
  const username = source.username || source.name;
  if (!username) return null;
  return {
    username,
    email: source.email || '',
    name: source.name || username,
    mobile: source.mobile || source.Mobile || '',
    gender: source.gender || '',
    createdAt: source.createdAt || '',
    updatedAt: source.updatedAt || ''
  };
};

const App: React.FC = () => {
  const [isAuthenticated, setIsAuthenticated] = useState(!!localStorage.getItem('oss_token'));
  const [user, setUser] = useState<UserInfo | null>(null);
  const [files, setFiles] = useState<FileItem[]>([]);
  const [loading, setLoading] = useState(false);
  const [searchQuery, setSearchQuery] = useState('');
  const [isUserMenuOpen, setIsUserMenuOpen] = useState(false);
  const [theme, setTheme] = useState<'dark' | 'light'>(
    (localStorage.getItem('oss_theme') as 'dark' | 'light') || 'dark'
  );
  
  // Editor & Create States
  const [editingFile, setEditingFile] = useState<FileItem | null>(null);
  const [isNewFile, setIsNewFile] = useState(false);
  const [editContent, setEditContent] = useState('');
  const [isSaving, setIsSaving] = useState(false);
  const [isPreviewMode, setIsPreviewMode] = useState(false);
  
  // Modal States
  const [showCreateModal, setShowCreateModal] = useState(false);
  const [newFileName, setNewFileName] = useState('');
  const [selectedExt, setSelectedExt] = useState('.md');
  const [purgeFile, setPurgeFile] = useState<FileItem | null>(null);

  useEffect(() => {
    document.body.setAttribute('data-theme', theme);
    localStorage.setItem('oss_theme', theme);
  }, [theme]);

  const fetchFiles = async () => {
    if (!isAuthenticated) return;
    setLoading(true);
    try {
      const res = await api.queryFiles();
      setFiles(res.filelist || []);
    } catch (err) {
      console.error('Failed to fetch files', err);
    } finally {
      setLoading(false);
    }
  };

  const fetchUserInfo = async () => {
    try {
      const res = await api.getUserInfo();
      const normalized = normalizeUserInfo(res);
      if (!normalized) {
        throw new Error('Invalid user info payload');
      }
      setUser(normalized);
    } catch (err) {
      handleLogout();
    }
  };

  useEffect(() => {
    if (isAuthenticated) {
      fetchFiles();
      fetchUserInfo();
    }
  }, [isAuthenticated]);

  useEffect(() => {
    const pendingFiles = files.filter((f) => f.status === 'pending_scan');
    if (pendingFiles.length === 0) return;

    const poll = setInterval(fetchFiles, 3000);
    return () => clearInterval(poll);
  }, [files]);

  const handleLogout = async () => {
    await api.logout();
    setIsAuthenticated(false);
    setUser(null);
    setFiles([]);
    setEditingFile(null);
    setIsUserMenuOpen(false);
  };

  const handlePreview = async (file: FileItem) => {
    setLoading(true);
    try {
      const res = await api.getPreviewUrl(file);
      if (!res?.preview_url) {
        throw new Error('Preview URL missing');
      }
      if (!isTextPreviewable(file)) {
        openExternalUrl(res.preview_url);
        return;
      }
      const contentResponse = await fetch(rewriteMinioUrl(res.preview_url), { cache: 'no-store' });
      if (!contentResponse.ok) {
        const errorText = await contentResponse.text();
        throw new Error(`[STATUS_${contentResponse.status}]: ${errorText || 'Preview failed'}`);
      }
      const text = await contentResponse.text();
      setEditContent(text);
      setEditingFile(file);
      setIsNewFile(false);
      setIsPreviewMode(false);
    } catch (err: any) {
      alert('Failed to load file content: ' + err.message);
    } finally {
      setLoading(false);
    }
  };

  const handleDownload = async (file: FileItem) => {
    setLoading(true);
    try {
      const res = await api.getDownloadUrl(file);
      if (!res?.download_url) {
        throw new Error('Download URL missing');
      }
      triggerBrowserDownload(res.download_url);
    } catch (err: any) {
      alert('Download failed: ' + err.message);
    } finally {
      setLoading(false);
    }
  };

  const handleInitCreate = () => {
    setShowCreateModal(true);
    setNewFileName('');
  };

  const confirmCreate = () => {
    if (!newFileName.trim()) return;
    const fullFileName = newFileName.trim() + selectedExt;
    setEditingFile({
      filename: fullFileName,
      filehash: 'PENDING_COMMIT',
      filesize: 0
    });
    setEditContent('');
    setIsNewFile(true);
    setShowCreateModal(false);
    setIsPreviewMode(false);
  };

  const handleSaveEdit = async () => {
    if (!editingFile) return;
    setIsSaving(true);
    try {
      const fileHash = await sha256Hex(editContent);
      const blob = new Blob([editContent], { type: textContentType(editingFile.filename) });
      await uploadBlobViaMultipart(blob, editingFile.filename, fileHash);
      setEditingFile(null);
      setIsNewFile(false);
      await fetchFiles();
    } catch (err: any) {
      alert('Sync failed: ' + err.message);
    } finally {
      setIsSaving(false);
    }
  };

  const confirmPurge = async () => {
    if (!purgeFile) return;
    setLoading(true);
    try {
      await api.deleteFile(purgeFile);
      setPurgeFile(null);
      await fetchFiles();
    } catch (err: any) {
      alert('Purge sequence failed: ' + err.message);
    } finally {
      setLoading(false);
    }
  };

  const formatSize = (bytes: number) => {
    if (!bytes || bytes <= 0) return '0 B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(2) + ' KB';
    return (bytes / 1024 / 1024).toFixed(2) + ' MB';
  };

  const renderMarkdown = (text: string) => {
    return text
      .replace(/^# (.*$)/gm, '<h1 class="text-3xl font-bold text-blue-400 mb-4 border-b border-blue-500/20 pb-2">$1</h1>')
      .replace(/^## (.*$)/gm, '<h2 class="text-2xl font-bold text-blue-300 mb-3">$1</h2>')
      .replace(/^### (.*$)/gm, '<h3 class="text-xl font-bold text-blue-200 mb-2">$1</h3>')
      .replace(/\*\*(.*)\*\*/g, '<strong class="text-white">$1</strong>')
      .replace(/\*(.*)\*/g, '<em class="text-blue-400/80">$1</em>')
      .replace(/`(.*?)`/g, '<code class="bg-blue-600/20 px-1 rounded text-blue-300">$1</code>')
      .replace(/^\- (.*$)/gm, '<li class="ml-4 list-disc text-slate-300">$1</li>')
      .replace(/\n/g, '<br/>');
  };

  if (!isAuthenticated) {
    return <AuthPage onSuccess={() => setIsAuthenticated(true)} theme={theme} />;
  }

  const filteredFiles = files.filter(f => 
    f.filename.toLowerCase().includes(searchQuery.toLowerCase())
  );

  const getProtocol = (filename: string) => {
    const ext = filename.split('.').pop();
    return `PROTO_${ext?.toUpperCase() || 'UNKNOWN'}`;
  };

  const isFileAvailable = (file: FileItem) => file.status === 'success';

  const formatFileStatus = (file: FileItem) => {
    switch (file.status) {
      case 'pending_scan':
        return 'Scanning';
      case 'success':
        return 'Passed';
      case 'infected':
        return 'Blocked';
      case 'scan_failed':
        return 'Scan Failed';
      default:
        return file.status || 'Unknown';
    }
  };

  const fileStatusClass = (file: FileItem) => {
    switch (file.status) {
      case 'success':
        return 'border-green-500/30 text-green-400 bg-green-500/10';
      case 'pending_scan':
        return 'border-amber-500/30 text-amber-400 bg-amber-500/10';
      case 'infected':
      case 'scan_failed':
        return 'border-red-500/30 text-red-400 bg-red-500/10';
      default:
        return 'border-slate-500/30 text-slate-400 bg-slate-500/10';
    }
  };

  return (
    <div className="min-h-screen flex flex-col transition-colors duration-500">
      {/* Sci-fi Header */}
      <nav className="glass-panel sticky top-0 z-40 border-b border-white/5 px-6">
        <div className="max-w-7xl mx-auto h-16 flex items-center justify-between">
          <div className="flex items-center gap-10">
            <div className="flex items-center gap-2 cursor-pointer group" onClick={() => {setEditingFile(null); setIsNewFile(false);}}>
              <div className="w-8 h-8 bg-blue-600 rounded-lg flex items-center justify-center neon-border group-hover:rotate-12 transition-transform">
                <svg className="w-5 h-5 text-white" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2.5" d="M13 10V3L4 14h7v7l9-11h-7z" />
                </svg>
              </div>
              <h1 className="text-xl font-bold tracking-tighter neon-text uppercase">NOVA<span className="text-blue-500">STORAGE</span></h1>
            </div>
          </div>

          <div className="flex items-center gap-6">
            <div className="relative">
              <button onClick={() => setIsUserMenuOpen(!isUserMenuOpen)} className="flex items-center gap-3 p-1 rounded-full hover:bg-white/5 transition focus:outline-none">
                <div className="text-right hidden sm:block">
                  <p className="text-xs font-bold uppercase tracking-tight">{user?.username || 'User'}</p>
                  <p className="text-[10px] text-blue-500 font-mono">AUTHORIZED</p>
                </div>
                <div className="w-9 h-9 rounded-full bg-slate-800 border border-blue-500/30 flex items-center justify-center text-blue-400 font-bold overflow-hidden">
                   {user?.username?.charAt(0).toUpperCase() || 'U'}
                </div>
              </button>
              {isUserMenuOpen && (
                <>
                  <div className="fixed inset-0 z-10" onClick={() => setIsUserMenuOpen(false)}></div>
                  <div className="absolute right-0 mt-3 w-56 glass-panel rounded-xl shadow-2xl p-2 z-50 animate-in fade-in slide-in-from-top-2 duration-200">
                    <button onClick={() => setTheme(theme === 'dark' ? 'light' : 'dark')} className="w-full flex items-center justify-between px-3 py-2.5 text-xs font-semibold hover:bg-white/5 rounded-lg transition">
                      <span className="flex items-center gap-2">Switch to {theme === 'dark' ? 'Bright' : 'Dark'} Mode</span>
                    </button>
                    <button onClick={handleLogout} className="w-full flex items-center gap-2 px-3 py-2.5 text-xs font-semibold text-red-400 hover:bg-red-400/10 rounded-lg transition">
                      Deauthorize Access
                    </button>
                  </div>
                </>
              )}
            </div>
          </div>
        </div>
      </nav>

      <main className="flex-1 max-w-7xl w-full mx-auto p-8">
        {!editingFile ? (
          <>
            <div className="flex flex-col md:flex-row md:items-center justify-between gap-6 mb-10">
              <div>
                <h2 className="text-3xl font-bold tracking-tight neon-text uppercase">Data Core</h2>
                <p className="text-slate-400 text-sm font-mono mt-1">Nodes Active: <span className="text-blue-500">{files.length}</span></p>
              </div>
              
              <div className="flex items-center gap-4">
                <button 
                  onClick={handleInitCreate}
                  className="px-6 py-2.5 bg-blue-600/10 border border-blue-500/30 text-blue-400 text-xs font-black uppercase tracking-widest rounded-xl hover:bg-blue-600 hover:text-white transition-all neon-border flex items-center gap-2"
                >
                  <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth="3" d="M12 4v16m8-8H4" /></svg>
                  Initialize Entity
                </button>
                <div className="relative group">
                  <input 
                    type="text" 
                    placeholder="Search node..." 
                    className="pl-10 pr-4 py-2.5 glass-panel rounded-xl focus:ring-1 focus:ring-blue-500 outline-none w-64 text-sm transition"
                    value={searchQuery}
                    onChange={(e) => setSearchQuery(e.target.value)}
                  />
                  <svg className="w-4 h-4 absolute left-3.5 top-3.5 text-slate-500" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z" /></svg>
                </div>
              </div>
            </div>

            <div className="glass-panel rounded-2xl overflow-hidden shadow-2xl border-white/5">
              <table className="w-full text-left">
                <thead>
                  <tr className="bg-white/5 border-b border-white/5 text-[10px] font-black uppercase tracking-widest text-slate-500">
                    <th className="px-6 py-5">Entity ID</th>
                    <th className="px-6 py-5">Hash Signature</th>
                    <th className="px-6 py-5">Density</th>
                    <th className="px-6 py-5 text-right">Commands</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-white/5">
                  {loading && files.length === 0 ? (
                    <tr><td colSpan={4} className="px-6 py-32 text-center text-slate-500 animate-pulse font-mono uppercase tracking-widest">Scanning Grid...</td></tr>
                  ) : filteredFiles.length === 0 ? (
                    <tr><td colSpan={4} className="px-6 py-32 text-center text-slate-500 font-mono italic">EMPTY_SECTOR</td></tr>
                  ) : (
                    filteredFiles.map((file, idx) => (
                      <tr key={idx} className="hover:bg-blue-600/5 transition-all group">
                        <td className="px-6 py-5">
                          <div className="flex items-center gap-4">
                            <div className="w-10 h-10 rounded-lg bg-blue-600/10 border border-blue-500/20 flex items-center justify-center text-blue-500 group-hover:neon-border transition">
                              <svg className="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth="1.5" d="M9 12h6m-6 4h6m2 5H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z" /></svg>
                            </div>
                            <p className="text-sm font-bold tracking-tight">{file.filename}</p>
                          </div>
                        </td>
                        <td className="px-6 py-5 font-mono text-[10px] text-slate-500">{file.filehash}</td>
                        <td className="px-6 py-5 text-sm text-slate-400">
                          <div>{formatSize(file.filesize)}</div>
                          <div className="mt-2 flex items-center gap-2">
                            <span className={`px-2 py-0.5 rounded-full border text-[9px] font-black uppercase tracking-widest ${fileStatusClass(file)}`}>
                              {formatFileStatus(file)}
                            </span>
                          </div>
                          {file.scan_detail && (
                            <div className="mt-2 text-[10px] text-slate-500 break-words max-w-xs">{file.scan_detail}</div>
                          )}
                        </td>
                        <td className="px-6 py-5 text-right space-x-2">
                          <button
                            onClick={() => handlePreview(file)}
                            disabled={!isFileAvailable(file)}
                            className="p-2 glass-panel hover:text-blue-400 rounded-lg transition disabled:opacity-40 disabled:cursor-not-allowed"
                            title={isFileAvailable(file) ? "Inspect Node" : "File is not downloadable until scan passes"}
                          ><svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M15 12a3 3 0 11-6 0 3 3 0 016 0z" /><path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M2.458 12C3.732 7.943 7.523 5 12 5c4.478 0 8.268 2.943 9.542 7-1.274 4.057-5.064 7-9.542 7-4.477 0-8.268-2.943-9.542-7z" /></svg></button>
                          <button
                            onClick={() => handleDownload(file)}
                            disabled={!isFileAvailable(file)}
                            className="p-2 glass-panel hover:text-green-400 rounded-lg transition disabled:opacity-40 disabled:cursor-not-allowed"
                            title={isFileAvailable(file) ? "Extract Data" : "File is not downloadable until scan passes"}
                          ><svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M4 16v1a3 3 0 003 3h10a3 3 0 003-3v-1m-4-4l-4 4m0 0l-4-4m4 4V4" /></svg></button>
                          <button onClick={(e) => { e.stopPropagation(); setPurgeFile(file); }} className="p-2 glass-panel hover:text-red-500 hover:neon-border rounded-lg transition" title="Purge Sequence"><svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" /></svg></button>
                        </td>
                      </tr>
                    ))
                  )}
                </tbody>
              </table>
            </div>
          </>
        ) : (
          <div className="space-y-6 animate-in zoom-in-95 duration-300">
            <div className="flex items-center justify-between glass-panel p-5 rounded-2xl border-white/10">
              <div className="flex items-center gap-5">
                <button onClick={() => {setEditingFile(null); setIsNewFile(false);}} className="p-2.5 hover:bg-white/5 rounded-xl transition text-slate-400"><svg className="w-6 h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M10 19l-7-7m0 0l7-7m-7 7h18" /></svg></button>
                <div>
                  <h2 className="text-xl font-bold neon-text">{editingFile.filename}</h2>
                  <p className="text-[10px] text-blue-500 font-mono tracking-widest uppercase">
                    {isNewFile ? 'INITIALIZING_NODE' : `HASH: ${editingFile.filehash.substring(0,16)}...`}
                  </p>
                </div>
              </div>
              <div className="flex items-center gap-4">
                {editingFile.filename.endsWith('.md') && (
                  <button 
                    onClick={() => setIsPreviewMode(!isPreviewMode)}
                    className={`px-4 py-2 text-[10px] font-black uppercase tracking-widest rounded-xl transition-all border ${isPreviewMode ? 'bg-blue-600 border-blue-500 text-white neon-border' : 'border-white/10 text-slate-400 hover:bg-white/5'}`}
                  >
                    {isPreviewMode ? 'Exit Preview' : 'Show Preview'}
                  </button>
                )}
                <button onClick={handleSaveEdit} disabled={isSaving} className="px-8 py-2.5 bg-blue-600 text-white text-sm font-black rounded-xl hover:bg-blue-500 transition disabled:opacity-50 neon-border shadow-blue-600/20">
                  {isSaving ? "SYNCING..." : "COMMIT CHANGES"}
                </button>
              </div>
            </div>

            <div className="flex gap-6 h-[calc(100vh-280px)]">
              <div className={`glass-panel rounded-2xl border-white/10 overflow-hidden flex flex-col shadow-2xl transition-all duration-500 ${isPreviewMode ? 'w-1/2' : 'w-full'}`}>
                <textarea 
                  className="flex-1 w-full p-8 font-mono text-sm bg-transparent text-slate-200 focus:outline-none resize-none leading-relaxed selection:bg-blue-500/30 custom-scrollbar" 
                  value={editContent} 
                  onChange={(e) => setEditContent(e.target.value)} 
                  spellCheck={false}
                  placeholder="Enter data payload..."
                />
                <div className="bg-white/5 border-t border-white/5 px-6 py-3 flex justify-between items-center text-[10px] text-slate-500 font-black tracking-widest uppercase">
                  <span>PROTOCOL: {getProtocol(editingFile.filename)}</span>
                  <span>PAYLOAD: {editContent.length} BYTES</span>
                </div>
              </div>
              
              {isPreviewMode && (
                <div className="w-1/2 glass-panel rounded-2xl border-blue-500/20 bg-blue-500/5 p-8 overflow-y-auto custom-scrollbar shadow-inner animate-in slide-in-from-right-10">
                  <div className="prose prose-invert max-w-none font-sans" dangerouslySetInnerHTML={{ __html: renderMarkdown(editContent) }} />
                </div>
              )}
            </div>
          </div>
        )}
      </main>

      {/* New Entity Modal */}
      {showCreateModal && (
        <div className="fixed inset-0 z-[100] flex items-center justify-center p-6 backdrop-blur-xl animate-in fade-in duration-300">
          <div className="fixed inset-0 bg-black/40" onClick={() => setShowCreateModal(false)}></div>
          <div className="relative w-full max-w-md glass-panel rounded-3xl p-8 border-white/10 shadow-[0_0_100px_rgba(59,130,246,0.2)]">
            <h3 className="text-xl font-bold neon-text mb-6 uppercase tracking-tighter">Initialize Data Entity</h3>
            <div className="space-y-6">
              <div>
                <label className="text-[10px] font-black text-blue-500 uppercase tracking-widest mb-2 block">Entity_Label</label>
                <div className="flex items-center gap-2">
                  <input 
                    type="text" 
                    placeholder="entity_name_01" 
                    className="flex-1 px-4 py-3 bg-black/20 border border-white/10 rounded-xl text-white outline-none focus:ring-1 focus:ring-blue-500 transition font-mono text-sm"
                    value={newFileName}
                    onChange={(e) => setNewFileName(e.target.value)}
                    autoFocus
                  />
                  <select 
                    className="px-3 py-3 bg-black/20 border border-white/10 rounded-xl text-blue-400 outline-none focus:ring-1 focus:ring-blue-500 transition font-mono text-sm"
                    value={selectedExt}
                    onChange={(e) => setSelectedExt(e.target.value)}
                  >
                    {FILE_TYPES.map(t => <option key={t.ext} value={t.ext}>{t.ext}</option>)}
                  </select>
                </div>
              </div>
              <div className="grid grid-cols-2 gap-4">
                <button onClick={() => setShowCreateModal(false)} className="px-6 py-3 border border-white/10 rounded-xl text-xs font-bold uppercase tracking-widest text-slate-500 hover:bg-white/5 transition">Abort</button>
                <button onClick={confirmCreate} disabled={!newFileName.trim()} className="px-6 py-3 bg-blue-600 text-white rounded-xl text-xs font-black uppercase tracking-widest hover:bg-blue-500 transition neon-border disabled:opacity-30">Initialize</button>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* Sci-Fi Purge Modal */}
      {purgeFile && (
        <div className="fixed inset-0 z-[110] flex items-center justify-center p-6 backdrop-blur-2xl animate-in fade-in duration-300">
          <div className="fixed inset-0 bg-red-950/20" onClick={() => setPurgeFile(null)}></div>
          <div className="relative w-full max-w-md glass-panel border-red-500/30 rounded-3xl p-8 shadow-[0_0_80px_rgba(239,68,68,0.2)]">
            <div className="flex items-center gap-4 mb-6">
              <div className="w-12 h-12 bg-red-500/20 rounded-xl flex items-center justify-center text-red-500 animate-pulse">
                <svg className="w-8 h-8" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2.5" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" /></svg>
              </div>
              <div>
                <h3 className="text-xl font-bold text-red-500 uppercase tracking-tighter">Purge Sequence</h3>
                <p className="text-[10px] text-red-400 font-black tracking-widest uppercase">Danger: Irreversible Action</p>
              </div>
            </div>
            
            <div className="bg-red-500/5 border border-red-500/10 rounded-xl p-4 mb-8">
              <p className="text-sm text-slate-300 mb-2">Are you sure you want to de-initialize the following entity from the grid?</p>
              <div className="font-mono text-xs bg-black/30 p-2 rounded border border-red-500/20 text-red-400 truncate">
                ID: {purgeFile.filename}
              </div>
            </div>

            <div className="grid grid-cols-2 gap-4">
              <button onClick={() => setPurgeFile(null)} className="px-6 py-3 glass-panel border-white/5 rounded-xl text-xs font-bold uppercase tracking-widest text-slate-400 hover:bg-white/5 transition">Cancel</button>
              <button onClick={confirmPurge} className="px-6 py-3 bg-red-600 text-white rounded-xl text-xs font-black uppercase tracking-widest hover:bg-red-500 transition-all shadow-lg shadow-red-600/20 border border-red-400/50">Purge Node</button>
            </div>
          </div>
        </div>
      )}

      {!editingFile && <UploadManager onComplete={fetchFiles} />}
    </div>
  );
};

export default App;
