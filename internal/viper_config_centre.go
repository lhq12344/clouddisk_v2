package internal

import (
	"bytes"

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
}

var ViperConf ViperConfig

//var filename = "./dev-config.yaml"

func init() {
	v := viper.New()
	v.SetConfigType("json")

	// 1. Nacos 服务配置
	serverConfigs := []constant.ServerConfig{
		{
			IpAddr: "127.0.0.1",
			Port:   30848, // NodePort
			Scheme: "http",
		},
	}

	// 2. 客户端配置（必须带用户名密码 + 正确 namespace）
	clientConfig := constant.ClientConfig{
		NamespaceId:         "ce99961c-0fcf-4f4f-81d6-ac2183f24df1", // ← 更新
		TimeoutMs:           10000,
		NotLoadCacheAtStart: true,
		LogLevel:            "debug",
		LogDir:              "/home/lihaoqian/project/clouddisk_v2/log/nacos",
		CacheDir:            "/home/lihaoqian/project/clouddisk_v2/log/nacos/cache",
		Username:            "nacos", // ← 重要
		Password:            "nacos", // ← 重要
	}
	Logger.Info("Creating Nacos client...",
		zap.String("server", "127.0.0.1:30848"),
		zap.String("namespace", clientConfig.NamespaceId))

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
