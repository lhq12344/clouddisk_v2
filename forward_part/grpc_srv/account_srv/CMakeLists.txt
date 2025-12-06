cmake_minimum_required(VERSION 3.15)
project(account_srv CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ========== 找 gRPC / Protobuf ==========
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)
find_package(CURL REQUIRED)

message(STATUS "Using protobuf ${Protobuf_VERSION}")
message(STATUS "Using gRPC ${gRPC_VERSION}")

# ========== Proto 文件路径 ==========
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../proto/account_srv)
set(PROTO_SRC
    ${PROTO_DIR}/account.pb.cc
    ${PROTO_DIR}/account.grpc.pb.cc
)

set(PROTO_HDR
    ${PROTO_DIR}/account.pb.h
    ${PROTO_DIR}/account.grpc.pb.h
)

# ========== 源码文件 ==========
set(SOURCES
    main.cpp
    accountServer.cpp
	../../internal/consul.cpp
	../../internal/internal.cpp
	../../logs/Logger.cpp
    ${PROTO_SRC}
)


set(HEADERS
    accountServer.h
    ${PROTO_HDR}
)

# ========== 包含头文件目录 ==========
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROTO_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
    ${Protobuf_INCLUDE_DIRS}
	/usr/local/include
    /usr/local/include/nacos
)

# ========== 创建可执行文件 ==========
add_executable(account_srv ${SOURCES} ${HEADERS})

# ========== 链接 grpc / protobuf ==========
target_link_libraries(account_srv
	PRIVATE CURL::libcurl
    gRPC::grpc++
    gRPC::grpc++_reflection
    protobuf::libprotobuf
	nacos-cli  
)

