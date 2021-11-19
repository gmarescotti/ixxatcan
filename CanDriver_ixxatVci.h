#ifndef CANDRIVER_IXXATVCI_H
#define CANDRIVER_IXXATVCI_H

#include <QObject>
#include <QThread>

// #include "QtCAN.h"
// #include "CanDriver.h"
#include <QMap>
#include <QtSerialBus/qcanbusframe.h>
typedef QCanBusFrame CanMessage;
typedef QObject CanDriver;

extern "C" {
#include "vcinpl.h"
#include "vcitype.h"
#include "vcierr.h"
}

struct QTCAN_DRIVER_EXPORT IxxatVciCanCtrlAddr {
    VCIID deviceId;
    UINT32 ctrlIdx;
};

struct QTCAN_DRIVER_EXPORT IxxatVciBaudRateRegisters {
    int baud_rate;
    quint8 bt0;
    quint8 bt1;
};

struct QTCAN_DRIVER_EXPORT IxxatVciFilter {
    BOOL fExtended;
    UINT32 dwCode;
    UINT32 dwMask;
};

class QTCAN_DRIVER_EXPORT IxxatVciCanCtrlsList {
public:
    static IxxatVciCanCtrlsList & instance (void);

    void refreshControllers (void);

    IxxatVciCanCtrlAddr * controllerByName (const QString & name) const;

    QList<QString> allControllers (void) const;

protected:
    explicit IxxatVciCanCtrlsList (void);

private:
    QMap<QString, IxxatVciCanCtrlAddr *> m_controllers;
};


class QTCAN_DRIVER_EXPORT CanDriver_ixxatVciPollWorker : public QObject {
    Q_OBJECT

public:
    explicit CanDriver_ixxatVciPollWorker (HANDLE canChannelHandle/*, qint64 initTime*/);
    ~CanDriver_ixxatVciPollWorker (void);

    HRESULT ProcessMessages(WORD wLimit);

    struct Controller {
        bool exit;

        explicit Controller (void) : exit (false) { }
    } controller;

public slots:
    void poll (void);

signals:
    void recv (CanMessage * msg);
    void diag (int level, const QString & description);

private:
    qint64 m_initialTime;
    HANDLE m_canChannelHandle;
};

class QTCAN_DRIVER_EXPORT CanDriver_ixxatVci : public CanDriver {
    Q_OBJECT

public:
    explicit CanDriver_ixxatVci (QObject * parent = Q_NULLPTR);
    ~CanDriver_ixxatVci (void);

    static const QString CTRL_NAME;
    static const QString BIT_RATE;
    static const QString FILTERS;

public slots:
    bool init (const QVariantMap & options);
    bool send (CanMessage * message);
    bool stop (void);

signals: // from original CanDriver
    void recv (CanMessage * msg);

private:
    bool m_valid;
    qint64 m_initialTime;
    HANDLE m_canDevHanldle;
    HANDLE m_canCtrlHandle;
    HANDLE m_canChannelHandle;
    QThread * m_thread;
    CanDriver_ixxatVciPollWorker * m_worker;
};

#ifndef QTCAN_STATIC_DRIVERS

class QTCAN_DRIVER_EXPORT CanDriverPlugin_ixxatVci : public QObject, public CanDriverPlugin {
    Q_OBJECT
    Q_INTERFACES (CanDriverPlugin)
    Q_PLUGIN_METADATA (IID "QtCAN.CanDriverPlugin")

public:
    QString getDriverName (void);
    CanDriver * createDriverInstance (QObject * parent = Q_NULLPTR);
    QList<CanDriverOption *> optionsRequired (void);
};

#endif

#endif // CANDRIVER_IXXATVCI_H
