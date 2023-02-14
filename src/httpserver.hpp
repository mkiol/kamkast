/* Copyright (C) 2022-2023 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <microhttpd.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "databuffer.hpp"

class HttpServer {
   public:
    struct InvalidIfnameError : public std::runtime_error {
        InvalidIfnameError() : std::runtime_error{"invalid interface name"} {}
        InvalidIfnameError(const InvalidIfnameError&) = delete;
        InvalidIfnameError& operator=(const InvalidIfnameError&) = delete;
    };

    using ConnectionId = unsigned int;
    using Header = std::pair<std::string, std::string>;
    using ConnectionHandler =
        std::function<int(ConnectionId id, const char* url,
                          const std::vector<Header>& requestHeaders,
                          std::vector<Header>& responseHeaders)>;
    using ConnectionRemovedHandler = std::function<void(ConnectionId id)>;
    using ShutdownHandler = std::function<void(void)>;

    struct Config {
        uint16_t port = 0;
        uint32_t connectionLimit = 10;
        std::string ifname;
        std::string address;
    };

    inline static const std::string anyAddress = "0.0.0.0";
    inline static const size_t connectionBufSize = 0x1000000;
    inline static const size_t connectionBlockSize = 0x1000000;

    explicit HttpServer(Config config, ConnectionHandler connectionHandler,
                        ConnectionRemovedHandler connectionRemovedHandler = {},
                        ShutdownHandler shutdownHandler = {});
    HttpServer(const HttpServer&) = delete;
    HttpServer(HttpServer&&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;
    HttpServer& operator=(HttpServer&&) = delete;
    ~HttpServer();
    uint16_t port() const;
    std::set<std::string> listeningAddresses() const;
    inline auto shuttingDown() const { return m_shutdownRequested; }
    std::optional<size_t> pushData(ConnectionId id, const uint8_t* data,
                                   size_t size);
    std::optional<size_t> pushData(ConnectionId id, std::string_view s);
    void dropConnection(ConnectionId id);
    std::optional<std::string> clientAddress(ConnectionId id) const;
    std::optional<std::string> queryValue(ConnectionId id, const char* key);
    static std::set<std::string> machineIfs();
    static std::set<std::string> machineAddresses();

   private:
    using TimePoint = decltype(std::chrono::steady_clock::now());

    struct ConnectionCtx {
        ConnectionId id = 0;
        HttpServer* server = nullptr;
        MHD_Connection* mhdConn = nullptr;
        DataBuffer buf{connectionBufSize, connectionBufSize * 10};
        bool removed = false;
        bool suspended = false;
        TimePoint suspendTime;

        ConnectionCtx(ConnectionId id, HttpServer* server,
                      MHD_Connection* mhdConn);
    };

    inline static const int32_t maxSuspendTime = 5000;  // millisec

    MHD_Daemon* m_daemon = nullptr;
    Config m_config;
    ConnectionHandler m_connectionHandler;
    ConnectionRemovedHandler m_connectionRemovedHandler;
    ShutdownHandler m_shutdownHandler;
    ConnectionId m_nextConnectionId = 1;
    std::unordered_map<ConnectionId, ConnectionCtx> m_connections;
    bool m_shutdownRequested = false;
    std::string m_address;
    std::thread m_gcThread;
    std::mutex m_connMtx;
    static ssize_t mhdContentReaderCallback(void* cls, uint64_t pos, char* buf,
                                            size_t max);
    static MHD_Result mhdConnectionHandler(
        void* cls, MHD_Connection* connection, const char* url,
        const char* method, const char* version, const char* upload_data,
        size_t* upload_data_size, void** con_cls);
    static MHD_Result rejectMhdConnection(MHD_Connection* connection,
                                          unsigned int code);
    static void mhdNotifyConnectionCallback(void* cls,
                                            MHD_Connection* connection,
                                            void** socket_context,
                                            MHD_ConnectionNotificationCode toe);
    static std::pair</*ipv4*/ std::string, /*ipv6*/ std::string>
    addressForInterface(const std::string& ifname);
    static void mhdLogCallback(void* cls, const char* fm, va_list ap);
    static std::string connectionClientAddress(MHD_Connection* connection);
    static std::optional<size_t> pushDataInternal(ConnectionCtx& ctx,
                                                  const uint8_t* data,
                                                  size_t size);
    void makeDaemonUsingAddress(const std::string& address);
    void makeDaemonUsingIfname();
    void makeDaemon();
    unsigned int connectionsCount() const;
    ConnectionCtx& addMhdConnection(MHD_Connection* connection);
    std::optional<ConnectionId> connectionIdFromMhd(
        MHD_Connection* connection) const;
    std::optional<std::reference_wrapper<HttpServer::ConnectionCtx>>
    connectionCtx(ConnectionId id);
    std::optional<std::reference_wrapper<HttpServer::ConnectionCtx>>
    connectionCtxFromMhd(MHD_Connection* connection);
    void removeConnection(ConnectionId id);
    static void suspendConnection(ConnectionCtx& ctx);
    static void resumeConnection(ConnectionCtx& ctx);
    void removeGhostConnections();
    void startGhostConnectionCheckThread();
};

#endif  // HTTPSERVER_H
