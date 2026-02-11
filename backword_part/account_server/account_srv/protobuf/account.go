package accountpb

import (
	"context"
	"crypto/md5"
	"go_test/backword_part/account_server/custom_error"
	"go_test/backword_part/log"
	"go_test/backword_part/model"
	"go_test/internal"
	"time"

	"github.com/anaskhan96/go-password-encoder"
	"github.com/dgrijalva/jwt-go"
	"github.com/goccy/go-json"
	"github.com/pkg/errors"
	"go.uber.org/zap"
)

func Model2Pb(code int32, message string) *Resp {
	accountRes := &Resp{
		Code:    code,
		Message: message,
	}
	return accountRes
}

type AccountServer struct {
	UnimplementedAccountServiceServer
}

func (a *AccountServer) Signin(ctx context.Context, signin *ReqSignin) (*Resp, error) {
	var account model.Account
	result := internal.DB.Where(&model.Account{Name: signin.Username}).First(&account)
	if result.RowsAffected == 0 {
		log.Logger.Info(custom_error.AccountNotFound, zap.String("username", signin.Username))
		return Model2Pb(1, custom_error.AccountNotFound), errors.New(custom_error.AccountNotFound)
	}
	//判断密码
	options := password.Options{
		SaltLen:      16,
		Iterations:   100,
		KeyLen:       32,
		HashFunction: md5.New,
	}
	ret := password.Verify(signin.Password, account.Salt, account.Password, &options)
	if !ret {
		log.Logger.Info(custom_error.PasswordError, zap.String("username", signin.Username))
		return Model2Pb(2, custom_error.PasswordError), errors.New(custom_error.PasswordError)
	}
	//生成jwt_token
	option := internal.CustomClaims{
		ID:   int32(account.ID),
		Name: account.Name,
		StandardClaims: jwt.StandardClaims{
			ExpiresAt: time.Now().Add(time.Hour * 24).Unix(), // 有效期
			Issuer:    "Signin",                              // 签发者
		},
	}
	token, err := internal.GetJWT().GenerateToken(option)
	if err != nil {
		log.Logger.Error(custom_error.TokenError, zap.String("username", signin.Username), zap.Error(err))
		return Model2Pb(3, custom_error.TokenError), errors.New(custom_error.TokenError)
	}
	//存入redis来缓存
	//返回token
	log.Logger.Info("ISSUE JWT TO USER", zap.String("username", signin.Username))
	return Model2Pb(0, token), nil
}

// Todo要具体返回错误原因
func (a *AccountServer) Signup(ctx context.Context, signup *ReqSignup) (*Resp, error) {
	var account model.Account
	result := internal.DB.Where(&model.Account{Name: signup.Username}).First(&account)
	if result.RowsAffected == 1 {
		log.Logger.Info(custom_error.AccountExists, zap.String("username", signup.Username))
		return Model2Pb(1, custom_error.AccountExists), errors.New(custom_error.AccountExists)
	}
	options := password.Options{
		SaltLen:      16,
		Iterations:   100,
		KeyLen:       32,
		HashFunction: md5.New,
	}
	salt, encodePwd := password.Encode(signup.Password, &options)
	account.Salt = salt
	account.Password = encodePwd
	account.Name = signup.Username
	account.Email = signup.Email
	r := internal.DB.Create(&account)
	if r.Error != nil {
		log.Logger.Error(custom_error.InternalError, zap.Error(r.Error))
		return Model2Pb(2, custom_error.InternalError), errors.New(custom_error.InternalError)
	}
	log.Logger.Info("FIND ACCOUNT", zap.String("username", signup.Username))
	return Model2Pb(0, "FIND ACCOUNT"), nil
}

func (c *AccountServer) Userinfo(ctx context.Context, Userinfo *ReqUserinfo) (*Resp, error) {
	var account model.Account
	result := internal.DB.Where("id = ?", Userinfo.ID).First(&account)
	if result.RowsAffected == 0 {
		log.Logger.Info(custom_error.AccountAbnormal, zap.String("username", Userinfo.Username))
		return Model2Pb(1, custom_error.AccountAbnormal), errors.New(custom_error.AccountAbnormal)
	}
	//json传输数据给客户端
	data, _ := json.Marshal(map[string]any{
		"createdAt": account.CreatedAt,
		"updatedAt": account.UpdatedAt,
		"name":      account.Name,
		"gender":    account.Gender,
		"Mobile":    account.Mobile,
	})
	log.Logger.Info("FIND ACCOUNT", zap.String("username", Userinfo.Username))
	return Model2Pb(0, string(data)), nil
}

func (a *AccountServer) mustEmbedUnimplementedAccountServiceServer() {
	panic("implement me")
}
