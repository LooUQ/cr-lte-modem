// Copyright (c) 2020 LooUQ Incorporated.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "ltem1c.h"

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define ACTIONS_RETRY_MAX 20
#define ACTIONS_RETRY_INTERVAL 50

// Local private declarations
static const char *strnend(const char *charStr, uint16_t maxSz);
static action_t *createAction(uint8_t resultSz);
static bool tryActionLock(const char *cmdStr, bool retry);



/**
 *	\brief Resets (initializes) an AT command structure.
 *
 *  \param [in] atCmd - Pointer to command struct to reset
 */
action_t *action_reset()
{
    if (g_ltem1->action == NULL)
    {
        g_ltem1->action = calloc(1, sizeof(action_t));
    }
    memset(g_ltem1->action->cmdStr, 0, ACTION_INVOKE_CMDSTR_SZ);
    g_ltem1->action->resultHead = NULL;
    g_ltem1->action->resultTail = NULL;
    g_ltem1->action->resultCode = ACTION_RESULT_PENDING;
    g_ltem1->action->invokedAt = 0;
    g_ltem1->action->irdPending = iopProcess_void;
    return g_ltem1->action;
}



/**
 *	\brief Invokes a simple AT command to the BG96 module using the driver default atCmd object. 
 *
 *	\param [in] cmdStr - The command string to send to the BG96 module.
 *  \param [in] retry - Function will retry busy module if set to true.
 *  \param [in] customCmdCompleteParser_func - Custom command response parser to signal result is complete. NULL for std parser.
 * 
 *  \return True if action was invoked, false if not
 */
bool action_tryInvoke(const char * cmdStr, bool retry)
{
    if ( !tryActionLock(cmdStr, retry) )
        return false;

    action_reset(g_ltem1->action);
    strncpy(g_ltem1->action->cmdStr, cmdStr, ACTION_INVOKE_CMDSTR_SZ);
    
    PRINTF("\raction=%s\r", g_ltem1->action->cmdStr);

    g_ltem1->action->invokedAt = timing_millis();
    strcat(g_ltem1->action->cmdStr, ASCII_sCR);
    iop_txSend(g_ltem1->action->cmdStr, strlen(g_ltem1->action->cmdStr));
    return true;
}



/**
 *	\brief Performs data transfer (send) sub-action.

 *  \param [in] data - Pointer to the block of binary data to send.
 *  \param [in] dataSz - The size of the data block. 
 */
void action_sendData(const char *data, uint16_t dataSz)
{
    g_ltem1->action->cmdCompleteParser_func = action_okResultParser;
    g_ltem1->action->invokedAt = timing_millis();
    //g_ltem1->currentAction = g_ltem1->stdAction;

    iop_txSend(data, dataSz);
}



/**
 *	\brief Gathers command response strings and determines if command has completed.
 *
 *  \param [in] actionCmd - Pointer to command struct.
 *  \param [in] autoClose - If true, action will automatically close and release action lock on completion.
 * 
 *  \return Command action result type. See ACTION_RESULT_* macros in LTEm1c.h.
 */
actionResult_t action_getResult(char *response, uint16_t responseSz, uint16_t timeout, uint16_t (*customCmdCompleteParser_func)(const char *response), bool autoClose)
{
    // return pendingAtCommand.resultCode;
    // wait for BG96 response in FIFO buffer

    if (g_ltem1->action->resultHead == NULL)
    {
        g_ltem1->action->resultHead = response;
        g_ltem1->action->resultTail = response;
        g_ltem1->action->resultSz = responseSz;
        g_ltem1->action->timeoutMillis = timeout == 0 ? ACTION_DEFAULT_TIMEOUT_MILLIS : timeout;
        g_ltem1->action->cmdCompleteParser_func = (customCmdCompleteParser_func == NULL) ? action_okResultParser : customCmdCompleteParser_func;
        memchr(response, 0, responseSz);
    }
    
    actionResult_t parserResult = ACTION_RESULT_PENDING;
    iopXfrResult_t rxResult = iop_rxGetCmdQueued(g_ltem1->action->resultTail, g_ltem1->action->resultSz);

    if (rxResult == iopXfrResult_complete || rxResult == iopXfrResult_truncated)
    {
        // deplete last result from the overall command result buffer
        uint8_t resultSz = strlen(g_ltem1->action->resultTail);
        g_ltem1->action->resultSz -= resultSz;
        g_ltem1->action->resultTail += resultSz;

        // invoke command result complete parser, true returned if complete
        parserResult = (*g_ltem1->action->cmdCompleteParser_func)(g_ltem1->action->resultHead);
        PRINTF_GRAY("prsr=%d \r", parserResult);
    }

    if (parserResult >= ACTION_RESULT_SUCCESS)                          // parser signaled complete
    {
        PRINTF_INFO("action duration=%d\r", timing_millis() - g_ltem1->action->invokedAt);
        if (autoClose || parserResult != ACTION_RESULT_SUCCESS)         // defer close (as requested) for send data subcommand
            g_ltem1->action->cmdStr[0] = NULL;
        return parserResult;
    }

    if (timing_millis() - g_ltem1->action->invokedAt > g_ltem1->action->timeoutMillis)
    {
        PRINTF_WARN("action duration=%d\r", timing_millis() - g_ltem1->action->invokedAt);
        g_ltem1->action->cmdStr[0] = NULL;
        return ACTION_RESULT_TIMEOUT;
    }
    return ACTION_RESULT_PENDING;
}



/**
 *	\brief Waits for an AT command result.
 *
 *  \param [in] actionCmd - Pointer to command struct. Pass in NULL to use LTEm1 integrated action structure.
 * 
 *  \return Command action result type. See ACTION_RESULT_* macros in LTEm1c.h.
 */
actionResult_t action_awaitResult(char *response, uint16_t responseSz, uint16_t timeout, uint16_t (*customCmdCompleteParser_func)(const char *response), bool autoClose)
{
    actionResult_t actionResult;
    PRINTF_GRAY("awaitRslt\r");
    do
    {
        actionResult = action_getResult(response, responseSz, timeout, customCmdCompleteParser_func, autoClose);
        yield();

    } while (actionResult == ACTION_RESULT_PENDING);

    return actionResult;
}



/**
 *	\brief Cancels an AT command currently underway.
 *
 *  \param[in] actionCmd Pointer to command struct
 */
void action_cancel()
{
    action_reset();
    g_ltem1->action = NULL;
}


#pragma region completionParsers
/* --------------------------------------------------------------------------------------------------------
 * Action command completion parsers
 * ----------------------------------------------------------------------------------------------------- */

#define OK_COMPLETED_STRING "OK\r\n"
#define OK_COMPLETED_LENGTH 4
#define ERROR_COMPLETED_STRING "ERROR\r\n"
#define ERROR_VALUE_OFFSET 7
#define FAIL_COMPLETED_STRING "FAIL\r\n"
#define FAIL_VALUE_OFFSET 6
#define NOCARRIER_COMPLETED_STRING "NO CARRIER\r\n"
#define NOCARRIER_VALUE_OFFSET 12
#define CME_PREABLE "+CME ERROR:"
#define CME_PREABLE_SZ 11



/**
 *	\brief Performs a standardized parse of command responses. This function can be wrapped to match atcmd commandCompletedParse signature.
 *
 *  \param [in] response - The string returned from the getResult function.
 *  \param [in] landmark - The string to look for to signal response matches command (scans from end backwards).
 *  \param [in] landmarkReqd - The landmark string is required, set to false to allow for only gap and terminator
 *  \param [in] gap - The min. char count between landmark and terminator (0 is valid).
 *  \param [in] terminator - The string to signal the end of the command response.
 * 
 *  \return True if the response meets the conditions in the landmark, gap and terminator values.
 */
actionResult_t action_gapResultParser(const char *response, const char *landmark, bool landmarkReqd, uint8_t gap, const char *terminator)
{
    char *searchPtr = NULL;
    char *landmarkAt = NULL;
    char *terminatorAt = NULL;
    uint8_t landmarkSz = 0;

    if (landmark)
    {
        landmarkSz = strlen(landmark);
        searchPtr = strstr(response, landmark);
        
        if (landmarkReqd && searchPtr == NULL)
                return ACTION_RESULT_PENDING;

        while (searchPtr != NULL)                                   // find last occurrence of landmark
        {
            landmarkAt = searchPtr;
            searchPtr = strstr(searchPtr + landmarkSz, landmark);
        }
    }
    
    searchPtr = landmarkAt ? landmarkAt + landmarkSz : response;    // if landmark is NULL, start remaing search from response start

    if (terminator)                                                 // explicit terminator
        terminatorAt = strstr(searchPtr, terminator);

    if (!terminator)                                                // no explicit terminator, look for standard AT responses
    {
        terminatorAt = strstr(searchPtr, OK_COMPLETED_STRING);
        if (!terminatorAt)                                              
        {
            terminatorAt = strstr(searchPtr, CME_PREABLE);                  // no explicit terminator, look for extended CME errors
            if (terminatorAt)
            {
                // return error code, all CME >= 500
                return (strtol(terminatorAt + CME_PREABLE_SZ, NULL, 10));
            }

            terminatorAt = strstr(searchPtr, ERROR_COMPLETED_STRING);       // no explicit terminator, look for ERROR
            if (terminatorAt)
                return ACTION_RESULT_ERROR;

            terminatorAt = strstr(searchPtr, FAIL_COMPLETED_STRING);        // no explicit terminator, look for FAIL
            if (terminatorAt)
                return ACTION_RESULT_ERROR;
        }
    }

    if (terminatorAt && searchPtr + gap <= terminatorAt)            // term found with sufficient gap
        return ACTION_RESULT_SUCCESS;

    if (terminatorAt)                                               // else gap insufficient
        return ACTION_RESULT_ERROR;

    return ACTION_RESULT_PENDING;                                   // no term, keep looking
}



/**
 *	\brief Performs a standardized parse of command responses. This function can be wrapped to match atcmd commandCompletedParse signature.
 *
 *  \param [in] response - The string returned from the getResult function.
 *  \param [in] landmark - The string to look for to signal response matches command (scans from end backwards).
 *  \param [in] delim - The token delimeter char.
 *  \param [in] minTokens - The minimum number of tokens expected.
 * 
 *  \return True if the response meets the conditions in the landmark and token count required.
 */
actionResult_t action_tokenResultParser(const char *response, const char *landmark, char delim, uint8_t minTokens)
{
    char *landmarkAt;
    uint8_t delimitersFound = 0;

    char *next = strstr(response, landmark);
    if (next != NULL)
    {
        // find last occurrence of landmark
        uint8_t landmarkSz = strlen(landmark);
        while (next != NULL)
        {
            landmarkAt = next;
            next = strstr(next + landmarkSz, landmark);
        }
        next = landmarkAt + landmarkSz + 1;

        while (delimitersFound < (minTokens - 1) && *next != '\0')
        {
            next = strchr(next + 1, delim);
            delimitersFound++;
        }
        if (delimitersFound >= (minTokens -1))
            return ACTION_RESULT_SUCCESS;
    }

    char *cmeAt = strstr(response, CME_PREABLE); 
    if (cmeAt)
    {
        // return error code, all CME >= 500
        return (strtol(cmeAt + CME_PREABLE_SZ, NULL, 10));
    }
    return ACTION_RESULT_PENDING;
}




/**
 *	\brief Validate the response ends in a BGxx OK value.
 *
 *	\param [in] response - Pointer to the command response received from the BGxx.
 *
 *  \return bool If the response string ends in a valid OK sequence
 */
actionResult_t action_okResultParser(const char *response)
{
    return action_gapResultParser(response, NULL, false, 0, NULL);
}


#pragma endregion  // completionParsers

/* private (static) fuctions
 * --------------------------------------------------------------------------------------------- */
#pragma region private functions


/**
 *	\brief Safe string length, limits search for NULL char to maxSz.
 *
 *  \param [in] charStr - Pointer to character string to search for its length.
 *  \param [in] maxSz - The maximum number of characters to search.
 *
 *  \return AT command control structure.
 */
static const char *strnend(const char *charStr, uint16_t maxSz)
{
    return memchr(charStr, '\0', maxSz);
}



/**
 *	\brief Creates an AT command control stucture on the heap.
 *
 *  \param [in] resultSz - The size (length) of the command response buffer. Pass in 0 to use default size.
 *
 *  \return AT command control structure.
 */
// static action_t *createAction(uint8_t resultSz)
// {
//    	action_t *action = calloc(1, sizeof(action_t));
// 	if (action == NULL)
// 	{
//         ltem1_faultHandler(500, "actions-could not alloc at command object");
// 	}

//     if (resultSz == 0)
//         resultSz = ACTION_DEFAULT_RESPONSE_SZ;

//     action->resultHead = calloc(resultSz + 1, sizeof(char));
//     if (action->resultHead == NULL)
//     {
//         free(action);
//         ltem1_faultHandler(500, "actions-could not alloc response buffer");
//     }

//     action_reset(action);
//     action->cmdCompleteParser_func = action_okResultParser;

//     return action;
// }


/**
 *  \brief Attempts to get exclusive access to QBG module command interface.
 * 
 *  \param [in] actionCmd - Pointer to action structure.
 *  \param [in] retry - If exclusive access is not initially garnered, should retries be attempted.
*/
static bool tryActionLock(const char *cmdStr, bool retry)
{
    if (g_ltem1->action->cmdStr[0] != NULL)
    {
        if (retry)
        {
            uint8_t retryCnt = 0;
            while(g_ltem1->action->cmdStr[0] != NULL)
            {
                retryCnt++;

                if (retryCnt == ACTIONS_RETRY_MAX)
                    return false;

                timing_delay(ACTIONS_RETRY_INTERVAL);
                timing_yield();
                ip_receiverDoWork();
            }
        }
        else
            return false;
    }
    g_ltem1->action->cmdStr[0] = '*';
    return true;
}


#pragma endregion
