package log

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
		Filename:   "storeSrv.log",
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
	// 控制台输出
	consoleEncoder := zapcore.NewConsoleEncoder(encoderCfg)

	// 文件输出为 JSON 格式
	fileEncoder := zapcore.NewJSONEncoder(encoderCfg)

	core := zapcore.NewTee(
		zapcore.NewCore(consoleEncoder, zapcore.AddSync(os.Stdout), zap.DebugLevel),
		zapcore.NewCore(fileEncoder, fileWriter, zap.InfoLevel),
	)
	logger := zap.New(core, zap.AddCaller(), zap.AddCallerSkip(1))
	sugar := logger.Sugar()
	return logger, sugar, nil
}
