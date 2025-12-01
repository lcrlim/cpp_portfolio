cmake_minimum_required(VERSION 3.15)
project(TcpServer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00)
endif()

# 1. 패키지 찾기
find_package(Boost REQUIRED)
if(TARGET Boost::asio)
    set(BOOST_ASIO_TARGET Boost::asio)
else()
    set(BOOST_ASIO_TARGET Boost::headers)
endif()

find_package(Protobuf CONFIG REQUIRED)
find_package(spdlog REQUIRED)

include(FetchContent)

# nlohmann_json
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found. Fetching via FetchContent...")
    FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(json)
    if(NOT TARGET nlohmann_json::nlohmann_json)
        add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
    endif()
endif()

# yaml-cpp
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    message(STATUS "yaml-cpp not found. Fetching via FetchContent...")
    FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG 0.8.0
        PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_yaml.cmake
    )
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(yaml-cpp)
    if(NOT TARGET yaml-cpp::yaml-cpp)
        add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
    endif()
endif()

# 2. Protobuf 생성 경로 설정
set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(PROTO_SRC ${PROTO_GEN_DIR}/game.pb.cc)
set(PROTO_HDR ${PROTO_GEN_DIR}/game.pb.h)

if(APPLE)
    if(EXISTS "/opt/homebrew/bin/protoc")
        set(PROTOC_CMD "/opt/homebrew/bin/protoc")
    else()
        set(PROTOC_CMD "protoc")
    endif()
else()
    if(TARGET protobuf::protoc)
        set(PROTOC_CMD $<TARGET_FILE:protobuf::protoc>)
    else()
        set(PROTOC_CMD "protoc")
    endif()
endif()

add_custom_command(
    OUTPUT ${PROTO_SRC} ${PROTO_HDR}
    COMMAND ${PROTOC_CMD} --cpp_out=${PROTO_GEN_DIR} --proto_path=${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/game.proto
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/game.proto
    COMMENT "Generating Protobuf files..."
)

# Create a static library for protobuf-generated files
add_library(protobuf_messages STATIC ${PROTO_SRC})
target_include_directories(protobuf_messages PRIVATE ${PROTO_GEN_DIR}) # Include generated headers path
target_link_libraries(protobuf_messages PUBLIC protobuf::libprotobuf) # Link protobuf library to the static library



# --- [Server] ---
add_executable(Server Server.cpp)
target_include_directories(Server PRIVATE ${PROTO_GEN_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(Server PRIVATE 
    protobuf_messages # Link the new static library
    ${BOOST_ASIO_TARGET}
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
)

add_custom_command(
    TARGET Server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/server_config.yaml"
            "$<TARGET_FILE_DIR:Server>/server_config.yaml"
    COMMENT "Copying server_config.yaml to Server executable directory"
)

if(WIN32)
    target_link_libraries(Server PRIVATE DbgHelp)
endif()

# --- [Client] ---
add_executable(Client Client.cpp)
target_include_directories(Client PRIVATE ${PROTO_GEN_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(Client PRIVATE 
    protobuf_messages # Link the new static library
    ${BOOST_ASIO_TARGET}
    spdlog::spdlog
)