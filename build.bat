@echo off

set exe_name=Astroids
set dll_name=moose_astroids

rem paths relative to build dir
set moose_dir=%cd%\..\mooselib

set include_dirs=/I "%moose_dir%\3rdparty" /I "%moose_dir%\3rdparty\freetype\include\freetype2" /I "%moose_dir%\code"
set libs=kernel32.lib user32.lib gdi32.lib opengl32.lib
set srcs="%moose_dir%\code\win32_platform.cpp"
set options=/Zi /nologo /EHsc

if not exist data\ mkdir data

if not exist data\app_config.txt (
	@echo application_name            %dll_name%.dll>> data\app_config.txt
	@echo application_init_func       application_init>> data\app_config.txt
	@echo application_main_loop_func  application_main_loop>> data\app_config.txt
)

if not exist build\ mkdir build
pushd build

rem echo buid directory %cd%

set mode="debug"

if %mode%=="debug" (
  set libs=%libs% "%moose_dir%\3rdparty\freetype\lib\freetyped.lib"
  set options=%options% /Od /DDEBUG /MTd
  rem echo debug mode
) else (
  set libs=%libs% "%moose_dir%\3rdparty\freetype\lib\freetype.lib"
  set options=%options% /O2 /MT
  rem echo release mode
)

set t=%time:~0,8%
set t=%t::=-%

del *.pdb > NUL 2> NUL

echo "compiling dll" > compile_dll_lock.tmp
rem option /ignore:4099 disables linker warning, that .pdb files for freetype.lib do not exists (we only use release build)
cl -Fe%dll_name% %options% "%cd%\..\code\main.cpp" %libs% /DWIN32 /DWIN32_EXPORT %include_dirs% /LD /link /INCREMENTAL:NO /ignore:4099 /PDB:"%dll_name%%date%-%t%.pdb"

if errorlevel 1 (
	popd
	exit /B
)

del compile_dll_lock.tmp

cl -Fe"%exe_name%" %options% %include_dirs% %srcs% %libs% /link /INCREMENTAL:NO

if errorlevel 1 (
	popd
	exit /B
)

if not exist "%exe_name%.sln" (
	echo creating visual studio debug solution
	echo please set the working directory to "%cd%\..\data"
	devenv "%exe_name%.exe"
)

popd
