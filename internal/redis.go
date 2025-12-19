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

var RedisClient *redis.Client

func InitRedis() {
	h := ViperConf.RedisConfig.Host
	p := ViperConf.RedisConfig.Port
	addr := fmt.Sprintf("%s:%s", h, p)
	RedisClient = redis.NewClient(&redis.Options{
		Addr: addr,
	})
	ping := RedisClient.Ping(context.Background())
	fmt.Println(ping.String())
}
