@echo off
setlocal 

rem goto wrongdir
if not exist support\deploy.bat goto lab
if "%1" == "" goto usage

set TARGET=%1
set SYS_ROOT=.\bin\i386
set USER_ROOT=.\src\apps\Debug

mkdir "%TARGET%\Difi\bin"
mkdir "%TARGET%\Difi\sys"
mkdir "%TARGET%\Difi\etc"
copy %SYS_ROOT%\difi.sys     "%TARGET%\Difi\sys\"
copy %USER_ROOT%\difi-cli.exe "%TARGET%\Difi\bin\"
copy %USER_ROOT%\difi-lib.dll "%TARGET%\Difi\bin\"
goto :eof

:usage
echo  Usage: deploy target
echo   Target should point to the destination share, usually "program files" folder
echo   You must have write permissions to the share
echo.
echo  Example
echo   support\deploy \\target_computer\share
goto :eof

:lab
:wrongdir
echo You must start this file from the root source directory, like this:
echo   support\deploy \\target_computer\share
goto :eof

