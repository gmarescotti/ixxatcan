// see http://gitlab.unique-conception.org (dead link)

#define INITGUID

#include "vcisdk.h"
#include "vcifsl.h"

#include "CanDriver_ixxatVci.h"

#include <process.h>

// #include "CanMessage.h"
// #include <QtGlobal>
#include <QDebug>
#include <QtCore/qloggingcategory.h>
#include <QDate>

#include <QStringBuilder>

#define QS QStringLiteral

Q_DECLARE_LOGGING_CATEGORY(QT_CANBUS_PLUGINS_IXXATCAN)

int Error = 1;
int Information = 2;

#define MYASSERT(hResult, msg, ret) \
  if (hResult != VCI_OK) { \
    qCCritical(QT_CANBUS_PLUGINS_IXXATCAN) << msg << " (" << Qt::hex << (UINT32)hResult << ")"; \
    assert (FALSE); \
    return ret; \
  }

#define MYASSERT_INV(hResult, msg, ret) \
  if ((hResult != VCI_OK) && (hResult != VCI_E_INVALID_STATE)) { \
    qCCritical(QT_CANBUS_PLUGINS_IXXATCAN) << msg << " (" << Qt::hex << (UINT32) hResult << ")"; \
    assert (FALSE); \
    return ret; \
  }

#define MYASSERT_INFO(hResult, msg) \
  if ((hResult != VCI_OK) && (hResult != VCI_E_INVALID_STATE)) { \
    qCCritical(QT_CANBUS_PLUGINS_IXXATCAN) << msg << " (" << Qt::hex << (UINT32) hResult << ")"; \
    assert (FALSE); \
  }


//////////////////////////////////////////////////////////////////////////
// global variables
//////////////////////////////////////////////////////////////////////////

static IBalObject*    pBalObject = 0;     // bus access object

// static LONG           lSocketNo  = 0;     // socket number
// static LONG           lBusCtlNo  = 0;     // controller number

static ICanControl*  pCanControl = 0;     // control interface
static ICanChannel*  pCanChn = 0;         // channel interface

static LONG           lMustQuit = 0;      // quit flag for the receive thread

static HANDLE         hEventReader = 0;
static PFIFOREADER    pReader = 0;

static PFIFOWRITER    pWriter = 0;

//////////////////////////////////////////////////////////////////////////

IxxatVciCanCtrlsList & IxxatVciCanCtrlsList::instance (void) {
    static IxxatVciCanCtrlsList ret;
    return ret;
}

void IxxatVciCanCtrlsList::refreshControllers (void) {
    qDeleteAll (m_controllers);
    m_controllers.clear ();

    HRESULT hResult; // error code

    IVciDeviceManager*  pDevMgr = 0;    // device manager
    IVciEnumDevice*     pEnum   = 0;    // enumerator handle
    VCIDEVICEINFO       sInfo;          // device info

    hResult = VciGetDeviceManager(&pDevMgr);
    MYASSERT (hResult, "Invalid VciGetDeviceManager", );

    hResult = pDevMgr->EnumDevices(&pEnum);
    MYASSERT (hResult, "Invalid VciGetDeviceManager", );

    while (pEnum->Next(1, &sInfo, NULL) == VCI_OK) {

        // Get Capabilities..
        IVciDevice* pDevice;
        hResult = pDevMgr->OpenDevice(sInfo.VciObjectId, &pDevice);
        MYASSERT (hResult, "Invalid OpenDevice", );
        hResult = pDevice->OpenComponent(CLSID_VCIBAL, IID_IBalObject, (void**)&pBalObject);
        pDevice->Release();

        if ((VCI_OK != hResult) || (pBalObject == NULL)) {
            qCCritical(QT_CANBUS_PLUGINS_IXXATCAN) << "Can't OpenComponent ! ";
            return;
        }

        IxxatVciCanCtrlAddr * addr = new IxxatVciCanCtrlAddr;
        addr->deviceId = sInfo.VciObjectId;

        // check if controller supports CANFD
        BALFEATURES features = { 0 };
        hResult = pBalObject->GetFeatures(&features);
        if (VCI_OK != hResult) {
            qCCritical(QT_CANBUS_PLUGINS_IXXATCAN) << "Can't GetFeatures ! ";
            return;
        }

        for (int ctrlIdx = 0; ctrlIdx < features.BusSocketCount; ++ctrlIdx) {
            // check for the expected controller type
            if (VCI_BUS_TYPE(features.BusSocketType[ctrlIdx]) == VCI_BUS_CAN)
            {
                addr->ctrlIdx  = ctrlIdx;
                m_controllers.insert (QString::fromLatin1 (sInfo.UniqueHardwareId.AsChar), addr);
            }
        }

        //
        // release bal object
        //
        pBalObject->Release();
        pBalObject = NULL;
    }

    //
    // close the device list (no longer needed)
    //
    pEnum->Release();
    pEnum = NULL;
}

IxxatVciCanCtrlAddr * IxxatVciCanCtrlsList::controllerByName (const QString & name) const {
    return m_controllers.value (name, Q_NULLPTR);
}

QList<QString> IxxatVciCanCtrlsList::allControllers (void) const {
    return m_controllers.keys ();
}

IxxatVciCanCtrlsList::IxxatVciCanCtrlsList (void) { }

const QString CanDriver_ixxatVci::CTRL_NAME = QS ("CTRL_NAME");
const QString CanDriver_ixxatVci::BIT_RATE  = QS ("BIT_RATE");
const QString CanDriver_ixxatVci::FILTERS  = QS ("FILTERS");

CanDriver_ixxatVci::CanDriver_ixxatVci (QObject * parent)
    : CanDriver (parent)
    , m_valid (false)
//    , m_initialTime (0)
    , m_canDevHanldle (0)
    , m_canCtrlHandle (0)
    , m_canChannelHandle (0)
    , m_thread (Q_NULLPTR)
    , m_worker (Q_NULLPTR)
{ }

CanDriver_ixxatVci::~CanDriver_ixxatVci (void) {
    stop ();
}

HRESULT CheckCapabilities(ICanSocket* pCanSocket)
{
    HRESULT hResult = E_FAIL;

    // check capabilities
    CANCAPABILITIES capabilities = { 0 };
    hResult = pCanSocket->GetCapabilities(&capabilities);
    MYASSERT (hResult, "Invalid GetCapabilities", hResult);

    // check for CAN FD
    if ((capabilities.dwFeatures & CAN_FEATURE_EXTDATA) &&
            (capabilities.dwFeatures & CAN_FEATURE_FASTDATA))
    {
        // supports CAN FD -> ok
    }
    else
    {
        // CAN FD not supported
        qCCritical(QT_CANBUS_PLUGINS_IXXATCAN) << "CAN FD features not supported !"; \
        hResult = VCI_E_NOT_SUPPORTED;
    }

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
        qCInfo(QT_CANBUS_PLUGINS_IXXATCAN) << "\n Simultaneous standard and extended mode feature not supported !";
        hResult = VCI_E_NOT_SUPPORTED;
    }

    return hResult;
}

HRESULT InitSocket(LONG lCtrlNo, QList<IxxatVciFilter> &filters, const int bitRate)
{
  HRESULT hResult = E_FAIL;

  IVciDeviceManager*  pDevMgr = 0;    // device manager
  IVciEnumDevice*     pEnum   = 0;    // enumerator handle
  VCIDEVICEINFO       sInfo;          // device info
  IVciDevice*         pDevice;

  hResult = VciGetDeviceManager(&pDevMgr);
  if (hResult != VCI_OK)
  {
      qCCritical(QT_CANBUS_PLUGINS_IXXATCAN) << "Can't open VCI VciGetDeviceManager ! ";
      return hResult;
  }

  hResult = pDevMgr->EnumDevices(&pEnum);
  //
  // retrieve information about the first
  // device within the device list
  //
  if (hResult != VCI_OK)
  {
      qCCritical(QT_CANBUS_PLUGINS_IXXATCAN) << "Can't EnumDevices ! ";
      return hResult;
  }

  hResult = pEnum->Next(1, &sInfo, NULL);

  //
  // close the device list (no longer needed)
  //
  pEnum->Release();
  pEnum = NULL;

  // Get Capabilities..
  hResult = pDevMgr->OpenDevice(sInfo.VciObjectId, &pDevice);
  MYASSERT (hResult, "Invalid OpenDevice", hResult);

  hResult = pDevice->OpenComponent(CLSID_VCIBAL, IID_IBalObject, (void**)&pBalObject);
  MYASSERT (hResult, "Invalid OpenComponent", hResult);

  pDevice->Release();

  // check controller capabilities create a message channel
  ICanSocket* pCanSocket = 0;
  hResult = pBalObject->OpenSocket(lCtrlNo, IID_ICanSocket, (void**)&pCanSocket);
  MYASSERT (hResult, "Invalid OpenSocket", hResult);


  // create a message channel
  hResult = pCanSocket->CreateChannel(FALSE, &pCanChn);

  pCanSocket->Release();

  MYASSERT (hResult, "Invalid Release", hResult);

  ///////////////////////////////

  // initialize the message channel
  UINT16 wRxFifoSize  = 1024;
  UINT16 wRxThreshold = 1;
  UINT16 wTxFifoSize  = 128;
  UINT16 wTxThreshold = 1;

  // inclusive filtering (pass registered IDs) and
  // pass self-rec messages from all channels
  // If the interface ICanControl is used, the operating mode of the filter can not be changed and
  // is preset to CAN_FILTER_INCL. If the interface ICanControl2 resp. ICanChannel2 is
  // used, the operating mode can be set to one of the above stated modes with the function
  // SetFilterMode
  hResult = pCanChn->Initialize(wRxFifoSize, wTxFifoSize); // , 0, CAN_FILTER_INCL|CAN_FILTER_SRRA);
  MYASSERT (hResult, "Invalid Initialize", hResult);

  hResult = pCanChn->GetReader(&pReader);
  MYASSERT (hResult, "Invalid GetReader", hResult);

  pReader->SetThreshold(wRxThreshold);

  hEventReader = CreateEvent(NULL, FALSE, FALSE, NULL);

  pReader->AssignEvent(hEventReader);
  MYASSERT (hResult, "Invalid AssignEvent", hResult);

  hResult = pCanChn->GetWriter(&pWriter);
  MYASSERT (hResult, "Invalid GetWriter", hResult);

  pWriter->SetThreshold(wTxThreshold);

  // activate the CAN channel
  hResult = pCanChn->Activate();
  MYASSERT (hResult, "Invalid Activate", hResult);

  // Open the CAN control interface
  //
  // During the programs lifetime we have multiple options:
  // 1) Open the control interface and keep it open
  //     -> No other programm is able to get the control interface and change the line settings
  // 2) Try to get the control interface and change the settings only when we get it
  //     -> Other programs can change the settings by getting the control interface
  hResult = pBalObject->OpenSocket(lCtrlNo, IID_ICanControl, (void**)&pCanControl);
  // MYASSERT (hResult, "Invalid OpenSocket", hResult);

  if (hResult != VCI_OK) {
      qCInfo(QT_CANBUS_PLUGINS_IXXATCAN) << "Control interface occupied. Settings not applied !";
      hResult = VCI_OK;
  } else {

      // initialize the CAN controller
      //
      // set CAN-FD baudrate combination via raw controller settings
      // standard   500 kBit/s
      // fast      2000 kBit/s
      //

      CANINITLINE init =  {
        CAN_OPMODE_STANDARD |
        CAN_OPMODE_EXTENDED | CAN_OPMODE_ERRFRAME,      // opmode
        0,                                              // bReserved
        CAN_BT0_500KB, CAN_BT1_500KB                    // bt0, bt1
      };

      switch (bitRate) {
      case 5000: init.bBtReg0 = CAN_BT0_5KB; init.bBtReg1 = CAN_BT1_5KB; break;
      case 10000: init.bBtReg0 = CAN_BT0_10KB; init.bBtReg1 = CAN_BT1_10KB; break;
      case 20000: init.bBtReg0 = CAN_BT0_20KB; init.bBtReg1 = CAN_BT1_20KB; break;
      case 50000: init.bBtReg0 = CAN_BT0_50KB; init.bBtReg1 = CAN_BT1_50KB; break;
      case 100000: init.bBtReg0 = CAN_BT0_100KB; init.bBtReg1 = CAN_BT1_100KB; break;
      case 125000: init.bBtReg0 = CAN_BT0_125KB; init.bBtReg1 = CAN_BT1_125KB; break;
      case 250000: init.bBtReg0 = CAN_BT0_250KB; init.bBtReg1 = CAN_BT1_250KB; break;
      case 500000: init.bBtReg0 = CAN_BT0_500KB; init.bBtReg1 = CAN_BT1_500KB; break;
      case 800000: init.bBtReg0 = CAN_BT0_800KB; init.bBtReg1 = CAN_BT1_800KB; break;
      case 1000000: init.bBtReg0 = CAN_BT0_1000KB; init.bBtReg1 = CAN_BT1_1000KB; break;
      default:
          qCCritical(QT_CANBUS_PLUGINS_IXXATCAN) << "Can't use invalid bitrate ! " << bitRate;
          return VCI_E_INVALIDARG;
      }

      ////////////////////////////////////////

      hResult = pCanControl->InitLine(&init);
      MYASSERT (hResult, "Invalid InitLine", hResult);
      ////////////////////////////////////////


      // set the acceptance filter
      if(filters.size() > 0) {
          static bool first=true;
          for(IxxatVciFilter f : filters) {
              if (first) {
                  hResult = pCanControl->SetAccFilter(f.fExtended, f.dwCode, f.dwMask);
                  first=false;
              } else {
                  hResult = pCanControl->AddFilterIds(f.fExtended, f.dwCode, f.dwMask);
              }
              // SetAccFilter() returns VCI_E_INVALID_STATE if already controller is started.
              // We ignore this because the controller could already be started
              // by another application.
              MYASSERT_INV (hResult, "Invalid SetAccFilter", hResult);
          }
      }

      // start the CAN controller
      hResult = pCanControl->StartLine();
      MYASSERT (hResult, "Invalid StartLine", hResult);

      qCDebug(QT_CANBUS_PLUGINS_IXXATCAN) << "Got Control interface. Settings applied !";
  }
  return hResult;
}

bool CanDriver_ixxatVci::init (const QVariantMap & options) {
    const QString ctrlName = options.value (CTRL_NAME).toString ();

    if (ctrlName.isEmpty ()) {
        qCCritical(QT_CANBUS_PLUGINS_IXXATCAN) << "missing control name ! " << ctrlName;
        return FALSE;
    }

    qCInfo(QT_CANBUS_PLUGINS_IXXATCAN) << "Using controller with name : " << ctrlName;
    IxxatVciCanCtrlAddr * ctrlAddr = IxxatVciCanCtrlsList::instance ().controllerByName (ctrlName);

    if (ctrlAddr == Q_NULLPTR) {
        qCCritical(QT_CANBUS_PLUGINS_IXXATCAN) << "Can't open CAN controller !";
        return FALSE;
    }

    qCInfo(QT_CANBUS_PLUGINS_IXXATCAN) << "Using board " << ctrlAddr->deviceId.AsInt64 << ", controller " << ctrlAddr->ctrlIdx;
    const int bitRate = options.value (BIT_RATE).toInt();
    const QList<QList<uint>> filters_vars = qvariant_cast<QList<QList<uint>>>(options.value (FILTERS));
    QList<IxxatVciFilter> filters;
    for(QList<uint> filter : filters_vars) {
        IxxatVciFilter f;
        f.fExtended = filter[0] ? CAN_FILTER_EXT : CAN_FILTER_STD;
        f.dwCode = filter[1] << 1;
        f.dwMask = filter[2] << 1;
        int isRTR = filter[3];
        if(isRTR) {
            f.dwCode += 1;
            f.dwMask += 1;
        }
        filters.push_back(f);
    }

    if (VCI_OK != InitSocket(ctrlAddr->ctrlIdx /*lCtrlNo*/, filters, bitRate)) {
        m_valid = FALSE;
        return m_valid;
    }

    m_thread = new QThread (this);
    m_worker = new CanDriver_ixxatVciPollWorker (m_canChannelHandle/*, m_initialTime*/);
    m_worker->moveToThread (m_thread);
    connect (m_thread, &QThread::started, m_worker, &CanDriver_ixxatVciPollWorker::poll);
    connect (m_worker, &CanDriver_ixxatVciPollWorker::recv, this, &CanDriver_ixxatVci::recv);
    // connect (m_worker, &CanDriver_ixxatVciPollWorker::diag, this, &CanDriver_ixxatVci::diag);
    m_thread->start ();
    m_valid = TRUE;
    return m_valid;
}

bool CanDriver_ixxatVci::send (CanMessage * message) {
    bool ret = false;
    if (message != Q_NULLPTR) {
        if (m_valid) {
            static CANMSG canMsg;
            memset (&canMsg, 0x00, sizeof (canMsg));
            canMsg.uMsgInfo.Bits.type = CAN_MSGTYPE_DATA;
            canMsg.uMsgInfo.Bits.dlc = static_cast<UINT32>(message->payload().length());
            canMsg.uMsgInfo.Bits.ext = message->hasExtendedFrameFormat();
            canMsg.uMsgInfo.Bits.rtr = message->frameType() == CanMessage::RemoteRequestFrame;
            canMsg.dwMsgId = message->frameId();
            for (int idx = 0; idx < 8; ++idx) {
               canMsg.abData [idx] = (idx < message->payload().length () ? static_cast<UINT8>(message->payload()[idx]) : 0x00);
            }

            // write a single CAN message into the transmit FIFO
            while (VCI_E_TXQUEUE_FULL == pWriter->PutDataEntry(&canMsg))
            {
              Sleep(1);
            }
        }
    }
    return ret;
}

// was FinalizeApp()
bool CanDriver_ixxatVci::stop (void) {
    if (m_valid) {
        m_valid = false;
        qCInfo(QT_CANBUS_PLUGINS_IXXATCAN) << "Stop IXXAT VCI poll worker thread";
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
          MYASSERT_INFO (hResult, "CanControl->StopLine failed: 0x%08lX !");

          hResult = pCanControl->ResetLine();
          MYASSERT_INFO (hResult, "pCanControl->ResetLine failed !");

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

    m_canDevHanldle    = 0;
    m_canCtrlHandle    = 0;
    m_canChannelHandle = 0;
    m_worker = Q_NULLPTR;
    m_thread = Q_NULLPTR;


    // GGG
    // tell receive thread to quit
    //
    InterlockedExchange(&lMustQuit, 1);


    return true;
}

CanDriver_ixxatVciPollWorker::CanDriver_ixxatVciPollWorker (HANDLE canChannelHandle/*, qint64 initTime*/)
    : QObject (Q_NULLPTR)
    // , m_initialTime (initTime)
    , m_canChannelHandle (canChannelHandle)
{ m_initialTime = -1; }

CanDriver_ixxatVciPollWorker::~CanDriver_ixxatVciPollWorker (void) { }

static CanMessage::FrameType convert_type(CANMSG canMsg)
{
    switch (canMsg.uMsgInfo.Bits.type) {
    case CAN_MSGTYPE_ERROR:
        return CanMessage::ErrorFrame;
    case CAN_MSGTYPE_DATA:
        if (canMsg.uMsgInfo.Bits.rtr)
            return CanMessage::RemoteRequestFrame;
        if (canMsg.dwMsgId == CAN_MSGID_INVALID)
            return CanMessage::InvalidFrame;
        return CanMessage::DataFrame;
    default:
        return CanMessage::UnknownFrame;
    }
}

//////////////////////////////////////////////////////////////////////////
/**

  Print a message

*/
//////////////////////////////////////////////////////////////////////////
/// \brief PrintMessage
/// \param pCanMsg
#if 0
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
#endif

//////////////////////////////////////////////////////////////////////////
HRESULT CanDriver_ixxatVciPollWorker::ProcessMessages(WORD wLimit)
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
      // PrintMessage(pCanMsg);

      const bool err = (pCanMsg->uMsgInfo.Bits.type == CAN_MSGTYPE_ERROR);
      if (pCanMsg->uMsgInfo.Bits.type == CAN_MSGTYPE_DATA || err) {
          const bool rtr = bool (pCanMsg->uMsgInfo.Bits.rtr);

          if (!rtr && !err) {

              CanMessage *frame = new CanMessage(pCanMsg->dwMsgId, QByteArray((char*)pCanMsg->abData, pCanMsg->uMsgInfo.Bits.dlc));
              if (m_initialTime == -1) {
                  m_initialTime = QDateTime::currentDateTime ().toMSecsSinceEpoch ();
                  m_initialTime -= (pCanMsg->dwTime/1000);
              }
              frame->setTimeStamp(CanMessage::TimeStamp::fromMicroSeconds(pCanMsg->dwTime + (m_initialTime * 1000)));
              frame->setExtendedFrameFormat(pCanMsg->uMsgInfo.Bits.ext);
              frame->setLocalEcho(false);
              frame->setFrameType(convert_type(*pCanMsg));

              frame->setFlexibleDataRateFormat(false);
              frame->setBitrateSwitch(false);
              frame->setErrorStateIndicator(err);

              emit recv(frame);
          }
      }

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

void CanDriver_ixxatVciPollWorker::poll (void) {
    static CANMSG canMsg;
    qCInfo(QT_CANBUS_PLUGINS_IXXATCAN) << "Entered infinite poll loop";

    BOOL moreMsgMayAvail = FALSE;
    BOOL receiveSignaled = FALSE;

    while (!controller.exit) {
        memset (&canMsg, 0x00, sizeof (canMsg));

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



//    while (!controller.exit) {
//        memset (&canMsg, 0x00, sizeof (canMsg));
//        HRESULT result = canChannelReadMessage (m_canChannelHandle, 100, &canMsg);
//        if (result == VCI_OK) {
//            const bool err = (canMsg.uMsgInfo.Bits.type == CAN_MSGTYPE_ERROR);
//            if (canMsg.uMsgInfo.Bits.type == CAN_MSGTYPE_DATA || err) {
//                const bool rtr = bool (canMsg.uMsgInfo.Bits.rtr);
//                /*

//                emit recv (new CanMessage ((canMsg.uMsgInfo.Bits.ext
//                                            ? CanId (quint32 (canMsg.dwMsgId), rtr, err)
//                                            : CanId (quint16 (canMsg.dwMsgId), rtr, err)),
//                                           quint (canMsg.uMsgInfo.Bits.dlc),
//                                           canMsg.abData,
//                QDateTime::fromMSecsSinceEpoch (qint64 (canMsg.dwTime / 1000.0) + m_initialTime)
//                */

//                if (!rtr && !err) {

//                    CanMessage *frame = new CanMessage(canMsg.dwMsgId, QByteArray((char*)canMsg.abData, canMsg.uMsgInfo.Bits.dlc));
//                    if (m_initialTime == -1) {
//                        m_initialTime = QDateTime::currentDateTime ().toMSecsSinceEpoch ();
//                        m_initialTime -= (canMsg.dwTime/1000);
//                    }
//                    frame->setTimeStamp(CanMessage::TimeStamp::fromMicroSeconds(canMsg.dwTime + (m_initialTime * 1000)));
//                    frame->setExtendedFrameFormat(canMsg.uMsgInfo.Bits.ext);
//                    frame->setLocalEcho(false);
//                    frame->setFrameType(convert_type(canMsg));

//                    frame->setFlexibleDataRateFormat(false);
//                    frame->setBitrateSwitch(false);
//                    frame->setErrorStateIndicator(err);

//                    emit recv(frame);
//                }
//            }
//        }
//        else if (result == HRESULT (VCI_E_TIMEOUT) ||
//                 result == HRESULT (VCI_E_RXQUEUE_EMPTY)) {
//            // NOTE : nothing to read yet...
//        }
//        else {
//            qCWarning(QT_CANBUS_PLUGINS_IXXATCAN) << "Error " << result << " receiving CAN frame ! ";
//        }
//    }
//    qCInfo(QT_CANBUS_PLUGINS_IXXATCAN) << "Exited infinite poll loop";
}

