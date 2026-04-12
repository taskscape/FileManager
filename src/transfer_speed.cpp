// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include "cfgdlg.h"
#include "worker.h"
#include "execlog.h"

#include <Aclapi.h>
#include <Ntsecapi.h>

CTransferSpeedMeter::CTransferSpeedMeter()
{
    Clear();
}

void CTransferSpeedMeter::Clear()
{
    ActIndexInTrBytes = 0;
    ActIndexInTrBytesTimeLim = 0;
    CountOfTrBytesItems = 0;
    ActIndexInLastPackets = 0;
    CountOfLastPackets = 0;
    ResetSpeed = TRUE;
    MaxPacketSize = 0;
}

void CTransferSpeedMeter::GetSpeed(CQuadWord* speed)
{
    CALL_STACK_MESSAGE1("CTransferSpeedMeter::GetSpeed()");

    DWORD time = GetTickCount();

    if (CountOfLastPackets >= 2)
    { // test whether this is a low speed (calculated from LastPacketsSize and LastPacketsTime)
        int firstPacket = ((TRSPMETER_NUMOFSTOREDPACKETS + 1) + ActIndexInLastPackets - CountOfLastPackets) % (TRSPMETER_NUMOFSTOREDPACKETS + 1);
        int lastPacket = ((TRSPMETER_NUMOFSTOREDPACKETS + 1) + ActIndexInLastPackets - 1) % (TRSPMETER_NUMOFSTOREDPACKETS + 1);
        DWORD lastPacketTime = LastPacketsTime[lastPacket];
        DWORD totalTime = lastPacketTime - LastPacketsTime[firstPacket]; // time between receiving the first and last packet
        if (totalTime >= ((DWORD)(CountOfLastPackets - 1) * TRSPMETER_STPCKTSMININTERVAL) / TRSPMETER_NUMOFSTOREDPACKETS)
        {                                     // this is a low speed (up to TRSPMETER_NUMOFSTOREDPACKETS packets per TRSPMETER_STPCKTSMININTERVAL ms)
            if (time - lastPacketTime > 2000) // two-second "protection" period for the last computed slow speed
            {                                 // check whether the speed has dropped by more than double compared to the speed of the last packet; if so, display
                                              // zero speed (so that when a slow transfer stops we do not keep showing the last recorded speed value)
                int preLastPacket = ((TRSPMETER_NUMOFSTOREDPACKETS + 1) + ActIndexInLastPackets - 2) % (TRSPMETER_NUMOFSTOREDPACKETS + 1);
                if ((UINT64)2 * MaxPacketSize * (lastPacketTime - LastPacketsTime[preLastPacket]) < (UINT64)LastPacketsSize[lastPacket] * (time - lastPacketTime))
                {
                    speed->SetUI64(0);
                    ResetSpeed = TRUE;
                    return; // speed dropped at least two times, better show zero
                }
            }
            if (totalTime > TRSPMETER_ACTSPEEDSTEP * TRSPMETER_ACTSPEEDNUMOFSTEPS)
            { // compute the speed only from data closest to TRSPMETER_ACTSPEEDSTEP * TRSPMETER_ACTSPEEDNUMOFSTEPS
                // (if packets arrive slowly, the queue may contain packets from the last five minutes - but here we
                // compute the "instant" speed, not the average over the last five minutes)
                int i = firstPacket;
                while (1)
                {
                    if (++i >= TRSPMETER_NUMOFSTOREDPACKETS + 1)
                        i = 0;
                    if (i == lastPacket || lastPacketTime - LastPacketsTime[i] < TRSPMETER_ACTSPEEDSTEP * TRSPMETER_ACTSPEEDNUMOFSTEPS)
                        break;
                    firstPacket = i;
                }
                totalTime = lastPacketTime - LastPacketsTime[firstPacket];
            }
            UINT64 totalSize = 0; // sum of all packet sizes except the first one (from whitch we use only the time)
            do
            {
                if (++firstPacket >= TRSPMETER_NUMOFSTOREDPACKETS + 1)
                    firstPacket = 0;
                totalSize += LastPacketsSize[firstPacket];
            } while (firstPacket != lastPacket);
            speed->SetUI64((1000 * totalSize) / totalTime);
            return; // low speed computed, we are done
        }
        else // this is a high speed (more than TRSPMETER_NUMOFSTOREDPACKETS packets per TRSPMETER_STPCKTSMININTERVAL ms),
        {    // perform a sudden speed drop test (especially when copying zero-sized files or creating empty directories begins)
            if (time - lastPacketTime > 800)
            { // if no packet has arrived for 800 ms, report zero speed
                speed->SetUI64(0);
                ResetSpeed = TRUE;
                return;
            }
        }
    }
    else // nothing to calculate from yet, report "0 B/s"
    {
        speed->SetUI64(0);
        return;
    }
    // high speed (more than TRSPMETER_NUMOFSTOREDPACKETS packets per TRSPMETER_STPCKTSMININTERVAL ms)
    if (CountOfTrBytesItems > 0) // after the connection is established this is "always true"
    {
        int actIndexAdded = 0;                           // 0 = current index not included, 1 = current index included
        int emptyTrBytes = 0;                            // number of counted empty steps
        UINT64 total = 0;                                // total number of bytes over the last at most TRSPMETER_ACTSPEEDNUMOFSTEPS steps
        int addFromTrBytes = CountOfTrBytesItems - 1;    // number of closed steps to add from the queue
        DWORD restTime = 0;                              // time from the last counted step to now
        if ((int)(time - ActIndexInTrBytesTimeLim) >= 0) // current index already closed + empty steps may be needed
        {
            emptyTrBytes = (time - ActIndexInTrBytesTimeLim) / TRSPMETER_ACTSPEEDSTEP;
            restTime = (time - ActIndexInTrBytesTimeLim) % TRSPMETER_ACTSPEEDSTEP;
            emptyTrBytes = min(emptyTrBytes, TRSPMETER_ACTSPEEDNUMOFSTEPS);
            if (emptyTrBytes < TRSPMETER_ACTSPEEDNUMOFSTEPS) // empty steps are not enough; include the current index as well
            {
                total = TransferedBytes[ActIndexInTrBytes];
                actIndexAdded = 1;
            }
            addFromTrBytes = TRSPMETER_ACTSPEEDNUMOFSTEPS - actIndexAdded - emptyTrBytes;
            addFromTrBytes = min(addFromTrBytes, CountOfTrBytesItems - 1); // how many closed steps from the queue to include
        }
        else
        {
            restTime = time + TRSPMETER_ACTSPEEDSTEP - ActIndexInTrBytesTimeLim;
            total = TransferedBytes[ActIndexInTrBytes];
        }

        int actIndex = ActIndexInTrBytes;
        int i;
        for (i = 0; i < addFromTrBytes; i++)
        {
            if (--actIndex < 0)
                actIndex = TRSPMETER_ACTSPEEDNUMOFSTEPS; // moving along the circular queue
            total += TransferedBytes[actIndex];
        }
        DWORD t = (addFromTrBytes + actIndexAdded + emptyTrBytes) * TRSPMETER_ACTSPEEDSTEP + restTime;
        if (t > 0)
            speed->SetUI64((total * 1000) / t);
        else
            speed->SetUI64(0); // nothing to calculate from yet, report "0 B/s"
    }
    else
        speed->SetUI64(0); // nothing to calculate from yet, report "0 B/s"
}

void CTransferSpeedMeter::JustConnected()
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CTransferSpeedMeter::JustConnected()");

    TransferedBytes[0] = 0;
    ActIndexInTrBytes = 0;
    ActIndexInTrBytesTimeLim = (LastPacketsTime[0] = GetTickCount()) + TRSPMETER_ACTSPEEDSTEP;
    CountOfTrBytesItems = 1;
    LastPacketsSize[0] = 0;
    ActIndexInLastPackets = 1;
    CountOfLastPackets = 1;
    ResetSpeed = TRUE;
    MaxPacketSize = 0;
}

void CTransferSpeedMeter::BytesReceived(DWORD count, DWORD time, DWORD maxPacketSize)
{
    DEBUG_SLOW_CALL_STACK_MESSAGE1("CTransferSpeedMeter::BytesReceived(, ,)"); // ignore parameters for performance reasons (the call stack already slows us down)

    MaxPacketSize = maxPacketSize;

    if (count > 0)
    {
        //    if (count > MaxPacketSize)  // happens when the speed changes (due to SpeedLimit or ProgressBufferLimit); packets arrive that were read using the old buffer size
        //      TRACE_E("CTransferSpeedMeter::BytesReceived(): count > MaxPacketSize (" << count << " > " << MaxPacketSize << ")");

        if (ResetSpeed)
            ResetSpeed = FALSE;

        LastPacketsSize[ActIndexInLastPackets] = count;
        LastPacketsTime[ActIndexInLastPackets] = time;
        if (++ActIndexInLastPackets >= TRSPMETER_NUMOFSTOREDPACKETS + 1)
            ActIndexInLastPackets = 0;
        if (CountOfLastPackets < TRSPMETER_NUMOFSTOREDPACKETS + 1)
            CountOfLastPackets++;
    }
    if ((int)(time - ActIndexInTrBytesTimeLim) < 0) // within the current time interval, just add the byte count to the interval
    {
        TransferedBytes[ActIndexInTrBytes] += count;
    }
    else // outside the current time interval, we must create a new interval
    {
        int emptyTrBytes = (time - ActIndexInTrBytesTimeLim) / TRSPMETER_ACTSPEEDSTEP;
        int i = min(emptyTrBytes, TRSPMETER_ACTSPEEDNUMOFSTEPS); // more has no effect (the entire queue would be reset)
        if (i > 0 && CountOfTrBytesItems <= TRSPMETER_ACTSPEEDNUMOFSTEPS)
            CountOfTrBytesItems = min(TRSPMETER_ACTSPEEDNUMOFSTEPS + 1, CountOfTrBytesItems + i);
        while (i--)
        {
            if (++ActIndexInTrBytes > TRSPMETER_ACTSPEEDNUMOFSTEPS)
                ActIndexInTrBytes = 0; // moving along the circular queue
            TransferedBytes[ActIndexInTrBytes] = 0;
        }
        ActIndexInTrBytesTimeLim += (emptyTrBytes + 1) * TRSPMETER_ACTSPEEDSTEP;
        if (++ActIndexInTrBytes > TRSPMETER_ACTSPEEDNUMOFSTEPS)
            ActIndexInTrBytes = 0; // moving along the circular queue
        if (CountOfTrBytesItems <= TRSPMETER_ACTSPEEDNUMOFSTEPS)
            CountOfTrBytesItems++;
        TransferedBytes[ActIndexInTrBytes] = count;
    }
}

void CTransferSpeedMeter::AdjustProgressBufferLimit(DWORD* progressBufferLimit, DWORD lastFileBlockCount,
                                                    DWORD lastFileStartTime)
{
    if (CountOfLastPackets > 1 && lastFileBlockCount > 0) // "always true": at the start of the file CountOfLastPackets is 1 (2 = we already have one packet)
    {
        unsigned __int64 size = 0; // total size of stored packets of the last file
        int i = ((TRSPMETER_NUMOFSTOREDPACKETS + 1) + ActIndexInLastPackets - 1) % (TRSPMETER_NUMOFSTOREDPACKETS + 1);
        int c = min((DWORD)(CountOfLastPackets - 1), lastFileBlockCount);
        int packets = c;
        DWORD ti = GetTickCount();
        while (c--)
        {
            size += LastPacketsSize[i];
            if (i-- == 0)
                i = TRSPMETER_NUMOFSTOREDPACKETS;
            if (ti - LastPacketsTime[i] > 2000)
            {
                packets -= c;
                break; // take packets at most 2 seconds old (trying to compute the "current" speed)
            }
        }
        DWORD totalTime = min(ti - LastPacketsTime[i], ti - lastFileStartTime); // LastPacketsTime[i] may be older than lastFileStartTime (it is the last packet of the previous file); we care only about the time spent on this file
        if (totalTime == 0)
            totalTime = 10; // treat 0 ms as 10 ms (approx. the GetTickCount() step)
        unsigned __int64 speed = (size * 1000) / totalTime;
        DWORD bufLimit = ASYNC_SLOW_COPY_BUF_SIZE;
        while (bufLimit < ASYNC_COPY_BUF_SIZE)
        {
            // determined experimentally that Windows 7 loves a 32 KB buffer size; with it the utilization curve
            // of the network link is usually nicely smooth, whereas with 64 KB it jumps like crazy
            // and the overall achieved speed is about 5% lower... so a dirty bloody hack: we will also
            // prefer 32 KB... up to 8 * 128 (1024 KB/s)... that skips 64 KB and 128 KB, the next
            // buffer limit is as high as 256 KB
            // +
            // introduce a measure against oscillation between two buffer limit sizes when the speed is on the boundary
            // between two buffer limit sizes; raising it by one level will be harder (to choose the same buffer
            // the speed may be up to 9 * bufLimit instead of the standard 8 * bufLimit)
            if (bufLimit == 32 * 1024) // for the 32 KB buffer limit we use the values for the 128 KB buffer limit (instead of choosing 64 KB and 128 KB we pick 32 KB)
            {
                if (speed <= (bufLimit == *progressBufferLimit ? 9 * 128 * 1024 : 8 * 128 * 1024))
                    break;
                bufLimit = 256 * 1024; // 32 KB did not work, try up to 256 KB (64 KB and 128 KB cannot happen, 32 KB would have been chosen)
            }
            else
            {
                if (speed <= (bufLimit == *progressBufferLimit ? 9 * bufLimit : 8 * bufLimit))
                    break;
                bufLimit *= 2;
            }
        }
        if (bufLimit > ASYNC_COPY_BUF_SIZE)
            bufLimit = ASYNC_COPY_BUF_SIZE;
        *progressBufferLimit = bufLimit;
#ifdef WORKER_COPY_DEBUG_MSG
        TRACE_I("AdjustProgressBufferLimit(): speed=" << speed / 1024.0 << " KB/s, size=" << size << " B, packets=" << packets << ", new buffer limit=" << bufLimit);
#endif // WORKER_COPY_DEBUG_MSG
    }
    else
        TRACE_E("Unexpected situation in CTransferSpeedMeter::AdjustProgressBufferLimit()!");
}

//
// ****************************************************************************
// CProgressSpeedMeter
//

CProgressSpeedMeter::CProgressSpeedMeter()
{
    Clear();
}

void CProgressSpeedMeter::Clear()
{
    ActIndexInTrBytes = 0;
    ActIndexInTrBytesTimeLim = 0;
    CountOfTrBytesItems = 0;
    ActIndexInLastPackets = 0;
    CountOfLastPackets = 0;
    MaxPacketSize = 0;
}

void CProgressSpeedMeter::GetSpeed(CQuadWord* speed)
{
    CALL_STACK_MESSAGE1("CProgressSpeedMeter::GetSpeed()");

    DWORD time = GetTickCount();

    if (CountOfLastPackets >= 2)
    { // test whether this is a low speed (calculated from LastPacketsSize and LastPacketsTime)
        int firstPacket = ((PRSPMETER_NUMOFSTOREDPACKETS + 1) + ActIndexInLastPackets - CountOfLastPackets) % (PRSPMETER_NUMOFSTOREDPACKETS + 1);
        int lastPacket = ((PRSPMETER_NUMOFSTOREDPACKETS + 1) + ActIndexInLastPackets - 1) % (PRSPMETER_NUMOFSTOREDPACKETS + 1);
        DWORD lastPacketTime = LastPacketsTime[lastPacket];
        DWORD totalTime = lastPacketTime - LastPacketsTime[firstPacket]; // time between receiving the first and last packet
        if (totalTime >= ((DWORD)(CountOfLastPackets - 1) * PRSPMETER_STPCKTSMININTERVAL) / PRSPMETER_NUMOFSTOREDPACKETS)
        {                                     // this is a low speed (up to PRSPMETER_NUMOFSTOREDPACKETS packets per PRSPMETER_STPCKTSMININTERVAL ms)
            if (time - lastPacketTime > 5000) // five-second "protection" period for the last computed slow speed
            {                                 // check whether the speed has dropped by more than four times compared to the speed of the last packet; if so, display
                                              // zero speed (so that when a slow transfer stops we do not keep showing the last recorded time-left value)
                int preLastPacket = ((PRSPMETER_NUMOFSTOREDPACKETS + 1) + ActIndexInLastPackets - 2) % (PRSPMETER_NUMOFSTOREDPACKETS + 1);
                if ((UINT64)4 * MaxPacketSize * (lastPacketTime - LastPacketsTime[preLastPacket]) < (UINT64)LastPacketsSize[lastPacket] * (time - lastPacketTime))
                {
                    speed->SetUI64(0);
                    return; // speed dropped at least two times, better show zero
                }
            }
            if (totalTime > PRSPMETER_ACTSPEEDSTEP * PRSPMETER_ACTSPEEDNUMOFSTEPS)
            { // compute the speed only from data closest to PRSPMETER_ACTSPEEDSTEP * PRSPMETER_ACTSPEEDNUMOFSTEPS
                // (if packets arrive slowly, the queue may contain packets from the last five minutes, but here we
                // compute the speed over the last X seconds, not the average over the last five minutes)
                int i = firstPacket;
                while (1)
                {
                    if (++i >= PRSPMETER_NUMOFSTOREDPACKETS + 1)
                        i = 0;
                    if (i == lastPacket || lastPacketTime - LastPacketsTime[i] < PRSPMETER_ACTSPEEDSTEP * PRSPMETER_ACTSPEEDNUMOFSTEPS)
                        break;
                    firstPacket = i;
                }
                totalTime = lastPacketTime - LastPacketsTime[firstPacket];
            }
            UINT64 totalSize = 0; // sum of all packet sizes except the first one (from whitch we use only the time)
            do
            {
                if (++firstPacket >= PRSPMETER_NUMOFSTOREDPACKETS + 1)
                    firstPacket = 0;
                totalSize += LastPacketsSize[firstPacket];
            } while (firstPacket != lastPacket);
            speed->SetUI64((1000 * totalSize) / totalTime);
            return; // low speed computed, we are done
        }
        else // this is a high speed (more than PRSPMETER_NUMOFSTOREDPACKETS packets per PRSPMETER_STPCKTSMININTERVAL ms),
        {    // perform a sudden speed drop test (especially when copying zero-sized files or creating empty directories begins)
            if (time - lastPacketTime > 5000)
            { // if no packet has arrived for 5000 ms, report zero speed
                speed->SetUI64(0);
                return;
            }
        }
    }
    else // nothing to calculate from yet, report "0 B/s"
    {
        speed->SetUI64(0);
        return;
    }
    // high speed (more than PRSPMETER_NUMOFSTOREDPACKETS packets per PRSPMETER_STPCKTSMININTERVAL ms)
    if (CountOfTrBytesItems > 0) // after the connection is established this is "always true"
    {
        int actIndexAdded = 0;                           // 0 = current index not included, 1 = current index included
        int emptyTrBytes = 0;                            // number of counted empty steps
        UINT64 total = 0;                                // total number of bytes over the last at most PRSPMETER_ACTSPEEDNUMOFSTEPS steps
        int addFromTrBytes = CountOfTrBytesItems - 1;    // number of closed steps to add from the queue
        DWORD restTime = 0;                              // time from the last counted step to now
        if ((int)(time - ActIndexInTrBytesTimeLim) >= 0) // current index already closed + empty steps may be needed
        {
            emptyTrBytes = (time - ActIndexInTrBytesTimeLim) / PRSPMETER_ACTSPEEDSTEP;
            restTime = (time - ActIndexInTrBytesTimeLim) % PRSPMETER_ACTSPEEDSTEP;
            emptyTrBytes = min(emptyTrBytes, PRSPMETER_ACTSPEEDNUMOFSTEPS);
            if (emptyTrBytes < PRSPMETER_ACTSPEEDNUMOFSTEPS) // empty steps are not enough; include the current index as well
            {
                total = TransferedBytes[ActIndexInTrBytes];
                actIndexAdded = 1;
            }
            addFromTrBytes = PRSPMETER_ACTSPEEDNUMOFSTEPS - actIndexAdded - emptyTrBytes;
            addFromTrBytes = min(addFromTrBytes, CountOfTrBytesItems - 1); // how many closed steps from the queue to include
        }
        else
        {
            restTime = time + PRSPMETER_ACTSPEEDSTEP - ActIndexInTrBytesTimeLim;
            total = TransferedBytes[ActIndexInTrBytes];
        }

        int actIndex = ActIndexInTrBytes;
        int i;
        for (i = 0; i < addFromTrBytes; i++)
        {
            if (--actIndex < 0)
                actIndex = PRSPMETER_ACTSPEEDNUMOFSTEPS; // moving along the circular queue
            total += TransferedBytes[actIndex];
        }
        DWORD t = (addFromTrBytes + actIndexAdded + emptyTrBytes) * PRSPMETER_ACTSPEEDSTEP + restTime;
        if (t > 0)
            speed->SetUI64((total * 1000) / t);
        else
            speed->SetUI64(0); // nothing to calculate from yet, report "0 B/s"
    }
    else
        speed->SetUI64(0); // nothing to calculate from yet, report "0 B/s"
}

void CProgressSpeedMeter::JustConnected()
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CProgressSpeedMeter::JustConnected()");

    TransferedBytes[0] = 0;
    ActIndexInTrBytes = 0;
    ActIndexInTrBytesTimeLim = (LastPacketsTime[0] = GetTickCount()) + PRSPMETER_ACTSPEEDSTEP;
    CountOfTrBytesItems = 1;
    LastPacketsSize[0] = 0;
    ActIndexInLastPackets = 1;
    CountOfLastPackets = 1;
    MaxPacketSize = 0;
}

void CProgressSpeedMeter::BytesReceived(DWORD count, DWORD time, DWORD maxPacketSize)
{
    DEBUG_SLOW_CALL_STACK_MESSAGE1("CProgressSpeedMeter::BytesReceived(, ,)"); // ignore parameters for performance reasons (the call stack already slows us down)

    MaxPacketSize = maxPacketSize;

    if (count > 0)
    {
        //    if (count > MaxPacketSize)  // happens when the speed changes (due to SpeedLimit or ProgressBufferLimit); packets arrive that were read using the old buffer size
        //      TRACE_E("CProgressSpeedMeter::BytesReceived(): count > MaxPacketSize (" << count << " > " << MaxPacketSize << ")");

        LastPacketsSize[ActIndexInLastPackets] = count;
        LastPacketsTime[ActIndexInLastPackets] = time;
        if (++ActIndexInLastPackets >= PRSPMETER_NUMOFSTOREDPACKETS + 1)
            ActIndexInLastPackets = 0;
        if (CountOfLastPackets < PRSPMETER_NUMOFSTOREDPACKETS + 1)
            CountOfLastPackets++;
    }
    if ((int)(time - ActIndexInTrBytesTimeLim) < 0) // within the current time interval, just add the byte count to the interval
    {
        TransferedBytes[ActIndexInTrBytes] += count;
    }
    else // outside the current time interval, we must create a new interval
    {
        int emptyTrBytes = (time - ActIndexInTrBytesTimeLim) / PRSPMETER_ACTSPEEDSTEP;
        int i = min(emptyTrBytes, PRSPMETER_ACTSPEEDNUMOFSTEPS); // more has no effect (the entire queue would be reset)
        if (i > 0 && CountOfTrBytesItems <= PRSPMETER_ACTSPEEDNUMOFSTEPS)
            CountOfTrBytesItems = min(PRSPMETER_ACTSPEEDNUMOFSTEPS + 1, CountOfTrBytesItems + i);
        while (i--)
        {
            if (++ActIndexInTrBytes > PRSPMETER_ACTSPEEDNUMOFSTEPS)
                ActIndexInTrBytes = 0; // moving along the circular queue
            TransferedBytes[ActIndexInTrBytes] = 0;
        }
        ActIndexInTrBytesTimeLim += (emptyTrBytes + 1) * PRSPMETER_ACTSPEEDSTEP;
        if (++ActIndexInTrBytes > PRSPMETER_ACTSPEEDNUMOFSTEPS)
            ActIndexInTrBytes = 0; // moving along the circular queue
        if (CountOfTrBytesItems <= PRSPMETER_ACTSPEEDNUMOFSTEPS)
            CountOfTrBytesItems++;
        TransferedBytes[ActIndexInTrBytes] = count;
    }
}

