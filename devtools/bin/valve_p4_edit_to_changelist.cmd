@echo off

:: // This will make all new env variables local to this script setlocal

:: // If called with the start command, we need to exit, so also make sure you pass EXIT as the third param!
:: // Also, if you modify this script, make sure that endlocal and exit are within ()'s so valveExitArg works!
:: // Type 'help set' at a command prompt if you don't understand why.if NOT "%3"=="EXIT" set valveExitArg=/b

:: // Make sure we have 2 argsif .%2.==.. (
	echo  *** [valve_p4_edit_to_changelist] Error calling command! No file or changelist specified for checkout! Usage: valve_p4_edit_to_changelist.cmd file "Description" [EXIT]
	endlocal
	exit %valveExitArg% 1
)

:: // Get file infoset valveTmpFileName="%~n1%~x1"set valveTmpFullFilePath="%~f1"set valveTmpPathOnly="%~d1%~p1"
if "%valveTmpFileName%"==""	(
	echo  *** [valve_p4_edit_to_changelist] Error! Can't parse filename "%1"!
	endlocal
	exit %valveExitArg% 1
)
if "%valveTmpFullFilePath%"==""	(
	echo  *** [valve_p4_edit_to_changelist] Error! Can't parse filename "%1"!
	endlocal
	exit %valveExitArg% 1
)
if "%valveTmpPathOnly%"==""	(
	echo  *** [valve_p4_edit_to_changelist] Error! Can't parse filename "%1"!
	endlocal
	exit %valveExitArg% 1
)

:: // Change directories so that the p4 set commands give use useful datapushd %valveTmpPathOnly%

:: // Find userfor /f "tokens=2 delims== " %%A in ('p4 set ^| find /i "P4USER="') do set valveP4User=%%Aif "%valveP4User%"=="" goto RegularCheckoutrem //echo User="%valveP4User%"

:: // Find clientfor /f "tokens=2 delims== " %%A in ('p4 set ^| find /i "P4CLIENT="') do set valveP4Client=%%Aif "%valveP4Client%"=="" goto RegularCheckoutrem //echo Client="%valveP4Client%"

:: // Search for existing changelist that matches command line argset valveP4ChangelistName=%2%set valveP4ChangelistName=%valveP4ChangelistName:~1,-1%for /f "tokens=2 delims= " %%A in ('p4 changes -u %valveP4User% -s pending -c %valveP4Client% ^| sort /r ^| find /i "'%valveP4ChangelistName%"') do set valveP4ChangelistNumber=%%Aif NOT "%valveP4ChangelistNumber%"=="" goto HaveChangelist

:: // We didn't find a matching changelist but we did figure enough out to create a new changelistrem //echo Creating New Changelistfor /f "tokens=2 delims= " %%A in ('^( echo Change: new ^& echo Client: %valveP4Client% ^& echo User: %valveP4User% ^& echo Status: new ^& echo Description: %valveP4ChangelistName%^&echo.^) ^| p4 change -i') do set valveP4ChangelistNumberJustCreated=%%Aif "%valveP4ChangelistNumberJustCreated%"=="" goto RegularCheckout

:: // Now search for the changelist number even though we already have it to try to clean up after the race condition when it's hit
:: // This way, if more than one changelist is created in parallel, this will hopefully cause them to be checked out into the same changelist and the empty one deletedfor /f "tokens=2 delims= " %%A in ('p4 changes -u %valveP4User% -s pending -c %valveP4Client% ^| sort /r ^| find /i "'%valveP4ChangelistName%"') do set valveP4ChangelistNumber=%%Aif "%valveP4ChangelistNumber%"=="" goto RegularCheckout
if NOT "%valveP4ChangelistNumber%"=="%valveP4ChangelistNumberJustCreated%" p4 change -d %valveP4ChangelistNumberJustCreated% 2>&1 >nul

:: // We have a changelist number
:HaveChangelistset valveP4ChangelistArg=-c %valveP4ChangelistNumber%rem //echo valveP4ChangelistArg="%valveP4ChangelistArg%"rem //echo ChangelistNumber="%valveP4ChangelistNumber%"rem //echo ChangelistName="%valveP4ChangelistName%"

:: // Check the file out
:RegularCheckoutSET SEDPATH=%~dp0sed.exerem The sed command replaces '//' with the empty string and replaces '...' with '---'rem This is necessary so that VS will not interpret "//source2/main" or "... source2/main" asrem a path. Interpreting it as a path means that pressing F8 in the VS error window will stop onrem these output lines, which is a nuisance when looking for real errors and warnings, and it meansrem that VS will try to open \\source2\main\... which will often cause a 2-3 second hang!if "%VALVE_WAIT_ON_P4%"=="" (
	p4 edit %valveP4ChangelistArg% %valveTmpFullFilePath% 2>&1 | %SEDPATH% -e s!//!! -e s!\.\.\.!---! | find /v /i "- currently opened for edit" | find /v /i "- also opened by" | find /v /i "- file(s) not on client" | find /v /i "- can't change from"
) ELSE (
	:: // Filter out largely benign messages unless we're explicitly waiting on p4 results a la buildbot
	p4 edit %valveP4ChangelistArg% %valveTmpFullFilePath% 2>&1 | %SEDPATH% -e s!//!! -e s!\.\.\.!---! | find /v /i "- also opened by"
)
goto End

:Endpopd
( endlocal
  exit %valveExitArg% 0 )
