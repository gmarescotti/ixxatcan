#include <QtSerialBus/qcanbusframe.h>
typedef QCanBusFrame CanMessage;
typedef QObject CanDriver;

#include "vcisdk.h"

#include <process.h>
#include <stdio.h>
#include <conio.h>
// #include "SocketSelectDlg.hpp"

HRESULT SelectDevice ( BOOL fUserSelect );

HRESULT CheckBalFeatures(LONG lCtrlNo);
HRESULT InitSocket      (LONG lCtrlNo);

void    FinalizeApp  ( void );

void    TransmitViaPutDataEntry();
void    TransmitViaWriter();

void    ReceiveThread( void* Param );
