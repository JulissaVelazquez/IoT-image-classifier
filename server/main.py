import uvicorn
from fastapi import FastAPI, File, UploadFile, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, HTMLResponse
from pydantic import BaseModel
import tensorflow as tf
from PIL import Image
import numpy as np
import io
import sys
import nest_asyncio
from pyngrok import ngrok
import asyncio
import datetime
import os
import threading
import time
import logging
import base64
from typing import List

# --- CONFIGURACIÓN ---
NGROK_AUTHTOKEN = "2y6MCXzCtS9WuC3tFSG6E6uAX25_5GHn1ELAL8QURpGSE7TqZ"
IMG_HEIGHT = 224
IMG_WIDTH = 224
MODEL_PATH = 'model/dog_cat_model.h5'

# --- VARIABLES GLOBALES ---
model = None
AUTO_MODE = False  # False = Manual, True = Automático (IA manda a OLED)

# --- GESTOR DE WEBSOCKETS ---


class ConnectionManager:
    def __init__(self):
        self.active_connections: List[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket):
        self.active_connections.remove(websocket)

    async def broadcast(self, message: dict):
        for connection in self.active_connections:
            try:
                await connection.send_json(message)
            except:
                pass


manager = ConnectionManager()

# --- MODELOS DE DATOS ---


class OLEDText(BaseModel):
    text: str
    duration_s: int = 5


class SystemMode(BaseModel):
    auto_mode: bool


# --- CONFIGURACIÓN NGROK ---
nest_asyncio.apply()
logging.basicConfig(level=logging.INFO)
NGROK_AUTHTOKEN = os.environ.get("NGROK_AUTHTOKEN", NGROK_AUTHTOKEN)

if NGROK_AUTHTOKEN and NGROK_AUTHTOKEN != "PON_TU_TOKEN_AQUI":
    try:
        ngrok.set_auth_token(NGROK_AUTHTOKEN)
    except Exception:
        logging.exception("Error Auth Token")

try:
    for tunnel in ngrok.get_tunnels():
        ngrok.disconnect(tunnel.public_url)
except Exception:
    pass

# --- CARGAR MODELO ---
print("Cargando modelo TensorFlow...")
try:
    model = tf.keras.models.load_model(MODEL_PATH)
    print("Modelo cargado.")
except Exception as e:
    print(f"Error cargando modelo: {e}")

# --- API FASTAPI ---
app = FastAPI(title="ESP32 Vision Server")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# --- ENDPOINTS ---


@app.get("/", response_class=HTMLResponse)
async def read_root():
    try:
        with open("server/index.html", "r", encoding="utf-8") as f:
            return f.read()
    except FileNotFoundError:
        try:
            with open("index.html", "r", encoding="utf-8") as f:
                return f.read()
        except:
            return "<h1>Error: Falta index.html</h1>"


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        manager.disconnect(websocket)


@app.post("/settings/mode")
async def set_mode(data: SystemMode):
    """Cambia entre modo Manual y Automático desde el Frontend"""
    global AUTO_MODE
    AUTO_MODE = data.auto_mode
    print(f"🔄 MODO CAMBIADO: {'AUTOMÁTICO' if AUTO_MODE else 'MANUAL'}")
    return {"status": "ok", "auto_mode": AUTO_MODE}


@app.post("/display/text")
async def send_to_oled(data: OLEDText):
    """Endpoint para envío MANUAL de texto"""
    print(f"📡 Texto Manual OLED: {data.text}")
    return {"status": "received", "text_sent": data.text}


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
    if model is None:
        raise HTTPException(status_code=503, detail="Modo Pruebas: Sin modelo")

    try:
        contents = await file.read()
        processed_image = preprocess_image(contents)

        prediction = model.predict(processed_image, verbose=0)
        confidence = float(prediction[0][0])

        if confidence > 0.5:
            label = "GATO"
            prob = confidence * 100
        else:
            label = "PERRO"
            prob = (1 - confidence) * 100

        print(f"Predicción: {label} ({prob:.1f}%)")

        # --- LOGICA DEL MODO AUTOMÁTICO ---
        # Si está en auto, mandamos el texto en la respuesta JSON
        oled_response_text = ""
        if AUTO_MODE:
            oled_response_text = f"{label} {int(prob)}%"
            print(f"🤖 AUTO: Enviando '{oled_response_text}' al ESP32")

        # Enviar al Frontend (WebSocket)
        base64_image = base64.b64encode(contents).decode('utf-8')
        await manager.broadcast({
            "type": "new_prediction",
            "prediction": label,
            "confidence": round(prob, 2),
            "image": f"data:image/jpeg;base64,{base64_image}"
        })

        # Respuesta al ESP32
        return {
            "prediction": label,
            "confidence": round(prob, 2),
            "oled_text": oled_response_text,  # <--- EL ESP32 DEBE LEER ESTO
            "auto_mode": AUTO_MODE
        }

    except Exception as e:
        print(f"Error: {e}")
        raise HTTPException(status_code=500, detail=str(e))

# --- RUN ---
if __name__ == "__main__":
    def run_uvicorn():
        try:
            config = uvicorn.Config(
                app=app, host="0.0.0.0", port=8000, log_level="info")
            server = uvicorn.Server(config)
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
            loop.run_until_complete(server.serve())
        except Exception:
            logging.exception("Error uvicorn")

    server_thread = threading.Thread(target=run_uvicorn, daemon=True)
    server_thread.start()
    time.sleep(1.5)

    try:
        tunnel = ngrok.connect(
            8000, domain="usually-stable-lacewing.ngrok-free.app")
        print("="*60)
        print(f"  URL: {tunnel.public_url}")
        print("="*60)
    except Exception:
        logging.exception("Error Ngrok")

    try:
        while server_thread.is_alive():
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("Saliendo...")
