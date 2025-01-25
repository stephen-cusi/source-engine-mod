echo off
setlocal

rem look for prevention of building zips
if /i .%4.==.-nobuild. set no_build=yes
if /i .%4.==.-nobuild. shift /4
if /i .%3.==.-nobuild. set no_build=yes
if /i .%3.==.-nobuild. shift /3
if /i .%2.==.-nobuild. set no_build=yes
if /i .%2.==.-nobuild. shift /2
if /i .%1.==.-nobuild. set no_build=yes
if /i .%1.==.-nobuild. shift /1
 
rem look for prevention of copying binaries up to the xbox
if /i .%3.==.-nocopy. set no_copy=yes
if /i .%3.==.-nocopy. shift /3
if /i .%2.==.-nocopy. set no_copy=yes
if /i .%2.==.-nocopy. shift /2
if /i .%1.==.-nocopy. set no_copy=yes
if /i .%1.==.-nocopy. shift /1
 
rem look for stuff that prevents dependencies from being built
if /i .%2.==.-nodep. set no_dependency=yes
if /i .%2.==.-nodep. shift /2
if /i .%1.==.-nodep. set no_dependency=yes
if /i .%1.==.-nodep. shift /1

rem Deal with all of the different configs
if /i .%1.==.-all.		set do_platform=yes
if /i .%1.==.-all.		set do_hl2=yes
if /i .%1.==.-all.		set do_episodic=yes
if /i .%1.==.-all.		set do_ep2=yes
if /i .%1.==.-all.		set do_tf=yes
if /i .%1.==.-all.		set do_portal=yes

if /i .%1.==.-episodic.	set do_episodic=yes
if /i .%1.==.-ep2.		set do_ep2=yes
if /i .%1.==.-tf.		set do_tf=yes
if /i .%1.==.-portal.	set do_portal=yes
if /i .%1.==.-hl2.		set do_hl2=yes
if /i .%1.==.-platform.	set do_platform=yes

rem skip these if no dependencies should be built
if .%no_dependency%.==.yes. goto START
if /i .%1.==.-ep2.		set do_episodic=yes
if /i .%1.==.-ep2.		set do_hl2=yes
if /i .%1.==.-episodic.	set do_hl2=yes
if /i .%1.==.-tf.		set do_hl2=yes
if /i .%1.==.-portal.	set do_hl2=yes
set do_platform=yes

:START
pushd.
cd %VPROJECT%\..
set XBCP="%XEDK%\bin\win32\xbcp.exe"
set ZIPDIR=%TEMP%\xzips
if not exist %ZIPDIR% mkdir %ZIPDIR%

:PLATFORM
if not .%do_platform%.==.yes. goto HL2

REM
REM PLATFORM
REM
echo Builing platform zip...
pushd.
cd platform
if not exist %ZIPDIR%\platform mkdir %ZIPDIR%\platform
if .%no_build%.==.yes. goto PLATFORMCOPY
if exist %ZIPDIR%\platform\zip0.360.zip del %ZIPDIR%\platform\zip0.360.zip
makegamedata -q -z %ZIPDIR%\platform\zip0.360.zip
:PLATFORMCOPY
if not .%no_copy%.==.yes. %XBCP% /Y %ZIPDIR%\platform\zip0.360.zip xe:\valve\platform\zip0.360.zip
popd.

:HL2if not .%do_hl2%.==.yes. goto EPISODIC
REMREM HL2REMecho Builing hl2 zip...pushd.cd hl2if not exist %ZIPDIR%\hl2 mkdir %ZIPDIR%\hl2if .%no_build%.==.yes. goto HL2COPYif exist %ZIPDIR%\hl2\zip0.360.zip del %ZIPDIR%\hl2\zip0.360.zipmakegamedata -q -z %ZIPDIR%\hl2\zip0.360.zip
:HL2COPYif not .%no_copy%.==.yes. %XBCP% /Y %ZIPDIR%\hl2\zip0.360.zip xe:\valve\hl2\zip0.360.zippopd.

:EPISODICif not .%do_episodic%.==.yes. goto EP2
REMREM EPISODICREMecho Builing episodic zip...pushd.cd episodicif not exist %ZIPDIR%\episodic mkdir %ZIPDIR%\episodicif .%no_build%.==.yes. goto EPISODICCOPYif exist %ZIPDIR%\episodic\zip0.360.zip del %ZIPDIR%\episodic\zip0.360.zipmakegamedata -q -z %ZIPDIR%\episodic\zip0.360.zip
:EPISODICCOPYif not .%no_copy%.==.yes. %XBCP% /Y %ZIPDIR%\episodic\zip0.360.zip xe:\valve\episodic\zip0.360.zippopd.

:EP2if not .%do_ep2%.==.yes. goto TF
  REMREM EP2REMecho Builing ep2 zip...pushd.cd ep2if not exist %ZIPDIR%\ep2 mkdir %ZIPDIR%\ep2if .%no_build%.==.yes. goto EP2COPYif exist %ZIPDIR%\ep2\zip0.360.zip del %ZIPDIR%\ep2\zip0.360.zipmakegamedata -q -z %ZIPDIR%\ep2\zip0.360.zip
:EP2COPYif not .%no_copy%.==.yes. %XBCP% /Y %ZIPDIR%\ep2\zip0.360.zip xe:\valve\ep2\zip0.360.zippopd.

:TFif not .%do_tf%.==.yes. goto PORTAL
REMREM TFREMecho Builing tf2 zip...pushd.cd tfif not exist %ZIPDIR%\tf mkdir %ZIPDIR%\tfif .%no_build%.==.yes. goto TFCOPYif exist %ZIPDIR%\tf\zip0.360.zip del %ZIPDIR%\tf\zip0.360.zipmakegamedata -q -z %ZIPDIR%\tf\zip0.360.zip
:TFCOPYif not .%no_copy%.==.yes. %XBCP% /Y %ZIPDIR%\tf\zip0.360.zip xe:\valve\tf\zip0.360.zippopd.

:PORTALif not .%do_portal%.==.yes. goto END
REMREM PORTALREMecho Builing portal zip...pushd.cd portalif not exist %ZIPDIR%\portal mkdir %ZIPDIR%\portalif .%no_build%.==.yes. goto PORTALCOPYif exist %ZIPDIR%\portal\zip0.360.zip del %ZIPDIR%\portal\zip0.360.zipmakegamedata -q -z %ZIPDIR%\portal\zip0.360.zip
:PORTALCOPYif not .%no_copy%.==.yes. %XBCP% /Y %ZIPDIR%\portal\zip0.360.zip xe:\valve\portal\zip0.360.zippopd.

:ENDpopd.endlocal
