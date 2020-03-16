/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtSerialBus module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL3$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file. Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "ixxatcanbackend.h"

#include <QtCore/qdatastream.h>
#include <QtCore/qdebug.h>
#include <QtCore/qdiriterator.h>
#include <QtCore/qfile.h>
#include <QtCore/qloggingcategory.h>

#include "CanDriver_ixxatVci.h"

QT_BEGIN_NAMESPACE

Q_DECLARE_LOGGING_CATEGORY(QT_CANBUS_PLUGINS_IXXATCAN)

QList<QCanBusDeviceInfo> IxxatCanBackend::interfaces()
{
    qDebug() << "Function Name: " << Q_FUNC_INFO;
    QList<QCanBusDeviceInfo> result;

    IxxatVciCanCtrlsList::instance().refreshControllers();

    foreach (QString controller, IxxatVciCanCtrlsList::instance().allControllers()) {
        result.append(createDeviceInfo(controller));
    }

    return result;
}

IxxatCanBackend::IxxatCanBackend(const QString &name)
    // : canIxxatName(name)
{
    qDebug() << "Function Name: " << Q_FUNC_INFO;
    options[CanDriver_ixxatVci::CTRL_NAME] = name;
    options[CanDriver_ixxatVci::BIT_RATE] = 250000;
    // resetConfigurations();
}

IxxatCanBackend::~IxxatCanBackend()
{
    qDebug() << "Function Name: " << Q_FUNC_INFO;
    if (state() == ConnectedState)
        close();
}

bool IxxatCanBackend::open()
{
    qDebug() << "Function Name: " << Q_FUNC_INFO;

    driver = new CanDriver_ixxatVci(/*parent*/);
    driver->init(options);

    connect (driver, &CanDriver_ixxatVci::recv, this, &IxxatCanBackend::recv);

    setState(QCanBusDevice::ConnectedState);
    return true;
}

void IxxatCanBackend::recv (CanMessage * frame)
{
    QVector<QCanBusFrame> newFrames;

    newFrames.append(std::move(*frame));

    enqueueReceivedFrames(newFrames);
}

void IxxatCanBackend::close()
{
    qDebug() << "Function Name: " << Q_FUNC_INFO;
    driver->stop();
    setState(QCanBusDevice::UnconnectedState);
}

void IxxatCanBackend::setConfigurationParameter(int key, const QVariant &value)
{
    qDebug() << "Function Name: " << Q_FUNC_INFO << ": " << key << "=" << value;

    /*
    4 = QVariant(int, 250000)
    5 = QVariant(bool, false)
    6 = QVariant(int, 500000)
    BitRateKey,
    CanFdKey,
    DataBitRateKey,
    */

    switch (key) {
        case QCanBusDevice::RawFilterKey:
            setError(tr("Filter not yet managed!"), QCanBusDevice::CanBusError::ConfigurationError);
            //verify valid/supported filters
        break;

        // we need to check CAN FD option a lot -> cache it and avoid QVector lookup
        case QCanBusDevice::CanFdKey:
            if (value.toBool()) {
                setError(tr("CAN-FD not managed!"), QCanBusDevice::CanBusError::ConfigurationError);
                return;
            }
        break;

        case QCanBusDevice::BitRateKey:
            options[CanDriver_ixxatVci::BIT_RATE] = value.toInt();
        break;

        case QCanBusDevice::DataBitRateKey:
            setError(tr("CAN-FD not managed!"), QCanBusDevice::CanBusError::ConfigurationError);
            return;
        default:
            qDebug() << "No KEY";
            return;

    }

    qDebug() << "Configuration Activated!"; //  << key << "=>" << value.toString();
    QCanBusDevice::setConfigurationParameter(key, value);
}

bool IxxatCanBackend::writeFrame(const QCanBusFrame &newData)
{
    qDebug() << "Function Name: " << Q_FUNC_INFO;
    if (state() != ConnectedState)
        return false;

    if (Q_UNLIKELY(!newData.isValid())) {
        setError(tr("Cannot write invalid QCanBusFrame"), QCanBusDevice::WriteError);
        return false;
    }

    /*
    canid_t canId = newData.frameId();
    if (newData.hasExtendedFrameFormat())
        canId |= CAN_EFF_FLAG;

    if (newData.frameType() == QCanBusFrame::RemoteRequestFrame) {
        canId |= CAN_RTR_FLAG;
    } else if (newData.frameType() == QCanBusFrame::ErrorFrame) {
        canId = static_cast<canid_t>((newData.error() & QCanBusFrame::AnyError));
        canId |= CAN_ERR_FLAG;
    }

    if (Q_UNLIKELY(!canFdOptionEnabled && newData.hasFlexibleDataRateFormat())) {
        const QString error = tr("Cannot write CAN FD frame because CAN FD option is not enabled.");
        qCWarning(QT_CANBUS_PLUGINS_IXXATCAN, "%ls", qUtf16Printable(error));
        setError(error, QCanBusDevice::WriteError);
        return false;
    }

    qint64 bytesWritten = 0;
    if (newData.hasFlexibleDataRateFormat()) {
        canfd_frame frame;
        ::memset(&frame, 0, sizeof(frame));
        frame.len = newData.payload().size();
        frame.can_id = canId;
        frame.flags = newData.hasBitrateSwitch() ? CANFD_BRS : 0;
        frame.flags |= newData.hasErrorStateIndicator() ? CANFD_ESI : 0;
        ::memcpy(frame.data, newData.payload().constData(), frame.len);

        bytesWritten = ::write(canSocket, &frame, sizeof(frame));
    } else {
        can_frame frame;
        ::memset(&frame, 0, sizeof(frame));
        frame.can_dlc = newData.payload().size();
        frame.can_id = canId;
        ::memcpy(frame.data, newData.payload().constData(), frame.can_dlc);

        bytesWritten = ::write(canSocket, &frame, sizeof(frame));
    }

    if (Q_UNLIKELY(bytesWritten < 0)) {
        setError(qt_error_string(errno),
                 QCanBusDevice::CanBusError::WriteError);
        return false;
    }
    */

    emit framesWritten(1);

    return true;
}

QString IxxatCanBackend::interpretErrorFrame(const QCanBusFrame &errorFrame)
{
    qDebug() << "Function Name: " << Q_FUNC_INFO;
    if (errorFrame.frameType() != QCanBusFrame::ErrorFrame)
        return QString();

    // the payload may contain the error details
    const QByteArray data = errorFrame.payload();
    QString errorMsg;

    if (errorFrame.error() & QCanBusFrame::TransmissionTimeoutError)
        errorMsg += QStringLiteral("TX timout\n");

    if (errorFrame.error() & QCanBusFrame::MissingAcknowledgmentError)
        errorMsg += QStringLiteral("Received no ACK on transmission\n");

    if (errorFrame.error() & QCanBusFrame::BusOffError)
        errorMsg += QStringLiteral("Bus off\n");

    if (errorFrame.error() & QCanBusFrame::BusError)
        errorMsg += QStringLiteral("Bus error\n");

    if (errorFrame.error() & QCanBusFrame::ControllerRestartError)
        errorMsg += QStringLiteral("Controller restarted\n");

    if (errorFrame.error() & QCanBusFrame::UnknownError)
        errorMsg += QStringLiteral("Unknown error\n");

    if (errorFrame.error() & QCanBusFrame::LostArbitrationError) {
        errorMsg += QStringLiteral("Lost arbitration:\n");
        if (data.size() >= 1) {
            errorMsg += QString::number(data.at(0), 16);
            errorMsg += QStringLiteral(" bit\n");
        }
    }

    /*
    if (errorFrame.error() & QCanBusFrame::ControllerError) {
        errorMsg += QStringLiteral("Controller problem:\n");
        if (data.size() >= 2) {
            char b = data.at(1) ;
            if (b & CAN_ERR_CRTL_RX_OVERFLOW)
                errorMsg += QStringLiteral(" RX buffer overflow\n");
            if (b & CAN_ERR_CRTL_TX_OVERFLOW)
                errorMsg += QStringLiteral(" TX buffer overflow\n");
            if (b & CAN_ERR_CRTL_RX_WARNING)
                errorMsg += QStringLiteral(" reached warning level for RX errors\n");
            if (b & CAN_ERR_CRTL_TX_WARNING)
                errorMsg += QStringLiteral(" reached warning level for TX errors\n");
            if (b & CAN_ERR_CRTL_RX_PASSIVE)
                errorMsg += QStringLiteral(" reached error passive status RX\n");
            if (b & CAN_ERR_CRTL_TX_PASSIVE)
                errorMsg += QStringLiteral(" reached error passive status TX\n");

            if (b == CAN_ERR_CRTL_UNSPEC)
                errorMsg += QStringLiteral(" Unspecified error\n");
        }
    }

    if (errorFrame.error() & QCanBusFrame::TransceiverError) {
        errorMsg = QStringLiteral("Transceiver status:");
        if (data.size() >= 5) {
            char b = data.at(4);
            if (b & CAN_ERR_TRX_CANH_NO_WIRE)
                errorMsg += QStringLiteral(" CAN-transceiver CANH no wire\n");
            if (b & CAN_ERR_TRX_CANH_SHORT_TO_BAT)
                errorMsg += QStringLiteral(" CAN-transceiver CANH short to bat\n");
            if (b & CAN_ERR_TRX_CANH_SHORT_TO_VCC)
                errorMsg += QStringLiteral(" CAN-transceiver CANH short to vcc\n");
            if (b & CAN_ERR_TRX_CANH_SHORT_TO_GND)
                errorMsg += QStringLiteral(" CAN-transceiver CANH short to ground\n");
            if (b & CAN_ERR_TRX_CANL_NO_WIRE)
                errorMsg += QStringLiteral(" CAN-transceiver CANL no wire\n");
            if (b & CAN_ERR_TRX_CANL_SHORT_TO_BAT)
                errorMsg += QStringLiteral(" CAN-transceiver CANL short to bat\n");
            if (b & CAN_ERR_TRX_CANL_SHORT_TO_VCC)
                errorMsg += QStringLiteral(" CAN-transceiver CANL short to vcc\n");
            if (b & CAN_ERR_TRX_CANL_SHORT_TO_GND)
                errorMsg += QStringLiteral(" CAN-transceiver CANL short to ground\n");
            if (b & CAN_ERR_TRX_CANL_SHORT_TO_CANH)
                errorMsg += QStringLiteral(" CAN-transceiver CANL short to CANH\n");

            if (b == CAN_ERR_TRX_UNSPEC)
                errorMsg += QStringLiteral(" unspecified\n");
        }

    }

    if (errorFrame.error() & QCanBusFrame::ProtocolViolationError) {
        errorMsg += QStringLiteral("Protocol violation:\n");
        if (data.size() > 3) {
            char b = data.at(2);
            if (b & CAN_ERR_PROT_BIT)
                errorMsg += QStringLiteral(" single bit error\n");
            if (b & CAN_ERR_PROT_FORM)
                errorMsg += QStringLiteral(" frame format error\n");
            if (b & CAN_ERR_PROT_STUFF)
                errorMsg += QStringLiteral(" bit stuffing error\n");
            if (b & CAN_ERR_PROT_BIT0)
                errorMsg += QStringLiteral(" unable to send dominant bit\n");
            if (b & CAN_ERR_PROT_BIT1)
                errorMsg += QStringLiteral(" unable to send recessive bit\n");
            if (b & CAN_ERR_PROT_OVERLOAD)
                errorMsg += QStringLiteral(" bus overload\n");
            if (b & CAN_ERR_PROT_ACTIVE)
                errorMsg += QStringLiteral(" active error announcement\n");
            if (b & CAN_ERR_PROT_TX)
                errorMsg += QStringLiteral(" error occurred on transmission\n");

            if (b == CAN_ERR_PROT_UNSPEC)
                errorMsg += QStringLiteral(" unspecified\n");
        }
        if (data.size() > 4) {
            char b = data.at(3);
            if (b == CAN_ERR_PROT_LOC_SOF)
                errorMsg += QStringLiteral(" start of frame\n");
            if (b == CAN_ERR_PROT_LOC_ID28_21)
                errorMsg += QStringLiteral(" ID bits 28 - 21 (SFF: 10 - 3)\n");
            if (b == CAN_ERR_PROT_LOC_ID20_18)
                errorMsg += QStringLiteral(" ID bits 20 - 18 (SFF: 2 - 0 )\n");
            if (b == CAN_ERR_PROT_LOC_SRTR)
                errorMsg += QStringLiteral(" substitute RTR (SFF: RTR)\n");
            if (b == CAN_ERR_PROT_LOC_IDE)
                errorMsg += QStringLiteral(" identifier extension\n");
            if (b == CAN_ERR_PROT_LOC_ID17_13)
                errorMsg += QStringLiteral(" ID bits 17-13\n");
            if (b == CAN_ERR_PROT_LOC_ID12_05)
                errorMsg += QStringLiteral(" ID bits 12-5\n");
            if (b == CAN_ERR_PROT_LOC_ID04_00)
                errorMsg += QStringLiteral(" ID bits 4-0\n");
            if (b == CAN_ERR_PROT_LOC_RTR)
                errorMsg += QStringLiteral(" RTR\n");
            if (b == CAN_ERR_PROT_LOC_RES1)
                errorMsg += QStringLiteral(" reserved bit 1\n");
            if (b == CAN_ERR_PROT_LOC_RES0)
                errorMsg += QStringLiteral(" reserved bit 0\n");
            if (b == CAN_ERR_PROT_LOC_DLC)
                errorMsg += QStringLiteral(" data length code\n");
            if (b == CAN_ERR_PROT_LOC_DATA)
                errorMsg += QStringLiteral(" data section\n");
            if (b == CAN_ERR_PROT_LOC_CRC_SEQ)
                errorMsg += QStringLiteral(" CRC sequence\n");
            if (b == CAN_ERR_PROT_LOC_CRC_DEL)
                errorMsg += QStringLiteral(" CRC delimiter\n");
            if (b == CAN_ERR_PROT_LOC_ACK)
                errorMsg += QStringLiteral(" ACK slot\n");
            if (b == CAN_ERR_PROT_LOC_ACK_DEL)
                errorMsg += QStringLiteral(" ACK delimiter\n");
            if (b == CAN_ERR_PROT_LOC_EOF)
                errorMsg += QStringLiteral(" end of frame\n");
            if (b == CAN_ERR_PROT_LOC_INTERM)
                errorMsg += QStringLiteral(" Intermission\n");

            if (b == CAN_ERR_PROT_LOC_UNSPEC)
                errorMsg += QStringLiteral(" unspecified\n");
        }
    }
    */

    // cut trailing \n
    if (!errorMsg.isEmpty())
        errorMsg.chop(1);

    return errorMsg;
}

QT_END_NAMESPACE
