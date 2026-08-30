@echo off
:: Startet QLC+ 4 (klassische Widgets-UI) direkt per Doppelklick, ohne Parameter.
:: Ruft intern start-qlcplus.bat mit dem Argument "4" auf.
call "%~dp0start-qlcplus.bat" 4 %*
if errorlevel 1 pause
