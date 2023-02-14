/* Copyright (C) 2022 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef SFOSGUI_H
#define SFOSGUI_H

#include <sailfishapp.h>

#include <QObject>
#include <QQuickView>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <array>
#include <functional>
#include <utility>
#include <vector>

#include "caster.hpp"
#include "config.h"
#include "event.hpp"
#include "logger.hpp"
#include "settings.hpp"

class SfosGui : public QObject {
    Q_OBJECT
    Q_PROPERTY(
        QString urlPath READ getUrlPath WRITE setUrlPath NOTIFY configChanged)
    Q_PROPERTY(int port READ getPort WRITE setPort NOTIFY configChanged)
    Q_PROPERTY(
        int ifnameIdx READ getIfnameIdx WRITE setIfnameIdx NOTIFY configChanged)
    Q_PROPERTY(int audioVolume READ getAudioVolume WRITE setAudioVolume NOTIFY
                   configChanged)
    Q_PROPERTY(int videoSourceIdx READ getVideoSourceIdx WRITE setVideoSourceIdx
                   NOTIFY configChanged)
    Q_PROPERTY(int audioSourceIdx READ getAudioSourceIdx WRITE setAudioSourceIdx
                   NOTIFY configChanged)
    Q_PROPERTY(int streamFormatIdx READ getStreamFormatIdx WRITE
                   setStreamFormatIdx NOTIFY configChanged)
    Q_PROPERTY(int videoOrientationIdx READ getVideoOrientationIdx WRITE
                   setVideoOrientationIdx NOTIFY configChanged)
    Q_PROPERTY(
        bool castingActive READ getCastingActive NOTIFY castingActiveChanged)
    Q_PROPERTY(
        bool serverActive READ getServerActive NOTIFY serverActiveChanged)

   public:
    explicit SfosGui(int argc, char **argv, Event::Handler eventHandler,
                     Settings &settings);
    ~SfosGui();
    Q_INVOKABLE QUrl icon() const;
    Q_INVOKABLE void cancelCasting();
    Q_INVOKABLE QStringList getVideoSources() const;
    Q_INVOKABLE QStringList getAudioSources() const;
    Q_INVOKABLE QStringList getStreamFormats() const;
    Q_INVOKABLE QStringList getVideoOrientations() const;
    Q_INVOKABLE QStringList getIfnames();
    Q_INVOKABLE QStringList getWebUrls() const;
    Q_INVOKABLE QStringList getStreamUrls() const;

    // -- event loop api --
    void start() const;
    void enqueue(Event::Pack &&event);
    void enqueue(Event::Type event);
    void shutdown();
    inline bool shuttingDown() const { return m_shuttingDown; }
    void notifyCastingStarted(Event::CastingProps &&event);
    void notifyCastingEnded();
    void notifyServerStarted(Event::ServerProps &&event);
    void notifyServerEnded();
    // --------------------

   Q_SIGNALS:
    void eventEnqueued(Event::Pack event);
    void configChanged();
    void castingActiveChanged();
    void serverActiveChanged();

   private:
    Event::Handler m_eventHandler;
    Settings &m_settings;
    std::vector<Caster::VideoSourceProps> m_videoSources;
    std::vector<Caster::AudioSourceProps> m_audioSources;
    QStringList m_ifnames;
    std::array<std::pair<Settings::VideoOrientation, QString>, 5>
        m_videoOrientations = {
            std::pair{Settings::VideoOrientation::Auto, tr("Auto")},
            std::pair{Settings::VideoOrientation::Portrait, tr("Portrait")},
            std::pair{Settings::VideoOrientation::InvertedPortrait,
                      tr("Inverted portrait")},
            std::pair{Settings::VideoOrientation::Landscape, tr("Landscape")},
            std::pair{Settings::VideoOrientation::InvertedLandscape,
                      tr("Inverted landscape")}};
    std::array<std::pair<Settings::StreamFormat, QString>, 3> m_streamFormats =
        {std::pair{Settings::StreamFormat::Mp4, "MP4"},
         std::pair{Settings::StreamFormat::MpegTs, "MPEG-TS"},
         std::pair{Settings::StreamFormat::Mp3, "MP3"}};

    bool m_shuttingDown = false;
    std::optional<Event::CastingProps> m_castingProps;
    std::optional<Event::ServerProps> m_serverProps;

    void initGui();
    void setupConfiguration();
    void updateSettings(Settings &&settings);
    static QStringList makeIfnames();
    void setDefaultSettings();

    QString getUrlPath() const;
    void setUrlPath(const QString &value);
    int getIfnameIdx() const;
    void setIfnameIdx(int value);
    int getPort() const;
    void setPort(int value);
    int getAudioVolume() const;
    void setAudioVolume(float value);
    int getVideoSourceIdx() const;
    void setVideoSourceIdx(int value);
    int getAudioSourceIdx() const;
    void setAudioSourceIdx(int value);
    int getStreamFormatIdx() const;
    void setStreamFormatIdx(int value);
    int getVideoOrientationIdx() const;
    void setVideoOrientationIdx(int value);
    bool getCastingActive() const;
    bool getServerActive() const;
};

#endif  // SFOSGUI_H
