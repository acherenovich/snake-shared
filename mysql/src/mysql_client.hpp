#pragma once

#include "../interface/mysql.hpp"

#include <mysql/mysql.h>

#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <stop_token>
#include <atomic>

namespace Utils::DB::MySQL {

    class ClientImpl final :
        public Client,
        public std::enable_shared_from_this<ClientImpl>
    {
    public:
        using Shared = std::shared_ptr<ClientImpl>;

        explicit ClientImpl(Config  config,
                            const Logging::Logger::Shared & logger);

        ~ClientImpl() override;

        [[nodiscard]] bool IsConnected() override;

        Logging::Logger::Shared & Log() override;

        void Execute(const std::string & sql,
                     const QueryCallback & callback) override;

        [[nodiscard]] Task<QueryResult> Execute(const std::string & sql) override;

        std::string EscapeString(const std::string& s) override;

        void ProcessTick() override;

    private:
        struct Job
        {
            std::string   sql;
            QueryCallback callback;
        };

        struct CompletedJob
        {
            QueryCallback callback;
            QueryResult   result;
        };

        Config config_;
        Logging::Logger::Shared logger_;

        // ===== Пул соединений =====
        std::mutex poolMutex_;
        std::deque<MYSQL *> freeConnections_;
        std::vector<MYSQL *> allConnections_;

        // ===== Очередь задач на исполнение (для воркеров) =====
        std::mutex queueMutex_;
        std::condition_variable_any queueCv_;
        std::deque<Job> jobs_;

        // ===== Очередь выполненных задач (для ProcessTick в main-треде) =====
        std::mutex completedMutex_;
        std::deque<CompletedJob> completedJobs_;

        std::atomic<bool> stopRequested_ { false };
        std::vector<std::jthread> workers_;

    private:
        void StartWorkers();
        void StopWorkers();

        void WorkerThread(const std::stop_token& stopToken);

        MYSQL * AcquireConnection();
        void ReleaseConnection(MYSQL * conn);

        static QueryResult ExecuteOnConnection(MYSQL * conn, const std::string & sql);

        static JsonArray ConvertResultToJson(const MYSQL * conn, MYSQL_RES * res);
    };

} // namespace Utils::DB::MySQL