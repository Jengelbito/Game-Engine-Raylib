#pragma once // Evita que el archivo se incluya múltiples veces y cause errores
#include "raylib.h"
#include "raymath.h"
#include <cmath>

class Player {
public:
    // Atributos (Variables)
    Vector3 posicion;
    float radio;
    float alturaTotal;
    Color color;

    float velocidadBase;
    float velocidadActual;
    int puntos;
    int vidas;

    // Físicas
    float velocidadY;
    float gravedad;
    float fuerzaSalto;
    bool enElSuelo;

    // Estados
    bool puedeRecibirDano;
    float temporizadorDano;

    // Métodos (Funciones)
    Player(); // El Constructor: inicializa los valores al nacer

    // Le pasamos el anguloHorizontal porque el jugador necesita saber 
    // hacia dónde mira la cámara para moverse correctamente.
    void Actualizar(float anguloHorizontal);
    void Dibujar();
    BoundingBox ObtenerBoundingBox();

    bool hasScript = false;
    char scriptPath[512] = "";
    char scriptSource[8192];
};