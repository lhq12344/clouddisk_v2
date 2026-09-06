// Hand-written minimal C++ client stub for proto/storage_control/storage_control.proto.
// Keep this small until grpc_cpp_plugin is available in the build environment.
#pragma once

#include "storage_control.pb.h"

#include <grpcpp/grpcpp.h>
#include <grpcpp/client_context.h>
#include <grpcpp/impl/codegen/proto_utils.h>
#include <grpcpp/impl/rpc_method.h>
#include <grpcpp/support/stub_options.h>

#include <memory>

namespace storage_control
{

class StorageControl final
{
public:
    static constexpr char const *service_full_name()
    {
        return "storage_control.StorageControl";
    }

    class Stub
    {
    public:
        explicit Stub(const std::shared_ptr<::grpc::ChannelInterface> &channel,
                      const ::grpc::StubOptions &options = ::grpc::StubOptions());

        ::grpc::Status InitiateMultipart(::grpc::ClientContext *context,
                                         const ::storage_control::InitiateMultipartReq &request,
                                         ::storage_control::InitiateMultipartResp *response);
        ::grpc::Status PresignPart(::grpc::ClientContext *context,
                                   const ::storage_control::PresignPartReq &request,
                                   ::storage_control::PresignPartResp *response);
        ::grpc::Status ListParts(::grpc::ClientContext *context,
                                 const ::storage_control::ListPartsReq &request,
                                 ::storage_control::ListPartsResp *response);
        ::grpc::Status CompleteMultipart(::grpc::ClientContext *context,
                                         const ::storage_control::CompleteMultipartReq &request,
                                         ::storage_control::CompleteMultipartResp *response);
        ::grpc::Status AbortMultipart(::grpc::ClientContext *context,
                                      const ::storage_control::AbortMultipartReq &request,
                                      ::storage_control::AbortMultipartResp *response);
        ::grpc::Status PresignGet(::grpc::ClientContext *context,
                                  const ::storage_control::PresignGetReq &request,
                                  ::storage_control::PresignGetResp *response);
        ::grpc::Status HeadObject(::grpc::ClientContext *context,
                                  const ::storage_control::HeadObjectReq &request,
                                  ::storage_control::HeadObjectResp *response);

    private:
        std::shared_ptr<::grpc::ChannelInterface> channel_;
        const ::grpc::internal::RpcMethod rpcmethod_InitiateMultipart_;
        const ::grpc::internal::RpcMethod rpcmethod_PresignPart_;
        const ::grpc::internal::RpcMethod rpcmethod_ListParts_;
        const ::grpc::internal::RpcMethod rpcmethod_CompleteMultipart_;
        const ::grpc::internal::RpcMethod rpcmethod_AbortMultipart_;
        const ::grpc::internal::RpcMethod rpcmethod_PresignGet_;
        const ::grpc::internal::RpcMethod rpcmethod_HeadObject_;
    };

    static std::unique_ptr<Stub> NewStub(const std::shared_ptr<::grpc::ChannelInterface> &channel,
                                         const ::grpc::StubOptions &options = ::grpc::StubOptions())
    {
        return std::unique_ptr<Stub>(new Stub(channel, options));
    }
};

} // namespace storage_control
