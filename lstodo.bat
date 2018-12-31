@echo off

rem Win32 script that prints out all found TODO's in a source code files.
rem May not print an antire TODO text if it is multiline.

set Wildcard=*.cpp *.h

findstr "TODO(" %Wildcard%
