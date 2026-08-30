@echo off
:: Startet QLC+ 4 oder 5 aus dem lokalen Windows-Build.
:: Windows-Pendant zu start-qlcplus.sh. Details: README-WINDOWS-BUILD.md
::
:: Nutzung:
::   start-qlcplus.bat          startet QLC+ 4 (Standard)
::   start-qlcplus.bat 4        startet QLC+ 4 explizit
::   start-qlcplus.bat 5        startet QLC+ 5 (QML-UI)
::   start-qlcplus.bat 5 ARGS   weitere Argumente werden durchgereicht

setlocal
set "VERSION=4"
set "ROOT=%~dp0"

if "%~1"=="4" (
    set "VERSION=4"
    shift
)
if "%~1"=="5" (
    set "VERSION=5"
    shift
)

:: %* still contains the already-consumed version arg (shift does not remove
:: it from %*), so rebuild the passthrough args manually from what's left.
set "ARGS="
:collect_args
if "%~1"=="" goto args_done
set "ARGS=%ARGS% %1"
shift
goto collect_args
:args_done

if "%VERSION%"=="4" (
    set "BINDIR=%ROOT%build\main"
    set "BINEXE=qlcplus.exe"
) else (
    set "BINDIR=%ROOT%build-qml\qmlui"
    set "BINEXE=qlcplus-qml.exe"
)

echo Starte QLC+ %VERSION% (%BINEXE%) ...

if not exist "%BINDIR%\%BINEXE%" (
    echo FEHLER: "%BINDIR%\%BINEXE%" wurde nicht gefunden.
    echo Erst bauen - siehe README-WINDOWS-BUILD.md
    pause
    exit /b 1
)

pushd "%BINDIR%"
".\%BINEXE%" %ARGS%
set "EXITCODE=%ERRORLEVEL%"
popd

if not "%EXITCODE%"=="0" pause
exit /b %EXITCODE%
