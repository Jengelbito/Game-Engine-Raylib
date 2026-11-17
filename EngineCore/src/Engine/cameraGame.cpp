#include "cameraGame.h"
#include <cmath>

CameraGame::CameraGame() {
    camara = { 0 };
    anguloHorizontal = 0.0f;
    anguloVertical = 0.3f;
    distanciaAlJugador = 12.0f;
}

void CameraGame::Inicializar(Vector3 targetInicial) {
    camara.position = Vector3{ 0.0f, 5.0f, 10.0f };
    camara.target = targetInicial;
    camara.up = Vector3{ 0.0f, 1.0f, 0.0f };
    camara.fovy = 60.0f;
    camara.projection = CAMERA_PERSPECTIVE;
}

void CameraGame::Actualizar(Vector3 posicionJugador, Vector2 deltaMouse) {
    anguloHorizontal -= deltaMouse.x * 0.005f;
    anguloVertical += deltaMouse.y * 0.005f;

    if (anguloVertical > 1.4f) anguloVertical = 1.4f;
    if (anguloVertical < 0.1f) anguloVertical = 0.1f;

    camara.target = posicionJugador;

    float horizontalDistance = distanciaAlJugador * cosf(anguloVertical);
    camara.position.x = posicionJugador.x + horizontalDistance * sinf(anguloHorizontal);
    camara.position.y = posicionJugador.y + distanciaAlJugador * sinf(anguloVertical);
    camara.position.z = posicionJugador.z + horizontalDistance * cosf(anguloHorizontal);

    UpdateCamera(&camara, CAMERA_CUSTOM);
}