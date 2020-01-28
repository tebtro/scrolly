@echo off


call "%~dp0\shell.bat"
if "%environment_error%"=="true" (
    goto:EOF
)

cmd /k "%ConEmuDir%\..\init.bat" -new_console:d:%src_folder_path%  -new_console:t:scrolly

