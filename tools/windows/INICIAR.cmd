@echo off
cd /d "%~dp0"
echo MUNDO X - Interferencia y medicion de Bell
echo.
mundo_x.exe run examples\measurement_interference.x
if errorlevel 1 goto failed
echo.
mundo_x.exe run examples\measurement_bell.x
if errorlevel 1 goto failed
echo.
echo Practica con 2 qubits: h 0, cx 0 1, probz 0, measure 0, probz 1
echo Escribe quit para salir. Las demostraciones anteriores no cambian esta sesion.
mundo_x.exe practice 2
if errorlevel 1 goto failed
exit /b 0
:failed
echo La ejecucion fallo. Conserva este mensaje para revisar el error.
pause
exit /b 1
