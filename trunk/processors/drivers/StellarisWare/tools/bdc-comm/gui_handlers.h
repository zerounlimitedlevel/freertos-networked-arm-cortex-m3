//*****************************************************************************
//
// gui_handlers.h - Prototypes for the GUI handler functions.
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

#ifndef __GUI_HANDLERS_H__
#define __GUI_HANDLERS_H__

//*****************************************************************************
//
// This structure holds the current status for a device.
//
//*****************************************************************************
typedef struct
{
    float   fVout;
    float   fVbus;
    long    lFault;
    float   fCurrent;
    float   fTemperature;
    float   fPosition;
    float   fSpeed;
    char    pcLimit[8];
    long    lPower;
}
tBoardStatus;

//*****************************************************************************
//
// This structure type is used to hold state information about a board.
//
//*****************************************************************************
typedef struct
{
    unsigned long ulControlMode;
}
tBoardState;

extern int GUIFillCOMPortDropDown(void);
extern void GUIConnect(void);
extern void GUIDisconnectAndClear(void);
extern void GUIRefreshControlParameters(void);
extern void GUIMenuUpdateFirmware(void);
extern void GUIMenuStatus(void);
extern void GUIDropDownBoardID(void);
extern void GUIDropDownCOMPort(void);
extern void GUIButtonRun(void);
extern void GUIButtonStop(void);
extern void GUIModeDropDownMode(void);
extern void GUIModeButtonSync(void);
extern void GUIModeValueSet(int iBoxOrSlider);
extern void GUIModeSpinnerRamp(void);
extern void GUIModeRadioReference(int iChoice);
extern void GUIModeSpinnerPID(int iChoice);
extern void GUIConfigSpinnerEncoderLines(void);
extern void GUIConfigSpinnerPOTTurns(void);
extern void GUIConfigSpinnerMaxVout(void);
extern void GUIConfigSpinnerFaultTime(void);
extern void GUIConfigRadioStopAction(int iChoice);
extern void GUIConfigCheckLimitSwitches(void);
extern void GUIConfigValueFwdLimit(void);
extern void GUIConfigValueRevLimit(void);
extern void GUISystemAssignValue(void);
extern void GUISystemButtonAssign(void);
extern void GUISystemButtonHalt(void);
extern void GUISystemButtonResume(void);
extern void GUISystemButtonReset(void);
extern void GUISystemButtonEnumerate(void);
extern void GUISystemCheckHeartbeat(void);
extern void GUIMenuUpdateFirmware(void);
extern void GUIUpdateFirmware(void);
extern void GUIRecoverDevice(void);

extern tBoardStatus g_sBoardStatus;
extern tBoardState g_sBoardState[];

#endif
