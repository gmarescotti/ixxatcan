@echo off
@echo Compile and install IXXAT plugin for Qt Creator Projects

where /q git
IF ERRORLEVEL 1 (
    ECHO GIT is missing. Ensure it is installed and placed in your PATH.
	goto end
)

where /q qmake
IF ERRORLEVEL 1 (
    ECHO qmake is missing. Ensure to include in your PATH the qt binary folder.
	goto end
)

where /q jom
IF ERRORLEVEL 1 (
    ECHO jom is missing. Ensure to include in your PATH the qt binary folder.
	goto end
)

cd /D %temp%

set MYTEMP=RGM%RANDOM%

rem try MVSC2019 then MSVC2017
set PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build
set PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\VC\Auxiliary\Build

echo %PATH%

mkdir %MYTEMP%
cd %MYTEMP%

git.exe clone https://github.com/gmarescotti/ixxatcan.git 

CALL vcvars64.bat
pushd ixxatcan
qmake
C:\Qt\Tools\QtCreator\bin\jom.exe install
popd

:end
@echo IXXAT build terminated, click to remove temporary files and exit
pause

rmdir /S /Q %MYTEMP%

