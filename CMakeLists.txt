cmake_minimum_required(VERSION 3.15)
project(TcpServer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00)
endif()

# 1. 패키지 찾기 (Abseil 추가됨)
find_package(Boost REQUIRED)
find_package(Protobuf REQUIRED)
find_package(spdlog REQUIRED)
find_package(absl REQUIRED) # <-- 핵심 추가
find_package(nlohmann_json REQUIRED)

# 2. Protobuf 생성 경로 설정
set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(PROTO_SRC ${PROTO_GEN_DIR}/game.pb.cc)
set(PROTO_HDR ${PROTO_GEN_DIR}/game.pb.h)

if(APPLE)
    set(PROTOC_CMD "protoc")
else()
    set(PROTOC_CMD $<TARGET_FILE:protobuf::protoc>)
endif()

add_custom_command(
    OUTPUT ${PROTO_SRC} ${PROTO_HDR}
    COMMAND ${PROTOC_CMD} --cpp_out=${PROTO_GEN_DIR} --proto_path=${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/game.proto
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/game.proto
    COMMENT "Generating Protobuf files..."
)



# --- [Server] ---
add_executable(Server Server.cpp ${PROTO_SRC} ${PROTO_HDR})
target_include_directories(Server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROTO_GEN_DIR} ${Protobuf_INCLUDE_DIRS})
target_link_libraries(Server PRIVATE 
    Boost::headers 
    protobuf::libprotobuf 
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(Server PRIVATE DbgHelp)
endif()

# --- [Client] ---
add_executable(Client Client.cpp ${PROTO_SRC} ${PROTO_HDR})
target_include_directories(Client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROTO_GEN_DIR} ${Protobuf_INCLUDE_DIRS})
target_link_libraries(Client PRIVATE 
    Boost::headers 
    protobuf::libprotobuf 
    spdlog::spdlog
)