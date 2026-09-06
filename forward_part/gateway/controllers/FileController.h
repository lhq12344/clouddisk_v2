#pragma once

#include <drogon/HttpController.h>
#include <drogon/RequestStream.h>
#include <functional>
#include "../../../proto/storage_control/storage_control.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <memory>
#include "../ArcCache/ArcCache.h"
#include "../ArcCache/ARCtemp.h"
#include "../../internal/internal.h"
#include "../../internal/consul.h"
#include <jsoncpp/json/json.h>
#include "../MyAppData.h"
#include "../../logs/Logger.h"

using namespace drogon;

class FileController : public drogon::HttpController<FileController>
{
private:
	const int CAPACITY;
	mutable Cache::KArcCache<std::string, std::shared_ptr<ArcGrpcLB::Entry<::storage_control::StorageControl>>> storageControlCache_;
	std::shared_ptr<::storage_control::StorageControl::Stub> FindStorageControl(const std::string &key) const;

public:
	FileController()
		: CAPACITY(20),
		  storageControlCache_(CAPACITY) {};
	METHOD_LIST_BEGIN
	// use METHOD_ADD to add your custom processing function here;
	ADD_METHOD_TO(FileController::filequeryinfo, "/file/query", Post, "jwt_decode");
	ADD_METHOD_TO(FileController::filedowm, "/file/download", Post, "jwt_decode");
	ADD_METHOD_TO(FileController::LoadFile, "/file/upload", Post, "jwt_decode");
	ADD_METHOD_TO(FileController::Showfile, "/file/showfile", Post, "jwt_decode");
	ADD_METHOD_TO(FileController::DeleteFile, "/file/delete", Post, "jwt_decode");
	ADD_METHOD_TO(FileController::Initupload, "/file/initupload", Post, "jwt_decode");
	ADD_METHOD_TO(FileController::PresignParts, "/file/PresignParts", Post, "jwt_decode");
	ADD_METHOD_TO(FileController::Uploadpart, "/file/uploadpart", Post, "jwt_decode");
	ADD_METHOD_TO(FileController::CompleteMultipart, "/file/CompleteMultipart", Post, "jwt_decode");
	ADD_METHOD_TO(FileController::AbortMultipart, "/file/AbortMultipart", Post, "jwt_decode");
	ADD_METHOD_TO(FileController::Status, "/file/Status", Post, "jwt_decode");
	METHOD_LIST_END

	void filequeryinfo(const HttpRequestPtr &req,
					   std::function<void(const HttpResponsePtr &)> &&callback) const;
	void filedowm(const HttpRequestPtr &req,
				  std::function<void(const HttpResponsePtr &)> &&callback) const;
	void LoadFile(const HttpRequestPtr &req,
				  RequestStreamPtr &&streamCtx,
				  std::function<void(const HttpResponsePtr &)> &&callback) const;
	void Showfile(const HttpRequestPtr &req,
				  std::function<void(const HttpResponsePtr &)> &&callback) const;
	void Initupload(const HttpRequestPtr &req,
					std::function<void(const HttpResponsePtr &)> &&callback) const;
	void PresignParts(const HttpRequestPtr &req,
					  std::function<void(const HttpResponsePtr &)> &&callback) const;
	void Uploadpart(const HttpRequestPtr &req,
					std::function<void(const HttpResponsePtr &)> &&callback) const;
	void CompleteMultipart(const HttpRequestPtr &req,
						   std::function<void(const HttpResponsePtr &)> &&callback) const;
	void AbortMultipart(const HttpRequestPtr &req,
						std::function<void(const HttpResponsePtr &)> &&callback) const;
	void Status(const HttpRequestPtr &req,
				std::function<void(const HttpResponsePtr &)> &&callback) const;
	void DeleteFile(const HttpRequestPtr &req,
					std::function<void(const HttpResponsePtr &)> &&callback) const;
};
