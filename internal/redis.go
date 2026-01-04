package internal

import (
	"context"
	"fmt"

	"github.com/go-redis/redis/v8"
)

type RedisConfig struct {
	Host string `mapstructure:"host"`
	Port string `mapstructure:"port"`
}

// var RedisClient *redis.Client//只适合单机
var RedisClient redis.UniversalClient //可以适用于哨兵/集群/单机

func InitRedis() {
	h := ViperConf.RedisConfig.Host
	p := ViperConf.RedisConfig.Port
	addr := fmt.Sprintf("%s:%s", h, p)
	RedisClient = redis.NewUniversalClient(&redis.UniversalOptions{
		Addrs: []string{addr},
		//MasterName: "mymaster",
		// DB: 0,
		// Password: "...",//哨兵模式使用
	})

	if err := RedisClient.Ping(context.Background()).Err(); err != nil {
		panic(err)
	}
}
