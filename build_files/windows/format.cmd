if EXIST %BLENDER_DIR%\..\lib\win64_vc14\llvm\bin\clang-format.exe (
    set CF_PATH=..\lib\win64_vc14\llvm\bin
    goto detect_done
)

echo clang-format not found
exit /b 1

:detect_done
echo found clang-format in %CF_PATH%

if EXIST %BLENDER_DIR%\..\lib\win64_vc14\python\37\bin\python.exe (
    set PYTHON=%BLENDER_DIR%\..\lib\win64_vc14\python\37\bin\python.exe
    goto detect_python_done
)

echo python not found in lib folder
exit /b 1

:detect_python_done
echo found python (%PYTHON%)

set FORMAT_PATHS=%BLENDER_DIR%\source\tools\utils_maintenance\clang_format_paths.py

REM The formatting script expects clang-format to be in the current PATH.
set PATH=%CF_PATH%;%PATH%

%PYTHON% %FORMAT_PATHS% %FORMAT_ARGS%

:EOF
