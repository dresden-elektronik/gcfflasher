@echo off
SET OPTS = /VERSION:4.0 /nologo /W4 /O1 /EHsc /std:c11 /utf-8
 

cl %OPTS% %SRCS% /D PL_WIN /D NDEBUG /D PL_NO_ESCASCII /D PL_NO_UTF8 main_windows.c buffer_helper.c gcf.c protocol.c /link SetupAPI.lib Shlwapi.lib Advapi32.lib

del GCFFlasher.exe >nul 2>&1
rename main_windows.exe GCFFlasher.exe