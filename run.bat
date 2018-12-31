@echo off

if not exist base mkdir base

build\%1 -cwd ..\base %2 %3 %4 %5 %6 %7 %8 %9
if %ERRORLEVEL% equ 0 echo Program exited normally
if %ERRORLEVEL% neq 0 echo Program exited abnormally
