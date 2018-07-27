@echo off

set exe_name=Astroids
set dll_name=moose_astroids
set application_init_function=application_init
set application_main_loop_function=application_main_loop

rem paths relative to build dir
set moose_dir=%cd%\..\mooselib

set include_dirs=/I "%moose_dir%\3rdparty" /I "%moose_dir%\3rdparty\freetype\include\freetype2" /I "%moose_dir%\code"
set libs=kernel32.lib user32.lib gdi32.lib opengl32.lib
set srcs="%moose_dir%\code\win32_platform.cpp"
set options=/Zi /nologo /EHsc
set link_options=/link /INCREMENTAL:NO

if not exist build\ mkdir build
pushd build

rem check for live-code-editing: try to rename .exe, if its not possible, we assume the .exe is running
if exist "%exe_name%.exe" (
	COPY /B "%exe_name%.exe"+NUL "%exe_name%.exe" > NUL 2> NUL

	if errorlevel 1 goto enable_live_code_editing
)

set live_code_editing=0
echo normal compile mode
goto skip_enable_live_code_editing

:enable_live_code_editing:
echo live code editing mode
set live_code_editing=1

:skip_enable_live_code_editing
rem end of live-code-editing check

rem echo buid directory %cd%

set mode="debug"

if %mode%=="debug" (
  set libs=%libs% "%moose_dir%\3rdparty\freetype\lib\freetyped.lib"
  set options=%options% /Od /DDEBUG /MTd
  echo debug mode
) else (
  set libs=%libs% "%moose_dir%\3rdparty\freetype\lib\freetype.lib"
  set options=%options% /O2 /MT
  echo release mode
)

set t=%time:~0,8%
set t=%t::=-%

del *.pdb > NUL 2> NUL

echo "compiling dll" > compile_dll_lock.tmp
rem option /ignore:4099 disables linker warning, that .pdb files for freetype.lib do not exists (we only use release build)
cl -Fe%dll_name% %options% "%cd%\..\code\main.cpp" %libs% /DWIN32 /DWIN32_EXPORT %include_dirs% /LD %link_options% /ignore:4099 /PDB:"%dll_name%%date%-%t%.pdb"

if errorlevel 1 (
	del compile_dll_lock.tmp
	popd
	exit /B
)

del compile_dll_lock.tmp

if %live_code_editing%==0 (
	cl -Fe"%exe_name%" %options% /DWIN32_DLL_NAME=\"%dll_name%.dll\" /DWIN32_INIT_FUNCTION_NAME=\"%application_init_function%\" /DWIN32_MAIN_LOOP_FUNCTION_NAME=\"%application_main_loop_function%\" %include_dirs% %srcs% %libs% %link_options%

	if errorlevel 1 (
		popd
		exit /B
	)

	if not exist "%exe_name%.sln" (
		echo creating visual studio debug solution
		echo please set the working directory to "%cd%\..\data"
		devenv "%exe_name%.exe"
	)
)

popd
