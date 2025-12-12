package log

import (
	"os"

	"go.uber.org/zap"
	"go.uber.org/zap/zapcore"
	"gopkg.in/natefinch/lumberjack.v2"
)

var Logger *zap.Logger

func init() {
	var err error
	Logger, err = NewLogger()
	if err != nil {
		panic(err)
	}
}

// NewLogger 自动滚动日志（不会无限增大）
func NewLogger() (*zap.Logger, error) {
	config := zap.NewProductionEncoderConfig()
	config.TimeKey = "timestamp"

	jsonEncoder := zapcore.NewJSONEncoder(config)

	// 文件写入器
	fileWriter := zapcore.AddSync(&lumberjack.Logger{
		Filename:   "accountHandler.log",
		MaxSize:    50,
		MaxBackups: 3,
		MaxAge:     30,
		Compress:   true,
	})

	// stdout
	consoleWriter := zapcore.AddSync(os.Stdout)

	core := zapcore.NewTee(
		zapcore.NewCore(jsonEncoder, fileWriter, zap.InfoLevel),
		zapcore.NewCore(jsonEncoder, consoleWriter, zap.InfoLevel),
	)

	return zap.New(core), nil
}
