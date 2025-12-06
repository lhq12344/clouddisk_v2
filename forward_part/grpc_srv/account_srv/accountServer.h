#ifndef ACCOUNTSERVER_H
#define ACCOUNTSERVER_H

#pragma once
#include "../../proto/account_srv/account.grpc.pb.h"
#include "../../logs/Logger.h"
#include <grpcpp/grpcpp.h>
#include <iostream>

using namespace account;

class AccountServiceImpl final : public accountService::CallbackService
{
public:
	// ==== Signin（异步一元 RPC）====
	grpc::ServerUnaryReactor *Signin(
		grpc::CallbackServerContext *context,
		const ReqSignin *request,
		RespSignin *reply) override;

	// ==== Signup（异步一元 RPC）====
	grpc::ServerUnaryReactor *Signup(
		grpc::CallbackServerContext *context,
		const ReqSignup *request,
		RespSignup *reply) override;

	// ==== VerifyCode（异步一元 RPC）====
	grpc::ServerUnaryReactor *VerifyCode(
		grpc::CallbackServerContext *context,
		const Reqverifycode *request,
		Respverifycode *reply) override;
};

#endif // !ACCOUNTSERVER_H
