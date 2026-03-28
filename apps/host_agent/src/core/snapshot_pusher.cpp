#include "host_agent/core/snapshot_pusher.h"

#include <httplib.h>

#include <chrono>
#include <iostream>
#include <string>

namespace host_agent::core {

namespace {

// Parse "http://host:port/path" into host, port, path
bool ParseUrl(const std::string& url, std::string& host, int& port, std::string& path) {
    std::string rest = url;
    bool is_https = false;

    const auto scheme_end = rest.find("://");
    if (scheme_end != std::string::npos) {
        is_https = rest.substr(0, scheme_end) == "https";
        rest = rest.substr(scheme_end + 3);
    }

    const auto slash = rest.find('/');
    std::string authority;
    if (slash != std::string::npos) {
        authority = rest.substr(0, slash);
        path = rest.substr(slash);
    } else {
        authority = rest;
        path = "/api/ingest/snapshot";
    }

    const auto colon = authority.rfind(':');
    if (colon != std::string::npos) {
        host = authority.substr(0, colon);
        port = std::stoi(authority.substr(colon + 1));
    } else {
        host = authority;
        port = is_https ? 443 : 80;
    }

    return !host.empty();
}

}  // namespace

SnapshotPusher::SnapshotPusher(const model::PushConfig& config,
                               const model::ApiConfig& local_api)
    : config_(config), local_api_(local_api) {}

SnapshotPusher::~SnapshotPusher() {
    Stop();
}

void SnapshotPusher::Start() {
    if (running_.load()) return;
    running_.store(true);
    thread_ = std::thread([this]() { PushLoop(); });
}

void SnapshotPusher::Stop() {
    running_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool SnapshotPusher::PushOnce() {
    // 1. GET local snapshot from our own HTTP server
    httplib::Client local("127.0.0.1", local_api_.port);
    local.set_connection_timeout(2);
    local.set_read_timeout(3);

    auto snap = local.Get("/v1/snapshot");
    if (!snap || snap->status != 200) {
        return false;
    }

    // 2. POST it to the remote ingest endpoint
    std::string remote_host;
    int remote_port;
    std::string remote_path;
    if (!ParseUrl(config_.url, remote_host, remote_port, remote_path)) {
        return false;
    }

    httplib::Client remote(remote_host, remote_port);
    remote.set_connection_timeout(5);
    remote.set_read_timeout(5);

    auto res = remote.Post(remote_path, snap->body, "application/json");
    if (!res || res->status != 200) {
        std::cerr << "Push failed: "
                  << (res ? "HTTP " + std::to_string(res->status) : "connection error")
                  << "\n";
        return false;
    }

    return true;
}

void SnapshotPusher::PushLoop() {
    // Wait a bit for local HTTP server and collectors to start
    for (int i = 0; i < 8 && running_.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    while (running_.load()) {
        const auto start = std::chrono::steady_clock::now();

        PushOnce();

        const auto elapsed = std::chrono::steady_clock::now() - start;
        const auto target = std::chrono::milliseconds(config_.interval_ms);
        if (elapsed < target) {
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(target - elapsed);
            while (remaining.count() > 0 && running_.load()) {
                auto chunk = std::min(remaining, std::chrono::milliseconds(250));
                std::this_thread::sleep_for(chunk);
                remaining -= chunk;
            }
        }
    }
}

}  // namespace host_agent::core
