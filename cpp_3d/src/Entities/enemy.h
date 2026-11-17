#pragma once
#include "raylib.h"

struct Enemy {
    Vector3 posicion;
    Vector3 tamano;
    float velocidad;

    Enemy();
    void ActualizarMovimiento();
    BoundingBox ObtenerColision();
    bool hasScript = false;
    char scriptPath[512] = "";
    char scriptSource[8192];
};