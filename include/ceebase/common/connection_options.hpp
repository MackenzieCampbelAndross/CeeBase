#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace ceebase {

struct ConnectionOptions {
    std::string path{};
    std::string host{"127.0.0.1"};
    uint16_t port{8080};
    std::string database{};
    std::string token{};
    std::string user{};
    std::string password{};
    bool use_tls{false};
    uint32_t timeout_ms{5000};
    uint32_t max_pool_size{10};
    uint32_t retry_attempts{3};

    static ConnectionOptions Parse(std::string_view uri) {
        ConnectionOptions opts;
        if (uri.empty()) return opts;

        if (uri.starts_with("ceebase://")) {
            std::string_view rest = uri.substr(10);
            size_t query_pos = rest.find('?');
            std::string_view host_db = (query_pos != std::string_view::npos) ? rest.substr(0, query_pos) : rest;

            size_t slash_pos = host_db.find('/');
            if (slash_pos != std::string_view::npos) {
                opts.database = std::string(host_db.substr(slash_pos + 1));
                std::string_view host_part = host_db.substr(0, slash_pos);
                size_t colon_pos = host_part.find(':');
                if (colon_pos != std::string_view::npos) {
                    opts.host = std::string(host_part.substr(0, colon_pos));
                    opts.port = static_cast<uint16_t>(std::stoi(std::string(host_part.substr(colon_pos + 1))));
                } else {
                    opts.host = std::string(host_part);
                }
            } else {
                opts.path = std::string(host_db);
            }

            if (query_pos != std::string_view::npos) {
                std::string_view query = rest.substr(query_pos + 1);
                size_t token_pos = query.find("token=");
                if (token_pos != std::string_view::npos) {
                    size_t amp = query.find('&', token_pos);
                    opts.token = std::string(query.substr(token_pos + 6, amp - (token_pos + 6)));
                }
            }
        } else {
            opts.path = std::string(uri);
        }
        return opts;
    }

    static ConnectionOptions FromEnvironment() {
        ConnectionOptions opts;
        if (const char* db = std::getenv("CEEBASE_DATABASE")) {
            opts.database = db;
            opts.path = db;
        }
        if (const char* url = std::getenv("CEEBASE_URL")) {
            opts = Parse(url);
        }
        if (const char* token = std::getenv("CEEBASE_TOKEN")) {
            opts.token = token;
        }
        return opts;
    }
};

} // namespace ceebase
