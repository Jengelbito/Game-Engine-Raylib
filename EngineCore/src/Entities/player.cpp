#include "player.h"

// 1. EL CONSTRUCTOR: Se ejecuta una sola vez cuando creas al jugador
Player::Player() {
    posicion = { 0.0f, 1.0f, 0.0f };
    radio = 0.5f;
    alturaTotal = 2.0f;
    color = GREEN;

    velocidadBase = 0.15f;
    velocidadActual = velocidadBase;
    puntos = 0;
    vidas = 3;

    velocidadY = 0.0f;
    gravedad = -0.015f;
    fuerzaSalto = 0.35f;
    enElSuelo = true;

    puedeRecibirDano = true;
    temporizadorDano = 0.0f;

}

// 2. ACTUALIZAR: Se ejecuta en cada fotograma del bucle (Lógica y Físicas)
void Player::Actualizar(float anguloHorizontal) {
    // Sprint
    if (IsKeyDown(KEY_LEFT_SHIFT)) velocidadActual = velocidadBase * 2.5f;
    else velocidadActual = velocidadBase;

    // Movimiento (relativo a la cámara)
    Vector3 adelante = { sinf(anguloHorizontal), 0.0f, cosf(anguloHorizontal) };
    Vector3 derecha = { cosf(anguloHorizontal), 0.0f, -sinf(anguloHorizontal) };
    Vector3 movimientoDeseado = { 0.0f, 0.0f, 0.0f };

    if (IsKeyDown(KEY_W)) movimientoDeseado = Vector3Subtract(movimientoDeseado, adelante);
    if (IsKeyDown(KEY_S)) movimientoDeseado = Vector3Add(movimientoDeseado, adelante);
    if (IsKeyDown(KEY_A)) movimientoDeseado = Vector3Subtract(movimientoDeseado, derecha);
    if (IsKeyDown(KEY_D)) movimientoDeseado = Vector3Add(movimientoDeseado, derecha);

    if (Vector3Length(movimientoDeseado) > 0) {
        movimientoDeseado = Vector3Normalize(movimientoDeseado);
        posicion.x += movimientoDeseado.x * velocidadActual;
        posicion.z += movimientoDeseado.z * velocidadActual;
    }

    // Gravedad y Salto
    if (IsKeyPressed(KEY_SPACE) && enElSuelo) {
        velocidadY = fuerzaSalto;
        enElSuelo = false;
    }

    posicion.y += velocidadY;
    velocidadY += gravedad;

    if (posicion.y <= 1.0f) {
        posicion.y = 1.0f;
        velocidadY = 0.0f;
        enElSuelo = true;
    }

    // Temporizador de invulnerabilidad
    if (!puedeRecibirDano) {
        temporizadorDano += GetFrameTime();
        if (temporizadorDano >= 1.5f) {
            puedeRecibirDano = true;
            temporizadorDano = 0.0f;
            color = GREEN;
        }
    }
}

// 3. DIBUJAR: Se ejecuta dentro del BeginMode3D
void Player::Dibujar() {
    float alturaCilindro = alturaTotal - (radio * 2.0f);
    Vector3 puntoInferior = { posicion.x, posicion.y - (alturaCilindro / 2.0f), posicion.z };
    Vector3 puntoSuperior = { posicion.x, posicion.y + (alturaCilindro / 2.0f), posicion.z };

    DrawCylinderEx(puntoInferior, puntoSuperior, radio, radio, 16, color);
    DrawSphere(puntoSuperior, radio, color);
    DrawSphere(puntoInferior, radio, color);

    DrawSphereWires(puntoSuperior, radio, 10, 10, MAROON);
    DrawSphereWires(puntoInferior, radio, 10, 10, MAROON);
}

// 4. COLISIONES: Devuelve la caja actual para que el motor la compruebe
BoundingBox Player::ObtenerBoundingBox() {
    return {
        Vector3{ posicion.x - radio, posicion.y - (alturaTotal / 2.0f), posicion.z - radio },
        Vector3{ posicion.x + radio, posicion.y + (alturaTotal / 2.0f), posicion.z + radio }
    };
}