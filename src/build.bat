@echo off

IF NOT EXIST ..\run_tree mkdir ..\run_tree
pushd ..\run_tree

ctime -begin .\scrolly_build.ctm

del *.pdb > NUL 2> NUL


:: Warning Options
:: -wd4127  conditional expression is constant
:: -wd4505  'function' : unreferenced local function
:: -wd4701  potentially uninitialized local variable '' used
set WarningOptions=-WL -WX -W4 -wd4201 -wd4100 -wd4189 -wd4505 -wd4701

:: -Od for debbugging and looking at assembly, no optimization
:: -O2 optimization level 2
:: -Oi compiler intrinsics
:: /DEBUG -Zi  or  -Z7
:: -Gm-  minimal build (turn off incremental build)
:: -GR-  turn off runtime type information
:: -EHa-  turn off exception handling
:: -MT package all dlls into the executable!!! (-MTd for debugging)
set CommonCompilerFlags=-MTd -diagnostics:column -diagnostics:caret -Od -Oi -nologo -fp:fast -fp:except- -Gm- -GR- -EHa- -Z7 -Zo -FC %WarningOptions%

:: Additional Linker Options
:: @note :32bit_build  -subsystem:windows,5.1 is needed
:: -subsystem:windows  not needed in 64-bit build
:: @note depends.exe to see what dlls we depend on
:: -opt:ref reduce segments size (-Fmwin32_scrolly.map)
set AdditionalLinkerFlags=-incremental:no -opt:ref

:: Libraries
set Libraries=user32.lib gdi32.lib winmm.lib

:: Compile Time #define's
set CompileTimeDefines=-DBUILD_INTERNAL=1 -DBUILD_SLOW=1 -DBUILD_WIN32=1

set ExecutableName=scrolly

:: 64-bit build
echo WAITING FOR PDB > lock.tmp
cl %CommonCompilerFlags% %CompileTimeDefines% ..\src\scrolly.cpp -Fmscrolly.map -LD /link -DLL -OUT:scrolly.dll -PDB:scrolly_%random%.pdb %AdditionalLinkerFlags% -EXPORT:game_update_and_render

set LastError=%ERRORLEVEL%
del lock.tmp

cl -Fe:%ExecutableName% %CommonCompilerFlags% %CompileTimeDefines% ..\src\win32_scrolly.cpp -Fmwin32_scrolly.map /link %AdditionalLinkerFlags% %Libraries%


:: 32-bit build
:: cl %CommonCompilerFlags% -subsystem:windows,5.1 %CompileTimeDefines% ..\src\win32_scrolly.cpp /link %AdditionalLinkerFlags% %Libraries%


@echo.
ctime -end .\scrolly_build.ctm %LastError%

popd
