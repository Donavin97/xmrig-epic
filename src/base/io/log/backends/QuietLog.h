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

#ifndef XMRIG_QUIETLOG_H
#define XMRIG_QUIETLOG_H


#include "base/kernel/interfaces/ILogBackend.h"


namespace xmrig {


class QuietLog : public ILogBackend
{
public:
    QuietLog(const char *fileName = "/var/log/xmrig-quiet.log");
    ~QuietLog() override;

protected:
    void print(uint64_t timestamp, int level, const char *line, size_t offset, size_t size, bool colors) override;

private:
    FILE *m_tty;
};


} /* namespace xmrig */


#endif /* XMRIG_QUIETLOG_H */