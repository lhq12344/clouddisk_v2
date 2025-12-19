package internal

import (
	"sync"
	"time"

	"github.com/dgrijalva/jwt-go"
	"github.com/pkg/errors"
)

type JWTConfig struct {
	SigningKey string `mapstructure:"signing_key"`
}

const (
	TokenExpired     = "token过期"
	TokenNotValidYet = "token不再有效"
	TokenMalformed   = "token非法"
	TokenInvalid     = "token无效"
)

type CustomClaims struct {
	jwt.StandardClaims
	ID   int32
	Name string
}

type JWT struct {
	SigningKey []byte
}

var jwtOnce sync.Once
var jwtInstance *JWT

func GetJWT() *JWT {
	jwtOnce.Do(func() {
		jwtInstance = &JWT{SigningKey: []byte(ViperConf.JWTConfig.SigningKey)}
	})
	return jwtInstance
}

func (j *JWT) GenerateToken(claims CustomClaims) (string, error) {
	token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	tokenStr, err := token.SignedString(j.SigningKey)
	if err != nil {
		return "", err
	}
	return tokenStr, nil
}

func (j *JWT) ParseToken(tokenString string) (*CustomClaims, string) {
	token, err := jwt.ParseWithClaims(tokenString, &CustomClaims{}, func(token *jwt.Token) (interface{}, error) {
		return j.SigningKey, nil
	})
	if err != nil {
		var result jwt.ValidationError
		if errors.As(err, &result) {
			if result.Errors&jwt.ValidationErrorMalformed != 0 {
				return nil, TokenMalformed
			} else if result.Errors&jwt.ValidationErrorExpired != 0 {
				return nil, TokenExpired
			} else if result.Errors&jwt.ValidationErrorNotValidYet != 0 {
				return nil, TokenNotValidYet
			} else {
				return nil, TokenInvalid
			}
		}
	}
	if token != nil {
		if claims, ok := token.Claims.(*CustomClaims); ok && token.Valid {
			return claims, ""
		}
		return nil, TokenInvalid
	} else {
		return nil, TokenInvalid
	}

}

func (j *JWT) RefreshToken(tokenString string) (string, error) {
	//jwt-go 默认使用 time.Now() 判断过期,过期无法解析
	//所以需要重置时间为远古时间（即使过期也能解析）
	//即使已过期，也能“绕过验证”，成功解析出 payload（claims）内容。
	jwt.TimeFunc = func() time.Time {
		return time.Unix(0, 0)
	}
	token, err := jwt.ParseWithClaims(tokenString, &CustomClaims{}, func(token *jwt.Token) (interface{}, error) {
		return j.SigningKey, nil
	})
	if err != nil {
		return "", err
	}
	if claims, ok := token.Claims.(*CustomClaims); ok && token.Valid {
		jwt.TimeFunc = time.Now
		claims.StandardClaims.ExpiresAt = time.Now().Add(1 * time.Hour).Unix()
		return j.GenerateToken(*claims)
	}
	return "", errors.New(TokenInvalid)
}
