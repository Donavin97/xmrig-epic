/* XMRig
 * Copyright (c) 2024      XMRig       <https://github.com/xmrig>, <support@xmrig.com>
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


#include "base/io/log/backends/QuietLog.h"
#include "base/io/log/Log.h"


#include <cassert>
#include <cstdio>
#include <cstring>
#include <unistd.h>


xmrig::QuietLog::QuietLog(const char *fileName) :
    m_tty(nullptr)
{
    // Open /dev/tty directly to bypass stdout/stderr (and systemd capture)
    m_tty = fopen("/dev/tty", "w");
}


xmrig::QuietLog::~QuietLog()
{
    if (m_tty) {
        fclose(m_tty);
    }
}


void xmrig::QuietLog::print(uint64_t, int level, const char *line, size_t, size_t size, bool colors)
{
    // Only handle USER level messages (quiet-exempt commands: h, d, q, c, s, p, r)
    if (level != xmrig::Log::USER) {
        return;
    }

    // Skip colored output (we want plain text)
    if (colors) {
        return;
    }

    if (!m_tty) {
        return;
    }

    assert(strlen(line) == size);

    fwrite(line, 1, size, m_tty);
    fflush(m_tty);
}