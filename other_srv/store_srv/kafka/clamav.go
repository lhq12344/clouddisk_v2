package kafka

import (
	"bufio"
	"context"
	"encoding/binary"
	"fmt"
	"go_test/internal"
	"io"
	"net"
	"strings"
	"time"

	"go.uber.org/zap"
)

type ClamAVVerdict struct {
	Infected  bool
	Signature string
	Raw       string
}

func dialClamAV(ctx context.Context, timeout time.Duration, cfg internal.ClamAVConfig) (net.Conn, error) {
	dialer := &net.Dialer{Timeout: timeout}
	host := strings.TrimSpace(cfg.Host)
	port := strings.TrimSpace(cfg.Port)
	explicitSocket := strings.TrimSpace(cfg.Socket)
	socketPath := strings.TrimSpace(cfg.SocketPath())

	if explicitSocket != "" {
		conn, err := dialer.DialContext(ctx, "unix", explicitSocket)
		if err == nil {
			return conn, nil
		}
		return nil, fmt.Errorf("%w: dial configured clamd unix socket %s: %v", ErrServiceUnavailable, explicitSocket, err)
	}

	if socketPath != "" && isLocalClamAVHost(host) {
		conn, err := dialer.DialContext(ctx, "unix", socketPath)
		if err == nil {
			return conn, nil
		}
		if host == "" || port == "" {
			return nil, fmt.Errorf("%w: dial clamd unix socket: %v", ErrServiceUnavailable, err)
		}
		internal.Logger.Warn("[ClamAV]local unix socket unavailable, falling back to tcp",
			zap.String("socket_path", socketPath),
			zap.String("tcp_addr", net.JoinHostPort(host, port)),
			zap.Error(err))
	}

	if host != "" && port != "" {
		conn, err := dialer.DialContext(ctx, "tcp", net.JoinHostPort(host, port))
		if err == nil {
			return conn, nil
		}
		return nil, fmt.Errorf("%w: dial clamd tcp=%v", ErrServiceUnavailable, err)
	}

	return nil, fmt.Errorf("%w: clamav host/port not configured", ErrServiceUnavailable)
}

func isLocalClamAVHost(host string) bool {
	host = strings.TrimSpace(strings.ToLower(host))
	switch host {
	case "", "localhost":
		return true
	}

	ip := net.ParseIP(host)
	return ip != nil && ip.IsLoopback()
}

func ScanReaderWithClamAV(ctx context.Context, reader io.Reader, objectSize int64) (*ClamAVVerdict, error) {
	cfg := internal.ViperConf.ClamAV
	resolvedSocket := strings.TrimSpace(cfg.SocketPath())
	effectiveLimit := cfg.EffectiveStreamLimitBytes()
	timeout := cfg.Timeout()
	if effectiveLimit > 0 && objectSize > effectiveLimit {
		return nil, fmt.Errorf("%w: object size %d exceeds configured clamav limit %d", ErrFileTooLarge, objectSize, effectiveLimit)
	}
	dialCtx := ctx
	if _, hasDeadline := ctx.Deadline(); !hasDeadline {
		var cancel context.CancelFunc
		dialCtx, cancel = context.WithTimeout(ctx, timeout)
		defer cancel()
	}

	conn, err := dialClamAV(dialCtx, timeout, cfg)
	if err != nil {
		return nil, err
	}
	defer conn.Close()

	internal.Logger.Info("[ClamAV]scan session established",
		zap.String("remote_addr", conn.RemoteAddr().String()),
		zap.String("configured_socket", strings.TrimSpace(cfg.Socket)),
		zap.String("resolved_socket", resolvedSocket),
		zap.String("tcp_addr", net.JoinHostPort(strings.TrimSpace(cfg.Host), strings.TrimSpace(cfg.Port))),
		zap.Duration("timeout", timeout),
		zap.Int64("effective_limit_bytes", effectiveLimit),
		zap.Int64("object_size", objectSize))

	deadline := time.Now().Add(timeout)
	if dl, ok := dialCtx.Deadline(); ok {
		deadline = dl
	}
	_ = conn.SetDeadline(deadline)

	if _, err := conn.Write([]byte("nINSTREAM\n")); err != nil {
		return nil, classifyClamAVWriteError(conn, timeout, "send clamd command", err)
	}

	buf := make([]byte, 32*1024)
	sizeBuf := make([]byte, 4)
	for {
		n, readErr := reader.Read(buf)
		if n > 0 {
			binary.BigEndian.PutUint32(sizeBuf, uint32(n))
			if _, err := conn.Write(sizeBuf); err != nil {
				return nil, classifyClamAVWriteError(conn, timeout, "send chunk length", err)
			}
			if _, err := conn.Write(buf[:n]); err != nil {
				return nil, classifyClamAVWriteError(conn, timeout, "send chunk body", err)
			}
		}
		if readErr == io.EOF {
			break
		}
		if readErr != nil {
			return nil, fmt.Errorf("%w: read object stream: %v", ErrTemporaryFailure, readErr)
		}
	}

	binary.BigEndian.PutUint32(sizeBuf, 0)
	if _, err := conn.Write(sizeBuf); err != nil {
		return nil, classifyClamAVWriteError(conn, timeout, "finalize instream", err)
	}

	reply, err := bufio.NewReader(conn).ReadString('\n')
	if err != nil {
		return nil, fmt.Errorf("%w: read clamd response: %v", ErrServiceUnavailable, err)
	}

	return parseClamAVReply(reply)
}

func classifyClamAVWriteError(conn net.Conn, timeout time.Duration, stage string, writeErr error) error {
	if reply := tryReadClamAVReply(conn, timeout); reply != "" {
		_, err := parseClamAVReply(reply)
		if err != nil {
			return err
		}
		return fmt.Errorf("%w: unexpected clamd response while %s: %s", ErrServiceUnavailable, stage, reply)
	}
	return fmt.Errorf("%w: %s: %v", ErrConnectionReset, stage, writeErr)
}

func tryReadClamAVReply(conn net.Conn, timeout time.Duration) string {
	_ = conn.SetReadDeadline(time.Now().Add(minDuration(timeout, 500*time.Millisecond)))
	reply, err := bufio.NewReader(conn).ReadString('\n')
	if err != nil {
		return ""
	}
	return strings.TrimSpace(reply)
}

func parseClamAVReply(reply string) (*ClamAVVerdict, error) {
	reply = strings.TrimSpace(reply)
	switch {
	case reply == "":
		return nil, fmt.Errorf("%w: empty clamd response", ErrServiceUnavailable)
	case strings.HasSuffix(reply, "OK"):
		return &ClamAVVerdict{Raw: reply}, nil
	case strings.HasSuffix(reply, "FOUND"):
		signature := strings.TrimSpace(strings.TrimSuffix(strings.TrimPrefix(reply, "stream:"), "FOUND"))
		return &ClamAVVerdict{Infected: true, Signature: signature, Raw: reply}, nil
	case strings.Contains(strings.ToLower(reply), "size limit exceeded"):
		return nil, fmt.Errorf("%w: %s", ErrFileTooLarge, reply)
	default:
		return nil, fmt.Errorf("%w: unexpected clamd response: %s", ErrServiceUnavailable, reply)
	}
}

func minDuration(a, b time.Duration) time.Duration {
	if a <= 0 {
		return b
	}
	if a < b {
		return a
	}
	return b
}
