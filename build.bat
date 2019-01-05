@echo off

rem ---------------------------------------------------------------------------------------------------------------------------------------------
rem Note:
rem  This build script is temporarily hardcoded and configured to build a win32-x64 executable with debug code and debug information included.
rem  In a future we should rewrite it so it can be executed with parameters like target cpu architecture, internal/public build, etc...
rem ---------------------------------------------------------------------------------------------------------------------------------------------

rem General project name, must not contain spaces and deprecated symbols, no extension.
rem In a nutshell, the target game executable will be named as %OutputName%.exe,
rem the target game entities will be named as %OutputName%_ents.dll,
rem and the editor's dll is gonna be %OutputName%_editor.dll.
set OutputName=zdemo

rem Common compiler flags for all projects:
rem -TC					  	   	   	- treat source files only as a pure C code, no C++ at all.
rem -MDd							- [debug] use debug multithreaded DLL version of C runtime library.
rem -Zi					  		  	- [debug] include debug information in a program database compatible with Edit&Continue.
rem -Oi					  			- enable intrinsics generation.
rem -Od								- [debug] disable optimization.
rem -W4								- set warning level to 4.
rem -WX								- treat warnings as errors.
rem -D_CRT_SECURE_NO_WARNINGS=1		- shut VC's screams about some standard CRT functions usage, like snprintf().
rem -DWIN32=1						- signals we are compiling for Windows (some still name it Win32) platform.
rem -DINTERNAL=1					- [debug] signals we are compiling an internal build, not for public release.
rem -DSLOWCODE=1					- [debug] signals we are compiling a paranoid build with slow code enabled.
set CommonCompilerFlags=-MDd -Zi -Oi -Od -W4 -WX -wd4201 -D_CRT_SECURE_NO_WARNINGS -DWIN32=1 -DINTERNAL=1 -DSLOWCODE=1

rem Common linker flags for all projects:
rem -machine:x64					- [x64] target processor architecture is AMD64 (aka x86_64).
rem -subsystem:windows				- the program is a pure native Windows application, not a console one.
rem -opt:ref						- remove unreferenced functions and data.
rem -incremental:no					- disable incremental mode.
set CommonLinkerFlags=-machine:x64 -subsystem:windows -opt:ref -incremental:no

rem Create 'build' directory if not created yet
if not exist build mkdir build

rem Compile 'game'.
rem Used external libraries, except kernel32.lib:
rem 'user32.lib'  			 		- for windows API general functions.
rem 'shell32.lib'					- for windows API shell functions, like SHGetKnownFolderPath().
rem 'advapi32.lib'					- for windows API "advanced" functions, such as GetUserNameA().
rem 'gdi32.lib'						- for windows API graphics functions, such as GetDC() or ReleaseDC().
rem 'ole32.lib'						- for Component-Object-Model (COM) technology.
rem 'winmm.lib'						- for mmsystem.h interface, timeBeginPeriod()/timeEndPeriod().
pushd build
cl -Fe%OutputName%.exe -Fm%OutputName%.map %CommonCompilerFlags% ..\game.cpp /link %CommonLinkerFlags% user32.lib shell32.lib advapi32.lib gdi32.lib ole32.lib winmm.lib -pdb:%OutputName%.pdb
popd

rem Compile 'ents'.
rem We name the PDB here with %RANDOM% prefix so the live code reloading tenchnique works properly.
rem We also silently remove all previously generated PDBs for cleanness.
pushd build
del %OutputName%_ents_*.pdb > NUL 2> NUL
echo "WAITING FOR PDB" > %OutputName%_ents.lck
cl -Fe%OutputName%_ents.dll -Fm%OutputName%_ents.map %CommonCompilerFlags% ..\ents.cpp /link %CommonLinkerFlags% -pdb:%OutputName%_ents_%RANDOM%.pdb -dll -export:RegisterAllEntities
del %OutputName%_ents.lck
popd
