
import React, { useState } from 'react';
import { api } from '../services/api';
import { AuthMode } from '../types';

interface AuthPageProps {
  onSuccess: () => void;
  theme: 'dark' | 'light';
}

export const AuthPage: React.FC<AuthPageProps> = ({ onSuccess, theme }) => {
  const [mode, setMode] = useState<AuthMode>('signin');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [formData, setFormData] = useState({
    username: '',
    password: '',
    email: '',
    code: ''
  });

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setLoading(true);
    setError(null);

    try {
      if (mode === 'signin') {
        await api.signin({ username: formData.username, password: formData.password });
        onSuccess();
      } else if (mode === 'signup') {
        await api.signup({ 
          username: formData.username, 
          password: formData.password, 
          email: formData.email 
        });
        await api.sendCode(formData.email);
        setMode('verify');
      } else if (mode === 'verify') {
        await api.verifyCode(formData.email, formData.code);
        setMode('signin');
      }
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  };

  const handleSendCode = async () => {
    if (!formData.email) return setError('Identifier required');
    try {
      setLoading(true);
      await api.sendCode(formData.email);
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  };

  const inputClasses = "w-full px-5 py-3.5 rounded-xl border border-white/10 bg-black/20 text-white placeholder-slate-500 focus:ring-2 focus:ring-blue-500 focus:border-transparent outline-none transition-all font-mono text-sm";

  return (
    <div className="min-h-screen flex items-center justify-center p-6 relative overflow-hidden">
      <div className="max-w-md w-full glass-panel rounded-3xl shadow-[0_0_100px_rgba(0,0,0,0.5)] p-10 space-y-8 relative z-10 border-white/10">
        <div className="text-center">
          <div className="w-16 h-16 bg-blue-600 rounded-2xl flex items-center justify-center mx-auto mb-6 neon-border">
            <svg className="w-10 h-10 text-white" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2.5" d="M13 10V3L4 14h7v7l9-11h-7z" />
            </svg>
          </div>
          <h1 className="text-4xl font-bold tracking-tighter neon-text uppercase">NOVA STORAGE</h1>
          <p className="text-slate-500 mt-2 text-xs font-mono uppercase tracking-widest">
            {mode === 'signin' ? 'Sign in to your account' : mode === 'signup' ? 'Create your account' : 'Enter verification code'}
          </p>
        </div>

        <form onSubmit={handleSubmit} className="space-y-5">
          {error && (
            <div className="bg-red-500/10 text-red-400 p-4 rounded-xl text-xs font-mono border border-red-500/30 animate-pulse">
              [SYSTEM_ERROR]: {error}
            </div>
          )}

          {(mode === 'signin' || mode === 'signup') && (
            <div className="space-y-1.5">
              <label className="text-[10px] font-black text-blue-500 uppercase px-1 tracking-widest">Username_ID</label>
              <input
                type="text"
                placeholder="USER_NAME_01"
                className={inputClasses}
                value={formData.username}
                onChange={(e) => setFormData({ ...formData, username: e.target.value })}
                required
              />
            </div>
          )}

          {mode === 'signup' && (
            <div className="space-y-1.5">
              <label className="text-[10px] font-black text-blue-500 uppercase px-1 tracking-widest">Neural_Comms</label>
              <input
                type="email"
                placeholder="user@nexus.io"
                className={inputClasses}
                value={formData.email}
                onChange={(e) => setFormData({ ...formData, email: e.target.value })}
                required
              />
            </div>
          )}

          {(mode === 'signin' || mode === 'signup') && (
            <div className="space-y-1.5">
              <label className="text-[10px] font-black text-blue-500 uppercase px-1 tracking-widest">Access_Key</label>
              <input
                type="password"
                placeholder="••••••••"
                className={inputClasses}
                value={formData.password}
                onChange={(e) => setFormData({ ...formData, password: e.target.value })}
                required
              />
            </div>
          )}

          {mode === 'verify' && (
            <div className="space-y-5">
              <div className="text-center">
                <p className="text-green-400 text-xs font-mono">
                  Code sent to <span className="text-white font-bold">{formData.email}</span>
                </p>
              </div>
              <div className="space-y-1.5">
                <label className="text-[10px] font-black text-blue-500 uppercase px-1 tracking-widest">Identifier</label>
                <input
                  type="email"
                  className="w-full px-5 py-3.5 rounded-xl border border-white/5 bg-white/5 text-slate-500 font-mono text-sm cursor-not-allowed"
                  value={formData.email}
                  disabled
                />
              </div>
              <div className="space-y-1.5">
                <label className="text-[10px] font-black text-blue-500 uppercase px-1 tracking-widest">Verification_Code</label>
                <input
                  type="text"
                  placeholder="Enter 6-digit code"
                  className={inputClasses}
                  value={formData.code}
                  onChange={(e) => setFormData({ ...formData, code: e.target.value })}
                  maxLength={6}
                  required
                />
              </div>
            </div>
          )}

          <button
            type="submit"
            disabled={loading}
            className="w-full bg-blue-600 text-white py-4 rounded-2xl font-black text-sm uppercase tracking-widest hover:bg-blue-500 transition-all disabled:opacity-50 neon-border shadow-blue-600/20 active:scale-[0.98]"
          >
            {loading ? 'Processing...' : mode === 'signin' ? 'Sign In' : mode === 'signup' ? 'Create Account' : 'Verify'}
          </button>
        </form>

        <div className="text-center text-[10px] font-bold text-slate-500 uppercase tracking-widest">
          {mode === 'signin' ? (
            <p>New user? <button onClick={() => setMode('signup')} className="text-blue-500 hover:underline">Create Account</button></p>
          ) : mode === 'signup' ? (
            <p>Already have an account? <button onClick={() => setMode('signin')} className="text-blue-500 hover:underline">Sign In</button></p>
          ) : (
            <p>Didn't receive it? <button onClick={handleSendCode} className="text-blue-500 hover:underline">Resend</button></p>
          )}
        </div>
      </div>
    </div>
  );
};
