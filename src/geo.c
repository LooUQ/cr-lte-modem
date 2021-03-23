// Copyright (c) 2020 LooUQ Incorporated.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>

//#define _DEBUG
#include "ltem1c.h"
#include "geo.h"

// private local declarations
static resultCode_t geoQueryResponseParser(const char *response);


/* public functions
 * --------------------------------------------------------------------------------------------- */
#pragma region public functions

/**
 *	\brief Create a geo-fence for future position evaluations.
 */
resultCode_t geo_add(uint8_t geoId, geo_mode_t mode, geo_shape_t shape, double lat1, double lon1, double lat2, double lon2, double lat3, double lon3, double lat4, double lon4)
{
    #define COORD_SZ 12
    #define COORD_P 6
    #define CMDSZ 32+8*COORD_SZ

    char cmdStr[CMDSZ] = {0};

    if (mode != geo_mode_noUrc)                                 // currently only supporting mode 0 (no event reporting)
        return RESULT_CODE_BADREQUEST;

    //void floatToString(float fVal, char *buf, uint8_t bufSz, uint8_t precision)
    snprintf(cmdStr, CMDSZ, "AT+QCFGEXT=\"addgeo\",%d,0,%d,%4.6f,%4.6f,%4.6f", geoId, shape, lat1, lon1, lat2);

    if (shape == geo_shape_circlerad && (lon2 != 0 || lat3 != 0 || lon3 != 0 || lat4 != 0 || lon4 != 0) ||
        shape == geo_shape_circlept &&  (lat3 != 0 || lon3 != 0 || lat4 != 0 || lon4 != 0) ||
        shape == geo_shape_triangle &&  (lat4 != 0 || lon4 != 0))
    {
        return RESULT_CODE_BADREQUEST;
    }

    if (shape >= geo_shape_circlept)
    {
        char cmdChunk[COORD_SZ];
        snprintf(cmdChunk, COORD_SZ, ",%4.6f", lon2);
        strcat(cmdStr, cmdChunk);
    }
    if (shape >= geo_shape_triangle)
    {
        char cmdChunk[2*COORD_SZ];
        snprintf(cmdChunk, 2*COORD_SZ, ",%4.6f,%4.6f", lat3, lon3);
        strcat(cmdStr, cmdChunk);
    }
    if (shape == geo_shape_quadrangle)
    {
        char cmdChunk[2*COORD_SZ];
        snprintf(cmdChunk, 2*COORD_SZ, ",%4.6f,%4.6f", lat4, lon4);
        strcat(cmdStr, cmdChunk);
    }

    if (action_tryInvoke(cmdStr))
    {
        return action_awaitResult(true).statusCode;
    }
    return RESULT_CODE_CONFLICT;
}



/**
 *	\brief Delete a geo-fence for future position evaluations.
 */
resultCode_t geo_delete(uint8_t geoId)
{
    char cmdStr[28] = {0};
    snprintf(cmdStr, 80, "AT+QCFGEXT=\"deletegeo\",%d", geoId);
    if (action_tryInvoke(cmdStr))
    {
        return action_awaitResult(true).statusCode;
    }
    return RESULT_CODE_CONFLICT;
}



/**
 *	\brief Determine the current location relation to a geo-fence, aka are you inside or outside the fence.
 */
geo_position_t geo_query(uint8_t geoId)
{
    char cmdStr[28] = {0};
    snprintf(cmdStr, 80, "AT+QCFGEXT=\"querygeo\",%d", geoId);

    if (action_tryInvoke(cmdStr))
    {
        actionResult_t atResult = action_awaitResult(true);
        if (atResult.statusCode == RESULT_CODE_SUCCESS)
            return geo_position_unknown;
        return atResult.statusCode;
    }
    return RESULT_CODE_CONFLICT;
};



#pragma endregion

/* private (static) functions
 * --------------------------------------------------------------------------------------------- */
#pragma region private functions

/**
 *	\brief Action response parser for a geo-fence query.
 */
static resultCode_t geoQueryResponseParser(const char *response)
{
    return serviceResponseParser(response, "+QCFGEXT: \"querygeo\",");
}


#pragma endregion
