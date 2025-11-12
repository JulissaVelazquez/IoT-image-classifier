#!/bin/bash
echo "Creando el entorno virtual (venv)..."

# Usar python3 es el estandar en Unix
python3 -m venv venv

echo ""
echo "Instalando dependencias..."

# Llama al 'pip' que esta DENTRO del venv
./venv/bin/pip install -r requirements.txt

echo ""
echo "--- ¡Configuracion completada! ---"
echo ""
echo "Para activar el entorno, corre:"
echo "source venv/bin/activate"
echo ""
echo "Despues, para iniciar el servidor, corre:"
echo "python main.py"
echo ""