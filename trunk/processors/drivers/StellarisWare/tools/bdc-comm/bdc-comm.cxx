//*****************************************************************************
//
// bdc-comm.cxx - The main control loop for the bdc-comm application.
//
// Copyright (c) 2009-2010 Texas Instruments Incorporated.  All rights reserved.
// Software License Agreement
// 
// Texas Instruments (TI) is supplying this software for use solely and
// exclusively on TI's microcontroller products. The software is owned by
// TI and/or its suppliers, and is protected under applicable copyright
// laws. You may not combine this software with "viral" open-source
// software in order to form a larger program.
// 
// THIS SOFTWARE IS PROVIDED "AS IS" AND WITH ALL FAULTS.
// NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT
// NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE. TI SHALL NOT, UNDER ANY
// CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL, OR CONSEQUENTIAL
// DAMAGES, FOR ANY REASON WHATSOEVER.
// 
// This is part of revision 6075 of the Stellaris Firmware Development Package.
//
//*****************************************************************************

#include <libgen.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include "can_proto.h"
#include "cmdline.h"
#include "os.h"
#include "uart_handler.h"
#include "gui.h"
#include "gui_handlers.h"

#ifdef __WIN32
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#endif

#ifdef __WIN32
#define MUTEX HANDLE
#else
#define MUTEX pthread_mutex_t
#endif

//*****************************************************************************
//
// The global mutex used to protect the UART controller use by threads.
//
//*****************************************************************************
MUTEX mMutex;

#define MAX_CAN_ID              64


//*****************************************************************************
//
// This array holds the strings used for the currently defined Manufacturers.
//
//*****************************************************************************
static const char *g_ppcManufacturers[] =
{
    "none",
    "National Instruments",
    "Texas Instruments",
    "DEKA"
};

//*****************************************************************************
//
// This array holds the strings used for the currently defined device types.
//
//*****************************************************************************
static const char *g_ppcTypes[] =
{
    "none",
    "robot",
    "motor controller",
    "relay",
    "gyro",
    "accelerometer",
    "ultrasonic",
    "gear tooth"
};

//*****************************************************************************
//
// This variable is modified by a command line parameter to match the COM
// port that has been requested.
//
//*****************************************************************************
char g_szCOMName[32] =
{
#ifdef __WIN32
    "\\\\.\\COM1"
#else
    "/dev/ttyS0"
#endif
};

//*****************************************************************************
//
// The UART message buffer.
//
//*****************************************************************************
static unsigned char g_pucUARTTxMessage[516];
static unsigned long g_ulUARTMsgLen;

//*****************************************************************************
//
// These variables are used to hold the messages as they come in from the UART.
//
//*****************************************************************************
static unsigned char g_pucUARTMessage[12];
static unsigned long g_ulUARTSize;
static unsigned long g_ulUARTLength;

//*****************************************************************************
//
// The current UART state and its global variable.
//
//*****************************************************************************
#define UART_STATE_IDLE         0
#define UART_STATE_LENGTH       1
#define UART_STATE_DATA         2
#define UART_STATE_ESCAPE       3
static unsigned long g_ulUARTState = UART_STATE_IDLE;

//*****************************************************************************
//
// The current Device ID in use.
//
//*****************************************************************************
unsigned long g_ulID;

//*****************************************************************************
//
// Holds if the heart beat is enabled or not.
//
//*****************************************************************************
unsigned long g_ulHeartbeat = 1;

//*****************************************************************************
//
// Indicates if the device is currently active.
//
//*****************************************************************************
unsigned long g_ulBoardStatus = 0;
bool g_bBoardStatusActive = false;

//*****************************************************************************
//
// This value is true if GUI is in use and false if the command line interaface
// is being used.
//
//*****************************************************************************
bool g_bUseGUI = true;

//*****************************************************************************
//
// This value is true if the application is currently connected to the serial
// port.
//
//*****************************************************************************
bool g_bConnected = false;

//*****************************************************************************
//
// This value is true if there is currently a synchronous update pending.
//
//*****************************************************************************
bool g_bSynchronousUpdate = false;

//*****************************************************************************
//
// The current Maximum output voltage.
//
//*****************************************************************************
double g_dMaxVout;

//*****************************************************************************
//
// The current Vbus output voltage.
//
//*****************************************************************************
double g_dVbus;

//*****************************************************************************
//
// These values are used to hold the "faked" command line arguments that are
// passed into the command line hander functions.
//
//*****************************************************************************
static char g_pArg1[256];
static char g_pArg2[256];
static char g_pArg3[256];
static char g_pArg4[256];
char *g_argv[4] =
{
    g_pArg1, g_pArg2, g_pArg3, g_pArg4
};

//*****************************************************************************
//
// The current fault status string.
//
//*****************************************************************************
char g_pcFaultTxt[16];

//*****************************************************************************
//
// The usage function for the application.
//
//*****************************************************************************
void
Usage(char *pcFilename)
{
    fprintf(stdout, "Usage: %s [OPTION]\n", basename(pcFilename));
    fprintf(stdout, "A simple command-line interface to a Jaguar.\n");
    fprintf(stdout, "\n");
    fprintf(stdout, "Options are:\n");
#ifdef __WIN32
    fprintf(stderr, "  -c NUM   The number of the COM port to use"
                    " (default: COM1)\n");
#else
    fprintf(stderr, "  -c TTY   The name of the TTY device to use"
                    " (default: /dev/ttyS0)\n");
#endif
    fprintf(stderr, "\n");
    fprintf(stderr, "Report bugs to <support_lmi@ti.com>.\n");
}

//*****************************************************************************
//
// This function is used to create the MUTEX used by the application.
//
//*****************************************************************************
int
MutexInit(MUTEX *mutex)
{
#ifdef __WIN32
    //
    // Use the Windows call to create a mutex.
    //
    *mutex = CreateMutex(0, FALSE, 0);

    return(*mutex == 0);
#else
    return(pthread_mutex_init(mutex, NULL));
#endif
}

//*****************************************************************************
//
// Take the mutex and wait forever until it is released.
//
//*****************************************************************************
int
MutexLock(MUTEX *mutex)
{
    //
    // Wait for the mutex to be released.
    //
#ifdef __WIN32
    if(WaitForSingleObject(*mutex, INFINITE) == WAIT_FAILED)
    {
        return(1);
    }
    else
    {
        return(0);
    }
#else
    return(pthread_mutex_lock(mutex));
#endif
}

//*****************************************************************************
//
// Release the mutex.
//
//*****************************************************************************
int
MutexUnlock(MUTEX *mutex)
{
    //
    // Release the mutex.
    //
#ifdef __WIN32
    return(ReleaseMutex(*mutex) == 0);
#else
    return(pthread_mutex_unlock(mutex));
#endif
}

//*****************************************************************************
//
// Sends a character to the UART.
//
//*****************************************************************************
static void
UARTPutChar(unsigned long ulChar)
{
    //
    // See if the character being sent is 0xff.
    //
    if(ulChar == 0xff)
    {
        //
        // Send 0xfe 0xfe, the escaped version of 0xff.  A sign extended
        // version of 0xfe is used to avoid the check below for 0xfe, thereby
        // avoiding an infinite loop.  Only the lower 8 bits are actually sent,
        // so 0xfe is what is actually transmitted.
        //
        g_pucUARTTxMessage[g_ulUARTMsgLen++] = 0xfe;
        g_pucUARTTxMessage[g_ulUARTMsgLen++] = 0xfe;
    }

    //
    // Otherwise, see if the character being sent is 0xfe.
    //
    else if(ulChar == 0xfe)
    {
        //
        // Send 0xfe 0xfd, the escaped version of 0xfe.  A sign extended
        // version of 0xfe is used to avoid the check above for 0xfe, thereby
        // avoiding an infinite loop.  Only the lower 8 bits are actually sent,
        // so 0xfe is what is actually transmitted.
        //
        g_pucUARTTxMessage[g_ulUARTMsgLen++] = 0xfe;
        g_pucUARTTxMessage[g_ulUARTMsgLen++] = 0xfd;
    }

    //
    // Otherwise, simply send this character.
    //
    else
    {
        g_pucUARTTxMessage[g_ulUARTMsgLen++] = ulChar;
    }
}

//*****************************************************************************
//
// Sends a message to the UART.
//
//*****************************************************************************
static void
UARTSendMessage(unsigned long ulID, unsigned char *pucData,
                unsigned long ulDataLength)
{
    //
    // Lock the resource.
    //
    MutexLock(&mMutex);

    //
    // Send the start of packet indicator.  A sign extended version of 0xff is
    // used to avoid having it escaped.
    //
    g_ulUARTMsgLen = 0;
    UARTPutChar(0xffffffff);

    //
    // Send the length of the data packet.
    //
    UARTPutChar(ulDataLength + 4);

    //
    // Send the message ID.
    //
    UARTPutChar(ulID & 0xff);
    UARTPutChar((ulID >> 8) & 0xff);
    UARTPutChar((ulID >> 16) & 0xff);
    UARTPutChar((ulID >> 24) & 0xff);

    //
    // Send the associated data, if any.
    //
    while(ulDataLength--)
    {
        UARTPutChar(*pucData++);
    }

    //
    // Send the constructed message.
    //
    UARTSendData(g_pucUARTTxMessage, g_ulUARTMsgLen);

    //
    // Release the mutex so that other threads can access the UART TX path.
    //
    MutexUnlock(&mMutex);
}

//*****************************************************************************
//
// Parse the UART response message from the network.
//
//*****************************************************************************
void
ParseResponse(void)
{
    unsigned long ulID;
    long lValue, lValueFractional, lValueOriginal;
    char pcTempString[100];
    double dValue;
    int iDevice, iTemp;
    int iIdx;
    bool bFound = false;

    //
    // Get the device number out of the first byte of the message.
    //
    ulID = *(unsigned long *)g_pucUARTMessage;
    iDevice = ulID & CAN_MSGID_DEVNO_M;

    //
    // Read the actual command out of the message.
    //
    switch(ulID & ~(CAN_MSGID_DEVNO_M))
    {
        //
        // Handle the device enumeration command.
        //
        case CAN_MSGID_API_ENUMERATE:
        {
            if(g_bUseGUI)
            {
                sprintf(pcTempString, "%ld", ulID & CAN_MSGID_DEVNO_M);

                //
                // Add the board to the drop down list.
                //
                g_pSelectBoard->add(pcTempString);
            }
            else
            {
                printf("system enum = %ld\n", ulID & CAN_MSGID_DEVNO_M);
            }

            break;
        }

        //
        // Handle the firmware version request.
        //
        case CAN_MSGID_API_FIRMVER:
        {
            if(g_bUseGUI)
            {
                g_pSystemFirmwareVer->value(
                    *(unsigned long *)(g_pucUARTMessage + 4));
            }
            else
            {
                printf("system version (%ld) = %ld\n",
                       ulID & CAN_MSGID_DEVNO_M,
                       *(unsigned long *)(g_pucUARTMessage + 4));
            }

            break;
        }

        //
        // Handle the hardware version request.
        //
        case LM_API_HWVER:
        {
            if(g_bUseGUI)
            {
                g_pSystemHardwareVer->value(
                    *(unsigned char *)(g_pucUARTMessage + 5));
            }
            else
            {
                printf("hardware version (%ld) = %2d\n",
                       ulID & CAN_MSGID_DEVNO_M,
                       *(unsigned char *)(g_pucUARTMessage + 5));
            }

            break;
        }

        //
        // Handle the device query request.
        //
        case CAN_MSGID_API_DEVQUERY:
        {
            if(g_bUseGUI)
            {
                sprintf(pcTempString, "%s, %s",
                g_ppcManufacturers[g_pucUARTMessage[5]],
                g_ppcTypes[g_pucUARTMessage[4]]);

                g_pSystemBoardInformation->value(pcTempString);
            }
            else
            {
                printf("system query (%ld) = %s, %s\n",
                       ulID & CAN_MSGID_DEVNO_M,
                       g_ppcManufacturers[g_pucUARTMessage[5]],
                       g_ppcTypes[g_pucUARTMessage[4]]);
            }

            break;
        }

        //
        // Handle the Voltage mode set request.
        //
        case LM_API_VOLT_SET:
        {
            lValue = *(short *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                //
                // Apply the scaling factor so that the Vout is within range.
                //
                if(lValue < 0)
                {
                    lValue = ((lValue * 100) - 16384) / 32767;
                }
                else
                {
                    lValue = ((lValue * 100) + 16384) / 32767;
                }

                //
                // Update the GUI with the values.
                //
                g_pModeSetBox->value((double)lValue);
                g_pModeSetSlider->value(lValue);
            }
            else
            {
                printf("volt set (%ld) = %ld\n", ulID & CAN_MSGID_DEVNO_M,
                       lValue);
            }

            break;
        }

        //
        // Handle the Voltage mode ser ramp rate request.
        //
        case LM_API_VOLT_SET_RAMP:
        {
            lValue = *(short *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                g_pModeRamp->value(lValue);
            }
            else
            {
                printf("volt ramp (%ld) = %ld\n", ulID & CAN_MSGID_DEVNO_M,
                       lValue);
            }

            break;
        }

        //
        // Handle the Current control mode enable request.
        //
        case LM_API_ICTRL_SET:
        {
            lValue = *(short *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                if(lValue < 0)
                {
                    lValue -= 128 / 10;
                }
                else
                {
                    lValue += 128 / 10;
                }

                sprintf(pcTempString, "%s%ld.%01ld", (lValue < 0) ? "-" : "",
                        (lValue < 0) ? ((0 - lValue) / 256) : (lValue / 256),
                        (lValue < 0) ?
                        ((((0 - lValue) % 256) * 10) / 256) :
                        (((lValue % 256) * 10) / 256));

                //
                // Convert the string to a float.
                //
                dValue = strtod(pcTempString, NULL);

                //
                // Update the GUI with the values.
                //
                g_pModeSetBox->value(dValue);
                g_pModeSetSlider->value(dValue);
            }
            else
            {
                printf("cur set (%ld) = %ld (%s%ld.%02ld)\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue,
                       (lValue < 0) ? "-" : "",
                       (lValue < 0) ? ((0 - lValue) / 256) : (lValue / 256),
                       (lValue < 0) ?
                       ((((0 - lValue) % 256) * 100) / 256) :
                       (((lValue % 256) * 100) / 256));
            }

            break;
        }

        //
        // Handle the Current control mode P parameter set request.
        //
        case LM_API_ICTRL_PC:
        {
            lValue = *(long *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                if(lValue < 0)
                {
                    lValue -= 32768 / 1000;
                }
                else
                {
                    lValue += 32768 / 1000;
                }

                sprintf(pcTempString, "%s%ld.%03ld", (lValue < 0) ? "-" : "",
                        (lValue < 0) ? ((0 - lValue) / 65536) :
                                        (lValue / 65536),
                        (lValue < 0) ?
                        ((((0 - lValue) % 65536) * 1000) / 65536) :
                        (((lValue % 65536) * 1000) / 65536));

                dValue = strtod(pcTempString, NULL);
                g_pModeP->value(dValue);
            }
            else
            {
                printf("cur p (%ld) = %ld (%s%ld.%03ld)\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue,
                       (lValue < 0) ? "-" : "",
                       (lValue < 0) ? ((0 - lValue) / 65536) : (lValue / 65536),
                       (lValue < 0) ?
                       ((((0 - lValue) % 65536) * 1000) / 65536) :
                       (((lValue % 65536) * 1000) / 65536));
            }

            break;
        }

        //
        // Handle the Current control mode I parameter set request.
        //
        case LM_API_ICTRL_IC:
        {
            lValue = *(long *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                if(lValue < 0)
                {
                    lValue -= 32768 / 1000;
                }
                else
                {
                    lValue += 32768 / 1000;
                }

                sprintf(pcTempString, "%s%ld.%03ld", (lValue < 0) ? "-" : "",
                        (lValue < 0) ? ((0 - lValue) / 65536) :
                                        (lValue / 65536),
                        (lValue < 0) ?
                        ((((0 - lValue) % 65536) * 1000) / 65536) :
                        (((lValue % 65536) * 1000) / 65536));

                dValue = strtod(pcTempString, NULL);
                g_pModeI->value(dValue);
            }
            else
            {
                printf("cur i (%ld) = %ld (%s%ld.%03ld)\n",
                        ulID & CAN_MSGID_DEVNO_M, lValue,
                        (lValue < 0) ? "-" : "",
                        (lValue < 0) ? ((0 - lValue) / 65536) :
                                        (lValue / 65536),
                        (lValue < 0) ?
                        ((((0 - lValue) % 65536) * 1000) / 65536) :
                        (((lValue % 65536) * 1000) / 65536));
            }

            break;
        }

        //
        // Handle the Current control mode D parameter set request.
        //
        case LM_API_ICTRL_DC:
        {
            lValue = *(long *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                if(lValue < 0)
                {
                    lValue -= 32768 / 1000;
                }
                else
                {
                    lValue += 32768 / 1000;
                }

                sprintf(pcTempString, "%s%ld.%03ld", (lValue < 0) ? "-" : "",
                        (lValue < 0) ? ((0 - lValue) / 65536) :
                                        (lValue / 65536),
                        (lValue < 0) ?
                        ((((0 - lValue) % 65536) * 1000) / 65536) :
                        (((lValue % 65536) * 1000) / 65536));

                dValue = strtod(pcTempString, NULL);
                g_pModeD->value(dValue);
            }
            else
            {
                printf("cur d (%ld) = %ld (%s%ld.%03ld)\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue,
                       (lValue < 0) ? "-" : "",
                       (lValue < 0) ? ((0 - lValue) / 65536) : (lValue / 65536),
                       (lValue < 0) ?
                       ((((0 - lValue) % 65536) * 1000) / 65536) :
                       (((lValue % 65536) * 1000) / 65536));
            }

            break;
        }

        //
        // Handle the Speed mode enable request.
        //
        case LM_API_SPD_SET:
        {
            lValue = *(long *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                sprintf(pcTempString, "%ld", lValue / 65536);

                //
                // Convert the string to a float.
                //
                dValue = strtod(pcTempString, NULL);

                //
                // Update the GUI with the values.
                //
                g_pModeSetBox->value(dValue);
                g_pModeSetSlider->value(dValue);
            }
            else
            {
                printf("speed set (%ld) = %ld (%s%ld.%03ld)\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue,
                       (lValue < 0) ? "-" : "",
                       (lValue < 0) ? ((0 - lValue) / 65536) : (lValue / 65536),
                       (lValue < 0) ?
                       ((((0 - lValue) % 65536) * 1000) / 65536) :
                       (((lValue % 65536) * 1000) / 65536));
            }

            break;
        }

        //
        // Handle the Speed control mode P parameter set request.
        //
        case LM_API_SPD_PC:
        {
            lValue = *(long *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                if(lValue < 0)
                {
                    lValue -= 32768 / 1000;
                }
                else
                {
                    lValue += 32768 / 1000;
                }

                sprintf(pcTempString, "%s%ld.%03ld", (lValue < 0) ? "-" : "",
                        (lValue < 0) ? ((0 - lValue) / 65536) :
                                        (lValue / 65536),
                        (lValue < 0) ?
                        ((((0 - lValue) % 65536) * 1000) / 65536) :
                        (((lValue % 65536) * 1000) / 65536));

                dValue = strtod(pcTempString, NULL);
                g_pModeP->value(dValue);
            }
            else
            {
                printf("speed p (%ld) = %ld (%s%ld.%03ld)\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue,
                       (lValue < 0) ? "-" : "",
                       (lValue < 0) ? ((0 - lValue) / 65536) : (lValue / 65536),
                       (lValue < 0) ?
                       ((((0 - lValue) % 65536) * 1000) / 65536) :
                       (((lValue % 65536) * 1000) / 65536));
            }

            break;
        }

        //
        // Handle the Speed control mode I parameter set request.
        //
        case LM_API_SPD_IC:
        {
            lValue = *(long *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                if(lValue < 0)
                {
                    lValue -= 32768 / 1000;
                }
                else
                {
                    lValue += 32768 / 1000;
                }

                sprintf(pcTempString, "%s%ld.%03ld", (lValue < 0) ? "-" : "",
                        (lValue < 0) ? ((0 - lValue) / 65536) :
                                        (lValue / 65536),
                        (lValue < 0) ?
                        ((((0 - lValue) % 65536) * 1000) / 65536) :
                        (((lValue % 65536) * 1000) / 65536));

                dValue = strtod(pcTempString, NULL);

                g_pModeI->value(dValue);
            }
            else
            {
                printf("speed i (%ld) = %ld (%s%ld.%03ld)\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue,
                       (lValue < 0) ? "-" : "",
                       (lValue < 0) ? ((0 - lValue) / 65536) :
                                       (lValue / 65536),
                       (lValue < 0) ?
                       ((((0 - lValue) % 65536) * 1000) / 65536) :
                       (((lValue % 65536) * 1000) / 65536));
            }

            break;
        }

        //
        // Handle the Speed control mode D parameter set request.
        //
        case LM_API_SPD_DC:
        {
            lValue = *(long *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                if(lValue < 0)
                {
                    lValue -= 32768 / 1000;
                }
                else
                {
                    lValue += 32768 / 1000;
                }

                sprintf(pcTempString, "%s%ld.%03ld", (lValue < 0) ? "-" : "",
                        (lValue < 0) ? ((0 - lValue) / 65536) :
                                        (lValue / 65536),
                        (lValue < 0) ?
                        ((((0 - lValue) % 65536) * 1000) / 65536) :
                        (((lValue % 65536) * 1000) / 65536));

                dValue = strtod(pcTempString, NULL);
                g_pModeD->value(dValue);
            }
            else
            {
                printf("speed d (%ld) = %ld (%s%ld.%03ld)\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue,
                       (lValue < 0) ? "-" : "",
                       (lValue < 0) ? ((0 - lValue) / 65536) : (lValue / 65536),
                       (lValue < 0) ?
                       ((((0 - lValue) % 65536) * 1000) / 65536) :
                       (((lValue % 65536) * 1000) / 65536));
            }

            break;
        }

        //
        // Handle the Speed control mode speed reference set request.
        //
        case LM_API_SPD_REF:
        {
            if(g_bUseGUI)
            {
                if(g_pucUARTMessage[4] == 0)
                {
                    g_pModeEncoder->value(1);
                    g_pModePot->value(0);
                }
                else if(g_pucUARTMessage[4] == 1)
                {
                    g_pModeEncoder->value(0);
                    g_pModePot->value(1);
                }
                else
                {
                    g_pModeEncoder->value(0);
                    g_pModePot->value(0);
                }
            }
            else
            {
                printf("speed ref = %d\n", g_pucUARTMessage[4]);
            }

            break;
        }

        //
        // Handle the Position control mode position set request.
        //
        case LM_API_POS_SET:
        {
            lValue = *(long *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                if(lValue < 0)
                {
                    lValue -= 32768 / 1000;
                }
                else
                {
                    lValue += 32768 / 1000;
                }

                sprintf(pcTempString, "%s%ld.%03ld", (lValue < 0) ? "-" : "",
                        (lValue < 0) ? ((0 - lValue) / 65536) :
                                        (lValue / 65536),
                        (lValue < 0) ?
                        ((((0 - lValue) % 65536) * 1000) / 65536) :
                        (((lValue % 65536) * 1000) / 65536));

                //
                // Convert the string to a float.
                //
                dValue = strtod(pcTempString, NULL);

                //
                // Update the GUI with the values.
                //
                g_pModeSetBox->value(dValue);
                g_pModeSetSlider->value(dValue);
            }
            else
            {
                printf("pos set (%ld) = %ld (%s%ld.%03ld)\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue,
                       (lValue < 0) ? "-" : "",
                       (lValue < 0) ? ((0 - lValue) / 65536) : (lValue / 65536),
                       (lValue < 0) ?
                       ((((0 - lValue) % 65536) * 1000) / 65536) :
                       (((lValue % 65536) * 1000) / 65536));
            }

            break;
        }

        //
        // Handle the Position control mode P parameter set request.
        //
        case LM_API_POS_PC:
        {
            lValue = *(long *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                if(lValue < 0)
                {
                    lValue -= 32768 / 1000;
                }
                else
                {
                    lValue += 32768 / 1000;
                }

                sprintf(pcTempString, "%s%ld.%03ld", (lValue < 0) ? "-" : "",
                        (lValue < 0) ? ((0 - lValue) / 65536) :
                                        (lValue / 65536),
                        (lValue < 0) ?
                        ((((0 - lValue) % 65536) * 1000) / 65536) :
                        (((lValue % 65536) * 1000) / 65536));

                dValue = strtod(pcTempString, NULL);

                g_pModeP->value(dValue);
            }
            else
            {
                printf("pos p (%ld) = %ld (%s%ld.%03ld)\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue,
                       (lValue < 0) ? "-" : "",
                       (lValue < 0) ? ((0 - lValue) / 65536) : (lValue / 65536),
                       (lValue < 0) ?
                       ((((0 - lValue) % 65536) * 1000) / 65536) :
                       (((lValue % 65536) * 1000) / 65536));
            }

            break;
        }

        //
        // Handle the Position control mode I parameter set request.
        //
        case LM_API_POS_IC:
        {
            lValue = *(long *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                if(lValue < 0)
                {
                    lValue -= 32768 / 1000;
                }
                else
                {
                    lValue += 32768 / 1000;
                }

                sprintf(pcTempString, "%s%ld.%03ld", (lValue < 0) ? "-" : "",
                        (lValue < 0) ? ((0 - lValue) / 65536) :
                                        (lValue / 65536),
                        (lValue < 0) ?
                        ((((0 - lValue) % 65536) * 1000) / 65536) :
                        (((lValue % 65536) * 1000) / 65536));

                dValue = strtod(pcTempString, NULL);

                g_pModeI->value(dValue);
            }
            else
            {
                printf("pos i (%ld) = %ld (%s%ld.%03ld)\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue,
                       (lValue < 0) ? "-" : "",
                       (lValue < 0) ? ((0 - lValue) / 65536) : (lValue / 65536),
                       (lValue < 0) ?
                       ((((0 - lValue) % 65536) * 1000) / 65536) :
                       (((lValue % 65536) * 1000) / 65536));
            }

            break;
        }

        //
        // Handle the Position control mode D parameter set request.
        //
        case LM_API_POS_DC:
        {
            lValue = *(long *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                if(lValue < 0)
                {
                    lValue -= 32768 / 1000;
                }
                else
                {
                    lValue += 32768 / 1000;
                }

                sprintf(pcTempString, "%s%ld.%03ld", (lValue < 0) ? "-" : "",
                        (lValue < 0) ? ((0 - lValue) / 65536) :
                                        (lValue / 65536),
                        (lValue < 0) ?
                        ((((0 - lValue) % 65536) * 1000) / 65536) :
                        (((lValue % 65536) * 1000) / 65536));

                dValue = strtod(pcTempString, NULL);

                g_pModeD->value(dValue);
            }
            else
            {
                printf("pos d (%ld) = %ld (%s%ld.%03ld)\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue,
                       (lValue < 0) ? "-" : "",
                       (lValue < 0) ? ((0 - lValue) / 65536) : (lValue / 65536),
                       (lValue < 0) ?
                       ((((0 - lValue) % 65536) * 1000) / 65536) :
                       (((lValue % 65536) * 1000) / 65536));
            }

            break;
        }

        //
        // Handle the Position control mode position reference set request.
        //
        case LM_API_POS_REF:
        {
            if(g_bUseGUI)
            {
                if(g_pucUARTMessage[4] == 0)
                {
                    g_pModeEncoder->value(1);
                    g_pModePot->value(0);
                }
                else if(g_pucUARTMessage[4] == 1)
                {
                    g_pModeEncoder->value(0);
                    g_pModePot->value(1);
                }
                else
                {
                    g_pModeEncoder->value(0);
                    g_pModePot->value(0);
                }
            }
            else
            {
                printf("pos ref (%ld) = %d\n", ulID & CAN_MSGID_DEVNO_M,
                g_pucUARTMessage[4]);
            }

            break;
        }

        //
        // Handle the get Output Voltage request.
        //
        case LM_API_STATUS_VOLTOUT:
        {
            lValue = *(short *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                //
                // Convert the output voltage percentage into the "actual"
                // output voltage.
                //
                dValue = (((float)lValue * g_dVbus * g_dMaxVout) /
                          (32767 * 100));

                //
                // Update the status structure with the new value.
                //
                g_sBoardStatus.fVout = dValue;
            }
            else
            {
                printf("stat vout (%ld) = %ld\n", ulID & CAN_MSGID_DEVNO_M,
                       lValue);
            }

            break;
        }

        //
        // Handle the get Bus Voltage request.
        //
        case LM_API_STATUS_VOLTBUS:
        {
            //
            // Grab the response data and store it into a single variable.
            //
            lValue = *(unsigned short *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                //
                // Get the number into the xx.xx format for display.
                //
                sprintf(pcTempString, "%ld.%02ld", lValue / 256,
                        ((lValue % 256) * 100) / 256);

                //
                // Convert the string to a float.
                //
                dValue = strtod(pcTempString, NULL);

                //
                // Update the global value.
                //
                g_dVbus = dValue;

                //
                // Update the status structure with the new value.
                //
                g_sBoardStatus.fVbus = dValue;
            }
            else
            {
                //
                // Update the window with the new value.
                //
                printf("stat vbus (%ld) = %ld (%ld.%02ld)\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue, lValue / 256,
                       ((lValue % 256) * 100) / 256);
            }

            break;
        }

        //
        // Handle the get Fault status request.
        //
        case LM_API_STATUS_FAULT:
        {
            //
            // Grab the response data and store it into a single variable.
            //
            lValue = *(unsigned short *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                g_sBoardStatus.lFault = lValue;
            }
            else
            {
                printf("stat fault (%ld) = %ld\n", ulID & CAN_MSGID_DEVNO_M,
                       lValue);
            }

            break;
        }

        //
        // Handle the get Current request.
        //
        case LM_API_STATUS_CURRENT:
        {
            //
            // Grab the response data and store it into a single variable.
            //
            lValue = *(short *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                //
                // Get the number into the xx.xx format for display.
                //
                sprintf(pcTempString, "%ld.%01ld", lValue / 256,
                        ((lValue % 256) * 100) / 256);

                //
                // Convert the string to a float.
                //
                dValue = strtod(pcTempString, NULL);

                //
                // Update the status structure with the new value.
                //
                g_sBoardStatus.fCurrent = dValue;
            }
            else
            {
                //
                // Update the window with the new value.
                //
                printf("stat cur (%ld) = %ld (%ld.%01ld)\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue, lValue / 256,
                       ((lValue % 256) * 100) / 256);
            }

            break;
        }

        //
        // Handle the get Temperature request.
        //
        case LM_API_STATUS_TEMP:
        {
            //
            // Grab the response data and store it into a single variable.
            //
            lValue = *(short *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                //
                // Update the status structure with the new value.
                //
                g_sBoardStatus.fTemperature = (float)lValue / 256.0;
            }
            else
            {
                //
                // Update the window with the new value.
                //
                printf("stat temp (%ld) = %ld (%ld.%02ld)\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue, lValue / 256,
                       ((lValue % 256) * 100) / 256);
            }

            break;
        }

        //
        // Handle the get Position request.
        //
        case LM_API_STATUS_POS:
        {
            //
            // Grab the response data and store it into a single variable.
            //
            lValue = *(long *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                //
                // Get the number into the xx.xx format for display.
                //
                sprintf(pcTempString, "%s%ld.%03ld", (lValue < 0) ? "-" : "",
                        (lValue < 0) ? ((0 - lValue) / 65536) :
                        (lValue / 65536), (lValue < 0) ?
                        ((((0 - lValue) % 65536) * 1000) / 65536) :
                        (((lValue % 65536) * 1000) / 65536));

                //
                // Convert the string to a float.
                //
                dValue = strtod(pcTempString, NULL);

                //
                // Update the status structure with the new value.
                //
                g_sBoardStatus.fPosition = dValue;
            }
            else
            {
                //
                // Update the window with the new value.
                //
                printf("stat pos (%ld) = %ld (%s%ld.%03ld)\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue,
                       (lValue < 0) ? "-" : "",
                       (lValue < 0) ? ((0 - lValue) / 65536) :
                                       (lValue / 65536),
                       (lValue < 0) ?
                       ((((0 - lValue) % 65536) * 1000) / 65536) :
                       (((lValue % 65536) * 1000) / 65536));
            }

            break;
        }

        //
        // Handle the get Speed request.
        //
        case LM_API_STATUS_SPD:
        {
            //
            // Grab the response data and store it into a single variable.
            //
            lValue = *(long *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                //
                // Get the number into the xx.xx format for display.
                //
                sprintf(pcTempString, "%s%ld.%03ld", (lValue < 0) ? "-" : "",
                        (lValue < 0) ? ((0 - lValue) / 65536) :
                        (lValue / 65536), (lValue < 0) ?
                        ((((0 - lValue) % 65536) * 1000) / 65536) :
                        (((lValue % 65536) * 1000) / 65536));

                //
                // Convert the string to a float.
                //
                dValue = strtod(pcTempString, NULL);

                //
                // Update the status structure with the new value.
                //
                g_sBoardStatus.fSpeed = dValue;
            }
            else
            {
                //
                // Update the window with the new value.
                //
                printf("stat speed (%ld) = %ld (%s%ld.%03ld)\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue,
                       (lValue < 0) ? "-" : "",
                       (lValue < 0) ? ((0 - lValue) / 65536) : (lValue / 65536),
                       (lValue < 0) ?
                       ((((0 - lValue) % 65536) * 1000) / 65536) :
                       (((lValue % 65536) * 1000) / 65536));
            }

            break;
        }

        //
        // Handle the get limit values request.
        //
        case LM_API_STATUS_LIMIT:
        {
            if(g_bUseGUI)
            {
                //
                // Create a string with the status of the limit switches.
                //
                sprintf(pcTempString, "%c%c",
                        (g_pucUARTMessage[4] & LM_STATUS_LIMIT_FWD) ? '.' :
                                                                      'F',
                        (g_pucUARTMessage[4] & LM_STATUS_LIMIT_REV) ? '.' :
                                                                      'R');

                //
                // Update the GUI with the new value.
                //
                strcpy(g_sBoardStatus.pcLimit, pcTempString);
            }
            else
            {
                //
                // Update the window with the new value.
                //
                printf("stat limit (%ld) = %c%c\n",
                       ulID & CAN_MSGID_DEVNO_M,
                       (g_pucUARTMessage[4] & LM_STATUS_LIMIT_FWD) ? '.' : 'F',
                       (g_pucUARTMessage[4] & LM_STATUS_LIMIT_REV) ? '.' : 'R');
            }

            break;
        }

        //
        // Handle the get Power status request.
        //
        case LM_API_STATUS_POWER:
        {
            if(g_bUseGUI)
            {
                //
                // Update the status structure with the new value.
                //
                g_sBoardStatus.lPower = (long)g_pucUARTMessage[4];
            }
            else
            {
                //
                // Update the window with the new value.
                //
                printf("stat power (%ld) = %d\n", ulID & CAN_MSGID_DEVNO_M,
                       g_pucUARTMessage[4]);
            }

            break;
        }

        //
        // Handle the get status for various the various control modes.
        //
        case LM_API_STATUS_CMODE:
        {
            if(g_bUseGUI)
            {
                //
                // Update the status structure with the new value.
                //
                g_sBoardState[g_ulID].ulControlMode = g_pucUARTMessage[4];

                //
                // Set the state of the mode select drop-down.
                //
                g_pSelectMode->value(g_pucUARTMessage[4]);
            }
            else
            {
                switch(g_pucUARTMessage[4])
                {
                    //
                    // Indicate Voltage control mode.
                    //
                    case LM_STATUS_CMODE_VOLT:
                    {
                        printf("Control Mode (%ld) = Voltage\n",
                               ulID & CAN_MSGID_DEVNO_M);

                        break;
                    }

                    //
                    // Indicate Current control mode.
                    //
                    case LM_STATUS_CMODE_CURRENT:
                    {
                        printf("Control Mode (%ld) = Current\n",
                               ulID & CAN_MSGID_DEVNO_M);

                        break;
                    }

                    //
                    // Indicate Speed control mode.
                    //
                    case LM_STATUS_CMODE_SPEED:
                    {
                        printf("Control Mode (%ld) = Speed\n",
                               ulID & CAN_MSGID_DEVNO_M);

                        break;
                    }

                    //
                    // Indicate Position control mode.
                    //
                    case LM_STATUS_CMODE_POS:
                    {
                        printf("Control Mode (%ld) = Position\n",
                               ulID & CAN_MSGID_DEVNO_M);

                        break;
                    }

                    //
                    // Indicate unknown control mode.
                    //
                    default:
                    {
                        printf("Control Mode (%ld) = Unknown\n",
                               ulID & CAN_MSGID_DEVNO_M);

                        break;
                    }
                }
            }

            break;
        }

        //
        // Handle the get Encoder Number of Lines request.
        //
        case LM_API_CFG_ENC_LINES:
        {
            if(g_bUseGUI)
            {
                g_pConfigEncoderLines->value(
                    (double)(*(unsigned short *)(g_pucUARTMessage + 4)));
            }
            else
            {
                printf("config lines (%ld) = %d\n", ulID & CAN_MSGID_DEVNO_M,
                       *(unsigned short *)(g_pucUARTMessage + 4));
            }

            break;
        }

        //
        // Handle the get Number of Pot Turns request.
        //
        case LM_API_CFG_POT_TURNS:
        {
            if(g_bUseGUI)
            {
                g_pConfigPOTTurns->value(
                    (double)(*(unsigned short *)(g_pucUARTMessage + 4)));
            }
            else
            {
                printf("config turns (%ld) = %d\n", ulID & CAN_MSGID_DEVNO_M,
                       *(unsigned short *)(g_pucUARTMessage + 4));
            }

            break;
        }

        //
        // Handle the Coast Break response.
        //
        case LM_API_CFG_BRAKE_COAST:
        {
            if(!g_bUseGUI)
            {
                printf("config brake (%ld) = ", ulID & CAN_MSGID_DEVNO_M);
            }

            if(g_pucUARTMessage[4] == 0)
            {
                if(g_bUseGUI)
                {
                    g_pConfigStopJumper->value(1);
                    g_pConfigStopBrake->value(0);
                    g_pConfigStopCoast->value(0);
                }
                else
                {
                    printf("jumper\n");
                }
            }
            else if(g_pucUARTMessage[4] == 1)
            {
                if(g_bUseGUI)
                {
                    g_pConfigStopJumper->value(0);
                    g_pConfigStopBrake->value(1);
                    g_pConfigStopCoast->value(0);
                }
                else
                {
                    printf("brake\n");
                }
            }
            else if(g_pucUARTMessage[4] == 2)
            {
                if(g_bUseGUI)
                {
                    g_pConfigStopJumper->value(0);
                    g_pConfigStopBrake->value(0);
                    g_pConfigStopCoast->value(1);
                }
                else
                {
                    printf("coast\n");
                }
            }
            else if(!g_bUseGUI)
            {
                printf("???\n");
            }

            break;
        }

        //
        // Handle the Limit switch mode response.
        //
        case LM_API_CFG_LIMIT_MODE:
        {
            if(!g_bUseGUI)
            {
                printf("config limit (%ld) = ", ulID & CAN_MSGID_DEVNO_M);
            }

            if(g_pucUARTMessage[4] == 0)
            {
                if(g_bUseGUI)
                {
                    g_pConfigLimitSwitches->value(0);
                    g_pConfigFwdLimitLt->deactivate();
                    g_pConfigFwdLimitGt->deactivate();
                    g_pConfigFwdLimitValue->deactivate();
                    g_pConfigRevLimitLt->deactivate();
                    g_pConfigRevLimitGt->deactivate();
                    g_pConfigRevLimitValue->deactivate();
                }
                else
                {
                    printf("off\n");
                }
            }
            else if(g_pucUARTMessage[4] == 1)
            {
                if(g_bUseGUI)
                {
                    g_pConfigLimitSwitches->value(1);
                    g_pConfigFwdLimitLt->activate();
                    g_pConfigFwdLimitGt->activate();
                    g_pConfigFwdLimitValue->activate();
                    g_pConfigRevLimitLt->activate();
                    g_pConfigRevLimitGt->activate();
                    g_pConfigRevLimitValue->activate();
                }
                else
                {
                    printf("on\n");
                }
            }
            else if(!g_bUseGUI)
            {
                printf("???\n");
            }

            break;
        }

        //
        // Handle the get Forward Limit response.
        //
        case LM_API_CFG_LIMIT_FWD:
        {
            //
            // Grab the response data and store it into a single variable.
            //
            lValue = *(long *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                if(lValue < 0)
                {
                    lValue -= 32768 / 1000;
                }
                else
                {
                    lValue += 32768 / 1000;
                }

                sprintf(pcTempString, "%s%ld.%03ld", (lValue < 0) ? "-" : "",
                        (lValue < 0) ? ((0 - lValue) / 65536) :
                                        (lValue / 65536),
                        (lValue < 0) ?
                        ((((0 - lValue) % 65536) * 1000) / 65536) :
                        (((lValue % 65536) * 1000) / 65536));

                //
                // Convert the string to a float.
                //
                dValue = strtod(pcTempString, NULL);

                g_pConfigFwdLimitValue->value(dValue);

                if(g_pucUARTMessage[8] == 0)
                {
                    g_pConfigFwdLimitLt->value(1);
                    g_pConfigFwdLimitGt->value(0);
                }
                else
                {
                    g_pConfigFwdLimitLt->value(0);
                    g_pConfigFwdLimitGt->value(1);
                }
            }
            else
            {
                printf("config limit fwd (%ld) = %ld %s\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue,
                       (g_pucUARTMessage[8] == 0) ? "lt" : "gt");
            }

            break;
        }

        //
        // Handle the get Reverse Limit response.
        //
        case LM_API_CFG_LIMIT_REV:
        {
            //
            // Grab the response data and store it into a single variable.
            //
            lValue = *(long *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                if(lValue < 0)
                {
                    lValue -= 32768 / 1000;
                }
                else
                {
                    lValue += 32768 / 1000;
                }

                sprintf(pcTempString, "%s%ld.%03ld", (lValue < 0) ? "-" : "",
                        (lValue < 0) ? ((0 - lValue) / 65536) :
                                        (lValue / 65536),
                        (lValue < 0) ?
                        ((((0 - lValue) % 65536) * 1000) / 65536) :
                        (((lValue % 65536) * 1000) / 65536));

                //
                // Convert the string to a float.
                //
                dValue = strtod(pcTempString, NULL);

                g_pConfigRevLimitValue->value(dValue);

                if(g_pucUARTMessage[8] == 0)
                {
                    g_pConfigRevLimitLt->value(1);
                    g_pConfigRevLimitGt->value(0);
                }
                else
                {
                    g_pConfigRevLimitLt->value(0);
                    g_pConfigRevLimitGt->value(1);
                }
            }
            else
            {
                printf("config limit rev (%ld) = %ld %s\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue,
                       (g_pucUARTMessage[8] == 0) ? "lt" : "gt");
            }

            break;
        }

        //
        // Handle the get Maximum Voltage out response.
        //
        case LM_API_CFG_MAX_VOUT:
        {
            lValue = *(unsigned short *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                if(lValue < 0)
                {
                    lValue = ((lValue - 1) * 1000) - (0xc00 / 2);
                }
                else
                {
                    lValue = ((lValue + 1) * 1000) + (0xc00 / 2);
                }

                sprintf(pcTempString, "%s%ld.%01ld", (lValue < 0) ? "-" : "",
                        (lValue < 0) ? ((0 - lValue) / 30720) :
                        (lValue / 30720),
                        (lValue < 0) ? (((0 - lValue) % 30720) / 3072) :
                        ((lValue % 30720) / 3072));

                g_dMaxVout = strtod(pcTempString, NULL);

                g_pConfigMaxVout->value(g_dMaxVout);
            }
            else
            {
                printf("config maxvout (%ld) = %ld\n", ulID & CAN_MSGID_DEVNO_M,
                       lValue);
            }

            break;
        }

        //
        // Handle the get Fault Time Configuration response.
        //
        case LM_API_CFG_FAULT_TIME:
        {
            lValue = *(unsigned short *)(g_pucUARTMessage + 4);

            if(g_bUseGUI)
            {
                g_pConfigFaultTime->value(lValue);
            }
            else
            {
                printf("config faulttime (%ld) = %ld (%ld.%03ld)\n",
                       ulID & CAN_MSGID_DEVNO_M, lValue, lValue / 1000,
                       lValue % 1000);
            }

            break;
        }
    }
}

//*****************************************************************************
//
// This function handles waiting for an ACK from the device, and includes a
// timeout.
//
//*****************************************************************************
int
WaitForAck(unsigned long ulID, unsigned long ulTimeout)
{
    unsigned char ucChar;

    while(1)
    {
        //
        // If the UART timed out or failed to read for some reason then just
        // return.
        //
        if(UARTReceiveData(&ucChar, 1) == -1)
        {
            if(--ulTimeout == 0)
            {
                return(-1);
            }
            continue;
        }

        //
        // See if this is a start of packet byte.
        //
        if(ucChar == 0xff)
        {
            //
            // Reset the length of the UART message.
            //
            g_ulUARTLength = 0;

            //
            // Set the state such that the next byte received is the size
            // of the message.
            //
            g_ulUARTState = UART_STATE_LENGTH;
        }

        //
        // See if this byte is the size of the message.
        //
        else if(g_ulUARTState == UART_STATE_LENGTH)
        {
            //
            // Save the size of the message.
            //
            g_ulUARTSize = ucChar;

            //
            // Subsequent bytes received are the message data.
            //
            g_ulUARTState = UART_STATE_DATA;
        }

        //
        // See if the previous character was an escape character.
        //
        else if(g_ulUARTState == UART_STATE_ESCAPE)
        {
            //
            // See if this 0xfe, the escaped version of 0xff.
            //
            if(ucChar == 0xfe)
            {
                //
                // Store a 0xff in the message buffer.
                //
                g_pucUARTMessage[g_ulUARTLength++] = 0xff;

                //
                // Subsequent bytes received are the message data.
                //
                g_ulUARTState = UART_STATE_DATA;
            }

            //
            // Otherwise, see if this is 0xfd, the escaped version of 0xfe.
            //
            else if(ucChar == 0xfd)
            {
                //
                // Store a 0xfe in the message buffer.
                //
                g_pucUARTMessage[g_ulUARTLength++] = 0xfe;

                //
                // Subsequent bytes received are the message data.
                //
                g_ulUARTState = UART_STATE_DATA;
            }

            //
            // Otherwise, this is a corrupted sequence.  Set the receiver
            // to idle so this message is dropped, and subsequent data is
            // ignored until another start of packet is received.
            //
            else
            {
                g_ulUARTState = UART_STATE_IDLE;
            }
        }

        //
        // See if this is a part of the message data.
        //
        else if(g_ulUARTState == UART_STATE_DATA)
        {
            //
            // See if this character is an escape character.
            //
            if(ucChar == 0xfe)
            {
                //
                // The next byte is an escaped byte.
                //
                g_ulUARTState = UART_STATE_ESCAPE;
            }
            else
            {
                //
                // Store this byte in the message buffer.
                //
                g_pucUARTMessage[g_ulUARTLength++] = ucChar;
            }
        }

        //
        // See if the entire message has been received but has not been
        // processed (i.e. the most recent byte received was the end of the
        // message).
        //
        if((g_ulUARTLength == g_ulUARTSize) &&
           (g_ulUARTState == UART_STATE_DATA))
        {
            //
            // The UART interface is idle, meaning all bytes will be
            // dropped until the next start of packet byte.
            //
            g_ulUARTState = UART_STATE_IDLE;

            //
            // Parse out the data that was received.
            //
            ParseResponse();

            if(*(unsigned long *)g_pucUARTMessage == ulID)
            {
                return(0);
            }
        }
    }
    return(0);
}

//*****************************************************************************
//
// This function is used to set the currently active device ID that is being
// communicated with.
//
//*****************************************************************************
int
CmdID(int argc, char *argv[])
{
    unsigned long ulValue;

    //
    // If there is a value then this is a set request otherwise it is a get
    // request.
    //
    if(argc > 1)
    {
        //
        // Convert the ID text value to a  number.
        //
        ulValue = strtoul(argv[1], 0, 0);

        //
        // Check that the value is valid.
        //
        if((ulValue == 0) || (ulValue > 63))
        {
            printf("%s: the ID must be between 1 and 63.\n", argv[0]);
        }
        else
        {
            g_ulID = ulValue;
        }
    }
    else
    {
        printf("id = %ld\n", g_ulID);
    }

    return(0);
}

//*****************************************************************************
//
// This command is used to toggle if the heart beat messages are being send
// out.
//
//*****************************************************************************
int
CmdHeartbeat(int argc, char *argv[])
{
    //
    // Just toggle the heart beat mode.
    //
    g_ulHeartbeat ^= 1;

    printf("heart beat is %s\n", g_ulHeartbeat ? "on" : "off");

    return(0);
}

//*****************************************************************************
//
// This command controls the setting when running in voltage control mode.
//
//*****************************************************************************
int
CmdVoltage(int argc, char *argv[])
{
    long plValue[2], lTemp;
    unsigned short *pusData;
    unsigned char *pucData;
    int iIdx;

    pusData = (unsigned short *)plValue;
    pucData = (unsigned char *)plValue;

    //
    // Check if this was a request to enable Voltage control mode.
    //
    if((argc > 1) && (strcmp(argv[1], "en") == 0))
    {
        UARTSendMessage(LM_API_VOLT_EN | g_ulID, 0, 0);
        WaitForAck(LM_API_ACK | g_ulID, 10);
    }

    //
    // Check if this was a request to disable Voltage control mode.
    //
    else if((argc > 1) && (strcmp(argv[1], "dis") == 0))
    {
        UARTSendMessage(LM_API_VOLT_DIS | g_ulID, 0, 0);

        WaitForAck(LM_API_ACK | g_ulID, 10);
    }

    //
    // Check if this was a request to set the current output Voltage.
    //
    else if((argc > 1) && (strcmp(argv[1], "set") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            //
            // Get the setting from the argument.
            //
            plValue[0] = strtol(argv[2], 0, 0);

            //
            // Limit the value to valid values.
            //
            if(plValue[0] < -32768)
            {
                plValue[0] = -32768;
            }
            if(plValue[0] > 32767)
            {
                plValue[0] = 32767;
            }

            //
            // If there is a third argument then this is a synchronous set
            // command.
            //
            if(argc > 3)
            {
                //
                // Get the synchronous group number.
                //
                plValue[1] = strtol(argv[3], 0, 0);

                //
                // Limit the value to valid values.
                //
                if(plValue[1] < 0)
                {
                    plValue[1] = 0;
                }
                if(plValue[1] > 255)
                {
                    plValue[1] = 0;
                }

                plValue[0] = (plValue[0] & 0x0000ffff) | (plValue[1] << 16);

                UARTSendMessage(LM_API_VOLT_SET | g_ulID,
                                (unsigned char *)plValue, 3);
            }
            else
            {
                UARTSendMessage(LM_API_VOLT_SET | g_ulID,
                                (unsigned char *)plValue, 2);
            }

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // A Set with no data is a get request.
            //
            UARTSendMessage(LM_API_VOLT_SET | g_ulID, 0, 0);

            WaitForAck(LM_API_VOLT_SET | g_ulID, 10);
        }
    }

    //
    // Check if this was a request to set the ramp rate in Voltage control mode.
    //
    else if((argc > 1) && (strcmp(argv[1], "ramp") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            //
            // Get the ramp rate value.
            //
            plValue[0] = strtol(argv[2], 0, 0);

            //
            // Limit the value to valid values.
            //
            if(plValue[0] < 0)
            {
                plValue[0] = 0;
            }
            if(plValue[0] > 65535)
            {
                plValue[0] = 65535;
            }

            //
            // Send the ramp rate to the device.
            //
            UARTSendMessage(LM_API_VOLT_SET_RAMP | g_ulID,
                            (unsigned char *)plValue, 2);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // A set command without data is a request for data.
            //
            UARTSendMessage(LM_API_VOLT_SET_RAMP | g_ulID, 0, 0);

            WaitForAck(LM_API_VOLT_SET_RAMP | g_ulID, 10);
        }
    }

    else
    {
        //
        // If this was an unknown request then print out the valid options.
        //
        printf("%s [en|dis|set|ramp]\n", argv[0]);
    }

    return(0);
}

//*****************************************************************************
//
// This command controls the setting when running in current control mode.
//
//*****************************************************************************
int
CmdCurrent(int argc, char *argv[])
{
    long plValue[2], lTemp;
    unsigned short *pusData;
    unsigned char *pucData;
    int iIdx;

    pusData = (unsigned short *)plValue;
    pucData = (unsigned char *)plValue;

    //
    // Check if this was a request to enable Current control mode.
    //
    if((argc > 1) && (strcmp(argv[1], "en") == 0))
    {
        UARTSendMessage(LM_API_ICTRL_EN | g_ulID, 0, 0);
        WaitForAck(LM_API_ACK | g_ulID, 10);
    }

    //
    // Check if this was a request to enable Current control mode.
    //
    else if((argc > 1) && (strcmp(argv[1], "dis") == 0))
    {
        UARTSendMessage(LM_API_ICTRL_DIS | g_ulID, 0, 0);

        WaitForAck(LM_API_ACK | g_ulID, 10);
    }

    //
    // Check if this was a request to set the Current.
    //
    else if((argc > 1) && (strcmp(argv[1], "set") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            //
            // Get the Current value.
            //
            plValue[0] = strtol(argv[2], 0, 0);

            //
            // Limit the value to valid values.
            //
            if(plValue[0] < -32768)
            {
                plValue[0] = -32768;
            }
            if(plValue[0] > 32767)
            {
                plValue[0] = 32767;
            }

            //
            // If there is a third argument then this is the synchronization
            // group number.
            //
            if(argc > 3)
            {
                //
                // Get the synchronization group.
                //
                plValue[1] = strtol(argv[3], 0, 0);

                //
                // Limit the value to valid values.
                //
                if(plValue[1] < 0)
                {
                    plValue[1] = 0;
                }
                if(plValue[1] > 255)
                {
                    plValue[1] = 0;
                }
                plValue[0] = (plValue[0] & 0x0000ffff) | (plValue[1] << 16);

                UARTSendMessage(LM_API_ICTRL_SET | g_ulID,
                                (unsigned char *)plValue, 3);
            }
            else
            {
                UARTSendMessage(LM_API_ICTRL_SET | g_ulID,
                                (unsigned char *)plValue, 2);
            }

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            UARTSendMessage(LM_API_ICTRL_SET | g_ulID, 0, 0);

            WaitForAck(LM_API_ICTRL_SET | g_ulID, 10);
        }
    }

    //
    // Check if this was a request to set the P value in Current control mode.
    //
    else if((argc > 1) && (strcmp(argv[1], "p") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            plValue[0] = strtol(argv[2], 0, 0);

            UARTSendMessage(LM_API_ICTRL_PC | g_ulID,
                            (unsigned char *)plValue, 4);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // A command with no data is a get request.
            //
            UARTSendMessage(LM_API_ICTRL_PC | g_ulID, 0, 0);

            WaitForAck(LM_API_ICTRL_PC | g_ulID, 10);
        }
    }

    //
    // Check if this was a request to set the I value in Current control mode.
    //
    else if((argc > 1) && (strcmp(argv[1], "i") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            plValue[0] = strtol(argv[2], 0, 0);

            UARTSendMessage(LM_API_ICTRL_IC | g_ulID,
                            (unsigned char *)plValue, 4);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // A command with no data is a get request.
            //
            UARTSendMessage(LM_API_ICTRL_IC | g_ulID, 0, 0);

            WaitForAck(LM_API_ICTRL_IC | g_ulID, 10);
        }
    }

    //
    // Check if this was a request to set the D value in Current control mode.
    //
    else if((argc > 1) && (strcmp(argv[1], "d") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            plValue[0] = strtol(argv[2], 0, 0);

            UARTSendMessage(LM_API_ICTRL_DC | g_ulID,
                            (unsigned char *)plValue, 4);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // A command with no data is a get request.
            //
            UARTSendMessage(LM_API_ICTRL_DC | g_ulID, 0, 0);

            WaitForAck(LM_API_ICTRL_DC | g_ulID, 10);
        }
    }
    else
    {
        //
        // If this was an unknown request then print out the valid options.
        //
        printf("%s [en|dis|set|p|i|d]\n", argv[0]);
    }

    return(0);
}

//*****************************************************************************
//
// This command controls the setting when running in Speed control mode.
//
//*****************************************************************************
int
CmdSpeed(int argc, char *argv[])
{
    long plValue[2], lTemp;
    unsigned short *pusData;
    unsigned char *pucData;
    int iIdx;

    pusData = (unsigned short *)plValue;
    pucData = (unsigned char *)plValue;

    //
    // Check if this was a request to enable Speed control mode.
    //
    if((argc > 1) && (strcmp(argv[1], "en") == 0))
    {
        UARTSendMessage(LM_API_SPD_EN | g_ulID, 0, 0);

        WaitForAck(LM_API_ACK | g_ulID, 10);
    }

    //
    // Check if this was a request to disable Speed control mode.
    //
    else if((argc > 1) && (strcmp(argv[1], "dis") == 0))
    {
        UARTSendMessage(LM_API_SPD_DIS | g_ulID, 0, 0);

        WaitForAck(LM_API_ACK | g_ulID, 10);
    }

    //
    // Check if this was a request to set the speed in Speed control mode.
    //
    else if((argc > 1) && (strcmp(argv[1], "set") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            plValue[0] = strtol(argv[2], 0, 0);
            if(argc > 3)
            {
                plValue[1] = strtol(argv[3], 0, 0);

                //
                // Limit the value to valid values.
                //
                if(plValue[1] < 0)
                {
                    plValue[1] = 0;
                }
                if(plValue[1] > 255)
                {
                    plValue[1] = 0;
                }

                UARTSendMessage(LM_API_SPD_SET | g_ulID,
                                (unsigned char *)plValue, 5);
            }
            else
            {
                UARTSendMessage(LM_API_SPD_SET | g_ulID,
                                (unsigned char *)plValue, 4);
            }
            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // A command with no data is a get request.
            //
            UARTSendMessage(LM_API_SPD_SET | g_ulID, 0, 0);

            WaitForAck(LM_API_SPD_SET | g_ulID, 10);
        }
    }

    //
    // Check if this was a request to set the P value in Speed control mode.
    //
    else if((argc > 1) && (strcmp(argv[1], "p") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            plValue[0] = strtol(argv[2], 0, 0);

            UARTSendMessage(LM_API_SPD_PC | g_ulID,
                            (unsigned char *)plValue, 4);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // A command with no data is a get request.
            //
            UARTSendMessage(LM_API_SPD_PC | g_ulID, 0, 0);

            WaitForAck(LM_API_SPD_PC | g_ulID, 10);
        }
    }

    //
    // Check if this was a request to set the I value in Speed control mode.
    //
    else if((argc > 1) && (strcmp(argv[1], "i") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            plValue[0] = strtol(argv[2], 0, 0);

            UARTSendMessage(LM_API_SPD_IC | g_ulID,
                            (unsigned char *)plValue, 4);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // A command with no data is a get request.
            //
            UARTSendMessage(LM_API_SPD_IC | g_ulID, 0, 0);

            WaitForAck(LM_API_SPD_IC | g_ulID, 10);
        }
    }

    //
    // Check if this was a request to set the D value in Speed control mode.
    //
    else if((argc > 1) && (strcmp(argv[1], "d") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            plValue[0] = strtol(argv[2], 0, 0);

            UARTSendMessage(LM_API_SPD_DC | g_ulID,
                            (unsigned char *)plValue, 4);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // A command with no data is a get request.
            //
            UARTSendMessage(LM_API_SPD_DC | g_ulID, 0, 0);

            WaitForAck(LM_API_SPD_DC | g_ulID, 10);
        }
    }

    //
    // Check if this was a request to set the speed reference for Speed control
    // mode.
    //
    else if((argc > 1) && (strcmp(argv[1], "ref") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            plValue[0] = strtol(argv[2], 0, 0);

            //
            // Limit the value to valid values.
            //
            if(plValue[0] < 0)
            {
                plValue[0] = 0;
            }
            if(plValue[0] > 255)
            {
                plValue[0] = 255;
            }

            UARTSendMessage(LM_API_SPD_REF | g_ulID,
                            (unsigned char *)plValue, 1);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // A command with no data is a get request.
            //
            UARTSendMessage(LM_API_SPD_REF | g_ulID, 0, 0);

            WaitForAck(LM_API_SPD_REF | g_ulID, 10);
        }
    }
    else
    {
        //
        // If this was an unknown request then print out the valid options.
        //
        printf("%s [en|dis|set|p|i|d|ref]\n", argv[0]);
    }

    return(0);
}

//*****************************************************************************
//
// This command controls the setting when running in Position control mode.
//
//*****************************************************************************
int
CmdPosition(int argc, char *argv[])
{
    long plValue[2], lTemp;
    unsigned short *pusData;
    unsigned char *pucData;
    int iIdx;

    pusData = (unsigned short *)plValue;
    pucData = (unsigned char *)plValue;

    //
    // Check if this was a request to enable Position control mode.
    //
    if((argc > 1) && (strcmp(argv[1], "en") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            plValue[0] = strtol(argv[2], 0, 0);

            UARTSendMessage(LM_API_POS_EN | g_ulID,
                            (unsigned char *)plValue, 4);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            printf("%s %s <value>\n", argv[0], argv[1]);
        }
    }

    //
    // Check if this was a request to disable Position control mode.
    //
    else if((argc > 1) && (strcmp(argv[1], "dis") == 0))
    {
        UARTSendMessage(LM_API_POS_DIS | g_ulID, 0, 0);

        WaitForAck(LM_API_ACK | g_ulID, 10);
    }

    //
    // Check if this was a request to set the position target.
    //
    else if((argc > 1) && (strcmp(argv[1], "set") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            plValue[0] = strtol(argv[2], 0, 0);
            if(argc > 3)
            {
                plValue[1] = strtol(argv[3], 0, 0);

                //
                // Limit the value to valid values.
                //
                if(plValue[1] < 0)
                {
                    plValue[1] = 0;
                }
                if(plValue[1] > 255)
                {
                    plValue[1] = 0;
                }
                UARTSendMessage(LM_API_POS_SET | g_ulID,
                                (unsigned char *)plValue, 5);
            }
            else
            {
                UARTSendMessage(LM_API_POS_SET | g_ulID,
                                (unsigned char *)plValue, 4);
            }
            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // A command with no data is a get request.
            //
            UARTSendMessage(LM_API_POS_SET | g_ulID, 0, 0);

            WaitForAck(LM_API_POS_SET | g_ulID, 10);
        }
    }

    //
    // Check if this was a request to set the P value in Position control mode.
    //
    else if((argc > 1) && (strcmp(argv[1], "p") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            plValue[0] = strtol(argv[2], 0, 0);
            UARTSendMessage(LM_API_POS_PC | g_ulID,
                            (unsigned char *)plValue, 4);
            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // A command with no data is a get request.
            //
            UARTSendMessage(LM_API_POS_PC | g_ulID, 0, 0);

            WaitForAck(LM_API_POS_PC | g_ulID, 10);
        }
    }

    //
    // Check if this was a request to set the I value in Position control mode.
    //
    else if((argc > 1) && (strcmp(argv[1], "i") == 0))
    {
        if(argc > 2)
        {
            plValue[0] = strtol(argv[2], 0, 0);

            UARTSendMessage(LM_API_POS_IC | g_ulID,
                            (unsigned char *)plValue, 4);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // A command with no data is a get request.
            //
            UARTSendMessage(LM_API_POS_IC | g_ulID, 0, 0);

            WaitForAck(LM_API_POS_IC | g_ulID, 10);
        }
    }

    //
    // Check if this was a request to set the D value in Position control mode.
    //
    else if((argc > 1) && (strcmp(argv[1], "d") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            plValue[0] = strtol(argv[2], 0, 0);

            UARTSendMessage(LM_API_POS_DC | g_ulID,
                            (unsigned char *)plValue, 4);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // A command with no data is a get request.
            //
            UARTSendMessage(LM_API_POS_DC | g_ulID, 0, 0);

            WaitForAck(LM_API_POS_DC | g_ulID, 10);
        }
    }

    //
    // Check if this was a request to set the position reference.
    //
    else if((argc > 1) && (strcmp(argv[1], "ref") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            plValue[0] = strtol(argv[2], 0, 0);

            //
            // Limit the value to valid values.
            //
            if(plValue[0] < 0)
            {
                plValue[0] = 0;
            }
            if(plValue[0] > 255)
            {
                plValue[0] = 255;
            }

            UARTSendMessage(LM_API_POS_REF | g_ulID,
                            (unsigned char *)plValue, 1);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // A command with no data is a get request.
            //
            UARTSendMessage(LM_API_POS_REF | g_ulID, 0, 0);

            WaitForAck(LM_API_POS_REF | g_ulID, 10);
        }
    }

    else
    {
        //
        // If this was an unknown request then print out the valid options.
        //
        printf("%s [en|dis|set|p|i|d|ref]\n", argv[0]);
    }

    return(0);
}

//*****************************************************************************
//
// This command handles status requests for devices.
//
//*****************************************************************************
int
CmdStatus(int argc, char *argv[])
{
    unsigned char ucData;

    if((argc > 1) && (strcmp(argv[1], "vout") == 0))
    {
        //
        // Request the current Voltage ouptut setting.
        //
        UARTSendMessage(LM_API_STATUS_VOLTOUT | g_ulID, 0, 0);

        WaitForAck(LM_API_ACK | g_ulID, 10);
    }

    else if((argc > 1) && (strcmp(argv[1], "vbus") == 0))
    {
        //
        // Request the current Vbus value.
        //
        UARTSendMessage(LM_API_STATUS_VOLTBUS | g_ulID, 0, 0);

        WaitForAck(LM_API_ACK | g_ulID, 10);
    }

    else if((argc > 1) && (strcmp(argv[1], "fault") == 0))
    {
        //
        // Request the current Fault value.
        //
        UARTSendMessage(LM_API_STATUS_FAULT | g_ulID, 0, 0);

        WaitForAck(LM_API_ACK | g_ulID, 10);
    }

    else if((argc > 1) && (strcmp(argv[1], "cur") == 0))
    {
        //
        // Request the current Current value.
        //
        UARTSendMessage(LM_API_STATUS_CURRENT | g_ulID, 0, 0);

        WaitForAck(LM_API_ACK | g_ulID, 10);
    }

    else if((argc > 1) && (strcmp(argv[1], "temp") == 0))
    {
        //
        // Request the current temperature.
        //
        UARTSendMessage(LM_API_STATUS_TEMP | g_ulID, 0, 0);

        WaitForAck(LM_API_ACK | g_ulID, 10);
    }

    else if((argc > 1) && (strcmp(argv[1], "pos") == 0))
    {
        //
        // Request the current position.
        //
        UARTSendMessage(LM_API_STATUS_POS | g_ulID, 0, 0);

        WaitForAck(LM_API_ACK | g_ulID, 10);
    }

    else if((argc > 1) && (strcmp(argv[1], "speed") == 0))
    {
        //
        // Request the current speed.
        //
        UARTSendMessage(LM_API_STATUS_SPD | g_ulID, 0, 0);

        WaitForAck(LM_API_ACK | g_ulID, 10);
    }

    else if((argc > 1) && (strcmp(argv[1], "limit") == 0))
    {
        //
        // Request the limit switch settings.
        //
        UARTSendMessage(LM_API_STATUS_LIMIT | g_ulID, 0, 0);

        WaitForAck(LM_API_ACK | g_ulID, 10);
    }

    else if((argc > 1) && (strcmp(argv[1], "power") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            ucData = 1;
            UARTSendMessage(LM_API_STATUS_POWER | g_ulID, &ucData, 1);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // A command with no data is a get request.
            //
            UARTSendMessage(LM_API_STATUS_POWER | g_ulID, 0, 0);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
    }

    else if((argc > 1) && (strcmp(argv[1], "cmode") == 0))
    {
        //
        // Get the current control mode.
        //
        UARTSendMessage(LM_API_STATUS_CMODE | g_ulID, 0, 0);

        WaitForAck(LM_API_ACK | g_ulID, 10);
    }

    else
    {
        printf("%s [vout|vbus|fault|cur|temp|pos|speed|limit|power|cmode]\n",
               argv[0]);
    }

    return(0);
}

//*****************************************************************************
//
// This command is used to set configuration parameters used by the devices.
//
//*****************************************************************************
int
CmdConfig(int argc, char *argv[])
{
    long plValue[2];

    //
    // Get or set the number of encoder lines based on the parameters.
    //
    if((argc > 1) && (strcmp(argv[1], "lines") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            plValue[0] = strtol(argv[2], 0, 0);

            //
            // Limit the value to valid values.
            //
            if(plValue[0] < 0)
            {
                plValue[0] = 0;
            }
            if(plValue[0] > 65535)
            {
                plValue[0] = 65535;
            }

            UARTSendMessage(LM_API_CFG_ENC_LINES | g_ulID,
                            (unsigned char *)plValue, 2);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // This is a request to retrieve the current number of encode lines.
            //
            UARTSendMessage(LM_API_CFG_ENC_LINES | g_ulID, 0, 0);

            WaitForAck(LM_API_CFG_ENC_LINES | g_ulID, 10);
        }
    }

    //
    // Get or set the number of turns in a potentiometer based on the
    // parameters.
    //
    else if((argc > 1) && (strcmp(argv[1], "turns") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            plValue[0] = strtol(argv[2], 0, 0);

            //
            // Limit the value to valid settings.
            //
            if(plValue[0] < 0)
            {
                plValue[0] = 0;
            }

            if(plValue[0] > 65535)
            {
                plValue[0] = 65535;
            }

            UARTSendMessage(LM_API_CFG_POT_TURNS | g_ulID,
                            (unsigned char *)plValue, 2);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // This is a request to retrieve the value.
            //
            UARTSendMessage(LM_API_CFG_POT_TURNS | g_ulID, 0, 0);

            WaitForAck(LM_API_CFG_POT_TURNS | g_ulID, 10);
        }
    }

    //
    // Get or set the brake/coast setting based on the parameters.
    //
    else if((argc > 1) && (strcmp(argv[1], "brake") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            if(strcmp(argv[2], "jumper") == 0)
            {
                //
                // Allow the jumper to control this setting.
                //
                plValue[0] = 0;
            }
            else if(strcmp(argv[2], "brake") == 0)
            {
                //
                // Override the jumper and set the mode to active braking.
                //
                plValue[0] = 1;
            }
            else if(strcmp(argv[2], "coast") == 0)
            {
                //
                // Override the jumper and set the mode to coast braking.
                //
                plValue[0] = 2;
            }
            else
            {
                printf("%s %s [jumper|brake|coast]\n", argv[0], argv[1]);
                return(0);
            }

            UARTSendMessage(LM_API_CFG_BRAKE_COAST | g_ulID,
                            (unsigned char *)plValue, 1);
            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // This is a request to retrieve the value.
            //
            UARTSendMessage(LM_API_CFG_BRAKE_COAST | g_ulID, 0, 0);

            WaitForAck(LM_API_CFG_BRAKE_COAST | g_ulID, 10);
        }
    }

    else if((argc > 1) && (strcmp(argv[1], "limit") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            if(strcmp(argv[2], "off") == 0)
            {
                //
                // Disable the limit switches.
                //
                plValue[0] = 0;
            }
            else if(strcmp(argv[2], "on") == 0)
            {
                //
                // Enable the limit switches.
                //
                plValue[0] = 1;
            }
            else
            {
                printf("%s %s [on|off]\n", argv[0], argv[1]);
                return(0);
            }

            UARTSendMessage(LM_API_CFG_LIMIT_MODE | g_ulID,
                            (unsigned char *)plValue, 1);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // This is a request to retrieve the value.
            //
            UARTSendMessage(LM_API_CFG_LIMIT_MODE | g_ulID, 0, 0);

            WaitForAck(LM_API_CFG_LIMIT_MODE | g_ulID, 10);
        }
    }

    else if((argc > 1) && (strcmp(argv[1], "fwd") == 0))
    {
        //
        // If there is enough data then this is a request to set the value.
        //
        if(argc > 3)
        {
            //
            // Get the position limit.
            //
            plValue[0] = strtol(argv[2], 0, 0);

            //
            // Check if the value is a less than or greater than some position.
            //
            if(strcmp(argv[3], "lt") == 0)
            {
                plValue[1] = 0;
            }
            else if(strcmp(argv[3], "gt") == 0)
            {
                plValue[1] = 1;
            }
            else
            {
                printf("%s %s <pos> [lt|gt]\n", argv[0], argv[1]);
                return(0);
            }

            UARTSendMessage(LM_API_CFG_LIMIT_FWD | g_ulID,
                            (unsigned char *)plValue, 5);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // This is a request to retrieve the value.
            //
            UARTSendMessage(LM_API_CFG_LIMIT_FWD | g_ulID, 0, 0);

            WaitForAck(LM_API_CFG_LIMIT_FWD | g_ulID, 10);
        }
    }

    else if((argc > 1) && (strcmp(argv[1], "rev") == 0))
    {
        //
        // If there is enough data then this is a request to set the value.
        //
        if(argc > 3)
        {
            //
            // Get the position limit.
            //
            plValue[0] = strtol(argv[2], 0, 0);

            //
            // Check if the value is a less than or greater than some position.
            //
            if(strcmp(argv[3], "lt") == 0)
            {
                plValue[1] = 0;
            }
            else if(strcmp(argv[3], "gt") == 0)
            {
                plValue[1] = 1;
            }
            else
            {
                printf("%s %s <pos> [lt|gt]\n", argv[0], argv[1]);
                return(0);
            }

            UARTSendMessage(LM_API_CFG_LIMIT_REV | g_ulID,
                            (unsigned char *)plValue, 5);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // This is a request to retrieve the value.
            //
            UARTSendMessage(LM_API_CFG_LIMIT_REV | g_ulID, 0, 0);

            WaitForAck(LM_API_CFG_LIMIT_REV | g_ulID, 10);
        }
    }

    else if((argc > 1) && (strcmp(argv[1], "maxvout") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            //
            // Get the Max voltage out setting.
            //
            plValue[0] = strtol(argv[2], 0, 0);

            //
            // Limit the value to valid settings.
            //
            if(plValue[0] < 0)
            {
                plValue[0] = 0;
            }

            if(plValue[0] > (12 * 256))
            {
                plValue[0] = 12 * 256;
            }

            UARTSendMessage(LM_API_CFG_MAX_VOUT | g_ulID,
                            (unsigned char *)plValue, 2);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // This is a request to retrieve the value.
            //
            UARTSendMessage(LM_API_CFG_MAX_VOUT | g_ulID, 0, 0);

            WaitForAck(LM_API_CFG_MAX_VOUT | g_ulID, 10);
        }
    }

    else if((argc > 1) && (strcmp(argv[1], "faulttime") == 0))
    {
        //
        // If there is a second argument then this is a set request.
        //
        if(argc > 2)
        {
            //
            // Get the fault timeout value to set.
            //
            plValue[0] = strtol(argv[2], 0, 0);

            //
            // Limit the value to valid settings.
            //
            if(plValue[0] < 0)
            {
                plValue[0] = 0;
            }
            if(plValue[0] > 65535)
            {
                plValue[0] = 65535;
            }

            UARTSendMessage(LM_API_CFG_FAULT_TIME | g_ulID,
                            (unsigned char *)plValue, 2);

            WaitForAck(LM_API_ACK | g_ulID, 10);
        }
        else
        {
            //
            // This is a request to retrieve the value.
            //
            UARTSendMessage(LM_API_CFG_FAULT_TIME | g_ulID, 0, 0);

            WaitForAck(LM_API_CFG_FAULT_TIME | g_ulID, 10);
        }
    }

    else
    {
        printf("%s [lines|turns|brake|limit|fwd|rev|maxvout|faulttime]\n",
               argv[0]);
    }

    return(0);
}

//*****************************************************************************
//
// This command handler takes care of the system leve commands.
//
//*****************************************************************************
int
CmdSystem(int argc, char *argv[])
{
    unsigned long ulValue;
    char pcBuffer[8];

    if((argc > 1) && (strcmp(argv[1], "halt") == 0))
    {
        //
        // Broadcast a Halt command.
        //
        UARTSendMessage(CAN_MSGID_API_SYSHALT, 0, 0);
    }
    else if((argc > 1) && (strcmp(argv[1], "resume") == 0))
    {
        //
        // Broadcast a Resume command.
        //
        UARTSendMessage(CAN_MSGID_API_SYSRESUME, 0, 0);
    }
    else if((argc > 1) && (strcmp(argv[1], "reset") == 0))
    {
        //
        // Broadcast a system reset command.
        //
        UARTSendMessage(CAN_MSGID_API_SYSRST, 0, 0);
    }
    else if((argc > 1) && (strcmp(argv[1], "enum") == 0))
    {
        //
        // Broadcast a system enumeration command.
        //
        UARTSendMessage(CAN_MSGID_API_ENUMERATE, 0, 0);

        //
        // Wait for a device query response.
        //
        WaitForAck(CAN_MSGID_API_DEVQUERY | g_ulID, 100);
    }
    else if((argc > 1) && (strcmp(argv[1], "assign") == 0))
    {
        //
        // There must be a value to set the device ID to before continuing.
        //
        if(argc > 2)
        {
            //
            // Get the requested ID from the parameter.
            //
            ulValue = strtoul(argv[2], 0, 0);

            //
            // Check if this request for a non-zero ID.
            //
            if(ulValue == 0)
            {
                UARTSendMessage(CAN_MSGID_API_DEVASSIGN,
                                (unsigned char *)&ulValue, 1);
            }
            else if(ulValue < MAX_CAN_ID)
            {
                //
                // Send out the device assignment ID and wait for a response
                // from a device.
                //
                UARTSendMessage(CAN_MSGID_API_DEVASSIGN,
                                (unsigned char *)&ulValue, 1);

                if(g_bUseGUI)
                {

                  //
                  // Give it some time to take effect.
                  //
                  for(ulValue = 5; ulValue > 0; ulValue--)
                  {
                    sprintf(pcBuffer, "...%ld...", ulValue);
                    g_pSystemAssign->copy_label(pcBuffer);

                    Fl::check();

                    OSSleep(1);
                  }

                  g_pSystemAssign->copy_label("Assign");
                  Fl::check();
                }
                else
                {
                    for(ulValue = 5; ulValue > 0; ulValue--)
                    {
                        printf("\r%ld", ulValue);
                        OSSleep(1);
                    }
                    printf("\r");
                }
            }
            else
            {
                printf("%s %s: the ID must be between 0 and 63.\n", argv[0],
                       argv[1]);
            }
        }
        else
        {
            printf("%s %s <id>\n", argv[0], argv[1]);
        }
    }
    else if((argc > 1) && (strcmp(argv[1], "query") == 0))
    {
        //
        // Handle the device query command that will return information about
        // the device.
        //
        UARTSendMessage(CAN_MSGID_API_DEVQUERY | g_ulID, 0, 0);

        WaitForAck(CAN_MSGID_API_DEVQUERY | g_ulID, 10);
    }
    else if((argc > 1) && (strcmp(argv[1], "sync") == 0))
    {
        //
        // Send out a synchronous update command.
        //
        if(argc > 2)
        {
            //
            // Get the synchronous update ID from the parameter.
            //
            ulValue = strtoul(argv[2], 0, 0);

            UARTSendMessage(CAN_MSGID_API_SYNC,
                            (unsigned char *)&ulValue, 1);
        }
        else
        {
            printf("%s %s <group>\n", argv[0], argv[1]);
        }
    }
    else if((argc > 1) && (strcmp(argv[1], "version") == 0))
    {
        //
        // Send out a firmware version number request.
        //
        UARTSendMessage(CAN_MSGID_API_FIRMVER | g_ulID, 0, 0);

        WaitForAck(CAN_MSGID_API_FIRMVER | g_ulID, 10);
    }
    else if((argc > 1) && (strcmp(argv[1], "hwver") == 0))
    {
        //
        // Send out a hardware version number request.
        //
        UARTSendMessage(LM_API_HWVER | g_ulID, 0, 0);

        WaitForAck(LM_API_HWVER | g_ulID, 10);
    }
    else
    {
        printf("%s [halt|resume|reset|enum|assign|query|sync|version|hwver]\n",
               argv[0]);
    }

    return(0);
}

//*****************************************************************************
//
// This function handles the firmware update command.
//
//*****************************************************************************
int
CmdUpdate(int argc, char *argv[])
{
    unsigned char *pucBuffer, pucData[8];
    unsigned long ulLength, ulIdx, ulHeartbeatSave;
    FILE *pFile;

    if(argc > 1)
    {
        //
        // Attempt to open the requested file.
        //
        pFile = fopen(argv[1], "rb");
        if(!pFile)
        {
            if(g_bUseGUI)
            {
                fl_alert("Unable to open specified file!");
            }
            else
            {
                printf("%s: Unable to open '%s'.\n", argv[0], argv[1]);
            }

            return(-1);
        }

        //
        // Find out the size of the file.
        //
        fseek(pFile, 0, SEEK_END);
        ulLength = ftell(pFile);
        fseek(pFile, 0, SEEK_SET);

        //
        // Allocate a buffer for the file.
        //
        pucBuffer = (unsigned char *)malloc(ulLength);

        if(!pucBuffer)
        {
            if(g_bUseGUI)
            {
                fl_alert("Unable to allocate memory for update!");
            }
            else
            {
                printf("%s: Unable to allocate memory for '%s'.\n", argv[0],
                       argv[1]);
            }

            //
            // Free the buffer.
            //
            free(pucBuffer);

            fclose(pFile);
            return(0);
        }

        //
        // Read the full file into the buffer and close out the file.
        //
        fread(pucBuffer, 1, ulLength, pFile);
        fclose(pFile);

        //
        // Remember the current heart beat setting and then disable the system
        // heart beats during the update.
        //
        ulHeartbeatSave = g_ulHeartbeat;
        g_ulHeartbeat = 0;

        //
        // If the ID is non-zero then send out a request to the specific ID to
        // force the update on.
        //
        if(g_ulID != 0)
        {
            pucData[0] = g_ulID;

            UARTSendMessage(CAN_MSGID_API_UPDATE, pucData, 1);

            usleep(50000);
        }

        //
        // Attempt to ping the CAN boot loader.
        //
        UARTSendMessage(LM_API_UPD_PING, 0, 0);

        //
        // Wait for an acknowledgment from the device from the boot loader.
        if(WaitForAck(LM_API_UPD_ACK, 250) == -1)
        {
            if(g_bUseGUI)
            {
                fl_alert("Unable to contact the boot loader!");
            }
            else
            {
                printf("%s: Unable to contact the boot loader.\n", argv[0]);
            }

            free(pucBuffer);

            return(0);
        }

        if(!g_bUseGUI)
        {
            printf("  0%%");
        }

        //
        // Create and send the download request to the boot loader.
        //
        *(unsigned long *)pucData = 0x800;
        *(unsigned long *)(pucData + 4) = ulLength;
        UARTSendMessage(LM_API_UPD_DOWNLOAD, pucData, 8);

        if(WaitForAck(LM_API_UPD_ACK, 4000) == -1)
        {
            if(g_bUseGUI)
            {
                fl_alert("Failed to erase the device's flash!");
            }
            else
            {
                printf("%s: Failed to erase the device's flash.\n", argv[0]);
            }

            free(pucBuffer);

            return(0);
        }

        //
        // Send out the new firmware to the device.
        //
        for(ulIdx = 0; ulIdx < ulLength; ulIdx += 8)
        {
            if(g_bUseGUI)
            {
                if(g_ulID == 0)
                {
                    g_pRecoverProgress->value(((ulIdx + 8) * 100) / ulLength);
                }
                else
                {
                    g_pUpdateProgress->value(((ulIdx + 8) * 100) / ulLength);
                }
                Fl::check();
            }
            else
            {
                printf("\r%3ld%%", ((ulIdx + 8) * 100) / ulLength);
            }

            if((ulIdx + 8) > ulLength)
            {
                UARTSendMessage(LM_API_UPD_SEND_DATA,
                                pucBuffer + ulIdx, ulLength - ulIdx);
            }
            else
            {
                UARTSendMessage(LM_API_UPD_SEND_DATA,
                                pucBuffer + ulIdx, 8);
            }
            if(WaitForAck(LM_API_UPD_ACK, 250) == -1)
            {
                if(g_bUseGUI)
                {
                    fl_alert("Failed to program the device's flash!");
                }
                else
                {
                    printf("%s: Failed to program the device's flash.\n",
                           argv[0]);
                }

                free(pucBuffer);

                return(0);
            }
        }

        if(g_bUseGUI)
        {
            if(g_ulID == 0)
            {
                g_pRecoverProgress->value(100);
            }
            else
            {
                g_pUpdateProgress->value(100);
            }
            Fl::check();
        }
        else
        {
            printf("\r    \r");
        }

        UARTSendMessage(LM_API_UPD_RESET, 0, 0);

        free(pucBuffer);

        g_ulHeartbeat = ulHeartbeatSave;
    }
    else
    {
        printf("%s <filename>\n", argv[0]);
    }

    return(0);
}

//*****************************************************************************
//
// Handle the boot loader forced button update.
//
//*****************************************************************************
int
CmdBoot(int argc, char *argv[])
{
    int iRet;
    unsigned long g_ulSavedID;

    //
    // Save the global ID.
    //
    g_ulSavedID = g_ulID;

    //
    // Set the global ID to 0 so that we only update devices that are in the
    // boot loader already.
    //
    g_ulID = 0;

    if(argc < 2)
    {
        printf("%s <filename>\n", argv[0]);
        return(0);
    }

    //
    // Just do a reset to allow updating without losing power.
    //
    UARTSendMessage(CAN_MSGID_API_SYSRST, 0, 0);

    printf("Waiting on a boot request\n");

    //
    // Send a generic updater ping to keep the state of the application ok.
    //
    UARTSendMessage(LM_API_UPD_PING, 0, 0);

    //
    // Now wait for a request to boot.
    //
    do
    {
        iRet = WaitForAck(LM_API_UPD_REQUEST, 10);
        printf(".");
    } while(iRet == -1);

    //
    // Got the request so respond and start updating.
    //
    UARTSendMessage(LM_API_UPD_REQUEST, 0, 0);

    if(WaitForAck(LM_API_UPD_ACK, 10) >= 0)
    {
        printf("\nUpdating\n");

        if(CmdUpdate(argc, argv) < 0)
        {
            UARTSendMessage(LM_API_UPD_RESET, 0, 0);
        }
    }
    else
    {
        printf("\nFailed to detect boot loader\n");
    }

    //
    // Restore the global ID.
    //
    g_ulID = g_ulSavedID;

    return(0);
}

//*****************************************************************************
//
// Handle shutting down the applcation.
//
//*****************************************************************************
int
CmdExit(int argc, char *argv[])
{
    CloseUART();
    exit(0);
}

//*****************************************************************************
//
// This function implements the "help" command.  It prints a simple list
// of the available commands with a brief description.
//
//*****************************************************************************
int
CmdHelp(int argc, char *argv[])
{
    tCmdLineEntry *pEntry;

    //
    // Point at the beginning of the command table.
    //
    pEntry = &g_sCmdTable[0];

    //
    // Enter a loop to read each entry from the command table.  The
    // end of the table has been reached when the command name is NULL.
    //
    while(pEntry->pcCmd)
    {
        //
        // Print the command name and the brief description.
        //
        printf("%s%s\n", pEntry->pcCmd, pEntry->pcHelp);

        //
        // Advance to the next entry in the table.
        //
        pEntry++;
    }

    //
    // Return success.
    //
    return(0);
}

//*****************************************************************************
//
// The table of the commands supported by the application.
//
//*****************************************************************************
tCmdLineEntry g_sCmdTable[] =
{
    { "help",       CmdHelp, "      - display a list of commands" },
    { "h",          CmdHelp, "         - alias for help" },
    { "?",          CmdHelp, "         - alias for help" },
    { "id",         CmdID, "        - set the target ID" },
    { "heartbeat",  CmdHeartbeat, " - start/stop the heartbeat" },
    { "volt",       CmdVoltage, "      - voltage control mode commands" },
    { "cur",        CmdCurrent, "       - current control mode commands" },
    { "speed",      CmdSpeed, "     - speed control mode commands" },
    { "pos",        CmdPosition, "       - position control mode commands" },
    { "stat",       CmdStatus, "      - status commands" },
    { "config",     CmdConfig, "    - configuration commands" },
    { "system",     CmdSystem, "    - system commands" },
    { "update",     CmdUpdate, "    - update the firmware" },
    { "boot",       CmdBoot, "      - wait for boot loader to request update" },
    { "exit",       CmdExit, "      - exit the program" },
    { "quit",       CmdExit, "      - alias for exit" },
    { "q",          CmdExit, "         - alias for exit" },
    { 0, 0, 0 }
};

//*****************************************************************************
//
// This function handles updating the current status display.
//
//*****************************************************************************
void
UpdateStatus(void *pvData)
{
    if(!g_bConnected)
    {
        return;
    }

    //
    // Update the status items on the GUI.
    //
    g_pStatusVout->value(g_sBoardStatus.fVout);
    g_pStatusVbus->value(g_sBoardStatus.fVbus);
    g_pStatusCurrent->value(g_sBoardStatus.fCurrent);
    g_pStatusTemperature->value(g_sBoardStatus.fTemperature);
    g_pStatusPosition->value(g_sBoardStatus.fPosition);
    g_pStatusSpeed->value(g_sBoardStatus.fSpeed);
    g_pStatusLimit->value(g_sBoardStatus.pcLimit);
    g_pStatusPower->value(g_sBoardStatus.lPower);

    if(g_sBoardStatus.lFault)
    {
        switch(g_sBoardStatus.lFault)
        {
            case 1:
            {
                strcpy(g_pcFaultTxt, "CUR FAULT");
                break;
            }

            case 2:
            {
                strcpy(g_pcFaultTxt, "TEMP FAULT");
                break;
            }

            case 4:
            {
                strcpy(g_pcFaultTxt, "VBUS FAULT");
                break;
            }

            case 8:
            {
                strcpy(g_pcFaultTxt, "GATE FAULT");
                break;
            }
        }

        g_pStatusFault->label(g_pcFaultTxt);
        g_pStatusFault->show();
    }
    else
    {
        g_pStatusFault->hide();
    }
}

//*****************************************************************************
//
// This function handles sending out heart beats to the devices.
//
//*****************************************************************************
void *
HeartbeatThread(void *pvData)
{
    unsigned short usToken;
    int iIndex;

    //
    // Set this thread to the highest priority.
    //
#ifdef __WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#else
    // TODO: Linux equivalent.
#endif

    while(1)
    {
        usleep(50000);

        //
        // If there is no COM port, or no board, then do nothing.
        //
        if(!g_bConnected)
        {
            continue;
        }

        if(g_ulHeartbeat)
        {
            UARTSendMessage(CAN_MSGID_API_HEARTBEAT, 0, 0);
        }
    }
}

//*****************************************************************************
//
// The thread used to update the status while the application is running.
//
//*****************************************************************************
void *
BoardStatusThread(void *pvData)
{
    while(1)
    {
        //
        // Sleep for 500ms.
        //
        usleep(500000);

        //
        // If there is no COM port, or no board, then do nothing.
        //
        if(!g_bConnected)
        {
            continue;
        }

        //
        // If the board status is active, get the data.
        //
        if(g_ulBoardStatus)
        {
            //
            // Set the global active flag for this thread.  This blocks the GUI
            // from accessing to the receive functionality of the COM port
            // until the status has been updated.
            //
            g_bBoardStatusActive = true;

            //
            // The first "argument" will always be "stat".
            //
            strcpy(g_argv[0], "stat");

            //
            // Get the speed.
            //
            strcpy(g_argv[1], "speed");
            CmdStatus(2, g_argv);

            usleep(1000);

            //
            // Get the current.
            //
            strcpy(g_argv[1], "cur");
            CmdStatus(2, g_argv);

            usleep(1000);

            //
            // Get the bus voltage.
            //
            strcpy(g_argv[1], "vbus");
            CmdStatus(2, g_argv);

            usleep(1000);

            //
            // Get the output voltage.
            //
            strcpy(g_argv[1], "vout");
            CmdStatus(2, g_argv);

            usleep(1000);

            //
            // Get the temperature.
            //
            strcpy(g_argv[1], "temp");
            CmdStatus(2, g_argv);

            usleep(1000);

            //
            // Get the motor position.
            //
            strcpy(g_argv[1], "pos");
            CmdStatus(2, g_argv);

            usleep(1000);

            //
            // Get the limit switch position.
            //
            strcpy(g_argv[1], "limit");
            CmdStatus(2, g_argv);

            usleep(1000);

            //
            // Get the power.
            //
            strcpy(g_argv[1], "power");
            CmdStatus(2, g_argv);

            usleep(1000);

            //
            // Get the fault status.
            //
            strcpy(g_argv[1] , "fault");
            CmdStatus(2, g_argv);

            //
            // Clear the global active flag for this thread.  This grants the
            // GUI access to the receive functionality of the COM port while
            // status is not being updated.
            //
            g_bBoardStatusActive = false;

            //
            // Tell the main thread to update the status values in the GUI.
            //
            if(g_bConnected)
            {
                Fl::awake(UpdateStatus, 0);
            }
        }
    }
}

//*****************************************************************************
//
// Finds the Jaguars on the network.
//
//*****************************************************************************
void
FindJaguars(void)
{
    int iIdx;

    //
    // Initialize the status structure list to all 0's.
    //
    for(iIdx = 0; iIdx < MAX_CAN_ID; iIdx++)
    {
        g_sBoardState[iIdx].ulControlMode = LM_STATUS_CMODE_VOLT;
    }

    //
    // Send the enumerate command.
    //
    strcpy(g_argv[0], "system");
    strcpy(g_argv[1], "enum");
    CmdSystem(2, g_argv);
}

//*****************************************************************************
//
// The main control loop.
//
//*****************************************************************************
int
main(int argc, char *argv[])
{
    int iIdx;
    char pcBuffer[256];
    long lCode;

#ifndef __WIN32
    pthread_t thread;
#endif
#ifdef __WIN32
    WSADATA wsaData;
#endif

    //
    // If running on Windows, initialize the COM library (required for multi-
    // threading).
    //
#ifdef __WIN32
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
#endif

    setbuf(stdout, 0);

    //
    // Decide whether or not to start the GUI based on input arguments.
    //
    if(argc == 1)
    {
        //
        // Hide the console window.
        //
#ifdef __WIN32
        FreeConsole();
#endif

        //
        // Create and show the main window.
        //
        CreateMainAppWindow()->show(argc, argv);

        //
        // Set the flag to use the GUI.
        //
        g_bUseGUI = true;
    }
    else
    {
        while((lCode = getopt(argc, argv, "?c:h")) != -1)
        {
            switch(lCode)
            {
                case 'c':
                {
#ifdef __WIN32
                    sprintf(g_szCOMName, "\\\\.\\COM%s", optarg);
#else
                    strncpy(g_szCOMName, optarg, sizeof(g_szCOMName));
#endif
                    break;
                }

                case 'h':
                case '?':
                {
                    Usage(argv[0]);
                    return(1);
                }

                default:
                {
                    fprintf(stderr, "Try `%s -h' for more information.\n",
                            basename(argv[0]));
                    return(1);
                }
            }
        }

        //
        // Set the flag to not use GUI.
        //
        g_bUseGUI = false;
    }

    //
    // If using the GUI, populate the COM port drop down menu.
    //
    if(g_bUseGUI && (GUIFillCOMPortDropDown() == 0))
    {
        fl_alert("There are no COM ports on your computer...exiting.");
        exit(1);
    }

    //
    // Open the COM port.
    //
    if(g_bUseGUI)
    {
        GUIConnect();
    }
    else if(OpenUART(g_szCOMName, 115200))
    {
        printf("Failed to configure Host UART\n");
        return(-1);
    }

    if(g_bUseGUI)
    {
        //
        // Prepare FLTK for multi-threaded operation.
        //
        Fl::lock();
    }

    //
    // Initialize the mutex that restricts access to the COM port.
    //
    MutexInit(&mMutex);

    //
    // Create the heart beat thread.
    //
    OSThreadCreate(HeartbeatThread);

    //
    // Decide what to do based on the g_bUseGUI flag.
    //
    if(g_bUseGUI)
    {
        //
        // When the GUI is active, start the board status thread.
        //
        OSThreadCreate(BoardStatusThread);

        //
        // Handle the FLTK events in the main thread.
        //
        return(Fl::run());
    }
    else
    {

        //
        // Begin the main loop for the command line version of the tool.
        //
        while(1)
        {
            printf("# ");
            if(fgets(pcBuffer, sizeof(pcBuffer), stdin) == 0)
            {
                printf("\n");
                CmdExit(0, 0);
            }

            while((pcBuffer[strlen(pcBuffer) - 1] == '\r') ||
            (pcBuffer[strlen(pcBuffer) - 1] == '\n'))
            {
                pcBuffer[strlen(pcBuffer) - 1] = '\0';
            }

            if(CmdLineProcess(pcBuffer) != 0)
            {
                printf("heartbeat|id|volt|cur|speed|pos|stat|config|system|"
                       "update|help|exit\n");
            }
        }
    }
}
