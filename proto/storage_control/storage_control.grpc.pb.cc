// Hand-written minimal C++ client stub for proto/storage_control/storage_control.proto.
// Keep this small until grpc_cpp_plugin is available in the build environment.
#include "storage_control.grpc.pb.h"

#include <grpcpp/impl/client_unary_call.h>

namespace storage_control
{

StorageControl::Stub::Stub(const std::shared_ptr<::grpc::ChannelInterface> &channel,
                           const ::grpc::StubOptions &options)
    : channel_(channel),
      rpcmethod_InitiateMultipart_("/storage_control.StorageControl/InitiateMultipart", options.suffix_for_stats(), ::grpc::internal::RpcMethod::NORMAL_RPC, channel),
      rpcmethod_PresignPart_("/storage_control.StorageControl/PresignPart", options.suffix_for_stats(), ::grpc::internal::RpcMethod::NORMAL_RPC, channel),
      rpcmethod_ListParts_("/storage_control.StorageControl/ListParts", options.suffix_for_stats(), ::grpc::internal::RpcMethod::NORMAL_RPC, channel),
      rpcmethod_CompleteMultipart_("/storage_control.StorageControl/CompleteMultipart", options.suffix_for_stats(), ::grpc::internal::RpcMethod::NORMAL_RPC, channel),
      rpcmethod_AbortMultipart_("/storage_control.StorageControl/AbortMultipart", options.suffix_for_stats(), ::grpc::internal::RpcMethod::NORMAL_RPC, channel),
      rpcmethod_PresignGet_("/storage_control.StorageControl/PresignGet", options.suffix_for_stats(), ::grpc::internal::RpcMethod::NORMAL_RPC, channel),
      rpcmethod_HeadObject_("/storage_control.StorageControl/HeadObject", options.suffix_for_stats(), ::grpc::internal::RpcMethod::NORMAL_RPC, channel)
{
}

::grpc::Status StorageControl::Stub::InitiateMultipart(::grpc::ClientContext *context,
                                                       const ::storage_control::InitiateMultipartReq &request,
                                                       ::storage_control::InitiateMultipartResp *response)
{
    return ::grpc::internal::BlockingUnaryCall<::storage_control::InitiateMultipartReq, ::storage_control::InitiateMultipartResp, ::grpc::protobuf::MessageLite, ::grpc::protobuf::MessageLite>(channel_.get(), rpcmethod_InitiateMultipart_, context, request, response);
}

::grpc::Status StorageControl::Stub::PresignPart(::grpc::ClientContext *context,
                                                 const ::storage_control::PresignPartReq &request,
                                                 ::storage_control::PresignPartResp *response)
{
    return ::grpc::internal::BlockingUnaryCall<::storage_control::PresignPartReq, ::storage_control::PresignPartResp, ::grpc::protobuf::MessageLite, ::grpc::protobuf::MessageLite>(channel_.get(), rpcmethod_PresignPart_, context, request, response);
}

::grpc::Status StorageControl::Stub::ListParts(::grpc::ClientContext *context,
                                               const ::storage_control::ListPartsReq &request,
                                               ::storage_control::ListPartsResp *response)
{
    return ::grpc::internal::BlockingUnaryCall<::storage_control::ListPartsReq, ::storage_control::ListPartsResp, ::grpc::protobuf::MessageLite, ::grpc::protobuf::MessageLite>(channel_.get(), rpcmethod_ListParts_, context, request, response);
}

::grpc::Status StorageControl::Stub::CompleteMultipart(::grpc::ClientContext *context,
                                                       const ::storage_control::CompleteMultipartReq &request,
                                                       ::storage_control::CompleteMultipartResp *response)
{
    return ::grpc::internal::BlockingUnaryCall<::storage_control::CompleteMultipartReq, ::storage_control::CompleteMultipartResp, ::grpc::protobuf::MessageLite, ::grpc::protobuf::MessageLite>(channel_.get(), rpcmethod_CompleteMultipart_, context, request, response);
}

::grpc::Status StorageControl::Stub::AbortMultipart(::grpc::ClientContext *context,
                                                    const ::storage_control::AbortMultipartReq &request,
                                                    ::storage_control::AbortMultipartResp *response)
{
    return ::grpc::internal::BlockingUnaryCall<::storage_control::AbortMultipartReq, ::storage_control::AbortMultipartResp, ::grpc::protobuf::MessageLite, ::grpc::protobuf::MessageLite>(channel_.get(), rpcmethod_AbortMultipart_, context, request, response);
}

::grpc::Status StorageControl::Stub::PresignGet(::grpc::ClientContext *context,
                                                const ::storage_control::PresignGetReq &request,
                                                ::storage_control::PresignGetResp *response)
{
    return ::grpc::internal::BlockingUnaryCall<::storage_control::PresignGetReq, ::storage_control::PresignGetResp, ::grpc::protobuf::MessageLite, ::grpc::protobuf::MessageLite>(channel_.get(), rpcmethod_PresignGet_, context, request, response);
}

::grpc::Status StorageControl::Stub::HeadObject(::grpc::ClientContext *context,
                                                const ::storage_control::HeadObjectReq &request,
                                                ::storage_control::HeadObjectResp *response)
{
    return ::grpc::internal::BlockingUnaryCall<::storage_control::HeadObjectReq, ::storage_control::HeadObjectResp, ::grpc::protobuf::MessageLite, ::grpc::protobuf::MessageLite>(channel_.get(), rpcmethod_HeadObject_, context, request, response);
}

} // namespace storage_control
