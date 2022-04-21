@echo off
set builddir=build2022
if not exist %builddir% goto GENERATE
del %builddir% /S /Q
:GENERATE
mkdir %builddir%
cd %builddir%
cmake -G "Visual Studio 17 2022" ../
cd ..

