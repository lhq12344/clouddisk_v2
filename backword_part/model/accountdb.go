package model

import "gorm.io/gorm"

type Account struct {
	gorm.Model
	Mobile   string `gorm:"type:varchar(11);not null;index:idx_mobile"`
	Password string `gorm:"type:varchar(64);not null"`
	Nickname string `gorm:"type:varchar(32)"`
	Name     string `gorm:"type:varchar(32);uniqueIndex"`
	Gender   string `gorm:"type:varchar(6);default:male"`
	Role     int    `gorm:"type:int;default:1;comment:'1-普通用户,2-管理员'"`
	Salt     string `gorm:"type:varchar(20);not null"`
	Email    string `gorm:"type:varchar(32);uniqueIndex"`
}
