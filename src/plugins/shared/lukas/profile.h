// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// ****************************************************************************
//
// CCounterDriver

class CCounterDriver
{
public:
    CCounterDriver(CCounter& counter) : Counter(counter)
    {
        Counter.HoldOff();
    }
    ~CCounterDriver()
    {
        Counter.HoldOn();
    }

private:
    CCounter& Counter;
};

extern CCounter TotalRuntime;
extern CCounter ShiftBoundaries;
extern CCounter Diff;
extern CCounter EditScriptBuilder;
