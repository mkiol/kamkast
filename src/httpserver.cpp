/* Copyright (C) 2022 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "httpserver.hpp"

#include <arpa/inet.h>
#include <fmt/core.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <variant>

#include "logger.hpp"

static std::optional<sockaddr_storage> makeSockaddr(const std::string& addr,
                                                    uint16_t port) {
    sockaddr_storage ss{};

    if (auto* sa = reinterpret_cast<sockaddr_in6*>(&ss);
        inet_pton(AF_INET6, addr.c_str(), &sa->sin6_addr) == 1) {
        sa->sin6_family = AF_INET6;
        sa->sin6_port = htons(port);
        return ss;
    }

    if (auto* sa = reinterpret_cast<sockaddr_in*>(&ss);
        inet_pton(AF_INET, addr.c_str(), &sa->sin_addr) == 1) {
        sa->sin_family = AF_INET;
        sa->sin_port = htons(port);
        return ss;
    }

    return std::nullopt;
}

void HttpServer::mhdLogCallback([[maybe_unused]] void* cls, const char* fm,
                                va_list ap) {
    static constexpr const size_t max_buf = 512;
    char buf[max_buf];
    vsnprintf(buf, max_buf, fm, ap);
    LOGE_NOENDL(buf);
}

void HttpServer::makeDaemon() {
    m_daemon = MHD_start_daemon(
        MHD_ALLOW_SUSPEND_RESUME | MHD_USE_AUTO_INTERNAL_THREAD | MHD_USE_DEBUG,
        m_config.port, nullptr, nullptr, &mhdConnectionHandler, this,
        MHD_OPTION_EXTERNAL_LOGGER, &mhdLogCallback, nullptr,
        MHD_OPTION_CONNECTION_LIMIT, m_config.connectionLimit,
        MHD_OPTION_NOTIFY_CONNECTION, &mhdNotifyConnectionCallback, this,
        MHD_OPTION_END);
}

void HttpServer::makeDaemonUsingAddress(const std::string& address) {
    LOGD("trying to bind to address: " << address);
    auto ss = makeSockaddr(address, m_config.port);
    if (!ss) throw std::runtime_error("invalid address");

    m_address = address;

    m_daemon = MHD_start_daemon(
        MHD_ALLOW_SUSPEND_RESUME | MHD_USE_AUTO_INTERNAL_THREAD |
            MHD_USE_DEBUG | (ss->ss_family == AF_INET6 ? MHD_USE_IPv6 : 0),
        m_config.port, nullptr, nullptr, &mhdConnectionHandler, this,
        MHD_OPTION_EXTERNAL_LOGGER, &mhdLogCallback, nullptr,
        MHD_OPTION_CONNECTION_LIMIT, m_config.connectionLimit,
        MHD_OPTION_SOCK_ADDR, &ss, MHD_OPTION_NOTIFY_CONNECTION,
        &mhdNotifyConnectionCallback, this, MHD_OPTION_END);
}

void HttpServer::makeDaemonUsingIfname() {
    auto [addr4, addr6] = addressForInterface(m_config.ifname);

    if (!addr4.empty())
        makeDaemonUsingAddress(addr4);
    else if (!addr6.empty())
        makeDaemonUsingAddress(addr6);
    else
        throw std::runtime_error("invalid interface name");
}

HttpServer::HttpServer(Config config, ConnectionHandler connectionHandler,
                       ConnectionRemovedHandler connectionRemovedHandler,
                       ShutdownHandler shutdownHandler)
    : m_config{std::move(config)}, m_connectionHandler{std::move(
                                       connectionHandler)},
      m_connectionRemovedHandler{std::move(connectionRemovedHandler)},
      m_shutdownHandler{std::move(shutdownHandler)} {
    if (!m_connectionHandler)
        throw std::runtime_error("connection handler not set");
    if (!m_config.ifname.empty())
        makeDaemonUsingIfname();
    else if (!m_config.address.empty() && m_config.address != anyAddress)
        makeDaemonUsingAddress(m_config.address);
    else
        makeDaemon();

    if (m_daemon == nullptr) throw std::runtime_error("failed to start server");

    startGhostConnectionCheckThread();

    LOGD("http-server started on port " << port());
}

HttpServer::~HttpServer() {
    LOGD("http-server shutdown started");
    m_shutdownRequested = true;

    if (m_shutdownHandler) m_shutdownHandler();

    if (m_gcThread.joinable()) m_gcThread.join();

    for (auto& [_, ctx] : m_connections) resumeConnection(ctx);

    MHD_stop_daemon(m_daemon);

    LOGD("http-server shutdown completed");
}

void HttpServer::startGhostConnectionCheckThread() {
    m_gcThread = std::thread([this] {
        LOGD("http-server gc thread started");

        while (!m_shutdownRequested) {
            removeGhostConnections();
            std::this_thread::sleep_for(
                std::chrono::milliseconds{maxSuspendTime / 5});
        }

        LOGD("http-server gc thread ended");
    });
}

uint16_t HttpServer::port() const {
    const auto* info = MHD_get_daemon_info(m_daemon, MHD_DAEMON_INFO_BIND_PORT);
    if (info == nullptr) throw std::runtime_error("get_daemon_info error");
    return info->port;
}

static std::string ntop(const sockaddr* sockAddr) {
    std::string addrStr;

    if (sockAddr->sa_family == AF_INET) {
        char addr[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET,
                      &reinterpret_cast<const sockaddr_in*>(sockAddr)->sin_addr,
                      addr, INET_ADDRSTRLEN) == nullptr) {
            LOGW("ntop4 error");
        } else {
            addrStr.assign(addr);
        }
    } else if (sockAddr->sa_family == AF_INET6) {
        if (!IN6_IS_ADDR_LINKLOCAL(
                &reinterpret_cast<const sockaddr_in6*>(sockAddr)->sin6_addr)) {
            char addr[INET6_ADDRSTRLEN];
            if (inet_ntop(
                    AF_INET6,
                    &reinterpret_cast<const sockaddr_in6*>(sockAddr)->sin6_addr,
                    addr, INET6_ADDRSTRLEN) == nullptr) {
                LOGW("ntop6 error");
            } else {
                addrStr.assign(addr);
            }
        }
    }

    return addrStr;
}

std::pair</*ipv4*/ std::string, /*ipv6*/ std::string>
HttpServer::addressForInterface(const std::string& ifname) {
    ifaddrs* ifap;
    if (getifaddrs(&ifap) != 0) throw std::runtime_error("getifaddrs error");

    std::pair</*ipv4*/ std::string, /*ipv6*/ std::string> addrPair;

    for (auto* ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr || ifname != ifa->ifa_name) continue;
        auto addr = ntop(ifa->ifa_addr);
        if (addr.empty()) continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            addrPair.first.assign(std::move(addr));
            if (!addrPair.second.empty()) break;
        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
            addrPair.second.assign(std::move(addr));
            if (!addrPair.first.empty()) break;
        }
    }

    freeifaddrs(ifap);

    LOGD(ifname << " address: " << addrPair.first << " " << addrPair.second);

    return addrPair;
}

std::set<std::string> HttpServer::machineIfs() {
    ifaddrs* ifap;
    if (getifaddrs(&ifap) != 0) throw std::runtime_error("getifaddrs error");

    std::set<std::string> ifs;

    for (auto* ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        if (!ntop(ifa->ifa_addr).empty()) ifs.insert(ifa->ifa_name);
    }

    freeifaddrs(ifap);

    return ifs;
}

std::set<std::string> HttpServer::machineAddresses() {
    ifaddrs* ifap;
    if (getifaddrs(&ifap) != 0) throw std::runtime_error("getifaddrs error");

    std::set<std::string> addrs;

    for (auto* ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        auto addr = ntop(ifa->ifa_addr);
        if (!addr.empty()) addrs.insert(std::move(addr));
    }

    freeifaddrs(ifap);

    return addrs;
}

std::set<std::string> HttpServer::listeningAddresses() const {
    if (m_address.empty()) return machineAddresses();
    return {m_address};
}

void HttpServer::suspendConnection(ConnectionCtx& ctx) {
    if (ctx.suspended) return;

    LOGT("suspending connection: " << ctx.id);

    MHD_suspend_connection(ctx.mhdConn);

    ctx.suspended = true;
    ctx.suspendTime = std::chrono::steady_clock::now();
}

void HttpServer::resumeConnection(ConnectionCtx& ctx) {
    if (!ctx.suspended) return;

    LOGT("resuming connection: " << ctx.id);

    MHD_resume_connection(ctx.mhdConn);

    ctx.suspended = false;
}

ssize_t HttpServer::mhdContentReaderCallback(void* cls,
                                             [[maybe_unused]] uint64_t pos,
                                             char* buf, size_t max) {
    auto* ctx = static_cast<ConnectionCtx*>(cls);

    if (ctx->server->m_shutdownRequested || ctx->removed) return -1;

    LOGT("read callback");

    std::unique_lock lock(ctx->server->m_connMtx, std::try_to_lock);
    if (!lock) return 0;

    if (ctx->buf.empty()) {
        suspendConnection(*ctx);
        lock.unlock();
        return 0;
    }

    LOGT("pull data: max=" << max << ", buf size=" << ctx->buf.size());

    auto pulledSize =
        ctx->buf.pull(reinterpret_cast<decltype(ctx->buf)::BufType*>(buf), max);

    lock.unlock();

    return static_cast<ssize_t>(pulledSize);
}

MHD_Result HttpServer::rejectMhdConnection(MHD_Connection* connection,
                                           unsigned int code) {
    LOGD("rejecting connection");

    auto* resp =
        MHD_create_response_from_buffer(0, nullptr, MHD_RESPMEM_PERSISTENT);
    if (resp == nullptr)
        throw std::runtime_error("create response from data error");
    auto ret = MHD_queue_response(connection, code, resp);
    MHD_destroy_response(resp);
    return ret;
}

HttpServer::ConnectionCtx& HttpServer::addMhdConnection(
    MHD_Connection* connection) {
    if (auto it = std::find_if(m_connections.begin(), m_connections.end(),
                               [connection](const auto& n) {
                                   return n.second.mhdConn == connection;
                               });
        it != m_connections.end()) {
        LOGW("connection already exists");
        return it->second;
    }

    auto result = m_connections.try_emplace(
        m_nextConnectionId, m_nextConnectionId, this, connection);

    if (!result.second) throw std::runtime_error("overlaping connection id");

    m_nextConnectionId++;

    return result.first->second;
}

void HttpServer::removeConnection(ConnectionId id) {
    auto ctx = connectionCtx(id);
    if (!ctx) return;

    ctx->get().removed = true;

    m_connections.erase(ctx->get().id);
}

void HttpServer::dropConnection(ConnectionId id) {
    std::lock_guard lock{m_connMtx};

    auto ctx = connectionCtx(id);
    if (!ctx) {
        LOGW("can't drop because no connection with id: " << id);
        return;
    }

    ctx->get().removed = true;
    if (ctx->get().suspended) MHD_resume_connection(ctx->get().mhdConn);
}

MHD_Result HttpServer::mhdConnectionHandler(
    void* cls, MHD_Connection* connection, const char* url, const char* method,
    [[maybe_unused]] const char* version,
    [[maybe_unused]] const char* upload_data,
    [[maybe_unused]] size_t* upload_data_size,
    [[maybe_unused]] void** con_cls) {
    auto* server = static_cast<HttpServer*>(cls);

    auto ctx = server->connectionCtxFromMhd(connection);
    if (!ctx)
        throw std::runtime_error(
            "connection handler for not existing connection");

    LOGD("new connection (" << ctx->get().id << "): " << method << " " << url);

    if (server->m_shutdownRequested) return MHD_NO;

    auto requestHeaders = [connection] {
        auto count = MHD_get_connection_values(connection, MHD_HEADER_KIND,
                                               nullptr, nullptr);
        std::vector<Header> headers;
        headers.reserve(count);
        LOGD("request headers:");
        MHD_get_connection_values(
            connection, MHD_HEADER_KIND,
            [](void* cls, [[maybe_unused]] MHD_ValueKind kind, const char* key,
               const char* value) {
                static_cast<decltype(&headers)>(cls)->emplace_back(key, value);
                LOGD(key << "=" << value);
                return MHD_YES;
            },
            &headers);
        return headers;
    }();

    std::vector<Header> responseHeaders;

    if (auto code = server->m_connectionHandler(
            ctx->get().id, url, requestHeaders, responseHeaders);
        code >= 400) {
        return rejectMhdConnection(connection, code);
    }

    auto* resp = [&]() {
        if (ctx->get().buf.empty()) {
            auto* resp = MHD_create_response_from_callback(
                MHD_SIZE_UNKNOWN, connectionBlockSize,
                &mhdContentReaderCallback, &ctx->get(), nullptr);
            if (resp == nullptr)
                throw std::runtime_error("create response from callback error");
            return resp;
        }

        auto bufPrt = ctx->get().buf.ptrForPull();
        auto* resp = MHD_create_response_from_buffer(
            bufPrt.second, bufPrt.first, MHD_RESPMEM_PERSISTENT);
        if (resp == nullptr)
            throw std::runtime_error("create response from buffer error");
        return resp;
    }();

    std::for_each(responseHeaders.cbegin(), responseHeaders.cend(),
                  [resp](const Header& h) {
                      if (MHD_add_response_header(resp, h.first.c_str(),
                                                  h.second.c_str()) == MHD_NO) {
                          LOGW("add response header error: " << h.first << "="
                                                             << h.second);
                      }
                  });

    auto ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);

    return ret;
}

std::optional<HttpServer::ConnectionId> HttpServer::connectionIdFromMhd(
    MHD_Connection* connection) const {
    auto it = std::find_if(
        m_connections.cbegin(), m_connections.cend(),
        [connection](const auto& n) { return n.second.mhdConn == connection; });

    if (it == m_connections.cend()) return std::nullopt;

    return it->first;
}

std::optional<std::reference_wrapper<HttpServer::ConnectionCtx>>
HttpServer::connectionCtxFromMhd(MHD_Connection* connection) {
    auto it = std::find_if(
        m_connections.begin(), m_connections.end(),
        [connection](const auto& n) { return n.second.mhdConn == connection; });

    if (it == m_connections.end()) return std::nullopt;

    return it->second;
}

std::optional<std::reference_wrapper<HttpServer::ConnectionCtx>>
HttpServer::connectionCtx(ConnectionId id) {
    auto it = m_connections.find(id);
    if (it == m_connections.end()) return std::nullopt;

    return it->second;
}

unsigned int HttpServer::connectionsCount() const {
    const auto* info =
        MHD_get_daemon_info(m_daemon, MHD_DAEMON_INFO_CURRENT_CONNECTIONS);
    if (info == nullptr) throw std::runtime_error("get daemon info error");
    return info->num_connections;
}

std::string HttpServer::connectionClientAddress(MHD_Connection* connection) {
    const auto* info =
        MHD_get_connection_info(connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
    if (info == nullptr) throw std::runtime_error("get connection info error");
    return ntop(info->client_addr);
}

std::optional<std::string> HttpServer::clientAddress(ConnectionId id) const {
    auto it = m_connections.find(id);
    if (it == m_connections.end()) return std::nullopt;

    return connectionClientAddress(it->second.mhdConn);
}

void HttpServer::mhdNotifyConnectionCallback(
    void* cls, MHD_Connection* connection,
    [[maybe_unused]] void** socket_context,
    MHD_ConnectionNotificationCode toe) {
    auto* server = static_cast<HttpServer*>(cls);

    std::lock_guard lock{server->m_connMtx};

    auto id = server->connectionIdFromMhd(connection);

    switch (toe) {
        case MHD_CONNECTION_NOTIFY_STARTED:
            if (id)
                throw std::runtime_error(
                    "connection started notification for exiting connection");
            id.emplace(server->addMhdConnection(connection).id);
            break;
        case MHD_CONNECTION_NOTIFY_CLOSED: {
            if (!id)
                throw std::runtime_error(
                    "connection closed notification for not exiting "
                    "connection");

            if (server->m_connectionRemovedHandler)
                server->m_connectionRemovedHandler(*id);

            server->removeConnection(*id);
            break;
        }
    }

    LOGD("connection notification ("
         << *id << "): "
         << (toe == MHD_ConnectionNotificationCode::MHD_CONNECTION_NOTIFY_CLOSED
                 ? "closed"
                 : "started"));
}

std::optional<size_t> HttpServer::pushDataInternal(ConnectionCtx& ctx,
                                                   const uint8_t* data,
                                                   size_t size) {
    if (ctx.removed) {
        LOGW("failed to push because connection was removed");
        return std::nullopt;
    }

    LOGT("push data: size=" << size << ", buf size=" << ctx.buf.size()
                            << ", buf max=" << ctx.buf.maxSize());

    ctx.buf.pushExactForce(data, size);

    resumeConnection(ctx);

    return size;
}

std::optional<size_t> HttpServer::pushData(ConnectionId id, const uint8_t* data,
                                           size_t size) {
    if (m_shutdownRequested) return std::nullopt;

    std::lock_guard lock{m_connMtx};

    auto ctx = connectionCtx(id);
    if (!ctx) return std::nullopt;

    return pushDataInternal(ctx->get(), data, size);
}

std::optional<size_t> HttpServer::pushData(ConnectionId id,
                                           std::string_view s) {
    if (m_shutdownRequested) return std::nullopt;

    std::lock_guard lock{m_connMtx};

    auto ctx = connectionCtx(id);
    if (!ctx) return std::nullopt;

    return pushDataInternal(
        ctx->get(), reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

std::optional<std::string> HttpServer::queryValue(ConnectionId id,
                                                  const char* key) {
    if (m_shutdownRequested) return std::nullopt;

    std::lock_guard lock{m_connMtx};

    auto ctx = connectionCtx(id);
    if (!ctx) return std::nullopt;

    const auto* query = MHD_lookup_connection_value(ctx->get().mhdConn,
                                                    MHD_GET_ARGUMENT_KIND, key);
    if (query == nullptr) return std::nullopt;

    return query;
}

void HttpServer::removeGhostConnections() {
    auto now = std::chrono::steady_clock::now();

    std::lock_guard lock{m_connMtx};

    for (auto& [_, ctx] : m_connections) {
        if (!ctx.removed && ctx.suspended) {
            auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - ctx.suspendTime)
                    .count();
            if (duration >= maxSuspendTime) {
                LOGW("removing ghost connection: id=" << ctx.id
                                                      << ", dur=" << duration);
                ctx.removed = true;
                resumeConnection(ctx);
            }
        }
    }
}

HttpServer::ConnectionCtx::ConnectionCtx(ConnectionId id, HttpServer* server,
                                         MHD_Connection* mhdConn)
    : id{id}, server{server}, mhdConn{mhdConn} {}
