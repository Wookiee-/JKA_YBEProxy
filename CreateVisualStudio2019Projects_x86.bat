@REM Create JKA_YBEProxy projects for Visual Studio 2019 using CMake
@echo off
for %%X in (cmake.exe) do (set FOUND=%%~$PATH:X)
if not defined FOUND (
	echo CMake was not found on your system. Please make sure you have installed CMake
	echo from http://www.cmake.org/ and cmake.exe is installed to your system's PATH
	echo environment variable.
	echo.
	pause
	exit /b 1
) else (
	echo Found CMake!
)
if not exist build\nul (mkdir build)
pushd build
cmake -G "Visual Studio 16" -A Win32 -T host=x86 -D MSVC_RUNTIME=static ..
popd
pause