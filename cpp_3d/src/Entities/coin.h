#pragma once
#include "raylib.h"

struct Coin {
    Vector3 posicion;
    float radio;
    bool activa;

    Coin();
    void Reaparecer();

    bool hasScript = false;
    char scriptPath[512] = "";
    char scriptSource[8192];
};