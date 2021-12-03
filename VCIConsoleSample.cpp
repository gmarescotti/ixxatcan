//////////////////////////////////////////////////////////////////////////
// HMS Technology Center Ravensburg GmbH
//////////////////////////////////////////////////////////////////////////
/**

  Demo application for the IXXAT interface based VCI-API.

  @note
    This demo demonstrates the following VCI features
    - controller selection
    - controller initialization
    - creation of a message channel
    - transmission / reception of CAN messages

*/
//////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016
// HMS Technology Center Ravensburg GmbH, all rights reserved
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// include files
//////////////////////////////////////////////////////////////////////////
#include "vcisdk.h"

#include <process.h>
#include <stdio.h>
#include <conio.h>
// #include "SocketSelectDlg.hpp"

//////////////////////////////////////////////////////////////////////////
// global variables
//////////////////////////////////////////////////////////////////////////

static IBalObject*    pBalObject = 0;     // bus access object

static LONG           lSocketNo  = 0;     // socket number
static LONG           lBusCtlNo  = 0;     // controller number

static ICanControl*   pCanControl = 0;    // control interface
static ICanChannel*   pCanChn = 0;        // channel interface

static LONG           lMustQuit = 0;      // quit flag for the receive thread

static HANDLE         hEventReader = 0;
static PFIFOREADER    pReader = 0;

static PFIFOWRITER    pWriter = 0;

//////////////////////////////////////////////////////////////////////////
// function prototypes
//////////////////////////////////////////////////////////////////////////

HRESULT SelectDevice ( BOOL fUserSelect );

HRESULT CheckBalFeatures(LONG lCtrlNo);
HRESULT InitSocket      (LONG lCtrlNo);

void    FinalizeApp  ( void );

void    TransmitViaPutDataEntry();
void    TransmitViaWriter();

void    ReceiveThread( void* Param );

//////////////////////////////////////////////////////////////////////////
/**
  Main entry point of the application.
*/
//////////////////////////////////////////////////////////////////////////
void main ( )
{
  HRESULT hResult;

  printf(" >>>> VCI - C-API Example V2.1 <<<<\n");
  printf("\n Initializes the CAN with 125 kBaud");
  printf("\n Shows all received messages");
  printf("\n 't'-key sends message with ID 100H via PutDataEntry()");
  printf("\n 'w'-key sends message with ID 200H via FIFO interface");
  printf("\n 'Esc'-key quit the application\n");

  printf("\n Select Adapter...");  
  hResult = SelectDevice( TRUE );

  if ( VCI_OK == hResult )
  {
    printf("\n Select Adapter.......... OK !");

    // This step is not necessary but shows how to get/check BAL features
    hResult = CheckBalFeatures( lBusCtlNo );
    if ( VCI_OK == hResult )
    {
      printf("\n CheckBalFeatures......... OK !");
    }
    else
    {
      printf("\n CheckBalFeatures......... 0x%08lX !", hResult);
    }

    printf("\n Initialize CAN...");
    hResult = InitSocket( lBusCtlNo );

    if ( VCI_OK == hResult )
    {
      printf("\n Initialize CAN............ OK !");
      
      //
      // start the receive thread
      //
      _beginthread( ReceiveThread, 0, NULL);

      //
      // wait for keyboard hit
      //
      while (1)
      { 
        // wait for the user to press a key
        int chKey = _getch();

        // when the key is 't' or 'T' the send a CAN message
        if ( (chKey == 't') || (chKey == 'T') )
          TransmitViaPutDataEntry();

        if ( (chKey == 'w') || (chKey == 'W') )
          TransmitViaWriter();

        // when the user press the ESC key then end the program
        if (chKey == VK_ESCAPE)
          break;

        Sleep(1);
      } 

      //
      // tell receive thread to quit
      //
      InterlockedExchange(&lMustQuit, 1);
    }
  }

  printf("\n Free VCI - Resources...");
  FinalizeApp();
  printf("\n Free VCI - Resources........ OK !");
}

//////////////////////////////////////////////////////////////////////////
/**
  Selects the first CAN adapter.

  @param fUserSelect
    If this parameter is set to TRUE the functions display a dialog box which 
    allows the user to select the device.

  @return
    VCI_OK on success, otherwise an Error code
*/
//////////////////////////////////////////////////////////////////////////
HRESULT SelectDevice( /*BOOL fUserSelect*/ )
{
  HRESULT hResult; // error code

//  if (fUserSelect == FALSE)
//  {
    IVciDeviceManager*  pDevMgr = 0;    // device manager
    IVciEnumDevice*     pEnum   = 0;    // enumerator handle
    VCIDEVICEINFO       sInfo;          // device info

    hResult = VciGetDeviceManager(&pDevMgr);
    if (hResult == VCI_OK)
    {
      hResult = pDevMgr->EnumDevices(&pEnum);
    }

    //
    // retrieve information about the first
    // device within the device list
    //
    if (hResult == VCI_OK)
    {
      hResult = pEnum->Next(1, &sInfo, NULL);

      //
      // close the device list (no longer needed)
      //
      pEnum->Release();
      pEnum = NULL;
    }

    //
    // open the first device via device manager and get the bal object
    //
    if (hResult == VCI_OK)
    {
      IVciDevice* pDevice;
      hResult = pDevMgr->OpenDevice(sInfo.VciObjectId, &pDevice);

      if (hResult == VCI_OK)
      {
        hResult = pDevice->OpenComponent(CLSID_VCIBAL, IID_IBalObject, (void**)&pBalObject);

        pDevice->Release();
      }
    }

    //
    // always select controller 0
    //
    lBusCtlNo = 0;

    //
    // close device manager
    //
    if (pDevMgr)
    {
      pDevMgr->Release();
      pDevMgr = NULL;
    }
//  }
//  else
//  {
//    //
//    // open a device selected by the user
//    //
//    hResult = SocketSelectDlg(NULL, VCI_BUS_CAN, &pBalObject, &lSocketNo, &lBusCtlNo);
//  }

//  DisplayError(NULL, hResult);
  return hResult;
}


//////////////////////////////////////////////////////////////////////////
/**

  Checks BAL features 

  @param lCtrlNo 
    controller number to check the features

  @return
    VCI_OK on success, otherwise an Error code

*/////////////////////////////////////////////////////////////////////////
HRESULT CheckBalFeatures(LONG lCtrlNo)
{
  HRESULT hResult = E_FAIL;

  if (pBalObject)
  {
    // check if controller supports CANFD
    BALFEATURES features = { 0 };
    hResult = pBalObject->GetFeatures(&features);
    if (VCI_OK == hResult)
    {
      // check if controller number is valid
      if (lCtrlNo >= features.BusSocketCount)
      {
        // As we select the controller via the selection dialog, we should never get here.
        printf("\n Invalid controller number. !");
        return VCI_E_UNEXPECTED;
      }

      // check for the expected controller type
      if (VCI_BUS_TYPE(features.BusSocketType[lCtrlNo]) != VCI_BUS_CAN)
      {
        // Invalid controller type selected
        printf("\n Invalid controller type selected !");
        return VCI_E_UNEXPECTED;
      }
    }
    else
    {
      printf("\n pBalObject->GetFeatures failed: 0x%08lX !", hResult);
    }
  }

  return hResult;
}


//////////////////////////////////////////////////////////////////////////
/**
  Opens the specified socket, creates a message channel, initializes
  and starts the CAN controller.

  @param dwCanNo
    Number of the CAN controller to open.

  @return
    VCI_OK on success, otherwise an Error code

  @note
    If <dwCanNo> is set to 0xFFFFFFFF, the function shows a dialog box
    which allows the user to select the VCI device and CAN controller.
*/
//////////////////////////////////////////////////////////////////////////
HRESULT InitSocket(LONG lCtrlNo)
{
  HRESULT hResult = E_FAIL;

  if (pBalObject != NULL)
  {
    //
    // check controller capabilities create a message channel
    //
    ICanSocket* pCanSocket = 0;
    hResult = pBalObject->OpenSocket(lCtrlNo, IID_ICanSocket, (void**)&pCanSocket);
    if (hResult == VCI_OK)
    {
      // check capabilities
      CANCAPABILITIES capabilities = { 0 };
      hResult = pCanSocket->GetCapabilities(&capabilities);
      if (VCI_OK == hResult)
      {
        //
        // This sample expects that standard and extended mode are
        // supported simultaneously. See use of
        // CAN_OPMODE_STANDARD | CAN_OPMODE_EXTENDED in InitLine() below
        //
        if (capabilities.dwFeatures & CAN_FEATURE_STDANDEXT)
        {
          // supports simultaneous standard and extended -> ok
        }
        else
        {
          printf("\n Simultaneous standard and extended mode feature not supported !");
          hResult = VCI_E_NOT_SUPPORTED;
        }
      }
      else
      {
        // should not occurr
        printf("\n pCanSocket->GetCapabilities failed: 0x%08lX !", hResult);
      }

      //
      // create a message channel
      //
      if (VCI_OK == hResult)      
      {
        hResult = pCanSocket->CreateChannel(FALSE, &pCanChn);
      }

      pCanSocket->Release();
    }

    //
    // initialize the message channel
    //
    if (hResult == VCI_OK)
    {
      UINT16 wRxFifoSize  = 1024;
      UINT16 wRxThreshold = 1;
      UINT16 wTxFifoSize  = 128;
      UINT16 wTxThreshold = 1;

      hResult = pCanChn->Initialize(wRxFifoSize, wTxFifoSize);
      if (hResult == VCI_OK)
      {
        hResult = pCanChn->GetReader(&pReader);
        if (hResult == VCI_OK)
        {
          pReader->SetThreshold(wRxThreshold);

          hEventReader = CreateEvent(NULL, FALSE, FALSE, NULL);
          pReader->AssignEvent(hEventReader);
        }
      }

      if (hResult == VCI_OK)
      {
        hResult = pCanChn->GetWriter(&pWriter);
        if (hResult == VCI_OK)
        {
          pWriter->SetThreshold(wTxThreshold);
        }
      }
    }

    //
    // activate the CAN channel
    //
    if (hResult == VCI_OK)
    {
      hResult = pCanChn->Activate();
    }

    //
    // Open the CAN control interface
    //
    // During the programs lifetime we have multiple options:
    // 1) Open the control interface and keep it open
    //     -> No other programm is able to get the control interface and change the line settings
    // 2) Try to get the control interface and change the settings only when we get it
    //     -> Other programs can change the settings by getting the control interface
    //
    if (hResult == VCI_OK)
    {
      hResult = pBalObject->OpenSocket(lCtrlNo, IID_ICanControl, (void**)&pCanControl);

      //
      // initialize the CAN controller
      //
      if (hResult == VCI_OK)
      {
        CANINITLINE init =  {
          CAN_OPMODE_STANDARD | 
          CAN_OPMODE_EXTENDED | CAN_OPMODE_ERRFRAME,      // opmode
          0,                                              // bReserved
		  CAN_BT0_500KB, CAN_BT1_500KB                    // bt0, bt1
		  // CAN_BT0_125KB, CAN_BT1_125KB                    // bt0, bt1
        };

        hResult = pCanControl->InitLine(&init);
        if (hResult != VCI_OK)
        {
          printf("\n pCanControl->InitLine failed: 0x%08lX !", hResult);
        }

        //
        // set the acceptance filter
        //
        if (hResult == VCI_OK)
        { 
          hResult = pCanControl->SetAccFilter(CAN_FILTER_STD, CAN_ACC_CODE_ALL, CAN_ACC_MASK_ALL);

          //
          // set the acceptance filter
          //
          if (hResult == VCI_OK)
          { 
            hResult = pCanControl->SetAccFilter(CAN_FILTER_EXT, CAN_ACC_CODE_ALL, CAN_ACC_MASK_ALL);
          }

          if (VCI_OK != hResult)
          {
            printf("\n pCanControl->SetAccFilter failed: 0x%08lX !", hResult);
          }

          //
          // SetAccFilter() returns VCI_E_INVALID_STATE if already controller is started. 
          // We ignore this because the controller could already be started
          // by another application.
          //
          if (VCI_E_INVALID_STATE == hResult)
          {
            hResult = VCI_OK;
          }
        }

        //
        // start the CAN controller
        //
        if (hResult == VCI_OK)
        {
          hResult = pCanControl->StartLine();
          if (hResult != VCI_OK)
          {
            printf("\n pCanControl->StartLine failed: 0x%08lX !", hResult);
          }
        }

        printf("\n Got Control interface. Settings applied !");
      }
      else
      {
        //
        // If we can't get the control interface it is occupied by another application.
        // This means the application is in charge of the controller parameters.
        // We live with it and move on.
        // 
        printf("\n Control interface occupied. Settings not applied: 0x%08lX !", hResult);
        hResult = VCI_OK;
      }
    }
  }
  else
  {
    hResult = VCI_E_INVHANDLE;
  }

//  DisplayError(NULL, hResult);
  return hResult;
}

//////////////////////////////////////////////////////////////////////////
/**

  Transmit message via PutDataEntry

*/////////////////////////////////////////////////////////////////////////
void TransmitViaPutDataEntry()
{
  CANMSG  sCanMsg = { 0 };

  // length of message payload
  UINT payloadLen = 8;

  sCanMsg.dwTime   = 0;
  sCanMsg.dwMsgId  = 0x100;    // CAN message identifier

  sCanMsg.uMsgInfo.Bytes.bType   = CAN_MSGTYPE_DATA;
  // Flags:
  // srr = 1
  sCanMsg.uMsgInfo.Bytes.bFlags  = CAN_MAKE_MSGFLAGS(CAN_LEN_TO_SDLC(payloadLen), 0, 1, 0, 0);
  // Flags2:
  // Set bFlags2 to 0
  sCanMsg.uMsgInfo.Bytes.bFlags2 = CAN_MAKE_MSGFLAGS2(0, 0, 0, 0, 0);

  for (UINT i = 0; i < payloadLen; i++ )
  {
    sCanMsg.abData[i] = i;
  }

  // write a single CAN message into the transmit FIFO
  while (VCI_E_TXQUEUE_FULL == pWriter->PutDataEntry(&sCanMsg))
  {
    Sleep(1);
  }
}

//////////////////////////////////////////////////////////////////////////
/**

  Transmit CAN via Writer API (AcquireWrite/ReleaseWrite)
   with ID 0x100.

*/////////////////////////////////////////////////////////////////////////
void TransmitViaWriter()
{
  // use the FIFO interface 
  // to write multiple messages
  UINT16  count = 0;
  PCANMSG pMsg;

  // length of message payload
  UINT payloadLen = 8;

  // aquire write access to FIFO
  HRESULT hr = pWriter->AcquireWrite((void**)&pMsg, &count);
  if (VCI_OK == hr)
  {
    // number of written messages needed for ReleaseWrite
    UINT16 written = 0;

    if (count > 0)
    {
      pMsg->dwTime  = 0;
      pMsg->dwMsgId = 0x200;

      pMsg->uMsgInfo.Bytes.bType   = CAN_MSGTYPE_DATA;
      // Flags:
      // srr = 1
      pMsg->uMsgInfo.Bytes.bFlags  = CAN_MAKE_MSGFLAGS (CAN_LEN_TO_SDLC(payloadLen), 0, 1, 0, 0);
      // Flags2:
      // Set bFlags2 to 0 because FIFO memory will not be initialized by AquireWrite
      pMsg->uMsgInfo.Bytes.bFlags2 = CAN_MAKE_MSGFLAGS2(0, 0, 0, 0, 0);

      for (UINT i = 0; i < payloadLen; i++ )
      {
        pMsg->abData[i] = i;
      }

      written = 1;
    }

    // release write access to FIFO
    hr = pWriter->ReleaseWrite(written);
    if (VCI_OK != hr)
    {
      printf("\nReleaseWrite failed: 0x%08lX", hr);
    }
  }
  else
  {
    printf("\nAcquireWrite failed: 0x%08lX", hr);
  }
}

//////////////////////////////////////////////////////////////////////////
/**

  Print a message

*/
//////////////////////////////////////////////////////////////////////////
void PrintMessage(PCANMSG pCanMsg)
{
  if (pCanMsg->uMsgInfo.Bytes.bType == CAN_MSGTYPE_DATA)
  {
    //
    // show data frames
    //
    if (pCanMsg->uMsgInfo.Bits.rtr == 0)
    {
      UINT j;

      // number of bytes in message payload
      UINT payloadLen = CAN_SDLC_TO_LEN(pCanMsg->uMsgInfo.Bits.dlc);

      printf("\nTime: %10u  ID: %3X %s  Len: %1u  Data:",
        pCanMsg->dwTime,
        pCanMsg->dwMsgId,
        (pCanMsg->uMsgInfo.Bits.ext == 1) ? "Ext" : "Std",
        payloadLen);

      // print payload bytes
      for (j = 0; j < payloadLen; j++)
      {
        printf(" %.2X", pCanMsg->abData[j]);
      }
    }
    else
    {
      printf("\nTime: %10u ID: %3X  DLC: %1u  Remote Frame",
        pCanMsg->dwTime,
        pCanMsg->dwMsgId,
        pCanMsg->uMsgInfo.Bits.dlc);
    }
  }
  else if (pCanMsg->uMsgInfo.Bytes.bType == CAN_MSGTYPE_INFO)
  {
    //
    // show informational frames
    //
    switch (pCanMsg->abData[0])
    {
      case CAN_INFO_START: printf("\nCAN started..."); break;
      case CAN_INFO_STOP : printf("\nCAN stopped..."); break;
      case CAN_INFO_RESET: printf("\nCAN reseted..."); break;
    }
  }
  else if (pCanMsg->uMsgInfo.Bytes.bType == CAN_MSGTYPE_ERROR)
  {
    //
    // show error frames
    //
    switch (pCanMsg->abData[0])
    {
      case CAN_ERROR_STUFF: printf("\nstuff error...");          break; 
      case CAN_ERROR_FORM : printf("\nform error...");           break; 
      case CAN_ERROR_ACK  : printf("\nacknowledgment error..."); break;
      case CAN_ERROR_BIT  : printf("\nbit error...");            break; 
      case CAN_ERROR_CRC  : printf("\nCRC error...");            break; 
      case CAN_ERROR_OTHER:
      default             : printf("\nother error...");          break;
    }
  }
}

//////////////////////////////////////////////////////////////////////////
/**

  Process messages

  @param wLimit  max number of messages to process

  @return VCI_OK if more messages (may be) available

*/
//////////////////////////////////////////////////////////////////////////
HRESULT ProcessMessages(WORD wLimit)
{
  // parameter checking
  if (!pReader) return E_UNEXPECTED;

  PCANMSG pCanMsg;

  // check if messages available
  UINT16  wCount = 0;
  HRESULT hr = pReader->AcquireRead((PVOID*) &pCanMsg, &wCount);
  if (VCI_OK == hr)
  {
    // limit number of messages to read
    if (wCount > wLimit)
    {
      wCount = wLimit;
    }

    UINT16 iter = wCount;
    while (iter)
    {
      PrintMessage(pCanMsg);

      // process next VCI message
      iter--;
      pCanMsg++;
    }
    pReader->ReleaseRead(wCount);
  }
  else if (VCI_E_RXQUEUE_EMPTY == hr)
  {
    // return error code
  }
  else
  {
    // ignore all other errors
    hr = VCI_OK;
  }

  return hr;
}


//////////////////////////////////////////////////////////////////////////
/**
  Receive thread.

  Note: 
    Here console output in the receive thread is used for demonstration purposes.
    Using console outout in the receive thread involves Asynchronous 
    Local Procedure Calls (ALPC) with the console host application (conhost.exe). 
    So expect console output to be slow.
    Slow output can stall receive queue handling and finally lead 
    to controller overruns on some CAN interfaces, even with moderate busloads 
    (moderate = 1000 kBit/s, dlc=8, busload >= 30%).

  @param Param
    ptr on a user defined information
*/
//////////////////////////////////////////////////////////////////////////
void ReceiveThread( void* Param )
{
  UNREFERENCED_PARAMETER(Param);

  BOOL receiveSignaled = FALSE;
  BOOL moreMsgMayAvail = FALSE;

  while ( lMustQuit == 0 )
  {
    // if no more messages available wait 100msec for reader event
    if (!moreMsgMayAvail)
    {
      receiveSignaled = (WAIT_OBJECT_0 == WaitForSingleObject(hEventReader, 100));
    }

    // process messages while messages are available
    if (receiveSignaled || moreMsgMayAvail)
    {
      // try to process next chunk of messages (with max 100 msgs)
      moreMsgMayAvail = (VCI_OK == ProcessMessages(100));
    }
  }

  _endthread();
}

//////////////////////////////////////////////////////////////////////////
/**
  Finalizes the application
*/
//////////////////////////////////////////////////////////////////////////
void FinalizeApp()
{
  //
  // release reader
  //
  if (pReader)
  {
    pReader->Release();
    pReader = 0;
  }

  //
  // release writer
  //
  if (pWriter)
  {
    pWriter->Release();
    pWriter = 0;
  }

  //
  // release channel interface
  //
  if (pCanChn)
  {
    pCanChn->Release();
    pCanChn = 0;
  }

  //
  // release CAN control object
  //
  if (pCanControl)
  {
    HRESULT hResult = pCanControl->StopLine();
    if (hResult != VCI_OK)
    {
      printf("\n pCanControl->StopLine failed: 0x%08lX !", hResult);
    }

    hResult = pCanControl->ResetLine();
    if (hResult != VCI_OK)
    {
      printf("\n pCanControl->ResetLine failed: 0x%08lX !", hResult);
    }

    pCanControl->Release();
    pCanControl = NULL;
  }

  //
  // release bal object
  //
  if (pBalObject)
  {
    pBalObject->Release();
    pBalObject = NULL;
  }
}
