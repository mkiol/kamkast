/* Copyright (C) 2022-2023 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "sfosgui.hpp"

#include <sailfishapp.h>

#include <QDir>
#include <QGuiApplication>
#include <QQmlContext>
#include <QStandardPaths>
#include <QString>
#include <algorithm>
#include <string>

#include "config.h"
#include "httpserver.hpp"
#include "logger.hpp"
#include "qtlogger.hpp"

SfosGui::SfosGui(int argc, char **argv, Event::Handler eventHandler,
                 Settings &settings)
    : m_eventHandler{std::move(eventHandler)},
      m_settings{settings},
      m_videoSources{Caster::videoSources()},
      m_audioSources{Caster::audioSources()},
      m_ifnames{makeIfnames()} {
    initQtLogger();

    qRegisterMetaType<Event::Pack>("Event::Pack");

    connect(
        this, &SfosGui::eventEnqueued, this,
        [this](Event::Pack event) {
            if (m_shuttingDown) return;
            if (m_eventHandler) {
                try {
                    m_eventHandler(std::move(event));
                } catch (const std::exception &e) {
                    LOGE("unexpected exception: " << e.what());
                    shutdown();
                }
            }
        },
        Qt::QueuedConnection);

    SailfishApp::application(argc, argv);
    QGuiApplication::setApplicationName(APP_ID);
    QGuiApplication::setOrganizationName(APP_ORG);
    QGuiApplication::setApplicationDisplayName(APP_NAME);
    QGuiApplication::setApplicationVersion(APP_VERSION);

    setupConfiguration();

    initGui();
}

SfosGui::~SfosGui() { shutdown(); }

void SfosGui::shutdown() {
    if (m_shuttingDown) return;
    m_shuttingDown = true;

    QGuiApplication::quit();
}

void SfosGui::updateSettings(Settings &&settings) {
    try {
        settings.check();
        m_settings = std::move(settings);
        m_settings.saveToFile();
        Q_EMIT configChanged();
    } catch (const std::runtime_error &e) {
        LOGW("invalid config: " << e.what());
    }
}

void SfosGui::setupConfiguration() {
    if (!m_settings.configFile.empty()) return;

    auto path = [] {
        QDir confDir{
            QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)};
        confDir.mkpath(QCoreApplication::organizationName() +
                       QDir::separator() + QCoreApplication::applicationName());
        return confDir.absolutePath() + QDir::separator() +
               QCoreApplication::organizationName() + QDir::separator() +
               QCoreApplication::applicationName() + QDir::separator() +
               QStringLiteral("settings.config");
    }();

    m_settings.configFile = path.toStdString();

    if (QFile::exists(path)) {
        m_settings.loadFromFile();
        try {
            m_settings.check();
        } catch (const std::runtime_error &e) {
            LOGW("invalid config: " << e.what());
            m_settings.configFile.clear();
        }
    } else {
        setDefaultSettings();
    }

    m_settings.saveToFile();
}

void SfosGui::setDefaultSettings() {
    if (m_settings.videoSourceName.empty()) {
        m_settings.videoSourceName = "front";
        m_settings.videoOrientation = Settings::VideoOrientation::Portrait;
    }
    if (m_settings.audioSourceName.empty()) m_settings.audioSourceName = "mic";
    if (m_settings.port == 0) m_settings.port = 9099;
    if (m_settings.ifname.empty()) m_settings.ifname = "wlan0";
}

void SfosGui::start() const {
    if (m_shuttingDown) return;
    QGuiApplication::exec();
}

void SfosGui::enqueue(Event::Pack &&event) {
    if (m_shuttingDown) return;
    Q_EMIT eventEnqueued(event);
}

void SfosGui::enqueue(Event::Type event) {
    if (m_shuttingDown) return;
    Q_EMIT eventEnqueued({event, {}, {}});
}

QUrl SfosGui::icon() const {
    return QUrl::fromLocalFile(
        QStringLiteral("/usr/share/icons/hicolor/172x172/apps/%1.png")
            .arg(APP_BINARY_ID));
}

void SfosGui::initGui() {
    auto *view = SailfishApp::createView();
    auto *context = view->rootContext();

    context->setContextProperty(QStringLiteral("APP_NAME"), APP_NAME);
    context->setContextProperty(QStringLiteral("APP_ID"), APP_ID);
    context->setContextProperty(QStringLiteral("APP_VERSION"), APP_VERSION);
    context->setContextProperty(QStringLiteral("APP_COPYRIGHT_YEAR"),
                                APP_COPYRIGHT_YEAR);
    context->setContextProperty(QStringLiteral("APP_AUTHOR"), APP_AUTHOR);
    context->setContextProperty(QStringLiteral("APP_AUTHOR_EMAIL"),
                                APP_AUTHOR_EMAIL);
    context->setContextProperty(QStringLiteral("APP_SUPPORT_EMAIL"),
                                APP_SUPPORT_EMAIL);
    context->setContextProperty(QStringLiteral("APP_WEBPAGE"), APP_WEBPAGE);
    context->setContextProperty(QStringLiteral("APP_LICENSE"), APP_LICENSE);
    context->setContextProperty(QStringLiteral("APP_LICENSE_URL"),
                                APP_LICENSE_URL);
    context->setContextProperty(QStringLiteral("APP_LICENSE_SPDX"),
                                APP_LICENSE_SPDX);
    context->setContextProperty(QStringLiteral("APP_TRANSLATORS_STR"),
                                APP_TRANSLATORS_STR);
    context->setContextProperty(QStringLiteral("APP_LIBS_STR"), APP_LIBS_STR);
    context->setContextProperty(QStringLiteral("gui"), this);

    view->setSource(SailfishApp::pathTo(QStringLiteral("qml/main.qml")));
    view->show();
}

QString SfosGui::getUrlPath() const {
    return QString::fromStdString(m_settings.urlPath);
}
void SfosGui::setUrlPath(const QString &value) {
    auto v = value.toStdString();
    if (v != m_settings.urlPath) {
        auto s = m_settings;
        s.urlPath = v;
        updateSettings(std::move(s));
    }
}

int SfosGui::getPort() const { return m_settings.port; }
void SfosGui::setPort(int value) {
    auto v = static_cast<decltype(m_settings.port)>(value);
    if (v != m_settings.port) {
        auto s = m_settings;
        s.port = v;
        updateSettings(std::move(s));
    }
}

float SfosGui::getAudioVolume() const { return m_settings.audioVolume; }
void SfosGui::setAudioVolume(float value) {
    auto v = static_cast<decltype(m_settings.audioVolume)>(value);
    if (v != m_settings.audioVolume) {
        auto s = m_settings;
        s.audioVolume = v;
        updateSettings(std::move(s));
    }
}

int SfosGui::getVideoSourceIdx() const {
    auto it = std::find_if(m_videoSources.cbegin(), m_videoSources.cend(),
                           [this](const Caster::VideoSourceProps &s) {
                               return s.name == m_settings.videoSourceName;
                           });
    if (it == m_videoSources.cend()) return 0;

    return std::distance(m_videoSources.cbegin(), it) + 1;
}
void SfosGui::setVideoSourceIdx(int value) {
    auto idx = getVideoSourceIdx();
    if (value == idx || value > static_cast<int>(m_videoSources.size()) ||
        value < 0)
        return;

    auto s = m_settings;
    if (value == 0)
        s.videoSourceName.clear();
    else
        s.videoSourceName = m_videoSources.at(value - 1).name;
    updateSettings(std::move(s));
}

QStringList SfosGui::getVideoSources() const {
    QStringList list;
    list.reserve(m_videoSources.size() + 1);
    list.push_back(tr("Don't use"));

    for (const auto &s : m_videoSources)
        list.push_back(QString::fromStdString(s.friendlyName));

    return list;
}

int SfosGui::getAudioSourceIdx() const {
    auto it = std::find_if(m_audioSources.cbegin(), m_audioSources.cend(),
                           [this](const Caster::AudioSourceProps &s) {
                               return s.name == m_settings.audioSourceName;
                           });
    if (it == m_audioSources.cend()) return 0;

    return std::distance(m_audioSources.cbegin(), it) + 1;
}
void SfosGui::setAudioSourceIdx(int value) {
    auto idx = getAudioSourceIdx();
    if (value == idx || value > static_cast<int>(m_audioSources.size()) ||
        value < 0)
        return;

    auto s = m_settings;
    if (value == 0)
        s.audioSourceName.clear();
    else
        s.audioSourceName = m_audioSources.at(value - 1).name;
    updateSettings(std::move(s));
}

QStringList SfosGui::getAudioSources() const {
    QStringList list;
    list.reserve(m_audioSources.size() + 1);
    list.push_back(tr("Don't use"));

    for (const auto &s : m_audioSources)
        list.push_back(QString::fromStdString(s.friendlyName));

    return list;
}

int SfosGui::getStreamFormatIdx() const {
    const auto it = std::find_if(
        m_streamFormats.cbegin(), m_streamFormats.cend(),
        [this](const auto &s) { return s.first == m_settings.streamFormat; });
    if (it == m_streamFormats.cend()) return 0;
    return std::distance(m_streamFormats.cbegin(), it);
}
void SfosGui::setStreamFormatIdx(int value) {
    auto idx = getStreamFormatIdx();
    if (value == idx || value >= static_cast<int>(m_streamFormats.size()) ||
        value < 0)
        return;

    auto s = m_settings;
    s.streamFormat = m_streamFormats.at(value).first;
    updateSettings(std::move(s));
}
QStringList SfosGui::getStreamFormats() const {
    QStringList list;
    list.reserve(m_streamFormats.size());

    for (const auto &s : m_streamFormats) list.push_back(s.second);

    return list;
}

int SfosGui::getVideoOrientationIdx() const {
    const auto it =
        std::find_if(m_videoOrientations.cbegin(), m_videoOrientations.cend(),
                     [this](const auto &s) {
                         return s.first == m_settings.videoOrientation;
                     });
    if (it == m_videoOrientations.cend()) return 0;
    return std::distance(m_videoOrientations.cbegin(), it);
}
void SfosGui::setVideoOrientationIdx(int value) {
    auto idx = getVideoOrientationIdx();
    if (value == idx || value >= static_cast<int>(m_videoOrientations.size()) ||
        value < 0)
        return;

    auto s = m_settings;
    s.videoOrientation = m_videoOrientations.at(value).first;
    updateSettings(std::move(s));
}
QStringList SfosGui::getVideoOrientations() const {
    QStringList list;
    list.reserve(m_videoOrientations.size());

    for (const auto &s : m_videoOrientations) list.push_back(s.second);

    return list;
}

int SfosGui::getIfnameIdx() const {
    const auto it = std::find_if(
        m_ifnames.cbegin(), m_ifnames.cend(), [this](const auto &n) {
            return n == QString::fromStdString(m_settings.ifname);
        });
    if (it == m_ifnames.cend()) return 0;
    return std::distance(m_ifnames.cbegin(), it);
}
void SfosGui::setIfnameIdx(int value) {
    auto idx = getIfnameIdx();
    if (value == idx || value >= static_cast<int>(m_ifnames.size()) ||
        value < 0)
        return;

    auto s = m_settings;
    if (value == 0)
        s.ifname.clear();
    else
        s.ifname = m_ifnames.at(value).toStdString();
    updateSettings(std::move(s));
}

QStringList SfosGui::makeIfnames() {
    auto ifs = HttpServer::machineIfs();

    QStringList list;
    list.reserve(ifs.size() + 1);
    list.push_back(tr("All"));

    for (const auto &n : ifs) list.push_back(QString::fromStdString(n));

    return list;
}

QStringList SfosGui::getIfnames() {
    m_ifnames = makeIfnames();
    return m_ifnames;
}

bool SfosGui::getCastingActive() const {
    return static_cast<bool>(m_castingProps);
}

void SfosGui::notifyCastingStarted(Event::CastingProps &&event) {
    auto old = getCastingActive();
    m_castingProps = std::move(event);
    if (!old) Q_EMIT castingActiveChanged();
}

void SfosGui::notifyCastingEnded() {
    if (getCastingActive()) {
        m_castingProps.reset();
        Q_EMIT castingActiveChanged();
    }
}

bool SfosGui::getServerActive() const {
    return static_cast<bool>(m_serverProps);
}

void SfosGui::notifyServerStarted(Event::ServerProps &&event) {
    auto old = getServerActive();
    m_serverProps = std::move(event);
    if (!old) Q_EMIT serverActiveChanged();
}

void SfosGui::notifyServerEnded() {
    if (getServerActive()) {
        m_serverProps.reset();
        Q_EMIT serverActiveChanged();
    }
}

QStringList SfosGui::getWebUrls() const {
    QStringList list;

    if (!getServerActive()) return list;

    list.reserve(m_serverProps->webUrls.size());

    for (const auto &url : m_serverProps->webUrls)
        list.push_back(QString::fromStdString(url));

    return list;
}

QStringList SfosGui::getStreamUrls() const {
    QStringList list;

    if (!getServerActive()) return list;

    list.reserve(m_serverProps->streamUrls.size());

    for (const auto &url : m_serverProps->streamUrls)
        list.push_back(QString::fromStdString(url));

    return list;
}

void SfosGui::cancelCasting() { enqueue(Event::Type::StopCaster); }
