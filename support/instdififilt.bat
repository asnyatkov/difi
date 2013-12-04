setlocal
set DEVICE=\Ide\IdeDeviceP0T0L0-0
copy /y "c:\Program Files\Difi\sys\difi.sys" \windows\system32\drivers\*
addfilter /device %DEVICE%  /add difi
psshutdown /r /t 0

