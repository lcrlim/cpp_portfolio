#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <vector>
#include <random>
#include <atomic> // 추가됨
#include <thread>
#include <iomanip>
#include "game.pb.h"
#include "Common.h"

using boost::asio::ip::tcp;
using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
namespace this_coro = boost::asio::this_coro;

// --- [전역 통계 변수] ---
std::atomic<int> g_TargetCount{0};       // 목표 접속 수
std::atomic<int> g_ActiveSessions{0};    // 현재 연결된 세션 수
std::atomic<int> g_TotalSent{0};         // 총 전송 패킷 수

// 랜덤 ID 생성기
std::string GenerateRandomID() {
    static const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string s;
    for (int i = 0; i < 8; ++i) s += alphanum[rand() % (sizeof(alphanum) - 1)];
    return s;
}

// 진행률 바 그리기 함수
void DrawProgressBar(int current, int total, int width = 50) {
    float progress = (total == 0) ? 0.0f : (float)current / total;
    if (progress > 1.0f) progress = 1.0f;
    
    int pos = (int)(width * progress);
    
    std::cout << "[";
    for (int i = 0; i < width; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << int(progress * 100.0) << " %\r";
    std::cout.flush();
}

awaitable<void> RunClient(boost::asio::io_context& io_context, int clientIdx) {
    auto executor = co_await this_coro::executor;
    std::string userId = "User_" + std::to_string(clientIdx) + "_" + GenerateRandomID();
    
    boost::asio::steady_timer timer(executor);

    while (true) {
        tcp::socket socket(executor);
        bool connected = false;

        try {
            tcp::resolver resolver(executor);
            auto endpoints = co_await resolver.async_resolve("127.0.0.1", "11000", use_awaitable);
            
            // 1. 연결 시도
            co_await boost::asio::async_connect(socket, endpoints, use_awaitable);
            
            // [통계] 연결 성공 시 증가
            connected = true;
            g_ActiveSessions++; 

            // 2. 로그인 요청 전송
            game::LoginRequest req;
            req.set_user_id(userId);
            
            uint16_t bodySize = (uint16_t)req.ByteSizeLong();
            std::vector<uint8_t> sendBuf(sizeof(PacketHeader) + bodySize);
            PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuf.data());
            header->length = bodySize;
            header->id = game::LOGIN_REQ;
            req.SerializeToArray(sendBuf.data() + sizeof(PacketHeader), bodySize);

            co_await boost::asio::async_write(socket, boost::asio::buffer(sendBuf), use_awaitable);
            g_TotalSent++;

            // 3. Get Character List 100 times
            for (int i = 0; i < 100; ++i) {
                game::GetCharacterListRequest charReq;
                charReq.set_user_id(userId);

                bodySize = (uint16_t)charReq.ByteSizeLong();
                sendBuf.resize(sizeof(PacketHeader) + bodySize);
                header = reinterpret_cast<PacketHeader*>(sendBuf.data());
                header->length = bodySize;
                header->id = game::GET_CHARACTER_LIST_REQ;
                charReq.SerializeToArray(sendBuf.data() + sizeof(PacketHeader), bodySize);

                co_await boost::asio::async_write(socket, boost::asio::buffer(sendBuf), use_awaitable);
                g_TotalSent++;
                
                // 10ms 딜레이 (부하 조절)
                timer.expires_after(std::chrono::milliseconds(10));
                co_await timer.async_wait(use_awaitable);
            }

            // 4. 로그아웃 요청 전송
            game::LogoutRequest logoutReq;
            logoutReq.set_user_id(userId);
            
            bodySize = (uint16_t)logoutReq.ByteSizeLong();
            sendBuf.resize(sizeof(PacketHeader) + bodySize);
            header = reinterpret_cast<PacketHeader*>(sendBuf.data());
            header->length = bodySize;
            header->id = game::LOGOUT_REQ;
            logoutReq.SerializeToArray(sendBuf.data() + sizeof(PacketHeader), bodySize);
            
            co_await boost::asio::async_write(socket, boost::asio::buffer(sendBuf), use_awaitable);
            g_TotalSent++;
            
            // 5. 연결 종료
            socket.close();
            
            // [통계] 연결 종료 시 감소
            if (connected) {
                g_ActiveSessions--;
                connected = false;
            }
        }
        catch (std::exception& e) {
            // 에러 발생 시 처리
            if (connected) {
                g_ActiveSessions--;
                connected = false;
            }
            if(socket.is_open()) socket.close();
        }

        // 6. 재접속 전 대기 (Backoff)
        // 너무 빨리 재접속하면 OS 포트 고갈 문제 발생하므로 랜덤 딜레이 권장
        try {
            int delayMs = 100 + (rand() % 500); // 100ms ~ 600ms 랜덤
            timer.expires_after(std::chrono::milliseconds(delayMs));
            co_await timer.async_wait(use_awaitable);
        } catch (...) {}
    }
}

int main(int argc, char* argv[]) {
    int clientCount = 1000; 
    if (argc > 1) {
        try {
            clientCount = std::stoi(argv[1]);
        } catch (...) {
            std::cout << "Invalid argument. Using default: 1000" << std::endl;
        }
    }

    g_TargetCount = clientCount;

    boost::asio::io_context io_context;

    // 클라이언트 코루틴 예약 (실행은 io_context.run()에서)
    // 한 번에 3만 개를 다 예약해도, 실제 실행은 스레드 풀에서 처리됨
    for (int i = 0; i < clientCount; ++i) {
        co_spawn(io_context, RunClient(io_context, i), detached);
    }

    // 워커 스레드 생성 (CPU 코어 수만큼)
    std::vector<std::thread> threads;
    int thread_count = std::thread::hardware_concurrency();
    
    // Work Guard: 작업이 없어도 스레드가 죽지 않게 함 (클라이언트 재접속 유지용)
    auto work_guard = boost::asio::make_work_guard(io_context);

    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&io_context] { io_context.run(); });
    }

    // --- [클라이언트 대시보드 (메인 스레드)] ---
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // 화면 갱신
        std::cout << "\033[2J\033[H"; // Clear Screen
        std::cout << "================ [Client Dashboard] ================" << std::endl;
        std::cout << " Target Users     : " << g_TargetCount << std::endl;
        std::cout << " Active Sessions  : " << g_ActiveSessions << " (Current TCP Connections)" << std::endl;
        std::cout << " Total Packets    : " << g_TotalSent << std::endl;
        std::cout << "----------------------------------------------------" << std::endl;
        std::cout << " Progress         : ";
        DrawProgressBar(g_ActiveSessions, g_TargetCount);
        std::cout << "\n====================================================" << std::endl;
        std::cout << " Press [Ctrl+C] to stop." << std::endl;
    }

    // (여기 도달하진 않지만) 스레드 정리
    io_context.stop();
    for (auto& t : threads) t.join();

    return 0;
}