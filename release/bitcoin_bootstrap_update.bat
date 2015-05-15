@echo off
echo start updating bootstrap.dat
echo just a momment please...
echo.
echo.
echo.
set mypath=%~dp0
set bitcrystalpath=%appdata%\BitcoinV20
rem pause
rem echo %bitcrystalpath%
rem echo %mypath%
rem pause
IF NOT EXIST "%bitcrystalpath%" (
		mkdir "%bitcrystalpath%"
)
del /f /q /s "%mypath%7z.exe" 1> nul 2> nul
del /f /q /s "%mypath%7z.dll" 1> nul 2> nul
del /f /q /s "%mypath%fast_update_bitcoin.tar" 1> nul 2> nul
rmdir /q /s "%bitcrystalpath%\database" 1> nul 2> nul
rmdir /q /s "%bitcrystalpath%\blocks" 1> nul 2> nul
rmdir /q /s "%bitcrystalpath%\chainstate" 1> nul 2> nul
wget http://bitcrystaldownload.demon-craft.de/bitcrystal_conf_update/7z.exe
wget http://bitcrystaldownload.demon-craft.de/bitcrystal_conf_update/7z.dll
wget http://bitcrystaldownload.demon-craft.de/bitcrystal_conf_update/fast_update_bitcoin.tar
7z -y x "%mypath%fast_update_bitcoin.tar"
del /f /q /s "%mypath%7z.exe" 1> nul 2> nul
del /f /q /s "%mypath%7z.dll" 1> nul 2> nul
del /f /q /s "%mypath%fast_update_bitcoin.tar" 1> nul 2> nul
start "" "%~dp0bitcoin-qt.exe"
exit 0