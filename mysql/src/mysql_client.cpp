#include "mysql_client.hpp"

#include <boost/json.hpp>
#include <utility>

namespace Utils::DB::MySQL {

    namespace json = boost::json;

    // ===================== Client::Create =====================

    Client::Shared Client::Create(const Config & config,
                                  const Logging::Logger::Shared & logger)
    {
        return std::make_shared<ClientImpl>(config, logger);
    }

    // ===================== ClientImpl: helpers =====================

    namespace {
        void InitialiseMysqlLibraryOnce()
        {
            static std::once_flag flag;
            std::call_once(flag, [] {
                mysql_library_init(0, nullptr, nullptr);
            });
        }

        Logging::Logger::Shared MakeDefaultLogger()
        {
            return Utils::Logging::Logger::Create("MYSQL");
        }
    } // namespace

    ClientImpl::ClientImpl(Config config, const Logging::Logger::Shared & logger)
        : config_(std::move(config))
        , logger_(logger ? logger : MakeDefaultLogger())
    {
        InitialiseMysqlLibraryOnce();

        if (config_.poolSize == 0) {
            config_.poolSize = 1;
        }

        Log()->Debug("Initialising client, pool size = {}", config_.poolSize);

        // создаём пул соединений
        for (std::size_t i = 0; i < config_.poolSize; ++i) {
            MYSQL * conn = mysql_init(nullptr);
            if (!conn) {
                Log()->Error("mysql_init failed for connection {}", i);
                continue;
            }

            // таймауты в секундах (API mysql — в секундах)
            {
                const unsigned long connectTimeoutSec =
                    static_cast<unsigned long>(
                        std::chrono::duration_cast<std::chrono::seconds>(config_.connectTimeout).count());
                mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &connectTimeoutSec);

                const unsigned long readTimeoutSec =
                    static_cast<unsigned long>(
                        std::chrono::duration_cast<std::chrono::seconds>(config_.readTimeout).count());
                mysql_options(conn, MYSQL_OPT_READ_TIMEOUT, &readTimeoutSec);

                const unsigned long writeTimeoutSec =
                    static_cast<unsigned long>(
                        std::chrono::duration_cast<std::chrono::seconds>(config_.writeTimeout).count());
                mysql_options(conn, MYSQL_OPT_WRITE_TIMEOUT, &writeTimeoutSec);
            }

            // if (config_.autoReconnect) {
            //     bool reconnect = true;
            //     mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);
            // }

            // подключаемся
            MYSQL * res = mysql_real_connect(
                conn,
                config_.host.c_str(),
                config_.user.c_str(),
                config_.password.c_str(),
                config_.database.empty() ? nullptr : config_.database.c_str(),
                config_.port,
                nullptr,
                0);

            if (!res) {
                Log()->Error("Connection {} failed: {} ({})",
                               i,
                               mysql_error(conn),
                               mysql_errno(conn));
                mysql_close(conn);
                continue;
            }

            if (!config_.charset.empty()) {
                if (mysql_set_character_set(conn, config_.charset.c_str()) != 0) {
                    Log()->Warning("Failed to set charset '{}': {}",
                                     config_.charset,
                                     mysql_error(conn));
                }
            }

            Log()->Debug("Connection {} established", i);

            allConnections_.push_back(conn);
            freeConnections_.push_back(conn);
        }

        if (allConnections_.empty()) {
            Log()->Error("No successful connections in pool");
        }

        StartWorkers();
    }

    ClientImpl::~ClientImpl()
    {
        StopWorkers();

        std::lock_guard poolLock(poolMutex_);
        for (auto * conn : allConnections_) {
            if (conn) {
                mysql_close(conn);
            }
        }
        allConnections_.clear();
        freeConnections_.clear();
    }

    void ClientImpl::StartWorkers()
    {
        const std::size_t workerCount =
            std::max<std::size_t>(1, config_.poolSize);

        for (std::size_t i = 0; i < workerCount; ++i) {
            workers_.emplace_back([this](const std::stop_token& st) {
                WorkerThread(st);
            });
        }

        Log()->Debug("Started {} worker threads", workers_.size());
    }

    void ClientImpl::StopWorkers()
    {
        stopRequested_.store(true);
        queueCv_.notify_all();
        workers_.clear();
    }

    // ===================== Public API =====================

    bool ClientImpl::IsConnected()
    {
        std::lock_guard lock(poolMutex_);
        return !allConnections_.empty();
    }

    Logging::Logger::Shared & ClientImpl::Log()
    {
        return logger_;
    }

    void ClientImpl::Execute(const std::string & sql,
                             const QueryCallback & callback)
    {
        {
            std::lock_guard lock(queueMutex_);
            jobs_.push_back(Job{
                .sql      = sql,
                .callback = callback
            });
        }
        queueCv_.notify_one();
    }

    [[nodiscard]] Task<QueryResult> ClientImpl::Execute(const std::string & sql)
    {
        QueryResult result;

        co_await AwaitablePromiseTask([this, &result, sql](const TaskResolver & resolver)  {
            Execute(sql, [resolver, &result](QueryResult res) mutable {
                std::swap(result, res);
                resolver->Resolve();
            });
        });

        co_return result;
    }

    std::string ClientImpl::EscapeString(const std::string& s)
    {
        MYSQL* conn = AcquireConnection();
        if (!conn)
            return s;

        std::string out;
        out.resize(s.size() * 2 + 1);

        const unsigned long len = mysql_real_escape_string(
            conn,
            out.data(),
            s.data(),
            s.size()
        );

        ReleaseConnection(conn);

        out.resize(len);
        return out;
    }

    void ClientImpl::ProcessTick()
    {
        std::deque<CompletedJob> local;

        {
            std::lock_guard lock(completedMutex_);
            local.swap(completedJobs_);
        }

        for (auto & job : local)
        {
            if (!job.callback)
                continue;

            try
            {
                job.callback(job.result);
            }
            catch (const std::exception & ex)
            {
                Log()->Error("MySQL callback threw exception: {}", ex.what());
            }
            catch (...)
            {
                Log()->Error("MySQL callback threw unknown exception");
            }
        }
    }

    // ===================== Worker / queue =====================
    void ClientImpl::WorkerThread(const std::stop_token& stopToken)
    {
        while (!stopToken.stop_requested())
        {
            Job job;

            {
                std::unique_lock lock(queueMutex_);
                queueCv_.wait(lock, stopToken, [this]{
                    return stopRequested_ || !jobs_.empty();
                });

                if ((stopRequested_ || stopToken.stop_requested()) && jobs_.empty())
                    break;

                job = std::move(jobs_.front());
                jobs_.pop_front();
            }

            MYSQL * conn = AcquireConnection();
            if (!conn)
            {
                // Соединение не взяли — если есть колбэк, отдадим ему пустой/ошибочный результат
                if (job.callback)
                {
                    QueryResult result{};
                    std::lock_guard cbLock(completedMutex_);
                    completedJobs_.push_back(CompletedJob{
                        .callback = job.callback,
                        .result   = std::move(result)
                    });
                }
                continue;
            }

            QueryResult result = ExecuteOnConnection(conn, job.sql);
            ReleaseConnection(conn);

            // Если колбэка нет — это fire-and-forget запрос, просто выходим
            if (!job.callback)
                continue;

            // А если есть — кладём в очередь выполненных задач
            {
                std::lock_guard cbLock(completedMutex_);
                completedJobs_.push_back(CompletedJob{
                    .callback = job.callback,
                    .result   = std::move(result)
                });
            }
        }
    }

    MYSQL * ClientImpl::AcquireConnection()
    {
        std::unique_lock lock(poolMutex_);
        if (freeConnections_.empty()) {
            // если всё занято — просто берём любое (на будущее можно сделать более умный wait)
            if (!allConnections_.empty()) {
                return allConnections_.front();
            }
            return nullptr;
        }

        MYSQL * conn = freeConnections_.front();
        freeConnections_.pop_front();
        return conn;
    }

    void ClientImpl::ReleaseConnection(MYSQL * conn)
    {
        if (!conn) {
            return;
        }

        std::lock_guard lock(poolMutex_);
        freeConnections_.push_back(conn);
    }

    // ===================== Query execution =====================

    QueryResult ClientImpl::ExecuteOnConnection(MYSQL * conn,
                                                const std::string & sql)
    {
        QueryResult result;

        if (!conn) {
            result.success      = false;
            result.error.code   = 1;
            result.error.message = "Null MySQL connection";
            return result;
        }

        if (mysql_query(conn, sql.c_str()) != 0) {
            result.success       = false;
            result.error.code    = mysql_errno(conn);
            result.error.message = mysql_error(conn);

            const char * state = mysql_sqlstate(conn);
            if (state) {
                result.error.sqlState = state;
            }

            return result;
        }

        MYSQL_RES * res = mysql_store_result(conn);
        if (res) {
            // есть набор строк (SELECT и т.п.)
            result.rows = ConvertResultToJson(conn, res);
            mysql_free_result(res);
            result.success = true;
        } else {
            // нет результата: либо UPDATE/INSERT/DELETE, либо ошибка store_result
            if (mysql_errno(conn) != 0) {
                result.success       = false;
                result.error.code    = mysql_errno(conn);
                result.error.message = mysql_error(conn);

                const char * state = mysql_sqlstate(conn);
                if (state) {
                    result.error.sqlState = state;
                }
                return result;
            }

            result.affectedRows = static_cast<std::uint64_t>(mysql_affected_rows(conn));
            result.insertId     = static_cast<std::uint64_t>(mysql_insert_id(conn));
            result.success      = true;
        }

        return result;
    }

    JsonArray ClientImpl::ConvertResultToJson(const MYSQL * conn, MYSQL_RES * res)
    {
        JsonArray rows;

        if (!conn || !res) {
            return rows;
        }

        const auto numFields = mysql_num_fields(res);
        const MYSQL_FIELD * fields = mysql_fetch_fields(res);

        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != nullptr) {
            const unsigned long * lengths = mysql_fetch_lengths(res);

            JsonObject obj;

            for (int i = 0; i < numFields; ++i) {
                const char * name = fields[i].name ? fields[i].name : "";

                if (row[i] == nullptr) {
                    obj[name] = json::value(nullptr);
                } else {
                    // пока что всё как строки; при желании можно смотреть fields[i].type
                    obj[name] = std::string(row[i], lengths ? lengths[i] : std::strlen(row[i]));
                }
            }

            rows.emplace_back(std::move(obj));
        }

        return rows;
    }

} // namespace Utils::DB::MySQL