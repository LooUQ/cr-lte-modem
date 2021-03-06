// Copyright (c) 2020 LooUQ Incorporated.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

/* 
* arduinoTiming.cpp provides the translation between LTEm1 platform "C" and Arduino "C++".
* Each platform function has a matching Arduino C++ free-function to wrap arduino functions.
------------------------------------------------------------------------------------------------------------------------- */

#include "lqPlatform-timing.h"
#include <Arduino.h>


platform_yieldCB_func_t platform_yieldCB_func;


uint32_t lMillis()
{
    return millis();
}

void lYield()
{
    yield();                            // allow for platform yield processing (ex: Arduino scheduler, ESPx, etc.)
    if (platform_yieldCB_func)          // allow for device application yield processing
        platform_yieldCB_func();
}


void lDelay(uint32_t delay_ms)
{
    delay(delay_ms);
}

bool lTimerExpired(uint32_t timerStart, uint32_t timerTimeout)
{
    return (timerStart == 0) ? 0 : millis() - timerStart > timerTimeout;
}

