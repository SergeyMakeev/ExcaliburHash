@echo off
set builddir=build2019_32
if not exist %builddir% goto GENERATE
del %builddir% /S /Q
:GENERATE
mkdir %builddir%
cd %builddir%
cmake -G "Visual Studio 16 2019" -A Win32 ../
cd ..

