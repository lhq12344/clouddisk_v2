#include "accountServer.h"

grpc::ServerUnaryReactor *AccountServiceImpl::Signin(
	grpc::CallbackServerContext *context,
	const ReqSignin *request,
	RespSignin *reply)
{
	auto *reactor = context->DefaultReactor();

	std::cout << "[Signin] username=" << request->username()
			  << " password=" << request->password() << std::endl;

	reply->set_code(0);
	reply->set_message("Signin OK");

	reactor->Finish(grpc::Status::OK);
	return reactor;
}

// ==== Signup（异步一元 RPC）====
grpc::ServerUnaryReactor *AccountServiceImpl::Signup(
	grpc::CallbackServerContext *context,
	const ReqSignup *request,
	RespSignup *reply)
{
	auto *reactor = context->DefaultReactor();
	LOG_INFO("[Signup_srv] username={}", request->username());
	reply->set_code(0);
	reply->set_message("Signup OK");
	reactor->Finish(grpc::Status::OK);
	return reactor;
}

// ==== VerifyCode（异步一元 RPC）====
grpc::ServerUnaryReactor *AccountServiceImpl::VerifyCode(
	grpc::CallbackServerContext *context,
	const Reqverifycode *request,
	Respverifycode *reply)
{
	auto *reactor = context->DefaultReactor();

	std::cout << "[VerifyCode] email=" << request->email()
			  << " code=" << request->code() << std::endl;

	reply->set_code(0);
	reply->set_message("Verify OK");

	reactor->Finish(grpc::Status::OK);
	return reactor;
}