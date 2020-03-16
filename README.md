# QT-CAN Plugin for IXXAT USB adapter

Here is an implementation of a Qt plugin for the IXXAT USB adapter V2.
The plugin should be installed in the Qt root plugin folder then it will be
available in any Qt CAN example tools, like "CAN Example" (see Testing).

*Now is working with Windows 7.*

## Requirements

- Windows (*by now tested on "Windows 7"*)
- Qt Creator 5.13

## Installation

follow these steps to install the plugin in Qt.

[These tests used the toolchain *MSVC2017 64bit*]

#### Clone the repository

clone in `C:\Qt\5.13.0\msvc2017_64\plugins\canbus\ixxatcan`

#### Add project to QT CAN PLUGIN "engine"

Edit the file `C:\Qt\5.13.0\Src\qtserialbus\src\plugins\canbus\canbus.pro` 
and add **ixxatcan** as SUBDIRS in qtConfig(library).

#### Compile project

Open C:\Qt\5.13.0\Src\qtserialbus\src\plugins\canbus\ixxatcan\ixxatcan.pro and compile

#### copy plugin in Qt root folder

from C:\Qt\5.13.0\Src\qtserialbus\src\plugins\canbus\build-ixxatcan-Desktop_Qt_5_13_0_**MSVC2017_64bit**-Debug\plugins\canbus
copy qtixxatcanbus.dll and qtixxatcanbusd.pdb
to C:\Qt\5.13.0\\**msvc2017_64**\plugins\canbus

## Testing

[Tested on "Window 7"]
Launch the tool "CAN Example" from Qt Creator opening folder "C:\Qt\Examples\Qt-5.13.0\serialbus\can"

## TODO 

- Could be useful have a deploy target to copy dll and pdb files automatically in Qt folder
- Add Linux support
- Tests

## Documentation

Starting from the QT manual and from some google search
I follow these two links.

- [Implementing a Custom CAN Plugin](https://doc.qt.io/qt-5/qtcanbus-backends.html)
- [The new Qt CAN stack 2.0](http://gitlab.unique-conception.org/qt-can-2.0)

Reading the first link, starting from a plugin in the folder ```C:\Qt\5.13.0\Src\qtserialbus\src\plugins\canbus```
the generic..

## Implementing a Custom CAN Plugin

https://doc.qt.io/qt-5/qtcanbus-backends.html

If the plugins provided by Qt are not suitable for the required target platform, 
a custom CAN bus plugin can be implemented. The implementation follows the standard way of implementing Qt plug-ins. 
The custom plugin must be deployed to $QTDIR/plugins/canbus.

plugin.json:
```
{
    "Key": "ixxatcan"
}
```

## Starting from non-qt-standard IXXAT plugin

http://gitlab.unique-conception.org/qt-can-2.0

See section/repository:
```
Qt CAN - IXXAT USB driver 
```

