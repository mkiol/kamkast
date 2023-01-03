/* Copyright (C) 2022 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef KAMKAST_H
#define KAMKAST_H

#include <fmt/format.h>

#include <cstdint>
#include <fstream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#ifdef USE_SFOS
#include <unistd.h>

#include "sfosgui.hpp"
#endif

#include "caster.hpp"
#include "event.hpp"
#include "httpserver.hpp"
#include "noguieventloop.hpp"
#include "settings.hpp"

class Kamkast {
   public:
#ifdef USE_SFOS
    using LoopType = std::variant<NoGuiEventLoop, SfosGui>;
#else
    using LoopType = std::variant<NoGuiEventLoop>;
#endif

    Kamkast(Settings&& settings, int argc, char** argv);
    ~Kamkast();
    static std::pair<std::string, std::string> sourcesTable();
    static std::string videoSourcesTable();
    static std::string audioSourcesTable();
    void start();
    void shutdown();

   private:
    enum class HttpRequestType {
        Unknown,
        Invalid,
        WebUi,
        Stream,
        Ctrl,
    };

    static const constexpr char* m_streamUrlPath = "/stream";
    static const constexpr char* m_ctrlUrlPath = "/ctrl";
    static const constexpr uint32_t m_connectionLimit = 5;

    Settings m_settings;
    std::optional<LoopType> m_loop;
    std::optional<HttpServer::ConnectionId> m_castingConnId;
    std::optional<Caster> m_caster;
    std::optional<HttpServer> m_server;
    std::optional<std::ofstream> m_logFile;

    template <typename Sources>
    static std::string sourcesTable(const Sources& sources) {
        size_t maxid = 4;
        size_t maxname = 4;

        for (const auto& s : sources) {
            if (s.name.size() > maxid) maxid = s.name.size();
            if (s.friendlyName.size() > maxname)
                maxname = s.friendlyName.size();
        }

        auto fmt = fmt::format("| {{:<{}}} | {{:<{}}} |\n", maxid, maxname);
        auto fmth = fmt::format("+-{{:-<{}}}-+-{{:-<{}}}-+\n", maxid, maxname);

        std::ostringstream os;

        os << fmt::format(fmt, "id", "name") << fmt::format(fmth, "-", "-");

        for (const auto& s : sources)
            os << fmt::format(fmt, s.name, s.friendlyName);

        return os.str();
    }

    void enqueueEvent(Event::Pack&& event);
    void enqueueEvent(Event::Type event);
    void logConnection(
        std::string_view message,
        std::optional<HttpServer::ConnectionId> connId = std::nullopt);
    void notifyCastingStarted(std::optional<HttpServer::ConnectionId> connId);
    void notifyCastingEnded();
    void notifyServerStarted();
    void notifyServerEnded();
    void startCaster(HttpServer::ConnectionId connId, Settings&& settings);
    HttpRequestType determineRequestType(const std::string& url) const;
    void stopCaster();
    void updateSettingsFromUrlParams(HttpServer::ConnectionId id,
                                     Settings& settings);
    int handleWebRequest(HttpServer::ConnectionId id,
                         std::vector<HttpServer::Header>& responseHeaders);
    static std::string contentType(Settings::StreamFormat format);
    int handleStreamRequest(Settings settings, HttpServer::ConnectionId id,
                            std::vector<HttpServer::Header>& responseHeaders);
    int handleCtrlRequest(HttpServer::ConnectionId id, const std::string& url,
                          std::vector<HttpServer::Header>& responseHeaders);
    void startServer();
    void stopServer();
    Event::ServerProps makeServerProps() const;
    void handleEvent(Event::Pack&& event);
};

#endif // KAMKAST_H
