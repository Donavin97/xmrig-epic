/* XMRig
 * Copyright (c) 2019      Spudz76     <https://github.com/Spudz76>
 * Copyright (c) 2018-2020 SChernykh   <https://github.com/SChernykh>
 * Copyright (c) 2016-2020 XMRig       <https://github.com/xmrig>, <support@xmrig.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include <syslog.h>


#include "base/io/log/backends/SysLog.h"
#include "version.h"


xmrig::SysLog::SysLog()
{
    openlog(APP_ID, LOG_PID, LOG_USER);
}


xmrig::SysLog::~SysLog()
{
    closelog();
}


void xmrig::SysLog::print(uint64_t, int level, const char *line, size_t offset, size_t, bool colors)
{
    if (colors) {
        return;
    }

    // XMRig's NONE (-1) and USER (8) are not syslog severities. Passing USER
    // directly to syslog() encodes it as facility LOG_USER | severity 0
    // (LOG_EMERG), so systemd-journald forwards the message to every terminal
    // as an emergency wall broadcast. Map levels outside the syslog severity
    // range (0..7) to severity 6 (LOG_INFO). Numeric constants are used because
    // Log.h defines same-named function-like logging macros.
    int priority = 6;
    if (level >= 0 && level <= 7) {
        priority = level;
    }

    syslog(priority, "%s", line + offset);
}
