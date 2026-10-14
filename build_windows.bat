@echo off
C:\msys64\msys2_shell.cmd -clang64 -defterm -no-start -where "%~dp0" -c "bash ./build_windows.sh"
pause
