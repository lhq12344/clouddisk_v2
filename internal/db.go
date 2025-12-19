package internal

import (
	"fmt"
	"go_test/model"
	"log"
	"os"
	"time"

	"gorm.io/driver/mysql"
	"gorm.io/gorm"
	"gorm.io/gorm/logger"
)

var DB *gorm.DB

type MysqlConfig struct {
	Host     string `mapstructure:"host"`
	Port     string `mapstructure:"port"`
	User     string `mapstructure:"user"`
	Password string `mapstructure:"password"`
}

func Initdb() *gorm.DB {
	// 先连接到 mysql 系统数据库
	var err error
	h := ViperConf.MysqlConfig.Host
	p := ViperConf.MysqlConfig.Port
	pwd := ViperConf.MysqlConfig.Password
	u := ViperConf.MysqlConfig.User
	dsn := fmt.Sprintf("%v:%v@tcp(%v:%v)/?charset=utf8mb4&parseTime=True&loc=Local", u, pwd, h, p)
	DB, err = gorm.Open(mysql.Open(dsn), &gorm.Config{})
	if err != nil {
		panic(fmt.Sprintf("数据库连接失败: %v", err))
	}

	// 创建 orm_test 数据库
	result := DB.Exec("CREATE DATABASE IF NOT EXISTS orm_test")
	if result.Error != nil {
		fmt.Printf("创建数据库失败: %v\n", result.Error)
	} else {
		fmt.Println("数据库 orm_test 创建成功或已存在")
	}

	// 现在连接到 orm_test 数据库
	dsn = fmt.Sprintf("%v:%v@tcp(%v:%v)/orm_test?charset=utf8mb4&parseTime=True&loc=Local", u, pwd, h, p)
	newLogger := logger.New(
		log.New(os.Stdout, "\r\n", log.LstdFlags),
		logger.Config{
			SlowThreshold:             time.Second,
			LogLevel:                  logger.Info,
			IgnoreRecordNotFoundError: true,
			Colorful:                  true,
		},
	)

	DB, err = gorm.Open(mysql.Open(dsn), &gorm.Config{
		Logger: newLogger,
	})
	if err != nil {
		panic(fmt.Sprintf("连接到 orm_test 数据库失败: %v", err))
	}
	//建表
	err = DB.AutoMigrate(&model.Account{}, &model.File{}, &model.UserFile{}, &model.Outbox{}, &model.Inbox{})
	if err != nil {
		panic(fmt.Sprintf("建表失败: %v", err))
	}
	fmt.Println("account数据库表创建成功，程序运行中...按 Ctrl+C 退出")
	return DB
}
