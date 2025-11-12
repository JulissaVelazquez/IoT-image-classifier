import uvicorn
from fastapi import FastAPI, File, UploadFile
from fastapi.responses import JSONResponse
import numpy as np
import tensorflow as tf
from PIL import Image
import io

# --- 1. Configuración Inicial ---

# Crea la aplicación FastAPI
app = FastAPI(title="Servidor de Clasificacion (Perros vs Gatos)")

# Carga tu modelo .h5 (¡Esto solo se hace una vez al iniciar!)
try:
    model = tf.keras.models.load_model('model/dog_cat_model.h5')
    print("Modelo cargado exitosamente.")
except Exception as e:
    print(f"Error cargando el modelo: {e}")
    model = None

# Define el tamaño de imagen que tu modelo espera
# (MobileNetV2 usualmente usa 224x224)
IMG_WIDTH = 224
IMG_HEIGHT = 224

# --- 2. Función de Pre-procesamiento ---

def preprocess_image(image_bytes: bytes) -> np.ndarray:
    """
    Toma los bytes de una imagen, la abre, la procesa y la 
    prepara para el modelo.
    """
    # Abre la imagen desde los bytes
    image = Image.open(io.BytesIO(image_bytes))
    
    # Asegúrate de que la imagen esté en modo RGB
    if image.mode != "RGB":
        image = image.convert("RGB")
        
    # Redimensiona la imagen al tamaño que el modelo espera
    image = image.resize((IMG_WIDTH, IMG_HEIGHT))
    
    # Convierte la imagen a un array de numpy
    image_array = np.array(image)
    
    # Normaliza la imagen (escala de 0-255 a 0-1)
    # (Asegúrate de que esto coincida con cómo entrenaste tu modelo)
    image_array = image_array / 255.0
    
    # Añade una dimensión extra (batch)
    # El modelo espera (1, 224, 224, 3) en lugar de (224, 224, 3)
    return np.expand_dims(image_array, axis=0)


# --- 3. El Endpoint de la API ---

@app.post("/predict")
async def predict(file: UploadFile = File(...)):
    """
    Endpoint principal. Recibe un archivo de imagen, lo procesa
    y devuelve una predicción en JSON.
    """
    if not model:
        return JSONResponse(status_code=500, content={"error": "Modelo no está cargado."})

    # Lee los bytes de la imagen enviada por el ESP32
    image_bytes = await file.read()
    
    try:
        # Pre-procesa la imagen
        processed_image = preprocess_image(image_bytes)
        
        # --- 4. Realiza la Predicción ---
        prediction = model.predict(processed_image)
        
        # El resultado de un modelo binario (sigmoid) es un solo número (ej. [[0.98]])
        confidence = float(prediction[0][0])
        
        # Interpreta el resultado (asumiendo 0=PERRO, 1=GATO)
        if confidence > 0.5:
            label = "GATO"
            percent_confidence = confidence * 100
        else:
            label = "PERRO"
            percent_confidence = (1 - confidence) * 100
            
        print(f"Prediccion: {label} (Conf: {percent_confidence:.2f}%)")

        # --- 5. Devuelve el JSON al ESP32 ---
        return {
            "prediction": label,
            "confidence": f"{percent_confidence:.2f}"
        }

    except Exception as e:
        print(f"Error procesando la imagen: {e}")
        return JSONResponse(status_code=400, content={"error": "No se pudo procesar la imagen."})

# --- 6. Cómo ejecutar el servidor (para desarrollo) ---
if __name__ == "__main__":
    # Inicia el servidor en el host 0.0.0.0 para que sea accesible
    # desde otros dispositivos en tu red (como el ESP32)
    uvicorn.run(app, host="0.0.0.0", port=8000)