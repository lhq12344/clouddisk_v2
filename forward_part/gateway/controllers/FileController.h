#pragma once

#include <drogon/HttpController.h>
#include "../../../proto/file_srv/file.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include "../ArcCache/ArcCache.h"
#include "../ArcCache/ArcCacheNode.h"
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
	mutable Cache::KArcCache<std::string, ServiceInstance> cache_;
	std::shared_ptr<file::fileService::Stub> FindService(const std::string &key) const;

public:
	FileController()
		: CAPACITY(20),
		  cache_(CAPACITY) {};
	METHOD_LIST_BEGIN
	// use METHOD_ADD to add your custom processing function here;
	ADD_METHOD_TO(FileController::filequeryinfo, "/file/query", Post, "jwt_decode");
	ADD_METHOD_TO(FileController::filedowm, "/file/download", Get, "jwt_decode");
	ADD_METHOD_TO(FileController::LoadFile, "/file/upload", Post, "jwt_decode");
	ADD_METHOD_TO(FileController::Showfile, "/file/showfile", Get, "jwt_decode");
	METHOD_LIST_END

	void filequeryinfo(const HttpRequestPtr &req,
					   std::function<void(const HttpResponsePtr &)> &&callback) const;
	void filedowm(const HttpRequestPtr &req,
				  std::function<void(const HttpResponsePtr &)> &&callback) const;
	void LoadFile(const HttpRequestPtr &req,
				  std::function<void(const HttpResponsePtr &)> &&callback) const;
	void Showfile(const HttpRequestPtr &req,
				  std::function<void(const HttpResponsePtr &)> &&callback) const;
};
