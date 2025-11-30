# C++20 TCP Server

이 프로젝트는 **Boost.Asio**와 **C++20 Coroutines**를 활용하여 개발된 고성능 비동기 TCP 게임 서버 프로토타입입니다. 
동시 접속자 처리(100K)를 목표로 설계되었으며, Google Protobuf를 사용하여 패킷 직렬화를 처리합니다.

## Key Features

* **Modern C++20**: `co_await`를 활용한 직관적인 비동기 코드 작성.
* **High Performance**: Boost.Asio (IOCP/epoll) 기반의 넌블러킹 I/O 모델.
* **Packet Serialization**: Google Protocol Buffers (Protobuf 3) 사용.
* **Fast Logging**: `spdlog` 비동기 로거 적용으로 로깅 병목 최소화.
* **Scalability**: 멀티 스레드 I/O Context 구조 (1 Thread per Core).
* **Stress Test**: 대량의 더미 클라이언트 연결 및 패킷 전송을 시뮬레이션하는 로드 테스터 포함.

## Tech Stack

* **Language**: C++20
* **Network**: Boost.Asio
* **Serialization**: Google Protobuf
* **Logging**: spdlog
* **Build System**: CMake (Cross-platform) or MSBuild (Windows)


## 1. 빌드 환경

### Windows (Visual Studio 2022)
Windows 환경에서는 Visual Studio 솔루션 파일을 생성하여 빌드하는 것을 권장합니다.
이 리포지토리에는 미리 생성된 솔루션 파일(.sln)이 포함되어 있지 않습니다. (사용자마다 경로가 다르기 때문)

1. `vcpkg`가 설치되어 있어야 합니다.
2. **CMake**를 사용하여 솔루션 파일을 생성합니다.
   ```powershell
   # 프로젝트 루트(cpp_portfolio) 상위 폴더에서 실행한다고 가정
   cmake -G "Visual Studio 17 2022" -S cpp_portfolio -B cpp_portfolio/build -DCMAKE_TOOLCHAIN_FILE=C:/work/vcpkg/scripts/buildsystems/vcpkg.cmake
   ```
3. `cpp_portfolio/vs_build/TcpServer.sln` 파일을 Visual Studio로 엽니다.
4. F7을 눌러 빌드하거나 실행(F5)합니다.

### macOS (CMake)
Mac 환경에서는 터미널에서 CMake를 직접 사용하여 빌드합니다.
`brew`를 통해 필요한 패키지를 먼저 설치해야 합니다.

```bash
# 1. 패키지 설치
brew install boost protobuf spdlog abseil nlohmann-json cmake

# 2. 빌드 폴더 생성 및 이동
mkdir build && cd build

# 3. CMake 설정
cmake ..

# 4. 빌드
make
```

## 2. 실행
- **Server**: `vs_build/Debug/Server.exe` (또는 `./Server`)
- **Client**: `vs_build/Debug/Client.exe` (또는 `./Client`)