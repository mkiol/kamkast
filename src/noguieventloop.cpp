/* Copyright (C) 2022 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "noguieventloop.hpp"

#include "fmt/core.h"
#include "logger.hpp"

NoGuiEventLoop::NoGuiEventLoop(Event::Handler eventHandler)
    : m_eventHandler{std::move(eventHandler)} {}

NoGuiEventLoop::~NoGuiEventLoop() {
    LOGD("no-gui-event-loop termination");
    shutdown();
}

void NoGuiEventLoop::start() { loop(); }

void NoGuiEventLoop::shutdown() {
    m_shuttingDown = true;
    m_cv.notify_one();
}

void NoGuiEventLoop::enqueue(Event::Pack&& event) {
    if (m_shuttingDown) return;

    {
        std::lock_guard lock{m_mutex};
        m_queue.push(std::move(event));
    }

    m_cv.notify_one();
}

void NoGuiEventLoop::enqueue(Event::Type event) { enqueue({event, {}, {}}); }

void NoGuiEventLoop::loop() {
    decltype(m_queue) queue;

    while (!m_shuttingDown) {
        {
            std::unique_lock<std::mutex> lock{m_mutex};
            m_cv.wait(lock,
                      [this] { return m_shuttingDown || !m_queue.empty(); });
            std::swap(queue, m_queue);
        }

        while (!m_shuttingDown && !queue.empty()) {
            m_eventHandler(std::move(queue.front()));
            queue.pop();
        }
    }

    LOGD("no-gui-event-loop ended");
}

void NoGuiEventLoop::notifyServerStarted(Event::ServerProps&& event) {
    fmt::print("Use the following URL(s) to open web-interface:\n");
    for (const auto& url : event.webUrls) fmt::print("{}\n", url);

    fmt::print(
        "\nUse the following URL(s) to start streaming with default "
        "configuration:\n");
    for (const auto& url : event.streamUrls) fmt::print("{}\n", url);
}
