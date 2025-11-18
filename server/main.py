import uvicorn
from fastapi import FastAPI, File, UploadFile, HTTPException
from fastapi.middleware.cors import CORSMiddleware
import tensorflow as tf
from PIL import Image
import numpy as np
import io
import sys
import nest_asyncio
from pyngrok import ngrok
import asyncio

# --- CONFIGURACIÓN ---
# TODO: poner token real
NGROK_AUTHTOKEN = "PON_TU_TOKEN_AQUI"
MODEL_PATH = 'model/dog_cat_model.h5'

IMG_HEIGHT = 224
IMG_WIDTH = 224

# ---  abrir tuneles ngrok ---
nest_asyncio.apply()

# Autenticación
if NGROK_AUTHTOKEN and NGROK_AUTHTOKEN != "PON_TU_TOKEN_AQUI":
    ngrok.set_auth_token(NGROK_AUTHTOKEN)

# Cierra túneles para no tronar la cuota de ngrok
try:
    tunnels = ngrok.get_tunnels()
    for tunnel in tunnels:
        ngrok.disconnect(tunnel.public_url)
    print("Túneles anteriores cerrados correctamente.")
except Exception as e:
    print(f"Nota al cerrar túneles: {e}")

# Abrir nuevo túnel
try:
    # Conecta al puerto 8000
    public_url = ngrok.connect(8000).public_url
    print("="*60)
    print(f" URL PÚBLICA NUEVA: {public_url}")
    print(f" Swagger UI:        {public_url}/docs")
    print("="*60)
except Exception as e:
    print(f" Error fatal iniciando Ngrok: {e}")
    sys.exit(1)


# ---  CARGAR MODELO ---
print("Cargando modelo TensorFlow...")
try:
    model = tf.keras.models.load_model(MODEL_PATH)
    print("Modelo cargado.")
except Exception as e:
    print(f"Error cargando modelo en {MODEL_PATH}: {e}")
    sys.exit(1)


# ---  API FASTAPI ---
app = FastAPI(title="ESP32 Vision Server (Ngrok Auto)")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


def preprocess_image(image_bytes):
    image = Image.open(io.BytesIO(image_bytes))
    if image.mode != "RGB":
        image = image.convert("RGB")
    image = image.resize((IMG_WIDTH, IMG_HEIGHT))
    img_array = np.array(image)
    img_array = np.expand_dims(img_array, axis=0)
    img_array = img_array / 255.0
    return img_array


@app.post("/predict")
async def predict(file: UploadFile = File(...)):
    try:
        contents = await file.read()
        processed_image = preprocess_image(contents)

        prediction = model.predict(processed_image, verbose=0)
        confidence = float(prediction[0][0])

        # Umbral (0 = Perro, 1 = Gato aprox)
        if confidence > 0.5:
            label = "GATO"
            prob = confidence * 100
        else:
            label = "PERRO"
            prob = (1 - confidence) * 100

        print(f"Recibido. Predicción: {label} ({prob:.1f}%)")

        return {
            "prediction": label,
            "confidence": round(prob, 2)
        }

    except Exception as e:
        print(f"Error: {e}")
        raise HTTPException(status_code=500, detail=str(e))

# --- RUN ---
if __name__ == "__main__":
    config = uvicorn.Config(app=app, host="0.0.0.0", port=8000)
    server = uvicorn.Server(config)
    loop = asyncio.get_event_loop()
    loop.run_until_complete(server.serve())
