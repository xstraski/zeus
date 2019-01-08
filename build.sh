# ---------------------------------------------------------------------------------------------------------------------------------------------
# Note:
#  This build script is temporarily hardcoded and configured to build a linux-x64 executable with debug code and debug information included.
#  In a future we should rewrite it so it can be executed with parameters like target cpu architecture, internal/public build, etc...
# ---------------------------------------------------------------------------------------------------------------------------------------------

# General project name, must not contain spaces and deprecated symbols, no extension.
# In a nutshell, the target game executable will be named as %OutputName%,
# the target game entities will be named as %OutputName%_ents.so,
# and the editor's dll is gonna be %OutputName%_editor.so.
OutputName="zdemo"

# Common compiler flags for all projects:
CommonCompilerFlags="-g -std=c++11 -DINTERNAL=1 -DSLOWCODE=1 -DLINUX=1"

# Stop at errors.
set -e

# Make 'build' directory.
mkdir -p build

# Compile 'game'.
g++ $CommonCompilerFlags "game.cpp" -o ./build/$OutputName -lm -lpthread -ldl -lX11 -lXext

# Compile 'ents'.
OutputEntsName=$OutputName
OutputEntsName+="_ents.so"
g++ $CommonCompilerFlags "ents.cpp" -o ./build/$OutputEntsName -shared -fPIC
