@echo off
set my_cd=%cd%
pushd ..\mooselib
git rev-parse HEAD  > %my_cd%\mooselib_version.txt
popd