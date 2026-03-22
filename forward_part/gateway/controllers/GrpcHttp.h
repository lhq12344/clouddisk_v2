#pragma once

#include <drogon/HttpResponse.h>
#include <grpcpp/grpcpp.h>
#include <json/json.h>

#include <string>

inline drogon::HttpStatusCode grpcStatusToHttpStatus(const grpc::Status &status)
{
	switch (status.error_code())
	{
	case grpc::StatusCode::OK:
		return drogon::k200OK;
	case grpc::StatusCode::INVALID_ARGUMENT:
	case grpc::StatusCode::OUT_OF_RANGE:
		return drogon::k400BadRequest;
	case grpc::StatusCode::UNAUTHENTICATED:
		return drogon::k401Unauthorized;
	case grpc::StatusCode::PERMISSION_DENIED:
		return drogon::k403Forbidden;
	case grpc::StatusCode::NOT_FOUND:
		return drogon::k404NotFound;
	case grpc::StatusCode::ALREADY_EXISTS:
	case grpc::StatusCode::FAILED_PRECONDITION:
	case grpc::StatusCode::ABORTED:
		return drogon::k409Conflict;
	case grpc::StatusCode::RESOURCE_EXHAUSTED:
		return drogon::k429TooManyRequests;
	case grpc::StatusCode::UNIMPLEMENTED:
		return drogon::k501NotImplemented;
	case grpc::StatusCode::UNAVAILABLE:
		return drogon::k503ServiceUnavailable;
	case grpc::StatusCode::DEADLINE_EXCEEDED:
		return drogon::k504GatewayTimeout;
	case grpc::StatusCode::UNKNOWN:
	case grpc::StatusCode::INTERNAL:
	case grpc::StatusCode::DATA_LOSS:
		// 这三类都表示上游 gRPC 服务未给出可安全降级的业务语义，
		// 对外仍按服务端内部错误暴露；若后续需要更细粒度 HTTP 语义，
		// 应在服务端显式返回 canonical gRPC status code。
		return drogon::k500InternalServerError;
	default:
		return drogon::k500InternalServerError;
	}
}

inline Json::Value grpcErrorPayload(const grpc::Status &status, const std::string &error = "grpc_error")
{
	Json::Value ret;
	ret["error"] = error;
	ret["grpc_code"] = static_cast<int>(status.error_code());
	if (!status.error_message().empty())
	{
		ret["details"] = status.error_message();
	}
	return ret;
}

inline drogon::HttpResponsePtr grpcErrorResponse(const grpc::Status &status, const std::string &error = "grpc_error")
{
	auto resp = drogon::HttpResponse::newHttpJsonResponse(grpcErrorPayload(status, error));
	resp->setStatusCode(grpcStatusToHttpStatus(status));
	return resp;
}
