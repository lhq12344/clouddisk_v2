package main

import (
	"fmt"
	"log"

	"go_test/backword_part/model"
	"gorm.io/driver/mysql"
	"gorm.io/gorm"
)

func main() {
	// 自动迁移 API Key 相关表
	log.Println("Starting MCP API Key database migration...")

	// 直接连接数据库（不依赖 Nacos）
	dsn := "root:123456@tcp(127.0.0.1:3306)/clouddisk?charset=utf8mb4&parseTime=True&loc=Local"
	db, err := gorm.Open(mysql.Open(dsn), &gorm.Config{})
	if err != nil {
		log.Fatalf("Failed to connect to database: %v", err)
	}

	// 迁移表结构
	err = db.AutoMigrate(
		&model.MCPAPIKey{},
		&model.MCPAPIKeyLog{},
		&model.MCPRateLimit{},
	)

	if err != nil {
		log.Fatalf("Migration failed: %v", err)
	}

	log.Println("✓ Migration completed successfully!")

	// 验证表是否创建成功
	tables := []string{"mcp_api_keys", "mcp_api_key_logs", "mcp_rate_limits"}
	for _, table := range tables {
		var exists bool
		err := db.Raw("SELECT EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema = DATABASE() AND table_name = ?)", table).Scan(&exists).Error
		if err != nil {
			log.Printf("✗ Failed to check table %s: %v", table, err)
		} else if exists {
			log.Printf("✓ Table %s created successfully", table)
		} else {
			log.Printf("✗ Table %s not found", table)
		}
	}

	fmt.Println("\nMigration summary:")
	fmt.Println("- mcp_api_keys: API Key 主表")
	fmt.Println("- mcp_api_key_logs: 使用日志表")
	fmt.Println("- mcp_rate_limits: 限流记录表")
}
