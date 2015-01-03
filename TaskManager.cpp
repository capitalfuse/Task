/*--------------------------------------------------------------------
Task is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as
published by the Free Software Foundation, either version 3 of
the License, or (at your option) any later version.

Task is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

See GNU Lesser General Public License at <http://www.gnu.org/licenses/>.
--------------------------------------------------------------------*/

#include "Task.h"
#include <avr/power.h>


ISR(WDT_vect) {} // empty but needed to so the default reset isn't called

TaskManager::TaskManager() :
        _pFirstTask( NULL ),
        _pLastTask( NULL )
{
    _lastTick = millis();
}

void TaskManager::StartTask(Task* pTask)
{
    // append to the list
    if (_pFirstTask == NULL)
    {
        _pFirstTask = pTask;
        _pLastTask = pTask;
    }
    else
    {
        _pLastTask->pNext = pTask;
        _pLastTask = pTask;
    }

    pTask->Start();
}

void TaskManager::StopTask(Task* pTask)
{
    pTask->Stop();
}

bool TaskManager::Loop(uint8_t sleepMode, uint8_t watchdogTimeOutFlag, float watchdogTimeRatio)
{
    uint32_t currentTick = millis();
    uint32_t deltaTimeMs = currentTick - _lastTick;
    bool awakeFromNonIdle = false;
    if (deltaTimeMs > 0)
    {
        uint32_t nextWakeTimeMs = ProcessTasks(deltaTimeMs);

        RemoveStoppedTasks();
  
        // calc how long that took to process, 
        // calc a good sleep time into deltaTimeMs
        //
        _lastTick = currentTick;
        currentTick = millis();
        deltaTimeMs = currentTick - _lastTick;

        // if we have a task with less time available that what the last process pass
        // took, no need to sleep at all
        if (nextWakeTimeMs > deltaTimeMs + 1)
        {
            // idle is done different than other sleep modes,
            // due to watchdog wakeup model having a minimum sleep time of 15ms, 
            // if we are below that we always idle sleep 
            // Due to error in watchdog timer, its best to avoid times less than 250 
            // as these are the error that can happen for the longest times
            if (sleepMode == SLEEP_MODE_IDLE || nextWakeTimeMs <= 250 * watchdogTimeRatio) 
            {
                // for idle sleep mode:
                // due to Millis() using timer interupt at 1 ms, 
                // the cpu will be woke up by that every millisecond 

                // use watchdog timer for failsafe mode, 
                // total task update time less than 500ms
                wdt_reset();
                wdt_enable(watchdogTimeOutFlag);

                // just sleep
                set_sleep_mode(SLEEP_MODE_IDLE);
                sleep_enable();
                sleep_cpu(); // will sleep in this call
                sleep_disable(); 
            }
            else
            {
                // for other modes, the millis will not continue to run
                // so we need to the watchdog timer to awaken the cpu and
                // also adjust timing based on the amount of sleep
                WatchDogWakeupSleep(sleepMode, nextWakeTimeMs, watchdogTimeOutFlag, deltaTimeMs, watchdogTimeRatio);
                awakeFromNonIdle = true;
            }
        }
    }
    return awakeFromNonIdle;
}

uint32_t TaskManager::ProcessTasks(uint32_t deltaTimeMs)
{
    uint32_t nextWakeTimeMs = ((uint32_t)-1); // MAX_UINT32

    // Update Tasks
    //
    Task* pIterate = _pFirstTask;
    while (pIterate != NULL)
    {
        // skip any non running tasks
        if (pIterate->taskState == TaskState_Running)
        {
            if (pIterate->remainingTimeMs <= deltaTimeMs)
            {
                // calc per task delta time
                uint32_t taskDeltaTime = max(1, ((pIterate->initialTimeMs - pIterate->remainingTimeMs) + deltaTimeMs) - 1);

                pIterate->OnUpdate(taskDeltaTime);

                // add the initial time so we don't loose any remainders
                pIterate->remainingTimeMs += pIterate->initialTimeMs;

                // if we are still less than delta time, things are running slow
                // so push to the next update frame
                if (pIterate->remainingTimeMs <= deltaTimeMs)
                {
                    pIterate->remainingTimeMs = deltaTimeMs + 1;
                }
            }

            pIterate->remainingTimeMs -= deltaTimeMs;

            if (pIterate->remainingTimeMs < nextWakeTimeMs)
            {
                nextWakeTimeMs = pIterate->remainingTimeMs;
            }
        }

        pIterate = pIterate->pNext;
    }
    return nextWakeTimeMs;
}

void TaskManager::RemoveStoppedTasks()
{
    // walk task list and remove stopped tasks
    //
    Task* pIterate = _pFirstTask;
    Task* pIteratePrev = NULL;
    while (pIterate != NULL)
    {
        Task* pNext = pIterate->pNext;
        if (pIterate->taskState == TaskState_Stopping)
        {
            // Remove it
            pIterate->taskState = TaskState_Stopped;
            pIterate->pNext = NULL; 

            if (pIterate == _pFirstTask)
            {
                // first one, correct our first pointer
                _pFirstTask = pNext;
                if (pIterate == _pLastTask)
                {
                    // last one, correct our last pointer
                    _pLastTask = _pFirstTask;
                }
            }
            else
            {
                // all others correct the previous to remove it
                pIteratePrev->pNext = pNext;
                if (pIterate == _pLastTask)
                {
                    // last one, correct our last pointer
                    _pLastTask = pIteratePrev;
                }
            }
        }
        else
        {
            // didn't remove, advance the previous pointer
            pIteratePrev = pIterate;
        }
        pIterate = pNext; // iterate to the next
    }
}

void TaskManager::WatchDogWakeupSleep(uint8_t sleepMode, 
                                      uint32_t nextWakeTimeMs, 
                                      uint8_t watchdogTimeOutFlag, 
                                      uint32_t deltaTimeMs,
                                      float watchdogTimeRatio)
{
    // calc watchdog timer best fit time
    uint32_t sleepDeltaTimeMs;
    uint8_t sleepTime;
                
    //if (nextWakeTimeMs < 30)
    //{
    //    sleepDeltaTimeMs = 15;
    //    sleepTime = WDTO_15MS;
    //}
    //else if (nextWakeTimeMs < 60)
    //{
    //    sleepDeltaTimeMs = 30;
    //    sleepTime = WDTO_30MS;
    //}
    //else if (nextWakeTimeMs < 120)
    //{
    //    sleepDeltaTimeMs = 60;
    //    sleepTime = WDTO_60MS;
    //}
    //else if (nextWakeTimeMs < 250)
    //{
    //    sleepDeltaTimeMs = 120 * watchdogTimeRatio;
    //    sleepTime = WDTO_120MS;
    //}
    //else 
    if (nextWakeTimeMs < 500 * watchdogTimeRatio)
    {
        sleepDeltaTimeMs = 250 * watchdogTimeRatio;
        sleepTime = WDTO_250MS;
    }
    else if (nextWakeTimeMs < 1000 * watchdogTimeRatio)
    {
        sleepDeltaTimeMs = 500 * watchdogTimeRatio;
        sleepTime = WDTO_500MS;
    }
    else if (nextWakeTimeMs < 2000 * watchdogTimeRatio)
    {
        sleepDeltaTimeMs = 1000 * watchdogTimeRatio;
        sleepTime = WDTO_1S;
    }
    else // if (nextWakeTimeMs < 4000)
    {
        sleepDeltaTimeMs = 2000 * watchdogTimeRatio;
        sleepTime = WDTO_2S;
    }
// these seem to be defined but do not function correctly
// tested on both  Mega 2560 & Mega 328
//#ifdef WDTO_4S
//    if (nextWakeTimeMs >= 4000 && nextWakeTimeMs < 8000)
//    {
//        sleepDeltaTimeMs = 4000;
//        sleepTime = WDTO_4S;
//    }
//#endif
//#ifdef WDTO_8S
//    if (nextWakeTimeMs >= 8000)
//    {
//        sleepDeltaTimeMs = 8000;
//        sleepTime = WDTO_8S;
//    }
//#endif
//Serial.println(sleepDeltaTimeMs);
//Serial.flush();

    // wdt_enable(sleepTime); 
    // the above method is hardcoded to always reset, which we dont want here
    // the following implements the trigger interrupt 
    cli();
    wdt_reset();
    MCUSR &= ~_BV(WDRF);
    WDTCSR |= _BV(WDCE) | _BV(WDE); // start timed change
    WDTCSR = _BV(WDIE) | (sleepTime);
    sei();

    // enter sleep
    set_sleep_mode(sleepMode);
    sleep_enable();
    sleep_cpu(); // will sleep in this call
    sleep_disable(); 

    // wdt_disable();
    // protocol requires a reset call before clearing,
    // all under interupts being off
    cli();
    wdt_reset();
    MCUSR &= ~_BV(WDRF);
    WDTCSR |= _BV(WDCE) | _BV(WDE); // start timed change
    // WDTCSR = 0x00; this will disable wathdog all together
    WDTCSR = _BV(WDE) | (watchdogTimeOutFlag); // this will go back to a reset watch dog timer
    sei();

    // based on sleep type, we may need to fixup
    // the time values as the timer for millis() may have
    // been stopped
    // FUTURE: If wake was caused by external interrupts, 
    // there is no way to ratify millis() so what do we do?
    _lastTick = millis() - (sleepDeltaTimeMs + deltaTimeMs);
}
