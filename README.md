# C++20 TCP Game Server

이 프로젝트는 **Boost.Asio**와 **C++20 Coroutines**를 활용하여 개발된 고성능 비동기 TCP 게임 서버 프로토타입입니다. 
동시 접속자 처리(100K)를 목표로 설계되었으며, Google Protobuf를 사용하여 패킷 직렬화를 처리합니다.

## 🚀 Key Features

* **Modern C++20**: `co_await`를 활용한 직관적인 비동기 코드 작성.
* **High Performance**: Boost.Asio (IOCP/epoll) 기반의 넌블러킹 I/O 모델.
* **Packet Serialization**: Google Protocol Buffers (Protobuf 3) 사용.
* **Fast Logging**: `spdlog` 비동기 로거 적용으로 로깅 병목 최소화.
* **Scalability**: 멀티 스레드 I/O Context 구조 (1 Thread per Core).
* **Stress Test**: 대량의 더미 클라이언트 연결 및 패킷 전송을 시뮬레이션하는 로드 테스터 포함.

## 🛠 Tech Stack

* **Language**: C++20
* **Network**: Boost.Asio
* **Serialization**: Google Protobuf
* **Logging**: spdlog
* **Build System**: CMake (Cross-platform) or MSBuild (Windows)

## 📋 Prerequisites

### macOS
```bash
brew install boost protobuf spdlog cmake
