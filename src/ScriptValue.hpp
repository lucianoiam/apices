/*
 * Hip-Hop / High Performance Hybrid Audio Plugins
 * Copyright (C) 2021 Luciano Iam <oss@lucianoiam.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef SCRIPTVALUE_HPP
#define SCRIPTVALUE_HPP

#include "extra/String.hpp"

#include "DistrhoPluginInfo.h"

START_NAMESPACE_DISTRHO

class ScriptValue {
public:
    ScriptValue(bool b)        : fB(b) {};
    ScriptValue(int32_t i)     : fI(i) {};
    ScriptValue(double d)      : fD(d) {};
    ScriptValue(String s)      : fS(s) {};
    ScriptValue(const char* s) : fS(String(s)) {};

    operator bool()        const { return fB; }
    operator int32_t()     const { return fI; }
    operator double()      const { return fD; }
    operator String()      const { return fS; }
    operator const char*() const { return fS; }

private:
    bool    fB;
    int32_t fI;
    double  fD;
    String  fS;

};

END_NAMESPACE_DISTRHO

#endif // SCRIPTVALUE_HPP
