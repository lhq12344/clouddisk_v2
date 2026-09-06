package internal

import (
	"errors"
	"fmt"
	"go_test/backword_part/model"
	"log"
	"os"
	"strings"
	"time"

	mysqlDriver "github.com/go-sql-driver/mysql"
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

func isTableAlreadyExistsError(err error) bool {
	var mysqlErr *mysqlDriver.MySQLError
	if errors.As(err, &mysqlErr) && mysqlErr.Number == 1050 {
		return true
	}

	return strings.Contains(err.Error(), "Error 1050")
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
			LogLevel:                  logger.Warn,
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
	// 按表逐个迁移，避免已有开发表结构导致整个服务启动失败。
	migrations := []struct {
		name  string
		model interface{}
	}{
		{name: "accounts", model: &model.Account{}},
		{name: "files", model: &model.File{}},
		{name: "user_files", model: &model.UserFile{}},
		{name: "upload_sessions", model: &model.UploadSession{}},
		{name: "outboxes", model: &model.Outbox{}},
		{name: "inboxes", model: &model.Inbox{}},
		{name: "dlq_failures", model: &model.DLQFailure{}},
	}

	for _, migration := range migrations {
		err = DB.AutoMigrate(migration.model)
		if err == nil {
			continue
		}
		if isTableAlreadyExistsError(err) {
			fmt.Printf("表 %s 已存在，跳过创建\n", migration.name)
			continue
		}
		panic(fmt.Sprintf("建表失败(%s): %v", migration.name, err))
	}
	fmt.Println("数据库表检查完成，程序运行中...按 Ctrl+C 退出")
	return DB
}
