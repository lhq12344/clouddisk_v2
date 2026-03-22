package internal

import (
	"os"

	"go.uber.org/zap"
	"go.uber.org/zap/zapcore"
	"gopkg.in/natefinch/lumberjack.v2"
)

var Logger *zap.Logger
var Log *zap.SugaredLogger

func init() {
	var err error
	Logger, Log, err = NewLogger()
	if err != nil {
		panic(err)
	}
}

// NewLogger 自动滚动日志（不会无限增大）
func NewLogger() (*zap.Logger, *zap.SugaredLogger, error) {
	// 文件写入器
	fileWriter := zapcore.AddSync(&lumberjack.Logger{
		Filename:   "/home/lihaoqian/project/clouddisk_v2/log/app.log",
		MaxSize:    50,
		MaxBackups: 3,
		MaxAge:     30,
		Compress:   true,
	})

	encoderCfg := zapcore.EncoderConfig{
		TimeKey:        "time",
		LevelKey:       "level",
		NameKey:        "logger",
		CallerKey:      "caller",
		MessageKey:     "msg",
		StacktraceKey:  "stacktrace",
		LineEnding:     zapcore.DefaultLineEnding,
		EncodeLevel:    zapcore.CapitalColorLevelEncoder, // 彩色日志级别
		EncodeTime:     zapcore.ISO8601TimeEncoder,       // 时间格式
		EncodeDuration: zapcore.StringDurationEncoder,
		EncodeCaller:   zapcore.ShortCallerEncoder,
	}
	consoleEncoder := zapcore.NewConsoleEncoder(encoderCfg)
	fileEncoder := zapcore.NewJSONEncoder(encoderCfg)

	isTTY := func() bool {
		f, _ := os.Stdout.Stat()
		return f != nil && (f.Mode()&os.ModeCharDevice) != 0
	}

	core := zapcore.NewTee(
		zapcore.NewCore(consoleEncoder, zapcore.AddSync(os.Stdout), zap.DebugLevel),
		zapcore.NewCore(fileEncoder, fileWriter, zap.InfoLevel),
	)
	if !isTTY() {
		core = zapcore.NewTee(
			zapcore.NewCore(fileEncoder, zapcore.AddSync(os.Stdout), zap.DebugLevel),
			zapcore.NewCore(fileEncoder, fileWriter, zap.InfoLevel),
		)
	}
	logger := zap.New(core, zap.AddCaller(), zap.AddCallerSkip(1))
	sugar := logger.Sugar()
	return logger, sugar, nil
}
