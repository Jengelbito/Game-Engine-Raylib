#include "coin.h"
#include <cstdlib>

Coin::Coin() {
    posicion = { -3.0f, 0.5f, 3.0f };
    radio = 0.5f;
    activa = true;
}

void Coin::Reaparecer() {
    posicion.x = (float)(rand() % 19 - 9);
    posicion.z = (float)(rand() % 19 - 9);
}