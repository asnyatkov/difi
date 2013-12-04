@echo off
echo Deploying Difi to test vm
xcopy /y /r ..\..\bin\i386\* \\wintestide\upload\Difi
copy ..\apps\Debug\*.exe \\wintestide\upload\Difi
copy ..\apps\Debug\*.dll \\wintestide\upload\Difi
