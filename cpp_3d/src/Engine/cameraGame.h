#pragma once
#include "raylib.h"

struct CameraGame {
    Camera3D camara;
    float anguloHorizontal;
    float anguloVertical;
    float distanciaAlJugador;

    CameraGame();
    void Inicializar(Vector3 targetInicial);
    void Actualizar(Vector3 posicionJugador, Vector2 deltaMouse);
};