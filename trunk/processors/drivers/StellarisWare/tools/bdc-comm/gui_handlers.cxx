//*****************************************************************************
//
// gui_handlers.cxx - GUI handler functions.
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

#include "gui_handlers.h"
#include "os.h"
#include "uart_handler.h"
#include "gui.h"
#include "can_proto.h"
#include "bdc-comm.h"

#ifdef __WIN32
#include <setupapi.h>
#else
#include <dirent.h>
#endif

//*****************************************************************************
//
// The maximum number of CAN IDs that can be on the network.
//
//*****************************************************************************
#define MAX_CAN_ID        64

//*****************************************************************************
//
// The current board status and board states for all devices.
//
//*****************************************************************************
tBoardStatus g_sBoardStatus;
tBoardState g_sBoardState[MAX_CAN_ID];

//*****************************************************************************
//
// Used when sorting port numbers for display.
//
//*****************************************************************************
static int
PortNumCompare(const void *a, const void *b)
{
    return(*(unsigned long *)a - *(unsigned long *)b);
}

//*****************************************************************************
//
// Used when selecting UART devices under Linux.
//
//*****************************************************************************
#ifndef __WIN32
static int
DevSelector(const struct dirent64 *pEntry)
{
    if((strncmp(pEntry->d_name, "ttyS", 4) == 0) ||
       (strncmp(pEntry->d_name, "ttyUSB", 6) == 0))
    {
        return(1);
    }
    else
    {
        return(0);
    }
}
#endif

//*****************************************************************************
//
// Fill the drop down box for the UARTs.
//
//*****************************************************************************
int
GUIFillCOMPortDropDown(void)
{
#ifdef __WIN32
#define STRING_BUFFER_SIZE      256
#define MAX_PORTS               256
    unsigned long pulPorts[MAX_PORTS], ulNumPorts = 0;
    char *pcBuffer, *pcData;
    SP_DEVINFO_DATA sDeviceInfoData;
    HDEVINFO sDeviceInfoSet;
    DWORD dwRequiredSize = 0, dwLen;
    HKEY hKeyDevice;
    int iDev = 0;

    //
    // Windows stuff for enumerating COM ports
    //
    SetupDiClassGuidsFromNameA("Ports", 0, 0, &dwRequiredSize);
    if(dwRequiredSize < 1)
    {
        return(0);
    }

    pcData = (char *)malloc(dwRequiredSize * sizeof(GUID));
    if(!pcData)
    {
        return(0);
    }

    if(!SetupDiClassGuidsFromNameA("Ports", (LPGUID)pcData,
                                   dwRequiredSize * sizeof(GUID),
                                   &dwRequiredSize))
    {
        free(pcData);
        return(0);
    }

    sDeviceInfoSet = SetupDiGetClassDevs((LPGUID)pcData, NULL, NULL,
                                         DIGCF_PRESENT);
    if(sDeviceInfoSet == INVALID_HANDLE_VALUE)
    {
        free(pcData);
        return(0);
    }

    //
    // Allocate space for a string buffer
    //
    pcBuffer = (char *)malloc(STRING_BUFFER_SIZE);
    if(!pcBuffer)
    {
        free(pcData);
        return(0);
    }

    //
    // Loop through the device set looking for serial ports
    //
    for(iDev = 0;
        sDeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA),
            SetupDiEnumDeviceInfo(sDeviceInfoSet, iDev, &sDeviceInfoData);
        iDev++)
    {
        //
        // Dont exceed the amount of space in the caller supplied list
        //
        if(ulNumPorts == MAX_PORTS)
        {
            break;
        }

        if(SetupDiGetDeviceRegistryProperty(sDeviceInfoSet, &sDeviceInfoData,
                                            SPDRP_UPPERFILTERS, 0,
                                            (BYTE *)pcBuffer,
                                            STRING_BUFFER_SIZE, 0))
        {
            if(strcmp(pcBuffer, "serenum") != 0)
            {
                continue;
            }
        }
        else
        {
            continue;
        }

        if(SetupDiGetDeviceRegistryProperty(sDeviceInfoSet, &sDeviceInfoData,
                                            SPDRP_FRIENDLYNAME, 0,
                                            (BYTE *)pcBuffer,
                                            STRING_BUFFER_SIZE, 0))
        {
        }
        else
        {
            continue;
        }

        hKeyDevice = SetupDiOpenDevRegKey(sDeviceInfoSet, &sDeviceInfoData,
                                          DICS_FLAG_GLOBAL, 0, DIREG_DEV,
                                          KEY_READ);
        if(hKeyDevice != INVALID_HANDLE_VALUE)
        {
            dwLen = STRING_BUFFER_SIZE;
            RegQueryValueEx(hKeyDevice, "portname", 0, 0, (BYTE *)pcBuffer,
                            &dwLen);

            //
            // A device was found, make sure it looks like COMx, and
            // then extract the numeric part and store it in the list.
            //
            if(!strncmp(pcBuffer, "COM", 3))
            {
                pulPorts[ulNumPorts++] = atoi(&pcBuffer[3]);
            }

            RegCloseKey(hKeyDevice);
        }
    }

    //
    // Sort the COM port numbers.
    //
    qsort(pulPorts, ulNumPorts, sizeof(pulPorts[0]), PortNumCompare);

    //
    // Loop through the ports, adding them to the COM port dropdown.
    //
    for(iDev = 0; iDev < ulNumPorts; iDev++)
    {
        snprintf(pcBuffer, STRING_BUFFER_SIZE, "%d", pulPorts[iDev]);
        g_pSelectCOM->add(pcBuffer);
    }

    //
    // Free up allocated buffers and return to the caller.
    //
    free(pcBuffer);
    free(pcData);

    //
    // Select the first COM port.
    //
    if(ulNumPorts != 0)
    {
        g_pSelectCOM->value(0);
        snprintf(g_szCOMName, sizeof(g_szCOMName), "\\\\.\\COM%s",
                 g_pSelectCOM->text(0));
    }

    //
    // Return the number of COM ports found.
    //
    return(ulNumPorts);
#else
    unsigned long ulIdx, ulCount;
    struct dirent64 **pEntries;

    //
    // Scan the /dev directory for serial ports.
    //
    ulCount = scandir64("/dev", &pEntries, DevSelector, alphasort64);

    //
    // Return that there are no serial ports if none were found or if an error
    // was encountered.
    //
    if((ulCount == 0) || (ulCount == -1))
    {
        return(0);
    }

    //
    // Add the found serial ports (which will already be sorted alphabetically)
    // to the drop down selector.
    //
    for(ulIdx = 0; ulIdx < ulCount; ulIdx++)
    {
        g_pSelectCOM->add(pEntries[ulIdx]->d_name);
    }

    //
    // Free the array of directory entries that were found.
    //
    free(pEntries);

    //
    // Select the first serial port by default.
    //
    g_pSelectCOM->value(0);
    strcpy(g_szCOMName, "/dev/");
    strcat(g_szCOMName, g_pSelectCOM->text(0));

    //
    // Success.
    //
    return(ulCount);
#endif
}

//*****************************************************************************
//
// Updates the range of the position slider based on the position reference  in
// use.
//
//*****************************************************************************
void
GUIUpdatePositionSlider(void)
{
    //
    // Do nothing if the current board is not in position control mode.
    //
    if(g_sBoardState[g_ulID].ulControlMode != LM_STATUS_CMODE_POS)
    {
        return;
    }

    //
    // Set the limits for the value and slider box based on the position
    // reference in use.
    //
    if(g_pConfigLimitSwitches->value() == 1)
    {
        double dFwd, dRev, dTemp;

        dFwd = g_pConfigFwdLimitValue->value();
        dRev = g_pConfigRevLimitValue->value();

        if(dRev > dFwd)
        {
            dTemp = dFwd;
            dFwd = dRev;
            dRev = dTemp;
        }

        g_pModeSetBox->range(dRev, dFwd);

        g_pModeSetSlider->range(dRev, dFwd);
        g_pModeSetSlider->activate();
    }
    else if(g_pModePot->value() == 1)
    {
        g_pModeSetBox->range(0, g_pConfigPOTTurns->value());

        g_pModeSetSlider->range(0, g_pConfigPOTTurns->value());
        g_pModeSetSlider->activate();
    }
    else
    {
        g_pModeSetBox->range(-32767, 32767);

        g_pModeSetSlider->range(-32767, 32767);
        g_pModeSetSlider->deactivate();
    }

    g_pModeSetBox->precision(3);

    g_pModeSetSlider->step(0.001);
}

//*****************************************************************************
//
// Sets up the enable/disable and ranges on the GUI elements based on the
// control mode.
//
//*****************************************************************************
void
GUIControlUpdate(void)
{
    //
    // Enable the default GUI items.
    //
    g_pSelectBoard->activate();
    g_pSelectMode->activate();
    g_pModeSync->activate();
    g_pModeSetBox->activate();
    g_pModeSetSlider->activate();
    g_pConfigEncoderLines->activate();
    g_pConfigPOTTurns->activate();
    g_pConfigMaxVout->activate();
    g_pConfigFaultTime->activate();
    g_pConfigStopJumper->activate();
    g_pConfigStopBrake->activate();
    g_pConfigStopCoast->activate();
    g_pConfigLimitSwitches->activate();
    g_pSystemAssignValue->activate();
    g_pSystemAssign->activate();
    g_pSystemHalt->activate();
    g_pSystemResume->activate();
    g_pSystemReset->activate();
    g_pMenuUpdate->activate();

    //
    // Get the current control mode.
    //
    strcpy(g_argv[0], "stat");
    strcpy(g_argv[1], "cmode");
    CmdStatus(2, g_argv);

    //
    // Determine which mode is in use.
    //
    switch(g_sBoardState[g_ulID].ulControlMode)
    {
        //
        // Voltage control mode.
        //
        case LM_STATUS_CMODE_VOLT:
        {
            //
            // Deactivate unneeded GUI items.
            //
            g_pModeEncoder->deactivate();
            g_pModePot->deactivate();
            g_pModeP->deactivate();
            g_pModeI->deactivate();
            g_pModeD->deactivate();

            //
            // Activate used GUI items.
            //
            g_pModeRamp->activate();
            g_pModeSetSlider->activate();

            //
            // Set new limits for the "Value" box.
            //
            g_pModeSetBox->range(-100, 100);
            g_pModeSetBox->precision(0);

            //
            // Set the new limits for the slider.
            //
            g_pModeSetSlider->range(-100, 100);
            g_pModeSetSlider->step(1);

            //
            // Update the label for the set value box.
            //
            g_pModeSetBox->label("Value (%):");

            //
            // Get the voltage setpoint.
            //
            strcpy(g_argv[0], "volt");
            strcpy(g_argv[1], "set");
            CmdVoltage(2, g_argv);

            //
            // Get the current ramp value.
            //
            strcpy(g_argv[1], "ramp");
            CmdVoltage(2, g_argv);

            //
            // Unselect the reference radios.
            //
            g_pModeEncoder->value(0);
            g_pModePot->value(0);

            //
            // Get the current P value.
            //
            g_pModeP->value(0.0);

            //
            // Get the current I value.
            //
            g_pModeI->value(0.0);

            //
            // Get the current D value.
            //
            g_pModeD->value(0.0);

            //
            // Done.
            //
            break;
        }

        //
        // Current control mode.
        //
        case LM_STATUS_CMODE_CURRENT:
        {
            //
            // Deactivate unneeded GUI items.
            //
            g_pModeRamp->deactivate();
            g_pModeEncoder->deactivate();
            g_pModePot->deactivate();

            //
            // Activate used GUI items.
            //
            g_pModeP->activate();
            g_pModeI->activate();
            g_pModeD->activate();
            g_pModeSetSlider->activate();

            //
            // Set new limits for the "Value" box.
            //
            g_pModeSetBox->range(-40, 40);
            g_pModeSetBox->precision(1);

            //
            // Set the new limits for the slider.
            //
            g_pModeSetSlider->range(-40, 40);
            g_pModeSetSlider->step(0.1);

            //
            // Update the label for the set value box.
            //
            g_pModeSetBox->label("Value (A):");

            //
            // Get the current setpoint.
            //
            strcpy(g_argv[0], "cur");
            strcpy(g_argv[1], "set");
            CmdCurrent(2, g_argv);

            //
            // Get the current ramp value.
            //
            g_pModeRamp->value(0.0);

            //
            // Unselect the reference radios.
            //
            g_pModeEncoder->value(0);
            g_pModePot->value(0);

            //
            // Get the current P value.
            //
            strcpy(g_argv[1], "p");
            CmdCurrent(2, g_argv);

            //
            // Get the current I value.
            //
            strcpy(g_argv[1], "i");
            CmdCurrent(2, g_argv);

            //
            // Get the current D value.
            //
            strcpy(g_argv[1], "d");
            CmdCurrent(2, g_argv);

            //
            // Done.
            //
            break;
        }

        //
        // Speed control mode.
        //
        case LM_STATUS_CMODE_SPEED:
        {
            //
            // Deactivate unneeded GUI items.
            //
            g_pModeRamp->deactivate();
            g_pModeEncoder->deactivate();
            g_pModePot->deactivate();

            //
            // Activate used GUI items.
            //
            g_pModeP->activate();
            g_pModeI->activate();
            g_pModeD->activate();
            g_pModeSetSlider->activate();

            //
            // Set new limits for the "Value" box.
            //
            g_pModeSetBox->range(-32767, 32767);
            g_pModeSetBox->precision(0);

            //
            // Set the new limits for the slider.
            //
            g_pModeSetSlider->range(-32767, 32767);
            g_pModeSetSlider->step(1);

            //
            // Update the label for the set value box.
            //
            g_pModeSetBox->label("Value (RPM):");

            //
            // Get the speed setpoint.
            //
            strcpy(g_argv[0], "speed");
            strcpy(g_argv[1], "set");
            CmdSpeed(2, g_argv);

            //
            // Get the current ramp value.
            //
            g_pModeRamp->value(0.0);

            //
            // Get the current P value.
            //
            strcpy(g_argv[1], "p");
            CmdSpeed(2, g_argv);

            //
            // Get the current I value.
            //
            strcpy(g_argv[1], "i");
            CmdSpeed(2, g_argv);

            //
            // Get the current D value.
            //
            strcpy(g_argv[1], "d");
            CmdSpeed(2, g_argv);

            //
            // Update the reference radios.
            //
            strcpy(g_argv[1], "ref");
            CmdSpeed(2, g_argv);

            //
            // Select the encoder reference if no reference has been selected.
            //
            if((g_pModeEncoder->value() == 0) && (g_pModePot->value() == 0))
            {
                strcpy(g_argv[2], "0");
                CmdSpeed(3, g_argv);
                CmdSpeed(2, g_argv);
            }

            //
            // Done.
            //
            break;
        }

        //
        // Position control mode.
        //
        case LM_STATUS_CMODE_POS:
        {
            //
            // Deactivate unneeded GUI items.
            //
            g_pModeRamp->deactivate();

            //
            // Activate used GUI items.
            //
            g_pModeP->activate();
            g_pModeI->activate();
            g_pModeD->activate();
            g_pModeSetSlider->activate();
            g_pModeEncoder->activate();
            g_pModePot->activate();

            //
            // Update the label for the set value box.
            //
            g_pModeSetBox->label("Value:");

            //
            // Get the current ramp value.
            //
            g_pModeRamp->value(0.0);

            //
            // Get the current P value.
            //
            strcpy(g_argv[1], "p");
            CmdPosition(2, g_argv);

            //
            // Get the current I value.
            //
            strcpy(g_argv[1], "i");
            CmdPosition(2, g_argv);

            //
            // Get the current D value.
            //
            strcpy(g_argv[1], "d");
            CmdPosition(2, g_argv);

            //
            // Update the reference radios.
            //
            strcpy(g_argv[1], "ref");
            CmdPosition(2, g_argv);

            //
            // Select the encoder reference if no reference has been selected.
            //
            if((g_pModeEncoder->value() == 0) && (g_pModePot->value() == 0))
            {
                strcpy(g_argv[2], "0");
                CmdPosition(3, g_argv);
                CmdPosition(2, g_argv);
            }

            //
            // Update the position slider.
            //
            GUIUpdatePositionSlider();

            //
            // Get the position setpoint.
            //
            strcpy(g_argv[0], "pos");
            strcpy(g_argv[1], "set");
            CmdPosition(2, g_argv);

            //
            // Done.
            //
            break;
        }
    }
}

//*****************************************************************************
//
// Updates the status at the bottom of the GUI.
//
//*****************************************************************************
void
GUIConfigUpdate(void)
{
    //
    // Get the hardware version.
    //
    strcpy(g_argv[0], "system");
    strcpy(g_argv[1], "hwver");
    CmdSystem(2, g_argv);

    //
    // Get the firmware version
    //
    strcpy(g_argv[1], "version");
    CmdSystem(2, g_argv);

    //
    // Get the board information.
    //
    strcpy(g_argv[1], "query");
    CmdSystem(2, g_argv);

    //
    // Get the number of encoder lines.
    //
    strcpy(g_argv[0], "config");
    strcpy(g_argv[1], "lines");
    CmdConfig(2, g_argv);

    //
    // Get the number of POT turns.
    //
    strcpy(g_argv[1], "turns");
    CmdConfig(2, g_argv);

    //
    // Get the brake action.
    //
    strcpy(g_argv[1], "brake");
    CmdConfig(2, g_argv);

    //
    // Get the soft limit switch enable.
    //
    strcpy(g_argv[1], "limit");
    CmdConfig(2, g_argv);

    //
    // Get the forward soft limit setting.
    //
    strcpy(g_argv[1], "fwd");
    CmdConfig(2, g_argv);

    //
    // Get the forward reverse limit setting.
    //
    strcpy(g_argv[1], "rev");
    CmdConfig(2, g_argv);

    //
    // Get the maximum Vout.
    //
    strcpy(g_argv[1], "maxvout");
    CmdConfig(2, g_argv);

    //
    // Get the fault time.
    //
    strcpy(g_argv[1], "faulttime");
    CmdConfig(2, g_argv);
}

//*****************************************************************************
//
// This function handles connecting to the selected UART.
//
//*****************************************************************************
void
GUIConnect(void)
{
    int iIndex;

    //
    // Get the current selected item.
    //
    iIndex = g_pSelectCOM->value();

    //
    // Get the name of this COM port.
    //
#ifdef __WIN32
    snprintf(g_szCOMName, sizeof(g_szCOMName), "\\\\.\\COM%s",
             g_pSelectCOM->text(iIndex));
#else
    snprintf(g_szCOMName, sizeof(g_szCOMName), "/dev/%s",
             g_pSelectCOM->text(iIndex));
#endif

    //
    // Open the connection to the COM port.
    //
    if(OpenUART(g_szCOMName, 115200))
    {
        fl_alert("Could not connect to specified COM port.");
        return;
    }

    //
    // Indicate that the COM port is open.
    //
    g_bConnected = true;

    //
    // Update the menu with the status.
    //
    g_pMenuStatus->label("&Status: Connected");
    g_pMenuStatusButton->label("&Disconnect...");

    //
    // Enable device recovery, device ID assignment controls, and the enumerate
    // button whenever there is an open COM port.
    //
    g_pMenuRecover->activate();
    g_pSystemAssignValue->activate();
    g_pSystemAssignValue->value("1");
    g_pSystemAssign->activate();
    g_pSystemEnumerate->activate();

    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Remove all existing items in the 'Boards' drop down.
    //
    for(iIndex = 0; iIndex < g_pSelectBoard->size(); iIndex++)
    {
        //
        // Remove the item.
        //
        g_pSelectBoard->remove(iIndex);
    }

    //
    // Clear the drop down list (clean up after removing the items).
    //
    g_pSelectBoard->clear();

    //
    // Find the Jaguars in the network.
    //
    FindJaguars();

    //
    // Make sure that there are boards connected.
    //
    if(g_pSelectBoard->size() == 0)
    {
        //
        // Add a dummy entry to the board ID select.
        //
        g_pSelectBoard->add("--");
        g_pSelectBoard->value(0);

        //
        // Select the System tab.
        //
        g_pTabMode->hide();
        g_pTabConfiguration->hide();
        g_pTabSystem->show();

        //
        // Set the default board ID to 1.
        //
        g_ulID = 1;

        //
        // Force FLTK to redraw the UI.
        //
        Fl::redraw();

        //
        // Done.
        //
        return;
    }

    //
    // Get the ID of the first board in the chain and set as the active.
    //
    g_ulID = strtoul(g_pSelectBoard->text(0), 0, 0);

    //
    // Select the first item in the list by default.
    //
    g_pSelectBoard->value(0);

    //
    // Set the value of the board ID update field to the same as the
    // current selected item.
    //
    g_pSystemAssignValue->value(g_pSelectBoard->text(0));

    //
    // Update the configuration.
    //
    GUIConfigUpdate();

    //
    // Update the controls.
    //
    GUIControlUpdate();

    //
    // Force FLTK to redraw the UI.
    //
    Fl::redraw();

    //
    // Set the global flag for the board status thread to active.
    //
    g_ulBoardStatus = 1;
}

//*****************************************************************************
//
// This function handles disconnecting from the UART and closing down all
// connections.
//
//*****************************************************************************
void
GUIDisconnectAndClear(void)
{
    int iIndex;

    //
    // Indicate that there is no connection.
    //
    g_bConnected = false;

    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Wait some time to make sure all threads have stopped using the UART.
    //
    usleep(50000);

    //
    // Close the connection to the current COM port.
    //
    CloseUART();

    //
    // Update the menu with the status.
    //
    g_pMenuStatus->label("&Status: Disconnected");
    g_pMenuStatusButton->label("&Connect...");

    //
    // Zero out the board state structure.
    //
    for(iIndex = 0; iIndex < MAX_CAN_ID; iIndex++)
    {
        g_sBoardState[iIndex].ulControlMode = LM_STATUS_CMODE_VOLT;
    }

    //
    // Deactivate unneeded GUI items.
    //
    g_pSelectBoard->deactivate();
    g_pSelectMode->deactivate();
    g_pModeSync->deactivate();
    g_pModeSetBox->deactivate();
    g_pModeSetSlider->deactivate();
    g_pModeRamp->deactivate();
    g_pModeEncoder->deactivate();
    g_pModePot->deactivate();
    g_pModeP->deactivate();
    g_pModeI->deactivate();
    g_pModeD->deactivate();
    g_pConfigEncoderLines->deactivate();
    g_pConfigPOTTurns->deactivate();
    g_pConfigMaxVout->deactivate();
    g_pConfigFaultTime->deactivate();
    g_pConfigStopJumper->deactivate();
    g_pConfigStopBrake->deactivate();
    g_pConfigStopCoast->deactivate();
    g_pConfigLimitSwitches->deactivate();
    g_pConfigFwdLimitLt->deactivate();
    g_pConfigFwdLimitGt->deactivate();
    g_pConfigFwdLimitValue->deactivate();
    g_pConfigRevLimitLt->deactivate();
    g_pConfigRevLimitGt->deactivate();
    g_pConfigRevLimitValue->deactivate();
    g_pSystemAssignValue->deactivate();
    g_pSystemAssign->deactivate();
    g_pSystemHalt->deactivate();
    g_pSystemResume->deactivate();
    g_pSystemReset->deactivate();
    g_pSystemEnumerate->deactivate();
    g_pMenuUpdate->deactivate();
    g_pMenuRecover->deactivate();

    //
    // Choose voltage control mode.
    //
    g_pSelectMode->value(0);

    //
    // Update the label for the set value box.
    //
    g_pModeSetBox->label("Value (%):");

    //
    // Set new limits for the "Value" box.
    //
    g_pModeSetBox->range(-100, 100);
    g_pModeSetBox->precision(0);

    //
    // Set the new limits for the slider.
    //
    g_pModeSetSlider->range(-100, 100);
    g_pModeSetSlider->step(1);
    g_pModeSetSlider->value(0);

    //
    // Remove all existing items in the 'Boards' drop down.
    //
    for(iIndex = 0; iIndex < g_pSelectBoard->size(); iIndex++)
    {
        //
        // Remove the item.
        //
        g_pSelectBoard->remove(iIndex);
    }

    //
    // Clear the drop down list (clean up after removing the items).
    //
    g_pSelectBoard->clear();

    //
    // Add something showing that no boards are present.
    //
    g_pSelectBoard->add("--");
    g_pSelectBoard->value(0);

    //
    // Clear all of the items in the Mode tab and set to voltage mode
    // (default).
    //
    g_pModeSetBox->value(0.0);
    g_pModeSetSlider->value(0);
    g_pModeRamp->value(0.0);
    g_pModeP->value(0.0);
    g_pModeI->value(0.0);
    g_pModeD->value(0.0);

    //
    // Clear all of the items in the Config tab.
    //
    g_pConfigEncoderLines->value(0.0);
    g_pConfigPOTTurns->value(0.0);
    g_pConfigMaxVout->value(0.0);
    g_pConfigFaultTime->value(0.0);

    //
    // Clear all of the items in the System tab.
    //
    g_pSystemBoardInformation->value("");
    g_pSystemFirmwareVer->value(0);
    g_pSystemHardwareVer->value(0);
    g_pSystemAssignValue->value("");

    //
    // Clear all of the status boxes at the bottom of the screen.
    //
    g_pStatusVout->value(0);
    g_pStatusVbus->value(0);
    g_pStatusCurrent->value(0);
    g_pStatusTemperature->value(0);
    g_pStatusPosition->value(0);
    g_pStatusSpeed->value(0);
    g_pStatusPower->value(0);
    g_pStatusLimit->value(0);
}

//*****************************************************************************
//
// This function is used to update the menu status.
//
//*****************************************************************************
void
GUIMenuStatus(void)
{
    int iStatus;

    if(g_bConnected)
    {
        GUIDisconnectAndClear();
    }
    else
    {
        GUIConnect();
    }

    //
    // Re-draw the GUI widgets.
    //
    Fl::redraw();
}

//*****************************************************************************
//
// Handle drawing and filling the list of devices found.
//
//*****************************************************************************
void
GUIDropDownBoardID(void)
{
    char pcBuffer[16];
    int iIndex;

    iIndex = g_pSelectBoard->value();

    //
    // Check to see if we are connected to a board.
    //
    if(strcmp(g_pSelectBoard->text(iIndex), "--") == 0)
    {
    }
    else
    {
        //
        // Disable board status updates.
        //
        g_ulBoardStatus = 0;

        //
        // Wait for any pending board status updates.
        //
        while(g_bBoardStatusActive)
        {
            usleep(10000);
        }

        //
        // Get the current selected item.
        //
        iIndex = g_pSelectBoard->value();

        //
        // Get the ID of the newly selected item.
        //
        g_ulID = strtoul(g_pSelectBoard->text(iIndex), NULL, 10);

        //
        // Set the value of the board ID update field to the same as the
        // current selected item.
        //
        snprintf(pcBuffer, sizeof(pcBuffer), "%ld", g_ulID);
        g_pSystemAssignValue->value(pcBuffer);

        //
        // Refresh the parameters in the Mode tab.
        //
        GUIControlUpdate();

        //
        // Update the control mode.
        //
        g_pSelectMode->value(g_sBoardState[g_ulID].ulControlMode);

        //
        // Clear all of the status boxes at the bottom of the screen.
        //
        g_pStatusVout->value(0);
        g_pStatusVbus->value(0);
        g_pStatusCurrent->value(0);
        g_pStatusTemperature->value(0);
        g_pStatusPosition->value(0);
        g_pStatusSpeed->value(0);
        g_pStatusPower->value(0);

        //
        // Update the configuration.
        //
        GUIConfigUpdate();

        //
        // Re-draw the GUI to make sure there are no text side effects from the
        // previous mode.
        //
        Fl::redraw();

        //
        // Re-enable board status updates.
        //
        g_ulBoardStatus = 1;
    }
}

//*****************************************************************************
//
// Handle drawing the COM port drop down.
//
//*****************************************************************************
void
GUIDropDownCOMPort(void)
{
    int iIndex;

    //
    // Close the current port.
    //
    if(g_bConnected)
    {
        GUIDisconnectAndClear();
    }

    //
    // Open the new port.
    //
    GUIConnect();
}

//*****************************************************************************
//
// Handle drawing the current mode drop down.
//
//*****************************************************************************
void
GUIModeDropDownMode(void)
{
    int iIndex;

    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Update window for the new mode and enable it.
    //
    switch(g_pSelectMode->value())
    {
        case LM_STATUS_CMODE_VOLT:
        {
            //
            // Send the enable command.
            //
            strcpy(g_argv[0], "volt");
            strcpy(g_argv[1], "en");
            CmdVoltage(2, g_argv);

            break;
        }

        case LM_STATUS_CMODE_CURRENT:
        {
            //
            // Send the enable command.
            //
            strcpy(g_argv[0], "cur");
            strcpy(g_argv[1], "en");
            CmdCurrent(2, g_argv);

            break;
        }

        case LM_STATUS_CMODE_SPEED:
        {
            //
            // Send the enable command.
            //
            strcpy(g_argv[0], "speed");
            strcpy(g_argv[1], "en");
            CmdSpeed(2, g_argv);

            //
            // Set the speed reference to the encoder.
            //
            strcpy(g_argv[1], "ref");
            strcpy(g_argv[2], "0");
            CmdSpeed(3, g_argv);

            break;
        }

        case LM_STATUS_CMODE_POS:
        {
            //
            // Send the enable command.
            //
            strcpy(g_argv[0], "pos");
            strcpy(g_argv[1], "en");
            sprintf(g_argv[2], "%ld",
                    (long)(g_sBoardStatus.fPosition * 65536));
            CmdPosition(3, g_argv);

            break;
        }
    }

    //
    // Update the controls.
    //
    GUIControlUpdate();

    //
    // Re-draw the GUI to make sure there are no text side effects from the
    // previous mode.
    //
    Fl::redraw();

    //
    // Re-enable board status updates.
    //
    g_ulBoardStatus = 1;
}

//*****************************************************************************
//
// Handle the sync button.
//
//*****************************************************************************
void
GUIModeButtonSync(void)
{
    //
    // Toggle the synchronous update flag.
    //
    g_bSynchronousUpdate ^= 1;

    if(g_bSynchronousUpdate)
    {
        //
        // Change the button color to indicate that this feature is active.
        //
        g_pModeSync->color((Fl_Color)1);
        g_pModeSync->labelcolor((Fl_Color)7);
    }
    else
    {
        //
        // Change the button color back to it's normal state.
        //
        g_pModeSync->color((Fl_Color)49);
        g_pModeSync->labelcolor((Fl_Color)0);

        //
        // Send the command to do the update.
        //
        strcpy(g_argv[0], "system");
        strcpy(g_argv[1], "sync");
        sprintf(g_argv[2], "%d", 1);
        CmdSystem(3, g_argv);
    }
}

//*****************************************************************************
//
// Handle mode set changes.
//
//*****************************************************************************
void
GUIModeValueSet(int iBoxOrSlider)
{
    double dValue;
    float fIntegral;
    float fFractional;
    int iIntegral;
    int iFractional;
    int iValue;

    if(g_ulBoardStatus == 0)
    {
        return;
    }

    //
    // If the slider caused the interrupt, update the box value.
    //
    if(iBoxOrSlider)
    {
        //
        // Get the value from the slider.
        //
        dValue = g_pModeSetSlider->value();

        //
        // Update the box with the slider value.
        //
        g_pModeSetBox->value(dValue);
    }
    else
    {
        //
        // Get the value from the box.
        //
        dValue = g_pModeSetBox->value();

        //
        // Update the slider with the box value.
        //
        g_pModeSetSlider->value(dValue);
    }

    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Choose the mode.
    //
    switch(g_sBoardState[g_ulID].ulControlMode)
    {
        case LM_STATUS_CMODE_VOLT:
        {
            //
            // Change the percentage to a real voltage.
            //
            dValue = (dValue * 32767) / 100;

            //
            // Set up the variables for the "set" update.  If synchronous
            // update is active, send a group number with the new value.
            //
            if(g_bSynchronousUpdate)
            {
                sprintf(g_argv[3], "%d", 1);
            }

            strcpy(g_argv[0], "volt");
            strcpy(g_argv[1], "set");
            sprintf(g_argv[2], "%ld", (long)dValue);
            CmdVoltage(g_bSynchronousUpdate ? 4 : 3, g_argv);

            break;
        }

        case LM_STATUS_CMODE_CURRENT:
        {
            //
            // Put the desired value into the required format.
            //
            dValue *= 256;

            //
            // Set up the variables for the "set" update.  If synchronous
            // update is active, send a group number with the new value.
            //
            if(g_bSynchronousUpdate)
            {
                sprintf(g_argv[3], "%d", 1);
            }

            strcpy(g_argv[0], "cur");
            strcpy(g_argv[1], "set");
            sprintf(g_argv[2], "%ld", ((long)dValue));
            CmdCurrent(g_bSynchronousUpdate ? 4 : 3, g_argv);

            break;
        }

        case LM_STATUS_CMODE_SPEED:
        {
            //
            // Put the desired value into the required format.
            //
            dValue *= 65536;

            //
            // Set up the variables for the "set" update.  If synchronous
            // update is active, send a group number with the new value.
            //
            if(g_bSynchronousUpdate)
            {
                sprintf(g_argv[3], "%d", 1);
            }

            strcpy(g_argv[0], "speed");
            strcpy(g_argv[1], "set");
            sprintf(g_argv[2], "%ld", ((long)dValue));
            CmdSpeed(g_bSynchronousUpdate ? 4 : 3, g_argv);

            break;
        }

        case LM_STATUS_CMODE_POS:
        {
            //
            // Put the desired value into the required format.
            //
            dValue *= 65536;

            //
            // Set up the variables for the "set" update.  If synchronous
            // update is active, send a group number with the new value.
            //
            if(g_bSynchronousUpdate)
            {
                sprintf(g_argv[3], "%d", 1);
            }

            strcpy(g_argv[0], "pos");
            strcpy(g_argv[1], "set");
            sprintf(g_argv[2], "%ld", ((long)dValue));
            CmdPosition(g_bSynchronousUpdate ? 4 : 3, g_argv);

            break;
        }
    }

    //
    // Re-enable board status updates.
    //
    g_ulBoardStatus = 1;
}

//*****************************************************************************
//
// Handle the spinner control.
//
//*****************************************************************************
void
GUIModeSpinnerRamp(void)
{
    double dValue;

    if(g_ulBoardStatus == 0)
    {
        return;
    }

    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Get the value from the spinner box.
    //
    dValue = g_pModeRamp->value();

    //
    // Update the voltage ramp.
    //
    strcpy(g_argv[0], "volt");
    strcpy(g_argv[1], "ramp");
    sprintf(g_argv[2], "%ld", ((long)dValue));
    CmdVoltage(3, g_argv);

    //
    // Re-enable board status updates.
    //
    g_ulBoardStatus = 1;
}

//*****************************************************************************
//
// Handle the speed/position reference for the gui.
//
//*****************************************************************************
void
GUIModeRadioReference(int iChoice)
{
    if(g_ulBoardStatus == 0)
    {
        return;
    }

    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Send the command.
    //
    strcpy(g_argv[0], "pos");
    strcpy(g_argv[1], "ref");
    sprintf(g_argv[2], "%ld", ((long)iChoice));
    CmdPosition(3, g_argv);

    //
    // Update the position slider.
    //
    GUIUpdatePositionSlider();

    //
    // Re-enable board status updates.
    //
    g_ulBoardStatus = 1;
}

//*****************************************************************************
//
// Handle the PID settings.
//
//*****************************************************************************
void
GUIModeSpinnerPID(int iChoice)
{
    double dValue;

    if(g_ulBoardStatus == 0)
    {
        return;
    }

    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Choose the mode.
    //
    switch(g_sBoardState[g_ulID].ulControlMode)
    {
        //
        // Does not apply to voltage mode (should never get here).
        //
        case LM_STATUS_CMODE_VOLT:
        {
            break;
        }

        case LM_STATUS_CMODE_CURRENT:
        {
            strcpy(g_argv[0], "cur");
            break;
        }
        case LM_STATUS_CMODE_SPEED:
        {
            strcpy(g_argv[0], "speed");
            break;
        }
        case LM_STATUS_CMODE_POS:
        {
            strcpy(g_argv[0], "pos");
            break;
        }
    }

    //
    // Add the P, I or D command and the new value.
    //
    if(g_sBoardState[g_ulID].ulControlMode != LM_STATUS_CMODE_VOLT)
    {
        switch(iChoice)
        {
            case 0:
            {
                //
                // Get the value from the spinner box.
                //
                dValue = g_pModeP->value();

                //
                // Add the "P" command.
                //
                strcpy(g_argv[1], "p");

                break;
            }
            case 1:
            {
                //
                // Get the value from the spinner box.
                //
                dValue = g_pModeI->value();

                //
                // Add the "I" command.
                //
                strcpy(g_argv[1], "i");

                break;
            }
            case 2:
            {
                //
                // Get the value from the spinner box.
                //
                dValue = g_pModeD->value();

                //
                // Add the "D" command.
                //
                strcpy(g_argv[1], "d");

                break;
            }
        }

        //
        // Add the new value.
        //
        sprintf(g_argv[2], "%ld", (long)(dValue * 65536));
    }

    //
    // Send the command.
    //
    switch(g_sBoardState[g_ulID].ulControlMode)
    {
        //
        // Does not apply to voltage mode (should never get here).
        //
        case LM_STATUS_CMODE_VOLT:
        {
            break;
        }

        case LM_STATUS_CMODE_CURRENT:
        {
            CmdCurrent(3, g_argv);
            break;
        }
        case LM_STATUS_CMODE_SPEED:
        {
            CmdSpeed(3, g_argv);
            break;
        }
        case LM_STATUS_CMODE_POS:
        {
            CmdPosition(3, g_argv);
            break;
        }
    }

    //
    // Re-enable board status updates.
    //
    g_ulBoardStatus = 1;
}

//*****************************************************************************
//
// Handle the number of encoder lines setting.
//
//*****************************************************************************
void
GUIConfigSpinnerEncoderLines(void)
{
    double dValue;

    if(g_ulBoardStatus == 0)
    {
        return;
    }

    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Get the value from the spinner box.
    //
    dValue = g_pConfigEncoderLines->value();

    //
    // Send the command.
    //
    strcpy(g_argv[0], "config");
    strcpy(g_argv[1], "lines");
    sprintf(g_argv[2], "%ld", ((long)dValue));
    CmdConfig(3, g_argv);

    //
    // Re-enable board status updates.
    //
    g_ulBoardStatus = 1;
}

//*****************************************************************************
//
// Handle the number of potentiometer turns setting.
//
//*****************************************************************************
void
GUIConfigSpinnerPOTTurns(void)
{
    double dValue;

    if(g_ulBoardStatus == 0)
    {
        return;
    }

    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Get the value from the spinner box.
    //
    dValue = g_pConfigPOTTurns->value();

    //
    // Send the command.
    //
    strcpy(g_argv[0], "config");
    strcpy(g_argv[1], "turns");
    sprintf(g_argv[2], "%ld", ((long)dValue));
    CmdConfig(3, g_argv);

    //
    // Update the position mode slider/value.
    //
    GUIUpdatePositionSlider();

    //
    // Re-enable board status updates.
    //
    g_ulBoardStatus = 1;
}

//*****************************************************************************
//
// Handle the Maximum voltage out setting.
//
//*****************************************************************************
void
GUIConfigSpinnerMaxVout(void)
{
    double dValue;

    if(g_ulBoardStatus == 0)
    {
        return;
    }

    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Get the value from the spinner box.
    //
    dValue = g_pConfigMaxVout->value();

    //
    // Send the command.
    //
    strcpy(g_argv[0], "config");
    strcpy(g_argv[1], "maxvout");
    sprintf(g_argv[2], "%ld", ((long)((dValue * 0xc00) / 100)));
    CmdConfig(3, g_argv);

    g_dMaxVout = dValue;

    //
    // Re-enable board status updates.
    //
    g_ulBoardStatus = 1;
}

//*****************************************************************************
//
// Handle the Fault time out setting.
//
//*****************************************************************************
void
GUIConfigSpinnerFaultTime(void)
{
    double dValue;

    if(g_ulBoardStatus == 0)
    {
        return;
    }

    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Get the value from the spinner box.
    //
    dValue = g_pConfigFaultTime->value();

    //
    // Send the command.
    //
    strcpy(g_argv[0], "config");
    strcpy(g_argv[1], "faulttime");
    sprintf(g_argv[2], "%ld", ((long)dValue));
    CmdConfig(3, g_argv);

    //
    // Re-enable board status updates.
    //
    g_ulBoardStatus = 1;
}

//*****************************************************************************
//
// Handle the braking mode setting.
//
//*****************************************************************************
void
GUIConfigRadioStopAction(int iChoice)
{
    static const char *ppucChoices[] = { "jumper", "brake", "coast" };

    if(g_ulBoardStatus == 0)
    {
        return;
    }

    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Send the command.
    //
    strcpy(g_argv[0], "config");
    strcpy(g_argv[1], "brake");
    strcpy(g_argv[2], ppucChoices[iChoice]);
    CmdConfig(3, g_argv);

    //
    // Re-enable board status updates.
    //
    g_ulBoardStatus = 1;
}

//*****************************************************************************
//
// Handle the limit mode switch settings.
//
//*****************************************************************************
void
GUIConfigCheckLimitSwitches(void)
{
    double dValue;
    double dFwdLimit;
    double dRevLimit;

    if(g_ulBoardStatus == 0)
    {
        return;
    }

    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Get the value from the spinner box.
    //
    dValue = g_pConfigLimitSwitches->value();

    //
    // Depending on the value, enable or disable soft limit items.
    //
    if(dValue)
    {
        g_pConfigFwdLimitLt->activate();
        g_pConfigFwdLimitGt->activate();
        g_pConfigFwdLimitValue->activate();
        g_pConfigRevLimitLt->activate();
        g_pConfigRevLimitGt->activate();
        g_pConfigRevLimitValue->activate();
    }
    else
    {
        g_pConfigFwdLimitLt->deactivate();
        g_pConfigFwdLimitGt->deactivate();
        g_pConfigFwdLimitValue->deactivate();
        g_pConfigRevLimitLt->deactivate();
        g_pConfigRevLimitGt->deactivate();
        g_pConfigRevLimitValue->deactivate();
    }

    //
    // Send the command.
    //
    strcpy(g_argv[0], "config");
    strcpy(g_argv[1], "limit");
    sprintf(g_argv[2], dValue ? "on" : "off");
    CmdConfig(3, g_argv);

    //
    // Update the position slider.
    //
    GUIUpdatePositionSlider();

    //
    // Re-enable board status updates.
    //
    g_ulBoardStatus = 1;
}

//*****************************************************************************
//
// Handle the forward limit setting.
//
//*****************************************************************************
void
GUIConfigValueFwdLimit(void)
{
    double dValue;
    double dRevLimit;
    long lLtGt;

    if(g_ulBoardStatus == 0)
    {
        return;
    }

    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Figure out which item is selected.
    //
    if(g_pConfigFwdLimitLt->value())
    {
        lLtGt = 0;
    }
    else
    {
        lLtGt = 1;
    }

    //
    // Get the position value.
    //
    dValue = g_pConfigFwdLimitValue->value();

    //
    // Send the command.
    //
    strcpy(g_argv[0], "config");
    strcpy(g_argv[1], "fwd");
    sprintf(g_argv[2], "%ld", ((long)(dValue * 65536)));
    sprintf(g_argv[3], lLtGt ? "lt" : "gt");
    CmdConfig(4, g_argv);

    //
    // Update the position slider.
    //
    GUIUpdatePositionSlider();

    //
    // Re-enable board status updates.
    //
    g_ulBoardStatus = 1;
}

//*****************************************************************************
//
// Handle the reverse limit setting.
//
//*****************************************************************************
void
GUIConfigValueRevLimit(void)
{
    double dValue;
    double dFwdLimit;
    long lLtGt;

    if(g_ulBoardStatus == 0)
    {
        return;
    }

    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Figure out which item is selected.
    //
    if(g_pConfigRevLimitLt->value())
    {
        lLtGt = 0;
    }
    else
    {
        lLtGt = 1;
    }

    //
    // Get the position value.
    //
    dValue = g_pConfigRevLimitValue->value();

    //
    // Send the command.
    //
    strcpy(g_argv[0], "config");
    strcpy(g_argv[1], "rev");
    sprintf(g_argv[2], "%ld", ((long)(dValue * 65536)));
    sprintf(g_argv[3], lLtGt ? "lt" : "gt");
    CmdConfig(4, g_argv);

    //
    // Update the position slider.
    //
    GUIUpdatePositionSlider();

    //
    // Re-enable board status updates.
    //
    g_ulBoardStatus = 1;
}

//*****************************************************************************
//
// Handle the system assignment setting.
//
//*****************************************************************************
void
GUISystemAssignValue(void)
{
    unsigned long ulValue;

    if(g_ulBoardStatus == 0)
    {
        return;
    }

    //
    // Get the current value of the box.
    //
    ulValue = strtoul(g_pSystemAssignValue->value(), 0, 0);

    //
    // Make sure that the value is valid.
    //
    if(ulValue < 1)
    {
        g_pSystemAssignValue->value("1");
    }
    if(ulValue > 63)
    {
        g_pSystemAssignValue->value("63");
    }
}

//*****************************************************************************
//
// Handle the assignment button.
//
//*****************************************************************************
void
GUISystemButtonAssign(void)
{
    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Disable the previous mode.
    //
    switch(g_sBoardState[g_ulID].ulControlMode)
    {
        case LM_STATUS_CMODE_VOLT:
        {
            strcpy(g_argv[0], "volt");
            strcpy(g_argv[1], "dis");
            CmdVoltage(2, g_argv);
            break;
        }

        case LM_STATUS_CMODE_CURRENT:
        {
            strcpy(g_argv[0], "cur");
            strcpy(g_argv[1], "dis");
            CmdCurrent(2, g_argv);
            break;
        }

        case LM_STATUS_CMODE_SPEED:
        {
            strcpy(g_argv[0], "speed");
            strcpy(g_argv[1], "dis");
            CmdSpeed(2, g_argv);
            break;
        }

        case LM_STATUS_CMODE_POS:
        {
            strcpy(g_argv[0], "pos");
            strcpy(g_argv[1], "dis");
            CmdPosition(2, g_argv);
            break;
        }
    }

    //
    // Send the command.
    //
    strcpy(g_argv[0], "system");
    strcpy(g_argv[1], "assign");
    strcpy(g_argv[2], g_pSystemAssignValue->value());
    CmdSystem(3, g_argv);

    //
    // Re-enumerate.
    //
    GUISystemButtonEnumerate();
}

//*****************************************************************************
//
// Handle the halt button.
//
//*****************************************************************************
void
GUISystemButtonHalt(void)
{
    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Send the command.
    //
    strcpy(g_argv[0], "system");
    strcpy(g_argv[1], "halt");
    CmdSystem(2, g_argv);

    //
    // Re-enable board status updates.
    //
    g_ulBoardStatus = 1;
}

//*****************************************************************************
//
// Handle the resume button.
//
//*****************************************************************************
void
GUISystemButtonResume(void)
{
    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Send the command.
    //
    strcpy(g_argv[0], "system");
    strcpy(g_argv[1], "resume");
    CmdSystem(2, g_argv);

    //
    // Re-enable board status updates.
    //
    g_ulBoardStatus = 1;
}

//*****************************************************************************
//
// Handle the system reset button.
//
//*****************************************************************************
void
GUISystemButtonReset(void)
{
    int iIndex;

    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Send the command.
    //
    strcpy(g_argv[0], "system");
    strcpy(g_argv[1], "reset");
    CmdSystem(2, g_argv);

    //
    // Clear the trust structure trusted flags.
    //
    for(iIndex = 0; iIndex < MAX_CAN_ID; iIndex++)
    {
        g_sBoardState[iIndex].ulControlMode = LM_STATUS_CMODE_VOLT;
    }

    //
    // Re-enable board status updates.
    //
    g_ulBoardStatus = 1;

    //
    // Disconnect the GUI.
    //
    GUIDisconnectAndClear();

    //
    // Re-draw the GUI widgets.
    //
    Fl::redraw();
}

//*****************************************************************************
//
// Handle the enumerate button.
//
//*****************************************************************************
void
GUISystemButtonEnumerate(void)
{
    //
    // Disconnect.
    //
    GUIDisconnectAndClear();

    //
    // Re-open the communication port, which will re-enumerate the bus.
    //
    GUIConnect();
}

//*****************************************************************************
//
// Handle the heart beat enable/disable button.
//
//*****************************************************************************
void
GUISystemCheckHeartbeat(void)
{
    g_ulHeartbeat = (g_pSystemHeartbeat->value() ? 1 : 0);
}

//*****************************************************************************
//
// Handle the firmware update button.
//
//*****************************************************************************
void
GUIUpdateFirmware(void)
{
    //
    // Produce an error and return without updating if no firmware filename has
    // been specified.
    //
    if(strlen(g_pathname) == 0)
    {
        g_pFirmwareUpdateWindow->hide();
        delete g_pFirmwareUpdateWindow;
        g_pFirmwareUpdateWindow = 0;
        fl_alert("No firmware was specified");
        Fl::check();
        return;
    }

    //
    // Show the progress bar.
    //
    g_pUpdateProgress->show();

    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Update the firmware.
    //
    strcpy(g_argv[0], "update");
    strcpy(g_argv[1], g_pathname);
    CmdUpdate(2, g_argv);

    //
    // Kill the firmware update window.
    //
    g_pFirmwareUpdateWindow->hide();
    delete g_pFirmwareUpdateWindow;
    g_pFirmwareUpdateWindow = 0;

    //
    // Disconnect from the board.
    //
    GUIDisconnectAndClear();

    //
    // Wait some time for the board to reset.
    //
    OSSleep(1);

    //
    // Reconnect.
    //
    GUIConnect();
}

//*****************************************************************************
//
// Handle the recover device button.
//
//*****************************************************************************
void
GUIRecoverDevice(void)
{
    //
    // Produce an error and return without updating if no firmware filename has
    // been specified.
    //
    if(strlen(g_pathname) == 0)
    {
        g_pRecoverDeviceWindow->hide();
        delete g_pRecoverDeviceWindow;
        g_pRecoverDeviceWindow = 0;
        fl_alert("No firmware was specified");
        Fl::check();
        return;
    }

    //
    // Show the progress bar.
    //
    g_pRecoverProgress->show();

    //
    // Disable board status updates.
    //
    g_ulBoardStatus = 0;

    //
    // Wait for any pending board status updates.
    //
    while(g_bBoardStatusActive)
    {
        usleep(10000);
    }

    //
    // Set the device ID to zero in order to perform a recovery.
    //
    g_ulID = 0;

    //
    // Update the firmware.
    //
    strcpy(g_argv[0], "update");
    strcpy(g_argv[1], g_pathname);
    CmdUpdate(2, g_argv);

    //
    // Kill the firmware update window.
    //
    g_pRecoverDeviceWindow->hide();
    delete g_pRecoverDeviceWindow;
    g_pRecoverDeviceWindow = 0;

    //
    // Disconnect from the board.
    //
    GUIDisconnectAndClear();

    //
    // Wait some time for the board to reset.
    //
    OSSleep(1);

    //
    // Reconnect.
    //
    GUIConnect();
}
