#include "Enemy.h"

Enemy::Enemy() {
    posicion = { 4.0f, 1.0f, -2.0f };
    tamano = { 2.0f, 2.0f, 2.0f };
    velocidad = 0.08f;
}

void Enemy::ActualizarMovimiento() {
    posicion.x += velocidad;
    if (posicion.x > 8.0f || posicion.x < -8.0f) {
        velocidad = -velocidad;
    }
}

BoundingBox Enemy::ObtenerColision() {
    return BoundingBox{
        Vector3{ posicion.x - tamano.x / 2.0f, posicion.y - tamano.y / 2.0f, posicion.z - tamano.z / 2.0f },
        Vector3{ posicion.x + tamano.x / 2.0f, posicion.y + tamano.y / 2.0f, posicion.z + tamano.z / 2.0f }
    };
}