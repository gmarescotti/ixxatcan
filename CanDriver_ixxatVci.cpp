
#include "CanDriver_ixxatVci.h"

// #include "CanMessage.h"
// #include <QtGlobal>
#include <QDebug>
#include <QDate>

#include <QStringBuilder>

#define QS QStringLiteral

IxxatVciCanCtrlsList & IxxatVciCanCtrlsList::instance (void) {
    static IxxatVciCanCtrlsList ret;
    return ret;
}

void IxxatVciCanCtrlsList::refreshControllers (void) {
    qDeleteAll (m_controllers);
    m_controllers.clear ();
    HANDLE devEnumeratorHandle;
    if (vciEnumDeviceOpen (&devEnumeratorHandle) == VCI_OK) {
        vciEnumDeviceReset (devEnumeratorHandle);
        HANDLE devHandle;
        VCIDEVICEINFO devInfo;
        VCIDEVICECAPS devCaps;
        while (vciEnumDeviceNext (devEnumeratorHandle, &devInfo) == VCI_OK) {
            if (vciDeviceOpen (devInfo.VciObjectId, &devHandle) == VCI_OK) {
                vciDeviceGetCaps (devHandle, &devCaps);
                qWarning () << qPrintable (QString::fromLatin1 (devInfo.UniqueHardwareId.AsChar))
                            << qPrintable (QString::fromLatin1 (devInfo.Manufacturer))
                            << qPrintable (QString::fromLatin1 (devInfo.Description));
                for (int ctrlIdx = 0; ctrlIdx < devCaps.BusCtrlCount; ++ctrlIdx) {
                    if (((devCaps.BusCtrlTypes [ctrlIdx] >> 8) & 0xFF) == VCI_BUS_CAN) {
                        IxxatVciCanCtrlAddr * addr = new IxxatVciCanCtrlAddr;
                        addr->deviceId = devInfo.VciObjectId;
                        addr->ctrlIdx  = ctrlIdx;
                        m_controllers.insert (QString::fromLatin1 (devInfo.UniqueHardwareId.AsChar), addr);
                    }
                }
                vciDeviceClose (devHandle);
            }
        }
        vciEnumDeviceClose (devEnumeratorHandle);
    }
    else {
        qWarning () << "Can't open VCI device enumerator !";
    }
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
    , m_initialTime (0)
    , m_canDevHanldle (0)
    , m_canCtrlHandle (0)
    , m_canChannelHandle (0)
    , m_thread (Q_NULLPTR)
    , m_worker (Q_NULLPTR)
{ }

CanDriver_ixxatVci::~CanDriver_ixxatVci (void) {
    stop ();
}

bool CanDriver_ixxatVci::init (const QVariantMap & options) {
    const QString ctrlName = options.value (CTRL_NAME).toString ();
    if (!ctrlName.isEmpty ()) {
        // emit diag (Information, QS ("Using controller with name : ") % ctrlName);
        IxxatVciCanCtrlAddr * ctrlAddr = IxxatVciCanCtrlsList::instance ().controllerByName (ctrlName);
        if (ctrlAddr != Q_NULLPTR) {
            // emit diag (Information, QS ("Using board %1, controller %2").arg (ctrlAddr->deviceId.AsInt64).arg (ctrlAddr->ctrlIdx));
            const int bitRate = options.value (BIT_RATE).toInt();
    	    const QList<QList<uint>> filters_vars = qvariant_cast<QList<QList<uint>>>(options.value (FILTERS));
	    QList<IxxatVciFilter> filters;
	    for(QList<uint> filter : filters_vars) {
		IxxatVciFilter f;
		f.fExtended = filter[0];
		f.dwCode = filter[1] << 1;
		f.dwMask = filter[2] << 1;
		int isRTR = filter[3];
		if(isRTR) {
			f.dwCode += 1;
			f.dwMask += 1;
		}
		filters.push_back(f);
	    }
            const int baudRatesCount = 9;
            const IxxatVciBaudRateRegisters listBaudRates [baudRatesCount] = {
                { 10000,  0x31, 0x1C },
                { 20000,  0x18, 0x1C },
                { 50000,  0x09, 0x1C },
                { 100000, 0x04, 0x1C },
                { 125000, 0x03, 0x1C },
                { 250000, 0x01, 0x1C },
                { 500000, 0x00, 0x1C },
                { 800000, 0x00, 0x16 },
                { 1000000,   0x00, 0x14 },
            };
            int idxBaudRate = -1;
            for (int tmp = 0; tmp < baudRatesCount; ++tmp) {
                if (listBaudRates [tmp].baud_rate == bitRate) {
                    idxBaudRate = tmp;
                    break;
                }
            }
            if (idxBaudRate > -1) {
                // emit diag (Information, QS ("Using bitrate ") % bitRate);
                if (vciDeviceOpen (ctrlAddr->deviceId, &m_canDevHanldle) == VCI_OK) {
                    if (canControlOpen (m_canDevHanldle, ctrlAddr->ctrlIdx, &m_canCtrlHandle) == VCI_OK) {
                        const IxxatVciBaudRateRegisters btReg = listBaudRates [idxBaudRate];
                        if (canControlInitialize (m_canCtrlHandle, CAN_OPMODE_STANDARD | CAN_OPMODE_EXTENDED | CAN_OPMODE_ERRFRAME, btReg.bt0, btReg.bt1) == VCI_OK) {
			        // auto t0 = canControlRemFilterIds(m_canCtrlHandle, false, 0x00000000, 0x00000000);
			        if(filters.size() > 0 && canControlSetAccFilter(m_canCtrlHandle, false, 0x00000000, (0x00000fff << 1)) != VCI_OK) {
					// :TODO: Who fucking cares
				}
				for(IxxatVciFilter f : filters) {
			        	if(canControlAddFilterIds(m_canCtrlHandle, f.fExtended, f.dwCode, f.dwMask) != VCI_OK) {
						// :TODO: Please handle me
					}
				}
                            if (canControlStart (m_canCtrlHandle, true) == VCI_OK) {
                                if (canChannelOpen (m_canDevHanldle, ctrlAddr->ctrlIdx, true, &m_canChannelHandle) == VCI_OK) {
                                    if (canChannelInitialize (m_canChannelHandle, 1000, 1, 1000, 1) == VCI_OK) {
                                        if (canChannelActivate (m_canChannelHandle, true) == VCI_OK) {
                                            // emit diag (Information, QS ("CAN messages channel ready."));
                                            m_initialTime = QDateTime::currentDateTime ().toMSecsSinceEpoch ();
                                            m_valid = true;
                                            m_thread = new QThread (this);
                                            m_worker = new CanDriver_ixxatVciPollWorker (m_canChannelHandle, m_initialTime);
                                            m_worker->moveToThread (m_thread);
                                            connect (m_thread, &QThread::started, m_worker, &CanDriver_ixxatVciPollWorker::poll);
                                            connect (m_worker, &CanDriver_ixxatVciPollWorker::recv, this, &CanDriver_ixxatVci::recv);
                                            // connect (m_worker, &CanDriver_ixxatVciPollWorker::diag, this, &CanDriver_ixxatVci::diag);
                                            m_thread->start ();
                                        }
                                        else {
                                            // emit diag (Error, QS ("Can't activate CAN messages channel !"));
                                        }
                                    }
                                    else {
                                        // emit diag (Error, QS ("Can't initialize CAN messages channel !"));
                                    }
                                    if (!m_valid) {
                                        canChannelClose (m_canChannelHandle);
                                        m_canChannelHandle = 0;
                                    }
                                }
                                else {
                                    // emit diag (Error, QS ("Can't open CAN messages channel !"));
                                }
                            }
                            else {
                                // emit diag (Error, QS ("Can't start CAN controller !"));
                            }
                        }
                        else {
                            // emit diag (Error, QS ("Can't initialize CAN controller !"));
                        }
                        if (!m_valid) {
                            canControlClose (m_canCtrlHandle);
                            m_canCtrlHandle = 0;
                        }
                    }
                    else {
                        // emit diag (Error, QS ("Can't open CAN controller !"));
                    }
                    if (!m_valid) {
                        vciDeviceClose (m_canDevHanldle);
                        m_canDevHanldle = 0;
                    }
                }
                else {
                    // emit diag (Error, QS ("Can't open VCI device ! ") % ctrlName);
                }
            }
            else {
                // emit diag (Error, QS ("Can't use invalid bitrate ! ") % bitRate);
            }
        }
        else {
            // emit diag (Error, QS ("No controller with name ! ") % ctrlName);
        }
    }
    else {
        // emit diag (Error, "Can't use empty controller name !");
    }
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

            if (canChannelPostMessage (m_canChannelHandle, &canMsg) == VCI_OK) {
                ret = true;
            }
            else {
                // emit diag (Error, "Can't send CAN message !");
            }
        }
    }
    return ret;
}

bool CanDriver_ixxatVci::stop (void) {
    if (m_valid) {
        m_valid = false;
        // emit diag (Information, QS ("Stop IXXAT VCI poll worker thread"));
        if (m_worker) {
            m_worker->controller.exit = true;
        }
        if (m_thread) {
            m_thread->quit ();
            m_thread->wait (3000);
            delete m_worker;
            delete m_thread;
        }
        // emit diag (Information, QS ("Stop IXXAT VCI controller and message channel"));
        canChannelActivate (m_canChannelHandle, false);
        canControlStart    (m_canCtrlHandle,    false);
        // emit diag (Information, QS ("Close IXXAT VCI channel"));
        canControlReset (m_canCtrlHandle);
        canChannelClose (m_canChannelHandle);
        canControlClose (m_canCtrlHandle);
        // emit diag (Information, QS ("Close IXXAT VCI device"));
        vciDeviceClose (m_canDevHanldle);
    }
    m_canDevHanldle    = 0;
    m_canCtrlHandle    = 0;
    m_canChannelHandle = 0;
    m_worker = Q_NULLPTR;
    m_thread = Q_NULLPTR;
    return true;
}

CanDriver_ixxatVciPollWorker::CanDriver_ixxatVciPollWorker (HANDLE canChannelHandle, qint64 initTime)
    : QObject (Q_NULLPTR)
    , m_initialTime (initTime)
    , m_canChannelHandle (canChannelHandle)
{ }

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

void CanDriver_ixxatVciPollWorker::poll (void) {
    static CANMSG canMsg;
    // emit diag (CanDriver::Information, QS ("Entered infinite poll loop"));
    while (!controller.exit) {
        memset (&canMsg, 0x00, sizeof (canMsg));
        HRESULT result = canChannelReadMessage (m_canChannelHandle, 100, &canMsg);
        if (result == VCI_OK) {
            // const bool err = (canMsg.uMsgInfo.Bits.type == CAN_MSGTYPE_ERROR);
            // if (canMsg.uMsgInfo.Bits.type == CAN_MSGTYPE_DATA || err) {
                /*
                const bool rtr = bool (canMsg.uMsgInfo.Bits.rtr);

                emit recv (new CanMessage ((canMsg.uMsgInfo.Bits.ext
                                            ? CanId (quint32 (canMsg.dwMsgId), rtr, err)
                                            : CanId (quint16 (canMsg.dwMsgId), rtr, err)),
                                           quint (canMsg.uMsgInfo.Bits.dlc),
                                           canMsg.abData,
                QDateTime::fromMSecsSinceEpoch (qint64 (canMsg.dwTime / 1000.0) + m_initialTime)
                */

                CanMessage *frame = new CanMessage(canMsg.dwMsgId, QByteArray((char*)canMsg.abData, canMsg.uMsgInfo.Bits.dlc));
                frame->setTimeStamp(CanMessage::TimeStamp::fromMicroSeconds(canMsg.dwTime + (m_initialTime * 1000)));
                frame->setExtendedFrameFormat(canMsg.uMsgInfo.Bits.ext);
                frame->setLocalEcho(false);
                frame->setFrameType(convert_type(canMsg));

                frame->setFlexibleDataRateFormat(false);
                frame->setBitrateSwitch(false);
                frame->setErrorStateIndicator(false);

                emit recv(frame);
            // }
        }
        else if (result == HRESULT (VCI_E_TIMEOUT) ||
                 result == HRESULT (VCI_E_RXQUEUE_EMPTY)) {
            // NOTE : nothing to read yet...
        }
        else {
            // emit diag (CanDriver::Warning, QS ("Error %1 receiving CAN frame ! ").arg (result));
        }
    }
    // emit diag (CanDriver::Information, QS ("Exited infinite poll loop"));
}

#ifndef QTCAN_STATIC_DRIVERS

QString CanDriverPlugin_ixxatVci::getDriverName (void) {
    return QStringLiteral ("IXXAT VCI 3.x");
}

CanDriver * CanDriverPlugin_ixxatVci::createDriverInstance (QObject * parent) {
    return new CanDriver_ixxatVci (parent);
}

QList<CanDriverOption *> CanDriverPlugin_ixxatVci::optionsRequired (void) {
    QList<CanDriverOption *> ret;
    // list of controllers
    IxxatVciCanCtrlsList & controllers = IxxatVciCanCtrlsList::instance ();
    controllers.refreshControllers ();
    const QList<QString> list = controllers.allControllers ();
    QVariantList tmp;
    tmp.reserve (list.count ());
    foreach (const QString & name, list) {
        tmp.append (name);
    }
    ret.append (new CanDriverOption (CanDriver_ixxatVci::CTRL_NAME, "Controller", CanDriverOption::ListChoice, "", tmp));
    // bitrates
    QVariantList listBaudRates;
    listBaudRates << "10K" << "20K" << "50K" << "100K" << "125K" << "250K" << "500K" << "800K" << "1M";
    ret << new CanDriverOption (CanDriver_ixxatVci::BIT_RATE, tr ("Baud rate"),  CanDriverOption::ListChoice, "250K", listBaudRates);
    return ret;
}

#endif
