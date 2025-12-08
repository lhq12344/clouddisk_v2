cmake_minimum_required(VERSION 3.5)
project(gateway CXX)

include(CheckIncludeFileCXX)

check_include_file_cxx(any HAS_ANY)
check_include_file_cxx(string_view HAS_STRING_VIEW)
check_include_file_cxx(coroutine HAS_COROUTINE)
if (NOT "${CMAKE_CXX_STANDARD}" STREQUAL "")
    # Do nothing
elseif (HAS_ANY AND HAS_STRING_VIEW AND HAS_COROUTINE)
    set(CMAKE_CXX_STANDARD 20)
elseif (HAS_ANY AND HAS_STRING_VIEW)
    set(CMAKE_CXX_STANDARD 17)
else ()
    set(CMAKE_CXX_STANDARD 14)
endif ()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(${PROJECT_NAME} main.cc)

# ##############################################################################
# If you include the drogon source code locally in your project, use this method
# to add drogon 
# add_subdirectory(drogon) 
# target_link_libraries(${PROJECT_NAME} PRIVATE drogon)
#
# and comment out the following lines
find_package(Drogon CONFIG REQUIRED)
find_package(Drogon REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon)
target_link_libraries(gateway PRIVATE Drogon::Drogon)
# ##############################################################################

# Link protobuf and gRPC for generated stubs used by controllers
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE protobuf::libprotobuf gRPC::grpc++ )

# Link libcurl (used by consul client) and Nacos SDK C++
find_package(CURL REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)

# Nacos SDK C++ typically installs headers to /usr/local/include/nacos and lib to /usr/local/lib
find_library(NACOSCPP_LIB nacos-cli PATHS /usr/local/lib /usr/lib)
if (NACOSCPP_LIB)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${NACOSCPP_LIB})
else()
    message(WARNING "Nacos C++ SDK library 'nacos-cli' not found. Ensure it's installed under /usr/local/lib.")
endif()

if (CMAKE_CXX_STANDARD LESS 17)
    message(FATAL_ERROR "c++17 or higher is required")
elseif (CMAKE_CXX_STANDARD LESS 20)
    message(STATUS "use c++17")
else ()
    message(STATUS "use c++20")
endif ()

aux_source_directory(controllers CTL_SRC)
aux_source_directory(filters FILTER_SRC)
aux_source_directory(plugins PLUGIN_SRC)
aux_source_directory(models MODEL_SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/../internal INTERNAL_SRC)

# Logs source
set(LOGS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../logs/Logger.cpp)

# gRPC/protobuf generated sources for account service client
set(ACCOUNT_PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../proto/account_srv)
set(ACCOUNT_PROTO_SRCS
    ${ACCOUNT_PROTO_DIR}/account.pb.cc
    ${ACCOUNT_PROTO_DIR}/account.grpc.pb.cc
)

drogon_create_views(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/views
                    ${CMAKE_CURRENT_BINARY_DIR})
# use the following line to create views with namespaces.
# drogon_create_views(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/views
#                     ${CMAKE_CURRENT_BINARY_DIR} TRUE)
# use the following line to create views with namespace CHANGE_ME prefixed
# and path namespaces.
# drogon_create_views(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/views
#                     ${CMAKE_CURRENT_BINARY_DIR} TRUE CHANGE_ME)

target_include_directories(${PROJECT_NAME}
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                   ${CMAKE_CURRENT_SOURCE_DIR}/models)
target_include_directories(${PROJECT_NAME}
               PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../internal
                   ${CMAKE_CURRENT_SOURCE_DIR}/../logs
                   ${ACCOUNT_PROTO_DIR})
target_include_directories(${PROJECT_NAME}
                PRIVATE /usr/local/include/nacos
                        /usr/include/nacos)
target_include_directories(gateway PRIVATE
    /home/lihaoqian/Tools/jwt-cpp-master/include
)
target_sources(${PROJECT_NAME}
               PRIVATE
               ${SRC_DIR}
               ${CTL_SRC}
               ${FILTER_SRC}
               ${PLUGIN_SRC}
           ${MODEL_SRC}
           ConsulRegister.cpp
           ${INTERNAL_SRC}
           ${LOGS_SRC}
           ${ACCOUNT_PROTO_SRCS})
# ##############################################################################
# uncomment the following line for dynamically loading views 
# set_property(TARGET ${PROJECT_NAME} PROPERTY ENABLE_EXPORTS ON)

# ##############################################################################

add_subdirectory(test)
