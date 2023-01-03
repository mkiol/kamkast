/* Copyright (C) 2022 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef NOGUIEVENTLOOP_H
#define NOGUIEVENTLOOP_H

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>

#include "event.hpp"

class NoGuiEventLoop {
   public:
    explicit NoGuiEventLoop(Event::Handler eventHandler);
    ~NoGuiEventLoop();

    // -- event loop api --
    void start();
    void enqueue(Event::Pack&& event);
    void enqueue(Event::Type event);
    void shutdown();
    inline bool shuttingDown() const { return m_shuttingDown; }
    inline void notifyCastingStarted(
        [[maybe_unused]] Event::CastingProps&& event) {}
    inline void notifyCastingEnded() {}
    static void notifyServerStarted(Event::ServerProps&& event);
    inline void notifyServerEnded() {}
    // --------------------

   private:
    Event::Handler m_eventHandler;
    bool m_shuttingDown = false;
    std::queue<Event::Pack> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;

    void loop();
};

#endif  // NOGUIEVENTLOOP_H
