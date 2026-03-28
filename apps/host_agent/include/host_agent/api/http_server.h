#ifndef HOST_AGENT_API_HTTP_SERVER_H
#define HOST_AGENT_API_HTTP_SERVER_H

#include "host_agent/model/telemetry_models.h"
#include "host_agent/storage/sqlite_store.h"

#include <atomic>
#include <string>
#include <thread>

namespace httplib {
class Server;
}

namespace host_agent::api {

class HttpServer {
public:
    HttpServer(const model::ApiConfig& config,
               const std::string& machine_id,
               storage::SqliteStore& store);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void Start();
    void Stop();
    bool IsRunning() const;

private:
    void RegisterRoutes();
    void Run();

    model::ApiConfig config_;
    std::string machine_id_;
    storage::SqliteStore& store_;
    std::unique_ptr<httplib::Server> svr_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace host_agent::api

#endif  // HOST_AGENT_API_HTTP_SERVER_H
