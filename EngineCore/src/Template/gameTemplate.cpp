#define _CRT_SECURE_NO_WARNINGS
#include "raylib.h"
#include "raymath.h"
#include <cstdio>
#include <vector>
#include <cstring>
#include <unordered_map>
#include <filesystem>
#include <string>
#include "player.h"
#include "coin.h"
#include "enemy.h"
#include "cameraGame.h"
#include "scripting.h" 

// Mapa para cachear modelos de cubo texturizados
static std::unordered_map<int, Model> cubeModelCache;

static Model GetCubeTexturedModel(Texture2D tex) {
    int texId = tex.id;
    auto it = cubeModelCache.find(texId);
    if (it != cubeModelCache.end()) return it->second;
    Mesh mesh = GenMeshCube(1.0f, 1.0f, 1.0f);
    Model model = LoadModelFromMesh(mesh);
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    cubeModelCache[texId] = model;
    return model;
}

static void UnloadCachedCubeModels() {
    for (auto& pair : cubeModelCache) UnloadModel(pair.second);
    cubeModelCache.clear();
}

static std::vector<unsigned char> ReadFileToVector(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> data(size);
    fread(data.data(), 1, size, f);
    fclose(f);
    return data;
}

static bool LoadProjectFromMemory(const unsigned char* data, size_t size,
    std::vector<std::vector<EditorBlock>>& listaEscenas,
    int& escenaActualIndex,
    Player& miJugador,
    Coin& miMoneda,
    Enemy& miEnemigo,
    ShapeType& formaActualCreacion,
    const std::unordered_map<std::string, std::vector<unsigned char>>& imageMap) {
    size_t pos = 0;
    auto readBytes = [&](void* out, size_t n) {
        if (pos + n > size) return false;
        memcpy(out, data + pos, n);
        pos += n;
        return true;
        };

    int numEscenas;
    if (!readBytes(&numEscenas, sizeof(int))) return false;
    listaEscenas.clear();
    for (int i = 0; i < numEscenas; i++) {
        int numBloques;
        if (!readBytes(&numBloques, sizeof(int))) return false;
        std::vector<EditorBlock> escena;
        for (int j = 0; j < numBloques; j++) {
            EditorBlock bloque;
            if (!readBytes(&bloque.position, sizeof(Vector3))) return false;
            if (!readBytes(&bloque.size, sizeof(Vector3))) return false;
            if (!readBytes(&bloque.color, sizeof(Color))) return false;
            int shape;
            if (!readBytes(&shape, sizeof(int))) return false;
            bloque.shapeType = (ShapeType)shape;
            if (!readBytes(&bloque.targetScene, sizeof(int))) return false;
            int hasTex;
            if (!readBytes(&hasTex, sizeof(int))) return false;
            bloque.hasTexture = (hasTex == 1);
            if (bloque.hasTexture) {
                if (!readBytes(bloque.texturePath, sizeof(char) * 512)) return false;
                std::string path = bloque.texturePath;
                std::string filename = std::filesystem::path(path).filename().string();
                auto it = imageMap.find(filename);
                if (it != imageMap.end()) {
                    const std::vector<unsigned char>& imgData = it->second;
                    Image img = LoadImageFromMemory(".png", imgData.data(), imgData.size());
                    if (img.data == NULL) {
                        img = LoadImageFromMemory(".jpg", imgData.data(), imgData.size());
                    }
                    if (img.data != NULL) {
                        bloque.texture = LoadTextureFromImage(img);
                        UnloadImage(img);
                        if (bloque.texture.id == 0) {
                            bloque.hasTexture = false;
                            bloque.texturePath[0] = '\0';
                        }
                    }
                    else {
                        bloque.hasTexture = false;
                        bloque.texturePath[0] = '\0';
                        bloque.texture.id = 0;
                    }
                }
                else {
                    bloque.hasTexture = false;
                    bloque.texturePath[0] = '\0';
                    bloque.texture.id = 0;
                }
            }
            else {
                bloque.texturePath[0] = '\0';
                bloque.texture.id = 0;
            }
            escena.push_back(bloque);
        }
        listaEscenas.push_back(escena);
    }

    if (!readBytes(&escenaActualIndex, sizeof(int))) return false;
    if (!readBytes(&miJugador.posicion, sizeof(Vector3))) return false;
    if (!readBytes(&miJugador.radio, sizeof(float))) return false;
    if (!readBytes(&miJugador.alturaTotal, sizeof(float))) return false;
    if (!readBytes(&miJugador.color, sizeof(Color))) return false;
    if (!readBytes(&miJugador.puntos, sizeof(int))) return false;
    if (!readBytes(&miJugador.vidas, sizeof(int))) return false;
    if (!readBytes(&miMoneda.posicion, sizeof(Vector3))) return false;
    if (!readBytes(&miMoneda.radio, sizeof(float))) return false;
    if (!readBytes(&miMoneda.activa, sizeof(bool))) return false;
    if (!readBytes(&miEnemigo.posicion, sizeof(Vector3))) return false;
    if (!readBytes(&miEnemigo.tamano, sizeof(Vector3))) return false;
    int forma;
    if (!readBytes(&forma, sizeof(int))) return false;
    formaActualCreacion = (ShapeType)forma;

    // Leer script
    int scriptLen;
    if (!readBytes(&scriptLen, sizeof(int))) {
        g_updateScriptCode = "";
    }
    else {
        std::vector<char> buffer(scriptLen);
        if (!readBytes(buffer.data(), scriptLen)) {
            g_updateScriptCode = "";
        }
        else {
            g_updateScriptCode = std::string(buffer.data());
        }
    }

    return true;
}

static bool UnpackAssets(const char* filename,
    std::vector<unsigned char>& projectData,
    std::unordered_map<std::string, std::vector<unsigned char>>& imageMap) {
    FILE* f = fopen(filename, "rb");
    if (!f) return false;

    uint32_t magic, version, numFiles;
    if (fread(&magic, sizeof(uint32_t), 1, f) != 1) { fclose(f); return false; }
    if (magic != 0x41534554) { fclose(f); return false; }
    if (fread(&version, sizeof(uint32_t), 1, f) != 1) { fclose(f); return false; }
    if (fread(&numFiles, sizeof(uint32_t), 1, f) != 1) { fclose(f); return false; }

    for (uint32_t i = 0; i < numFiles; i++) {
        uint32_t nameLen;
        if (fread(&nameLen, sizeof(uint32_t), 1, f) != 1) { fclose(f); return false; }
        std::string name(nameLen, '\0');
        if (fread(&name[0], 1, nameLen, f) != nameLen) { fclose(f); return false; }
        uint32_t dataSize;
        if (fread(&dataSize, sizeof(uint32_t), 1, f) != 1) { fclose(f); return false; }
        std::vector<unsigned char> data(dataSize);
        if (fread(data.data(), 1, dataSize, f) != dataSize) { fclose(f); return false; }

        if (name == "proyecto.sav") {
            projectData = std::move(data);
        }
        else {
            imageMap[name] = std::move(data);
        }
    }

    fclose(f);
    return true;
}

int main() {
    const int screenWidth = 800, screenHeight = 450;
    InitWindow(screenWidth, screenHeight, "Juego Exportado");
    SetWindowState(FLAG_WINDOW_RESIZABLE);

    // 1. Cargar assets.dat
    std::vector<unsigned char> projectData;
    std::unordered_map<std::string, std::vector<unsigned char>> imageMap;
    if (!UnpackAssets("assets.dat", projectData, imageMap)) {
        DrawText("Error: No se pudo cargar assets.dat", 10, 10, 20, RED);
        WaitTime(2.0f);
        CloseWindow();
        return 1;
    }

    // 2. Cargar proyecto desde memoria
    std::vector<std::vector<EditorBlock>> listaEscenas;
    int escenaActualIndex = 0;
    Player miJugador;
    Coin miMoneda;
    Enemy miEnemigo;
    ShapeType formaActualCreacion;

    if (!LoadProjectFromMemory(projectData.data(), projectData.size(),
        listaEscenas, escenaActualIndex,
        miJugador, miMoneda, miEnemigo, formaActualCreacion, imageMap)) {
        DrawText("Error: Datos del proyecto corruptos", 10, 10, 20, RED);
        WaitTime(2.0f);
        CloseWindow();
        return 1;
    }

    // ====== DEPURACIÓN: escribir el script cargado en un archivo ======
    {
        FILE* f = fopen("script_log.txt", "w");
        if (f) {
            fprintf(f, "Script cargado (%zu bytes):\n%s\n", g_updateScriptCode.size(), g_updateScriptCode.c_str());
            fclose(f);
        }
    }

    // ---- Establecer el script cargado ----
    SetUpdateScript(g_updateScriptCode.c_str());

    CameraGame miCamara;
    miCamara.camara.fovy = 45.0f;
    miCamara.camara.projection = CAMERA_PERSPECTIVE;
    miCamara.Inicializar(miJugador.posicion);

    float velocidadY = 0.0f;
    const float gravedad = -0.015f, fuerzaSalto = 0.35f;
    bool enElSuelo = false, puedeRecibirDano = true;
    float temporizadorDano = 0.0f;

    Mesh meshCielo = GenMeshSphere(150.0f, 16, 16);
    Model modeloCielo = LoadModelFromMesh(meshCielo);
    modeloCielo.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = Color{ 110,180,240,255 };

    SetTargetFPS(60);
    DisableCursor();

    // ====== DEPURACIÓN: heartbeat cada segundo ======
    int frameCount = 0;

    while (!WindowShouldClose()) {
        // ---- Ejecutar script de actualización ----
        UpdateScripts();

        // ====== DEPURACIÓN: heartbeat ======
        frameCount++;
        if (frameCount % 60 == 0) {
            FILE* f = fopen("heartbeat.txt", "a");
            if (f) {
                fprintf(f, "Frame %d: UpdateScripts() llamado\n", frameCount);
                fclose(f);
            }
        }

        std::vector<EditorBlock>& nivelEscenario = listaEscenas[escenaActualIndex];
        bool tieneObjetosEspeciales = (escenaActualIndex == 0);

        float velocidadMov = 0.08f;
        if (IsKeyDown(KEY_LEFT_SHIFT)) velocidadMov = 0.2f;

        Vector3 adelante = { sinf(miCamara.anguloHorizontal), 0.0f, cosf(miCamara.anguloHorizontal) };
        Vector3 derecha = { cosf(miCamara.anguloHorizontal), 0.0f, -sinf(miCamara.anguloHorizontal) };
        Vector3 movimientoDeseado = { 0.0f, 0.0f, 0.0f };
        if (IsKeyDown(KEY_W)) movimientoDeseado = Vector3Subtract(movimientoDeseado, adelante);
        if (IsKeyDown(KEY_S)) movimientoDeseado = Vector3Add(movimientoDeseado, adelante);
        if (IsKeyDown(KEY_A)) movimientoDeseado = Vector3Subtract(movimientoDeseado, derecha);
        if (IsKeyDown(KEY_D)) movimientoDeseado = Vector3Add(movimientoDeseado, derecha);

        auto getPlayerBox = [](const Player& p) {
            float alturaMitad = p.alturaTotal / 2.0f;
            return BoundingBox{
                Vector3{ p.posicion.x - p.radio, p.posicion.y - alturaMitad, p.posicion.z - p.radio },
                Vector3{ p.posicion.x + p.radio, p.posicion.y + alturaMitad, p.posicion.z + p.radio }
            };
            };
        auto getBlockBox = [](const EditorBlock& b) {
            return BoundingBox{
                Vector3{ b.position.x - b.size.x / 2.0f, b.position.y - b.size.y / 2.0f, b.position.z - b.size.z / 2.0f },
                Vector3{ b.position.x + b.size.x / 2.0f, b.position.y + b.size.y / 2.0f, b.position.z + b.size.z / 2.0f }
            };
            };

        BoundingBox boxJugador = getPlayerBox(miJugador);
        bool teletransportado = false;
        for (const auto& bloque : nivelEscenario) {
            BoundingBox boxBloque = getBlockBox(bloque);
            if (CheckCollisionBoxes(boxJugador, boxBloque)) {
                if (bloque.shapeType == SHAPE_PORTAL) {
                    int destino = bloque.targetScene;
                    if (destino < 0) destino = 0;
                    if (destino >= (int)listaEscenas.size()) listaEscenas.resize(destino + 1);
                    escenaActualIndex = destino;
                    miJugador.posicion = Vector3{ 0.0f,5.0f,0.0f };
                    velocidadY = 0.0f;
                    teletransportado = true;
                    break;
                }
            }
        }

        if (!teletransportado) {
            if (Vector3Length(movimientoDeseado) > 0) {
                movimientoDeseado = Vector3Normalize(movimientoDeseado);
                miJugador.posicion.x += movimientoDeseado.x * velocidadMov;
                {
                    BoundingBox boxJ = getPlayerBox(miJugador);
                    for (const auto& bloque : nivelEscenario) {
                        if (bloque.shapeType == SHAPE_CONE || bloque.shapeType == SHAPE_PORTAL) continue;
                        BoundingBox boxBloque = getBlockBox(bloque);
                        if (CheckCollisionBoxes(boxJ, boxBloque)) {
                            if (boxBloque.max.y > boxJ.min.y + 0.05f) {
                                miJugador.posicion.x -= movimientoDeseado.x * velocidadMov;
                                break;
                            }
                        }
                    }
                }
                miJugador.posicion.z += movimientoDeseado.z * velocidadMov;
                {
                    BoundingBox boxJ = getPlayerBox(miJugador);
                    for (const auto& bloque : nivelEscenario) {
                        if (bloque.shapeType == SHAPE_CONE || bloque.shapeType == SHAPE_PORTAL) continue;
                        BoundingBox boxBloque = getBlockBox(bloque);
                        if (CheckCollisionBoxes(boxJ, boxBloque)) {
                            if (boxBloque.max.y > boxJ.min.y + 0.05f) {
                                miJugador.posicion.z -= movimientoDeseado.z * velocidadMov;
                                break;
                            }
                        }
                    }
                }
            }

            if (IsKeyPressed(KEY_SPACE) && enElSuelo) {
                velocidadY = fuerzaSalto;
                enElSuelo = false;
            }
            velocidadY += gravedad;
            miJugador.posicion.y += velocidadY;
            enElSuelo = false;

            {
                BoundingBox boxJ = getPlayerBox(miJugador);
                for (const auto& bloque : nivelEscenario) {
                    if (bloque.shapeType == SHAPE_PORTAL) continue;
                    if (bloque.shapeType == SHAPE_CONE) {
                        float bottomY = bloque.position.y - (bloque.size.y / 2.0f);
                        float topY = bloque.position.y + (bloque.size.y / 2.0f);
                        float baseR = bloque.size.x / 2.0f;
                        float dx = miJugador.posicion.x - bloque.position.x;
                        float dz = miJugador.posicion.z - bloque.position.z;
                        float distXZ = sqrtf(dx * dx + dz * dz);
                        if (distXZ <= baseR) {
                            float pBottom = miJugador.posicion.y - (miJugador.alturaTotal / 2.0f);
                            float pTop = miJugador.posicion.y + (miJugador.alturaTotal / 2.0f);
                            float surfaceY = topY - (distXZ / baseR) * (topY - bottomY);
                            if (velocidadY <= 0.0f && pBottom <= surfaceY + 0.3f && pTop >= surfaceY) {
                                miJugador.posicion.y += (surfaceY - miJugador.posicion.y) * 0.4f;
                            }
                            velocidadY = 0.0f;
                            enElSuelo = true;
                        }
                    }
                    else {
                        BoundingBox boxBloque = getBlockBox(bloque);
                        if (CheckCollisionBoxes(boxJ, boxBloque)) {
                            float topeBloque = bloque.position.y + (bloque.size.y / 2.0f);
                            float fondoBloque = bloque.position.y - (bloque.size.y / 2.0f);
                            float alturaMitadJugador = miJugador.alturaTotal / 2.0f;
                            if (velocidadY <= 0.0f) {
                                miJugador.posicion.y = topeBloque + alturaMitadJugador;
                                velocidadY = 0.0f;
                                enElSuelo = true;
                            }
                            else {
                                miJugador.posicion.y = fondoBloque - alturaMitadJugador;
                                velocidadY = 0.0f;
                            }
                        }
                    }
                }
            }
        }

        if (miJugador.posicion.y < -10.0f) {
            miJugador.posicion = Vector3{ 0.0f,5.0f,0.0f };
            velocidadY = 0.0f;
        }

        miCamara.Actualizar(miJugador.posicion, GetMouseDelta());

        if (tieneObjetosEspeciales) {
            miEnemigo.ActualizarMovimiento();
            if (!puedeRecibirDano) {
                temporizadorDano += GetFrameTime();
                if (temporizadorDano >= 1.5f) {
                    puedeRecibirDano = true;
                    temporizadorDano = 0.0f;
                    miJugador.color = GREEN;
                }
            }
            if (CheckCollisionBoxes(getPlayerBox(miJugador), miEnemigo.ObtenerColision())) {
                if (puedeRecibirDano && miJugador.vidas > 0) {
                    miJugador.vidas -= 1;
                    puedeRecibirDano = false;
                    miJugador.color = RED;
                }
            }
            if (miMoneda.activa && CheckCollisionBoxSphere(getPlayerBox(miJugador), miMoneda.posicion, miMoneda.radio)) {
                miJugador.puntos += 10;
                miMoneda.Reaparecer();
            }
        }

        BeginDrawing();
        ClearBackground(Color{ 200,215,230,255 });

        BeginMode3D(miCamara.camara);
        DrawModel(modeloCielo, miCamara.camara.position, 1.0f, WHITE);

        auto dibujarObjeto3D = [](const EditorBlock& obj) {
            if (obj.hasTexture && obj.texture.id != 0) {
                Model model = GetCubeTexturedModel(obj.texture);
                Vector3 scale = { obj.size.x, obj.size.y, obj.size.z };
                DrawModelEx(model, obj.position, { 1,0,0 }, 0.0f, scale, WHITE);
                DrawCubeWires(obj.position, obj.size.x, obj.size.y, obj.size.z, BLACK);
            }
            else {
                switch (obj.shapeType) {
                case SHAPE_CUBE:
                    DrawCube(obj.position, obj.size.x, obj.size.y, obj.size.z, obj.color);
                    break;
                case SHAPE_SPHERE: {
                    float radio = obj.size.x * 0.5f;
                    DrawSphere(obj.position, radio, obj.color);
                    break;
                }
                case SHAPE_CYLINDER: {
                    float radio = obj.size.x * 0.5f;
                    float altura = obj.size.y;
                    Vector3 pInf = { obj.position.x, obj.position.y - altura / 2.0f, obj.position.z };
                    Vector3 pSup = { obj.position.x, obj.position.y + altura / 2.0f, obj.position.z };
                    DrawCylinderEx(pInf, pSup, radio, radio, 16, obj.color);
                    break;
                }
                case SHAPE_CONE: {
                    float radio = obj.size.x * 0.5f;
                    float altura = obj.size.y;
                    Vector3 pInf = { obj.position.x, obj.position.y - altura / 2.0f, obj.position.z };
                    Vector3 pSup = { obj.position.x, obj.position.y + altura / 2.0f, obj.position.z };
                    DrawCylinderEx(pInf, pSup, radio, 0.0f, 16, obj.color);
                    break;
                }
                case SHAPE_PORTAL:
                    DrawCube(obj.position, obj.size.x, obj.size.y, obj.size.z, Fade(PURPLE, 0.6f));
                    break;
                }
            }
            };

        for (const auto& obj : nivelEscenario) {
            dibujarObjeto3D(obj);
        }

        float alturaCilindro = miJugador.alturaTotal - (miJugador.radio * 2.0f);
        Vector3 pInf = { miJugador.posicion.x, miJugador.posicion.y - alturaCilindro / 2.0f, miJugador.posicion.z };
        Vector3 pSup = { miJugador.posicion.x, miJugador.posicion.y + alturaCilindro / 2.0f, miJugador.posicion.z };
        DrawCylinderEx(pInf, pSup, miJugador.radio, miJugador.radio, 16, miJugador.color);
        DrawSphere(pSup, miJugador.radio, miJugador.color);
        DrawSphere(pInf, miJugador.radio, miJugador.color);

        if (tieneObjetosEspeciales) {
            if (miMoneda.activa) DrawSphere(miMoneda.posicion, miMoneda.radio, YELLOW);
            DrawCube(miEnemigo.posicion, miEnemigo.tamano.x, miEnemigo.tamano.y, miEnemigo.tamano.z, BLUE);
        }

        EndMode3D();

        DrawText(TextFormat("Escena: %i | Puntos: %i | Vidas: %i", escenaActualIndex + 1, miJugador.puntos, miJugador.vidas), 10, 10, 20, WHITE);
        DrawFPS(10, 60);

        EndDrawing();
    }

    for (auto& escena : listaEscenas) {
        for (auto& bloque : escena) {
            if (bloque.hasTexture && bloque.texture.id != 0) UnloadTexture(bloque.texture);
        }
    }
    UnloadCachedCubeModels();
    UnloadModel(modeloCielo);
    CloseWindow();
    return 0;
}