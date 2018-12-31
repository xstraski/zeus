@echo off

rem Win32 batch script for cleaning, deletes all unwanted files, that has nothing to do with the source code.
rem Useful when preparing the source code for upload to the server, etc...

pushd build > NUL 2> NUL
rmdir /S /Q .vs > NUL 2> NUL
del /S /Q /F *.obj > NUL 2> NUL
del /S /Q /F *.exe > NUL 2> NUL
del /S /Q /F *.dll > NUL 2> NUL
del /S /Q /F *.csv > NUL 2> NUL
del /S /Q /F *.lib > NUL 2> NUL
del /S /Q /F *.map > NUL 2> NUL
del /S /Q /F *.pdb > NUL 2> NUL
del /S /Q /F *.mdmp > NUL 2> NUL
del /S /Q /F *.log > NUL 2> NUL
del /S /Q /F *.exp > NUL 2> NUL
del /S /Q /F *.manifest > NUL 2> NUL
del /S /Q /F *.tsf > NUL 2> NUL
popd
