@echo off
:: Startet QLC+ 5 (QML-UI) direkt per Doppelklick, ohne Parameter.
:: Ruft intern start-qlcplus.bat mit dem Argument "5" auf.
call "%~dp0start-qlcplus.bat" 5 %*
if errorlevel 1 pause
