
#define _CRT_SECURE_NO_WARNINGS
#include "raylib.h"
#include "raymath.h"
#include <cstdio>
#include <vector>

enum ShapeType { SHAPE_CUBE = 0, SHAPE_SPHERE, SHAPE_CYLINDER, SHAPE_CONE, SHAPE_PORTAL };
struct EditorBlock { Vector3 position; Vector3 size; Color color; ShapeType shapeType; int targetScene; };

bool LoadProject(std::vector<std::vector<EditorBlock>>& listaEscenas, int& escenaActualIndex,
    Player& miJugador, Coin& miMoneda, Enemy& miEnemigo, ShapeType& forma, const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) return false;
    listaEscenas.clear();
    int numEscenas;
    if (fread(&numEscenas, sizeof(int), 1, file) != 1) { fclose(file); return false; }
    for (int i = 0; i < numEscenas; i++) {
        int numBloques;
        if (fread(&numBloques, sizeof(int), 1, file) != 1) { fclose(file); return false; }
        std::vector<EditorBlock> escena;
        for (int j = 0; j < numBloques; j++) {
            EditorBlock bloque;
            if (fread(&bloque.position, sizeof(Vector3), 1, file) != 1) { fclose(file); return false; }
            if (fread(&bloque.size, sizeof(Vector3), 1, file) != 1) { fclose(file); return false; }
            if (fread(&bloque.color, sizeof(Color), 1, file) != 1) { fclose(file); return false; }
            int shape;
            if (fread(&shape, sizeof(int), 1, file) != 1) { fclose(file); return false; }
            bloque.shapeType = (ShapeType)shape;
            if (fread(&bloque.targetScene, sizeof(int), 1, file) != 1) { fclose(file); return false; }
            escena.push_back(bloque);
        }
        listaEscenas.push_back(escena);
    }
    if (fread(&escenaActualIndex, sizeof(int), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miJugador.posicion, sizeof(Vector3), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miJugador.radio, sizeof(float), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miJugador.alturaTotal, sizeof(float), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miJugador.color, sizeof(Color), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miJugador.puntos, sizeof(int), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miJugador.vidas, sizeof(int), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miMoneda.posicion, sizeof(Vector3), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miMoneda.radio, sizeof(float), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miMoneda.activa, sizeof(bool), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miEnemigo.posicion, sizeof(Vector3), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miEnemigo.tamano, sizeof(Vector3), 1, file) != 1) { fclose(file); return false; }
    int formaInt;
    if (fread(&formaInt, sizeof(int), 1, file) != 1) { fclose(file); return false; }
    forma = (ShapeType)formaInt;
    fclose(file);
    return true;
}

int main() {
    const int screenWidth = 800, screenHeight = 450;
    InitWindow(screenWidth, screenHeight, "Exported Game");
    SetWindowState(FLAG_WINDOW_RESIZABLE);

    Player miJugador;
    Coin miMoneda;
    Enemy miEnemigo;
    ShapeType forma;
    std::vector<std::vector<EditorBlock>> listaEscenas;
    int escenaActualIndex = 0;

    if (!LoadProject(listaEscenas, escenaActualIndex, miJugador, miMoneda, miEnemigo, forma, "export_data.sav")) {
        CloseWindow();
        return 1;
    }

    Camera3D camera = { 0 };
    camera.position = Vector3{ 0, 5, 8 };
    camera.target = Vector3{ 0, 0, 0 };
    camera.up = Vector3{ 0, 1, 0 };
    camera.fovy = 45;
    camera.projection = CAMERA_PERSPECTIVE;

    float velocidadY = 0;
    const float gravedad = -0.015f, fuerzaSalto = 0.35f;
    bool enElSuelo = false;

    Mesh meshCielo = GenMeshSphere(150, 16, 16);
    Model modelCielo = LoadModelFromMesh(meshCielo);
    modelCielo.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = Color{110,180,240,255};

    SetTargetFPS(60);
    DisableCursor();

    while (!WindowShouldClose()) {
        std::vector<EditorBlock>& nivel = listaEscenas[escenaActualIndex];
        bool especiales = (escenaActualIndex == 0);

        // Movimiento y física (simplificado, igual que en el editor)
        // ...
        // (Aquí iría la misma lógica de juego que en el modo juego del editor, copiada íntegramente)

        BeginDrawing();
        ClearBackground(Color{200,215,230,255});
        BeginMode3D(camera);
        DrawModel(modelCielo, Vector3{0,0,0}, 1, WHITE);
        // Dibujar objetos...
        EndMode3D();
        EndDrawing();
    }

    UnloadModel(modelCielo);
    CloseWindow();
    return 0;
}
