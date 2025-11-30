cmake_minimum_required(VERSION 3.15)
project(TcpGameServer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. 패키지 찾기 (Abseil 추가됨)
find_package(Boost REQUIRED)
find_package(Protobuf REQUIRED)
find_package(spdlog REQUIRED)
find_package(absl REQUIRED) # <-- 핵심 추가

# 2. Protobuf 생성 경로 설정
set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(PROTO_SRC ${PROTO_GEN_DIR}/game.pb.cc)
set(PROTO_HDR ${PROTO_GEN_DIR}/game.pb.h)

add_custom_command(
    OUTPUT ${PROTO_SRC} ${PROTO_HDR}
    COMMAND protoc --cpp_out=${PROTO_GEN_DIR} --proto_path=${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/game.proto
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/game.proto
    COMMENT "Generating Protobuf files..."
)

# 3. Abseil 라이브러리 목록 (Protobuf가 의존하는 핵심 라이브러리들)
# Protobuf 버전에 따라 다르지만 보통 아래 것들이 필수입니다.
set(ABSL_LIBS 
    absl::base
    absl::strings
    absl::log_internal_message
    absl::log_internal_check_op
    absl::log_internal_nullguard
    absl::log_globals
)

# --- [Server] ---
add_executable(Server Server.cpp ${PROTO_SRC} ${PROTO_HDR})
target_include_directories(Server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROTO_GEN_DIR} ${Protobuf_INCLUDE_DIRS})
target_link_libraries(Server PRIVATE 
    Boost::headers 
    protobuf::libprotobuf 
    spdlog::spdlog
    ${ABSL_LIBS} # <-- Abseil 링크 추가
)

# --- [Client] ---
add_executable(Client Client.cpp ${PROTO_SRC} ${PROTO_HDR})
target_include_directories(Client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROTO_GEN_DIR} ${Protobuf_INCLUDE_DIRS})
target_link_libraries(Client PRIVATE 
    Boost::headers 
    protobuf::libprotobuf 
    spdlog::spdlog
    ${ABSL_LIBS} # <-- Abseil 링크 추가
)