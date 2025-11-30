#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <unordered_map>
#include <shared_mutex>
#include <deque>
#include <numeric>
#include <atomic>
#include <iomanip>
#include <thread>
#include <vector>

// MacOS Memory Check Header
#ifdef __APPLE__
#include <mach/mach.h>
#endif

#include "game.pb.h"
#include "Common.h"

using boost::asio::ip::tcp;
using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
namespace this_coro = boost::asio::this_coro;

// =========================================================================================
// [전역 변수 선언] - 클래스 정의보다 반드시 먼저 나와야 합니다.
// =========================================================================================

// 1. IO 처리를 위한 컨텍스트 (CPU 코어 수만큼 스레드 할당)
boost::asio::io_context g_IoContext;

// 2. 로직/DB 처리를 위한 컨텍스트 (30개 고정 스레드)
boost::asio::io_context g_WorkerContext;


// =========================================================================================
// [유틸리티 함수]
// =========================================================================================

// 메모리 사용량 측정 함수 (MacOS)
size_t GetMemoryUsage() {
#ifdef __APPLE__
    struct mach_task_basic_info info;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
        (task_info_t)&info, &infoCount) != KERN_SUCCESS)
        return 0;
    return (size_t)info.resident_size; // Bytes
#else
    return 0; // Linux/Windows 등은 별도 구현 필요
#endif
}

std::string FormatBytes(size_t bytes) {
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << mb << " MB";
    return ss.str();
}


// =========================================================================================
// [통계 클래스]
// =========================================================================================
class ServerStats {
public:
    ServerStats() {}

    void RecordPacket() {
        total_processed_++;
    }

    // 통계 타이머는 IO 컨텍스트에서 돕니다.
    void Start(boost::asio::io_context& io) {
        timer_ = std::make_unique<boost::asio::steady_timer>(io);
        UpdateStatsLoop();
    }

    uint64_t GetCurrentTPS() const { return current_tps_.load(); }
    double GetAvgTPS() const { return avg_tps_.load(); }
    uint64_t GetTotalProcessed() const { return total_processed_.load(); }

private:
    void UpdateStatsLoop() {
        timer_->expires_after(std::chrono::seconds(1));
        timer_->async_wait([this](boost::system::error_code ec) {
            if (!ec) {
                Calculate();
                UpdateStatsLoop();
            }
        });
    }

    void Calculate() {
        uint64_t now_total = total_processed_.load();
        uint64_t prev = prev_total_.exchange(now_total);
        
        uint64_t diff = (now_total >= prev) ? (now_total - prev) : 0;
        current_tps_.store(diff);

        history_.push_back(diff);
        if (history_.size() > 60) history_.pop_front();

        uint64_t sum = std::accumulate(history_.begin(), history_.end(), 0ULL);
        if (!history_.empty()) {
            avg_tps_.store(static_cast<double>(sum) / history_.size());
        }
    }

    std::unique_ptr<boost::asio::steady_timer> timer_;
    std::atomic<uint64_t> total_processed_{0};
    std::atomic<uint64_t> prev_total_{0};
    std::atomic<uint64_t> current_tps_{0};
    std::atomic<double> avg_tps_{0.0};
    std::deque<uint64_t> history_; 
};

ServerStats g_ServerStats;


// =========================================================================================
// [세션 관리자]
// =========================================================================================
class GameSession;
class SessionManager {
public:
    void Add(std::shared_ptr<GameSession> session);
    void Remove(const std::string& id);
    size_t Count() {
        std::shared_lock lock(mutex_);
        return sessions_.size();
    }
private:
    std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<GameSession>> sessions_;
};
SessionManager g_SessionManager;


// =========================================================================================
// [게임 세션] - 핵심 로직
// =========================================================================================
class GameSession : public std::enable_shared_from_this<GameSession> {
public:
    GameSession(tcp::socket socket) 
        : socket_(std::move(socket)), 
          // [중요] 전역 변수 g_IoContext가 위에서 선언되어 있어야 에러가 안 납니다.
          io_strand_(boost::asio::make_strand(g_IoContext)),
          worker_strand_(boost::asio::make_strand(g_WorkerContext)) 
    {}

    void SetID(std::string id) { id_ = id; }
    std::string GetID() { return id_; }

    void Start() {
        // [수정] IO Strand 위에서 코루틴 시작 (Race Condition 방지)
        co_spawn(io_strand_, [self = shared_from_this()] { return self->ProcessLoop(); }, detached);
    }

    void Stop() {
        // [수정] 소켓 닫기도 IO Strand 안에서 수행
        boost::asio::post(io_strand_, [this, self = shared_from_this()]() {
            if (socket_.is_open()) {
                boost::system::error_code ec;
                socket_.close(ec);
            }
        });
        
        if (!id_.empty()) g_SessionManager.Remove(id_);
    }

    void Send(uint16_t id, const google::protobuf::Message& msg) {
        auto bodySize = static_cast<uint16_t>(msg.ByteSizeLong());
        auto packetSize = sizeof(PacketHeader) + bodySize;
        auto buffer = std::make_shared<std::vector<uint8_t>>(packetSize);
        PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer->data());
        header->length = bodySize;
        header->id = id;
        
        if (!msg.SerializeToArray(buffer->data() + sizeof(PacketHeader), bodySize)) return;

        // [수정] Write 작업도 IO Strand로 Post
        boost::asio::post(io_strand_, [self = shared_from_this(), buffer]() {
             boost::asio::async_write(self->socket_, boost::asio::buffer(*buffer),
                [self, buffer](boost::system::error_code ec, size_t) {
                    if (ec) self->Stop();
                });
        });
    }

private:
    // IO 스레드와 Worker 스레드를 오가는 메인 루프
    awaitable<void> ProcessLoop() {
        try {
            while (true) {
                // 1. [IO Strand] 헤더 읽기
                PacketHeader header;
                co_await boost::asio::async_read(socket_, boost::asio::buffer(&header, sizeof(header)), use_awaitable);

                if (header.length > MAX_PACKET_SIZE) throw std::runtime_error("Packet too large");

                // 2. [IO Strand] 바디 읽기
                std::vector<uint8_t> body(header.length);
                co_await boost::asio::async_read(socket_, boost::asio::buffer(body), use_awaitable);

                g_ServerStats.RecordPacket();

                // 3. [Context Switch] IO Strand -> Worker Strand
                // 이 줄을 지나면 실행 흐름이 Worker 스레드 중 하나로 이동합니다.
                co_await boost::asio::post(worker_strand_, use_awaitable);

                // 4. [Worker Strand] 로직 처리 (여기서 DB 쿼리 등으로 블로킹되어도 IO는 멈추지 않음)
                HandlePacket(header.id, body);

                // 5. [Context Switch] Worker Strand -> IO Strand
                // 다시 읽기 작업을 위해 소켓 Executor(IO Strand)로 돌아옵니다.
                co_await boost::asio::post(io_strand_, use_awaitable);
            }
        } catch (std::exception&) {
            Stop();
        }
    }

    void HandlePacket(uint16_t id, const std::vector<uint8_t>& body) {
        switch (id) {
        case game::LOGIN_REQ: {
            game::LoginRequest req;
            if (req.ParseFromArray(body.data(), static_cast<int>(body.size()))) {
                this->SetID(req.user_id());
                g_SessionManager.Add(shared_from_this());
                game::LoginResponse res;
                res.set_success(true);
                res.set_message("Welcome");
                Send(game::LOGIN_RES, res);
            }
            break;
        }
        case game::LOGOUT_REQ: {
            Stop();
            break;
        }
        }
    }

    tcp::socket socket_;
    // 소켓 접근 보호용 IO Strand
    boost::asio::strand<boost::asio::io_context::executor_type> io_strand_;
    // 로직 순서 보호용 Worker Strand
    boost::asio::strand<boost::asio::io_context::executor_type> worker_strand_;
    std::string id_;
};

void SessionManager::Add(std::shared_ptr<GameSession> session) {
    std::unique_lock lock(mutex_);
    sessions_[session->GetID()] = session;
}
void SessionManager::Remove(const std::string& id) {
    std::unique_lock lock(mutex_);
    sessions_.erase(id);
}

awaitable<void> Listener(tcp::acceptor acceptor) {
    while (true) {
        auto socket = co_await acceptor.async_accept(use_awaitable);
        std::make_shared<GameSession>(std::move(socket))->Start();
    }
}


// =========================================================================================
// [메인 함수]
// =========================================================================================
int main() {
    spdlog::init_thread_pool(8192, 1);
    auto stdout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    stdout_sink->set_level(spdlog::level::warn); 
    auto async_logger = std::make_shared<spdlog::async_logger>("server", stdout_sink, spdlog::thread_pool(), spdlog::async_overflow_policy::block);
    spdlog::set_default_logger(async_logger);

    try {
        // 통계 타이머는 IO 컨텍스트에서 실행
        g_ServerStats.Start(g_IoContext);

        tcp::acceptor acceptor(g_IoContext, { tcp::v4(), 11000 });
        co_spawn(g_IoContext, Listener(std::move(acceptor)), detached);

        // --- 스레드 그룹 생성 ---
        std::vector<std::thread> io_threads;
        std::vector<std::thread> worker_threads;

        // 1. IO 스레드 (CPU 코어 수)
        int io_thread_count = std::thread::hardware_concurrency();
        for (int i = 0; i < io_thread_count; ++i) {
            io_threads.emplace_back([] { g_IoContext.run(); });
        }

        // 2. Worker 스레드 (30개 고정)
        int worker_thread_count = 30;
        // Work Guard: Worker Context가 할 일이 없어도 종료되지 않도록 함
        auto work_guard = boost::asio::make_work_guard(g_WorkerContext);
        for (int i = 0; i < worker_thread_count; ++i) {
            worker_threads.emplace_back([] { g_WorkerContext.run(); });
        }

        // --- [대시보드 루프] ---
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            std::cout << "\033[2J\033[H";
            
            std::cout << "================ [Server Dashboard] ================" << std::endl;
            std::cout << " Status           : Running (IO/Worker Separated)" << std::endl;
            std::cout << " IO Threads       : " << io_thread_count << " (Net I/O)" << std::endl;
            std::cout << " Worker Threads   : " << worker_thread_count << " (Logic/DB)" << std::endl;
            std::cout << " Memory Usage     : " << FormatBytes(GetMemoryUsage()) << std::endl;
            std::cout << "----------------------------------------------------" << std::endl;
            std::cout << " Connected Users  : " << g_SessionManager.Count() << std::endl;
            std::cout << " Current TPS      : " << g_ServerStats.GetCurrentTPS() << " pkts/sec" << std::endl;
            std::cout << " Avg TPS (60s)    : " << std::fixed << std::setprecision(2) << g_ServerStats.GetAvgTPS() << " pkts/sec" << std::endl;
            std::cout << " Total Processed  : " << g_ServerStats.GetTotalProcessed() << std::endl;
            std::cout << "====================================================" << std::endl;
            std::cout << " Press [Ctrl+C] to exit." << std::endl;
        }

        // 종료 처리
        g_IoContext.stop();
        g_WorkerContext.stop();

        for (auto& t : io_threads) t.join();
        for (auto& t : worker_threads) t.join();
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    return 0;
}