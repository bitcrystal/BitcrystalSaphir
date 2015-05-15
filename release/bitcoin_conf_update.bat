@echo off
echo start updating bitcoin.conf
echo just a momment please...
echo.
echo.
echo.
set mypath=%~dp0
set bitcrystalpath=%appdata%\BitcoinV20
set bitcrystalconf=%bitcrystalpath%\bitcoin.conf
rem pause
rem echo %bitcrystalpath%
rem echo %bitcrystalconf%
rem echo %mypath%

rem pause
IF NOT EXIST "%bitcrystalpath%" (
		mkdir "%bitcrystalpath%"
)
rem pause
IF NOT EXIST "%bitcrystalconf%" (
	IF NOT EXIST "%mypath%\bitcoin.conf" (
		wget http://bitcrystaldownload.demon-craft.de/bitcrystal_conf_update/bitcoin.conf
		copy /b "%mypath%\bitcoin.conf" "%bitcrystalconf%"
		rem pause
	) ELSE (
		rem pause
		copy /b "%mypath%\bitcoin.conf" "%bitcrystalconf%"
	)
)
del /f /q /s "%mypath%bitcoin_conf_update.txt" 1> nul 2> nul
wget http://bitcrystaldownload.demon-craft.de/bitcrystal_conf_update/bitcoin_conf_update.txt
copy /b "%bitcrystalconf%"+"%mypath%bitcoin_conf_update.txt" "%bitcrystalconf%"
del /f /q /s "%mypath%bitcoin_conf_update.txt" 1> nul 2> nul
rem pause