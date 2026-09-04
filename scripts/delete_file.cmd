@echo off
setlocal

set "target_dir=%cd%\win"

if exist "%target_dir%" (
    echo Deleting files...
    del /s /q "%target_dir%\*.pdb"
    del /s /q "%target_dir%\*.lib"

    echo Deletion complete.
) else (
    echo The "win" directory does not exist.
)

endlocal