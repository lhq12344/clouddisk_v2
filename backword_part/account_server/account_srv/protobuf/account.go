package accountpb

import (
	"context"
	"crypto/md5"
	"fmt"
	"go_test/backword_part/account_server/custom_error"
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
	l := internal.LoggerWithRID(ctx, internal.Logger)
	var account model.Account
	result := internal.DB.Where(&model.Account{Name: signin.Username}).First(&account)
	if result.RowsAffected == 0 {
		l.Info(custom_error.AccountNotFound, zap.String("username", signin.Username))
		return Model2Pb(1, custom_error.AccountNotFound), errors.New(custom_error.AccountNotFound)
	}
	options := password.Options{
		SaltLen:      16,
		Iterations:   100,
		KeyLen:       32,
		HashFunction: md5.New,
	}
	ret := password.Verify(signin.Password, account.Salt, account.Password, &options)
	if !ret {
		l.Info(custom_error.PasswordError, zap.String("username", signin.Username))
		return Model2Pb(2, custom_error.PasswordError), errors.New(custom_error.PasswordError)
	}
	option := internal.CustomClaims{
		ID:   int32(account.ID),
		Name: account.Name,
		StandardClaims: jwt.StandardClaims{
			ExpiresAt: time.Now().Add(time.Hour * 24).Unix(),
			Issuer:    "Signin",
		},
	}
	token, err := internal.GetJWT().GenerateToken(option)
	if err != nil {
		l.Error(custom_error.TokenError, zap.String("username", signin.Username), zap.Error(err))
		return Model2Pb(3, custom_error.TokenError), errors.New(custom_error.TokenError)
	}
	l.Info("ISSUE JWT TO USER", zap.String("username", signin.Username))
	return Model2Pb(0, token), nil
}

func (a *AccountServer) Signup(ctx context.Context, signup *ReqSignup) (*Resp, error) {
	l := internal.LoggerWithRID(ctx, internal.Logger)

	var existing model.Account
	result := internal.DB.Where(&model.Account{Name: signup.Username}).First(&existing)
	if result.RowsAffected == 1 {
		l.Info(custom_error.AccountExists, zap.String("username", signup.Username))
		return Model2Pb(1, custom_error.AccountExists), errors.New(custom_error.AccountExists)
	}

	options := password.Options{
		SaltLen:      16,
		Iterations:   100,
		KeyLen:       32,
		HashFunction: md5.New,
	}
	salt, encodedPwd := password.Encode(signup.Password, &options)

	tempData := map[string]string{
		"username": signup.Username,
		"email":    signup.Email,
		"salt":     salt,
		"password": encodedPwd,
	}
	payload, _ := json.Marshal(tempData)

	pendingKey := fmt.Sprintf("pending_reg:%s", signup.Email)
	err := internal.RedisClient.Set(ctx, pendingKey, payload, 10*time.Minute).Err()
	if err != nil {
		l.Error("failed to store pending registration", zap.Error(err))
		return Model2Pb(2, custom_error.InternalError), errors.New(custom_error.InternalError)
	}

	l.Info("PENDING REGISTRATION Stored, awaiting email verification",
		zap.String("username", signup.Username),
		zap.String("email", signup.Email))
	return Model2Pb(0, "PENDING VERIFICATION"), nil
}

func (a *AccountServer) VerifyCode(ctx context.Context, req *Reqverifycode) (*Resp, error) {
	l := internal.LoggerWithRID(ctx, internal.Logger)

	pendingKey := fmt.Sprintf("pending_reg:%s", req.Email)
	payload, err := internal.RedisClient.Get(ctx, pendingKey).Bytes()
	if err != nil {
		l.Info("no pending registration found for email", zap.String("email", req.Email))
		return Model2Pb(1, "no pending registration, please sign up first"), errors.New("no pending registration")
	}

	var tempData map[string]string
	if err := json.Unmarshal(payload, &tempData); err != nil {
		l.Error("failed to unmarshal pending registration", zap.Error(err))
		return Model2Pb(2, custom_error.InternalError), errors.New(custom_error.InternalError)
	}

	account := model.Account{
		Name:     tempData["username"],
		Email:    tempData["email"],
		Salt:     tempData["salt"],
		Password: tempData["password"],
	}
	createResult := internal.DB.Create(&account)
	if createResult.Error != nil {
		l.Error("failed to create account after verification", zap.Error(createResult.Error))
		return Model2Pb(3, custom_error.InternalError), errors.New(custom_error.InternalError)
	}

	internal.RedisClient.Del(ctx, pendingKey).Result()

	l.Info("ACCOUNT CREATED AFTER EMAIL VERIFICATION",
		zap.String("username", account.Name),
		zap.String("email", account.Email))
	return Model2Pb(0, "REGISTRATION COMPLETE"), nil
}

func (c *AccountServer) Userinfo(ctx context.Context, Userinfo *ReqUserinfo) (*Resp, error) {
	l := internal.LoggerWithRID(ctx, internal.Logger)
	var account model.Account
	result := internal.DB.Where("id = ?", Userinfo.ID).First(&account)
	if result.RowsAffected == 0 {
		l.Info(custom_error.AccountAbnormal, zap.String("username", Userinfo.Username))
		return Model2Pb(1, custom_error.AccountAbnormal), errors.New(custom_error.AccountAbnormal)
	}
	data, _ := json.Marshal(map[string]any{
		"createdAt": account.CreatedAt,
		"updatedAt": account.UpdatedAt,
		"username":  account.Name,
		"name":      account.Name,
		"email":     account.Email,
		"gender":    account.Gender,
		"mobile":    account.Mobile,
		"Mobile":    account.Mobile,
	})
	l.Info("FIND ACCOUNT", zap.String("username", Userinfo.Username))
	return Model2Pb(0, string(data)), nil
}

func (a *AccountServer) mustEmbedUnimplementedAccountServiceServer() {
	panic("implement me")
}
