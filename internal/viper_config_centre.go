package internal

import (
	"bytes"
	"fmt"
	"os"
	"strconv"
	"strings"

	"github.com/nacos-group/nacos-sdk-go/v2/clients"
	"github.com/nacos-group/nacos-sdk-go/v2/common/constant"
	"github.com/nacos-group/nacos-sdk-go/v2/vo"
	"github.com/spf13/viper"
	"go.uber.org/zap"
)

type ViperConfig struct {
	RedisConfig  RedisConfig  `mapstructure:"redis"`
	MysqlConfig  MysqlConfig  `mapstructure:"mysql"`
	ConsulConfig ConsulConfig `mapstructure:"consul"`
	JWTConfig    JWTConfig    `mapstructure:"jwt"`
	KafkaConfig  KafkaConfig  `mapstructure:"kafka"`
	Alioss       Alioss       `mapstructure:"alioss"`
	MinIO        MinioConf    `mapstructure:"minio"`
	ClamAV       ClamAVConfig `mapstructure:"clamav"`
}

var ViperConf ViperConfig

//var filename = "./dev-config.yaml"

func envOrDefault(key, fallback string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return fallback
}

func envIntOrDefault(key string, fallback int) int {
	if v := os.Getenv(key); v != "" {
		if parsed, err := strconv.Atoi(v); err == nil {
			return parsed
		}
	}
	return fallback
}

func envBoolOrDefault(key string, fallback bool) bool {
	if v := strings.TrimSpace(os.Getenv(key)); v != "" {
		if parsed, err := strconv.ParseBool(v); err == nil {
			return parsed
		}
	}
	return fallback
}

func init() {
	v := viper.New()
	v.SetConfigType("json")

	nacosHost := envOrDefault("NACOS_HOST", "127.0.0.1")
	nacosPort := envIntOrDefault("NACOS_PORT", 30848)
	nacosNamespace := envOrDefault("NACOS_NAMESPACE", "ce99961c-0fcf-4f4f-81d6-ac2183f24df1")
	nacosContextPath := envOrDefault("NACOS_CONTEXT_PATH", "/nacos")
	nacosAuthEnabled := envBoolOrDefault("NACOS_AUTH_ENABLED", false)
	nacosUsername := strings.TrimSpace(os.Getenv("NACOS_USERNAME"))
	nacosPassword := strings.TrimSpace(os.Getenv("NACOS_PASSWORD"))
	nacosLogDir := envOrDefault("NACOS_LOG_DIR", "/home/lihaoqian/project/clouddisk_v2/log/nacos")
	nacosCacheDir := envOrDefault("NACOS_CACHE_DIR", "/home/lihaoqian/project/clouddisk_v2/log/nacos/cache")

	// 1. Nacos 服务配置
	serverConfigs := []constant.ServerConfig{
		{
			IpAddr:      nacosHost,
			Port:        uint64(nacosPort),
			Scheme:      "http",
			ContextPath: nacosContextPath,
		},
	}

	// 2. 客户端配置（开发环境默认不启用鉴权）
	clientConfig := constant.ClientConfig{
		NamespaceId:         nacosNamespace,
		TimeoutMs:           10000,
		NotLoadCacheAtStart: true,
		LogLevel:            "debug",
		LogDir:              nacosLogDir,
		CacheDir:            nacosCacheDir,
		ContextPath:         nacosContextPath,
	}
	if nacosAuthEnabled {
		if nacosUsername == "" || nacosPassword == "" {
			Logger.Warn("Nacos auth is enabled but credentials are incomplete")
		}
		clientConfig.Username = nacosUsername
		clientConfig.Password = nacosPassword
	}
	Logger.Info("Creating Nacos client...",
		zap.String("server", fmt.Sprintf("%s:%d", nacosHost, nacosPort)),
		zap.String("namespace", clientConfig.NamespaceId),
		zap.Bool("authEnabled", nacosAuthEnabled),
		zap.String("contextPath", nacosContextPath))

	client, err := clients.NewConfigClient(vo.NacosClientParam{
		ClientConfig:  &clientConfig,
		ServerConfigs: serverConfigs,
	})
	if err != nil {
		Logger.Error("create nacos client failed", zap.Error(err))
		return
	}

	// 3. 热更新监听
	err = client.ListenConfig(vo.ConfigParam{
		DataId: "clouddisk.json",
		Group:  "dev",
		OnChange: func(namespace, group, dataId, data string) {
			Logger.Warn("nacos config changed")
			if data == "" {
				Logger.Error("nacos returned empty hot-update config")
				return
			}

			if err := v.ReadConfig(bytes.NewBuffer([]byte(data))); err != nil {
				Logger.Error("viper read hot update failed", zap.Error(err))
				return
			}
			if err := v.Unmarshal(&ViperConf); err != nil {
				Logger.Error("unmarshal hot update failed", zap.Error(err))
				return
			}
		},
	})
	if err != nil {
		Logger.Error("listen config failed", zap.Error(err))
		return
	}

	// 4. 初次拉取配置
	content, err := client.GetConfig(vo.ConfigParam{
		DataId: "clouddisk.json",
		Group:  "dev",
	})
	if err != nil || content == "" {
		Logger.Error("get initial config failed", zap.Error(err))
		return
	}

	// 5. 解析 yaml -> struct
	if err := v.ReadConfig(bytes.NewBuffer([]byte(content))); err != nil {
		Logger.Error("viper read config failed", zap.Error(err))
		return
	}
	if err := v.Unmarshal(&ViperConf); err != nil {
		Logger.Error("unmarshal config failed", zap.Error(err))
		return
	}

	Logger.Info("Nacos config loaded successfully")

	// 6. 初始化模块
	InitRedis()
	Initdb()
	InitConsul()
	InitKafkaProducer()
	InitAli()
	InitMinio()
	Logger.Info("All services initialized")
}
