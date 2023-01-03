/* Copyright (C) 2022 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "logger.hpp"

#include <fmt/chrono.h>
#include <fmt/core.h>
#include <threads.h>

#include <chrono>
#include <cstdio>

Logger::LogType Logger::m_level = Logger::LogType::Error;

std::ostream &operator<<(std::ostream &os, Logger::LogType type) {
    switch (type) {
        case Logger::LogType::Trace:
            os << "trace";
            break;
        case Logger::LogType::Debug:
            os << "debug";
            break;
        case Logger::LogType::Info:
            os << "info";
            break;
        case Logger::LogType::Warning:
            os << "warning";
            break;
        case Logger::LogType::Error:
            os << "error";
            break;
    }
    return os;
}

void Logger::setLevel(LogType level) {
    if (m_level != level) {
        auto old = m_level;
        m_level = level;
        LOGD("logging level changed: " << old << " => " << m_level);
    }
}

Logger::LogType Logger::level() { return m_level; }

bool Logger::match(LogType type) {
    return static_cast<int>(type) >= static_cast<int>(m_level);
}

Logger::Message::Message(LogType type, const char *file, const char *function,
                         int line, bool endl)
    : m_type{type}, m_file{file}, m_fun{function}, m_line{line}, m_endl{endl} {}

inline static auto typeToChar(Logger::LogType type) {
    switch (type) {
        case Logger::LogType::Trace:
            return 'T';
        case Logger::LogType::Debug:
            return 'D';
        case Logger::LogType::Info:
            return 'I';
        case Logger::LogType::Warning:
            return 'W';
        case Logger::LogType::Error:
            return 'E';
    }
    return '-';
}

Logger::Message::~Message() {
    if (!match(m_type)) return;

    auto now = std::chrono::system_clock::now();
    auto msecs = std::chrono::duration_cast<std::chrono::milliseconds>(
                     now.time_since_epoch())
                     .count() %
                 1000;

    try {
        fmt::print(stderr,
                   m_endl ? "[{}] {:%H:%M:%S}.{} {:#10x} {}:{} - {}\n"
                          : "[{}] {:%H:%M:%S}.{} {:#10x} {}:{} - {}",
                   typeToChar(m_type), now, msecs, thrd_current(), m_fun,
                   m_line, m_os.str().c_str());
    } catch (const std::runtime_error &e) {
        fmt::print(stderr, "logger error: {}\n", e.what());
    }

    fflush(stderr);
}
