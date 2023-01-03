/* Copyright (C) 2022 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "event.hpp"

std::ostream& operator<<(std::ostream& os, Event::Type event) {
    switch (event) {
        case Event::Type::StartServer:
            os << "start-server";
            break;
        case Event::Type::StopServer:
            os << "stop-server";
            break;
        case Event::Type::StartCaster:
            os << "start-caster";
            break;
        case Event::Type::StopCaster:
            os << "stop-caster";
            break;
        case Event::Type::CasterStarted:
            os << "caster-started";
            break;
        case Event::Type::CasterEnded:
            os << "caster-ended";
            break;
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const Event::Pack& pack) {
    os << pack.type;
    return os;
}
