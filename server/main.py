import uvicorn
from fastapi import FastAPI, File, UploadFile, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import HTMLResponse
from pydantic import BaseModel
import tensorflow as tf
from PIL import Image
import numpy as np
import io
import base64
from typing import List

# --- CONFIGURACIÓN ---
IMG_HEIGHT = 64
IMG_WIDTH = 64
MODEL_PATH = 'model/dog_cat_model.h5' 

# --- VARIABLES GLOBALES ---
model = None
AUTO_MODE = False
LAST_OLED_MESSAGE = ""
ESP32_CONNECTED = False

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

# --- CARGAR MODELO ---
print(f"Cargando modelo desde {MODEL_PATH}...")
try:
    model = tf.keras.models.load_model(MODEL_PATH)
    print("✅ Modelo cargado exitosamente.")
except Exception as e:
    print(f"❌ Error cargando modelo: {e}")

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
        path = "server/index.html"
        try:
            with open(path, "r", encoding="utf-8") as f: return f.read()
        except:
            with open("index.html", "r", encoding="utf-8") as f: return f.read()
    except:
        return "<h1>Error: No encuentro index.html</h1>"

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    await websocket.send_json({"type": "handshake_event", "status": ESP32_CONNECTED})
    try:
        while True: await websocket.receive_text()
    except WebSocketDisconnect:
        manager.disconnect(websocket)

@app.post("/settings/mode")
async def set_mode(data: SystemMode):
    global AUTO_MODE
    AUTO_MODE = data.auto_mode
    print(f"🔄 MODO: {'AUTOMÁTICO' if AUTO_MODE else 'MANUAL'}")
    return {"status": "ok", "auto_mode": AUTO_MODE}

@app.post("/display/text")
async def send_to_oled(data: OLEDText):
    global LAST_OLED_MESSAGE
    print(f"📡 Texto para OLED: '{data.text}'")
    LAST_OLED_MESSAGE = data.text
    return {"status": "received", "text_sent": data.text}

@app.get("/handshake")
async def handshake():
    global ESP32_CONNECTED
    ESP32_CONNECTED = True
    print("🤝 ESP32 Conectado")
    await manager.broadcast({"type": "handshake_event", "status": True})
    return {"status": "connected"}

@app.get("/esp32/message")
async def get_message():
    global LAST_OLED_MESSAGE
    if LAST_OLED_MESSAGE:
        msg = LAST_OLED_MESSAGE
        LAST_OLED_MESSAGE = ""
        return {"has_message": True, "text": msg}
    return {"has_message": False}

# --- LA FUNCIÓN DE PREPROCESAMIENTO SIMPLE (La que funcionaba) ---
def preprocess_image(image_bytes):
    image = Image.open(io.BytesIO(image_bytes))
    
    # 1. Escala de Grises (Crucial para tu modelo)
    if image.mode != "L":
        image = image.convert("L")
    
    # 2. Redimensionar DIRECTO (Sin recortes complicados)
    image = image.resize((IMG_WIDTH, IMG_HEIGHT))
    
    img_array = np.array(image)
    
    # 3. Dar formato correcto: (Batch, Alto, Ancho, Canal)
    img_array = np.expand_dims(img_array, axis=-1) # Agrega el canal 1
    img_array = np.expand_dims(img_array, axis=0)  # Agrega el batch
    
    # 4. Normalizar
    img_array = img_array.astype('float32') / 255.0
    
    return img_array

@app.post("/predict")
async def predict(file: UploadFile = File(...)):
    if model is None: raise HTTPException(status_code=503, detail="Sin modelo")
    try:
        contents = await file.read()
        processed_image = preprocess_image(contents)
        
        # Predicción
        prediction = model.predict(processed_image, verbose=0)
        score = float(prediction[0][0])

        if score > 0.5:
            label = "PERRO"
            prob = score * 100
        else:
            label = "GATO"
            prob = (1 - score) * 100

        print(f"📸 Foto recibida -> {label} ({prob:.1f}%)")

        oled_text = ""
        if AUTO_MODE: oled_text = f"{label} {int(prob)}%"

        # Enviar a Web
        b64_img = base64.b64encode(contents).decode('utf-8')
        await manager.broadcast({
            "type": "new_prediction",
            "prediction": label,
            "confidence": round(prob, 2),
            "image": f"data:image/jpeg;base64,{b64_img}"
        })

        return {
            "prediction": label, 
            "confidence": round(prob, 2), 
            "oled_text": oled_text
        }
    except Exception as e:
        print(f"Error en predict: {e}")
        raise HTTPException(status_code=500, detail=str(e))

# --- ARRANQUE ---
if __name__ == "__main__":
    print(f"🚀 SERVIDOR INICIANDO EN LOCAL")
    uvicorn.run(app, host="0.0.0.0", port=8000)