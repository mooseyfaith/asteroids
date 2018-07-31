@echo off

set my_cd=%cd%
pushd ..\mooselib

set mooselib_version=

for /f "USEBACKQ TOKENS=*" %%i in ("%my_cd%\mooselib_version.txt") do set mooselib_version=%mooselib_version%%%i

echo mooselib version: %mooselib_version%

git checkout %mooselib_version%

popd