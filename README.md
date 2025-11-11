# IoT-image-classifier
ESP32-S3 Wireless Image Classifier: Dogs vs. Cats
This project is a Distributed Computer Vision IoT system that classifies images in real-time. It uses a Client-Server architecture where an ESP32-S3 acts as an edge device (handling capture and display) and a high-performance FastAPI server processes the AI inference.
Project Description
The objective is to differentiate between dogs and cats using computer vision, overcoming the hardware limitations of microcontrollers by delegating the heavy processing to a backend server.
The workflow is as follows:
Capture: The ESP32-S3 takes a photograph with the OV2640 camera.
Transmission: The image is sent via Wi-Fi through an HTTP POST request to a REST API.
Processing: The server (FastAPI) receives the image, pre-processes it, and feeds it into a Convolutional Neural Network (MobileNetV2) trained using Transfer Learning.
Response: The server returns the prediction and the confidence percentage in JSON format.
Feedback: The ESP32 receives the response and displays the result ("DOG" or "CAT") on an SSD1306 OLED display.
Tech Stack
Hardware
Microcontroller: Freenove ESP32-S3 WROOM (Arduino compatible).
Camera Sensor: OV2640.
Display: 0.96" I2C OLED (SSD1306).
Software (Backend - Server)
Language: Python 3.x
Web Framework: FastAPI (for its high speed and asynchronous handling).
ASGI Server: Uvicorn.
AI / ML: TensorFlow & Keras.
Model: MobileNetV2 (Transfer Learning on the Kaggle Dogs vs Cats dataset).
Image Processing: OpenCV / Pillow.
Software (Frontend - Embedded)
Framework: Arduino (C++).
IDE: Visual Studio Code + PlatformIO.
Key Libraries: esp_camera, HTTPClient, Arduino_JSON, Adafruit_SSD1306.


