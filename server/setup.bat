@echo off
echo Creando el entorno virtual (venv)...

:: Intenta usar 'python' primero. Si no funciona, prueba 'py -3'.
python -m venv venv
if %errorlevel% neq 0 (
    echo "Usando 'py -3'..."
    py -3 -m venv venv
)

echo.
echo Instalando dependencias...

:: Llama al 'pip' que esta DENTRO del venv para instalar todo
call .\venv\Scripts\pip.exe install -r requirements.txt

echo.
echo --- ¡Configuracion completada! ---
echo.
echo Para activar el entorno, corre:
echo .\venv\Scripts\activate
echo.
echo Despues, para iniciar el servidor, corre:
echo python main.py
echo.
pause