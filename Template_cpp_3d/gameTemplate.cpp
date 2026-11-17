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

// ========== DEFINICIÓN DE LogMessage PARA EL TEMPLATE ==========
void LogMessage(const std::string& text, Color color) {
    // (vacío o log opcional)
}
// ================================================================

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

            // Leer script del bloque (ahora leemos el código fuente, no la ruta)
            if (!readBytes(&bloque.hasScript, sizeof(bool))) return false;
            if (bloque.hasScript) {
                int scriptLen;
                if (!readBytes(&scriptLen, sizeof(int))) return false;
                if (scriptLen > 0 && scriptLen < (int)sizeof(bloque.scriptSource)) {
                    if (!readBytes(bloque.scriptSource, scriptLen)) return false;
                    bloque.scriptSource[scriptLen] = '\0';
                }
                else {
                    bloque.scriptSource[0] = '\0';
                    if (scriptLen > 0) pos += scriptLen;
                }
                bloque.scriptPath[0] = '\0'; // ya no usamos ruta
            }
            else {
                bloque.scriptSource[0] = '\0';
                bloque.scriptPath[0] = '\0';
            }

            escena.push_back(bloque);
        }
        listaEscenas.push_back(escena);
    }

    if (!readBytes(&escenaActualIndex, sizeof(int))) return false;

    // Leer jugador
    if (!readBytes(&miJugador.posicion, sizeof(Vector3))) return false;
    if (!readBytes(&miJugador.radio, sizeof(float))) return false;
    if (!readBytes(&miJugador.alturaTotal, sizeof(float))) return false;
    if (!readBytes(&miJugador.color, sizeof(Color))) return false;
    if (!readBytes(&miJugador.puntos, sizeof(int))) return false;
    if (!readBytes(&miJugador.vidas, sizeof(int))) return false;

    // Leer script del jugador (código fuente)
    if (!readBytes(&miJugador.hasScript, sizeof(bool))) return false;
    if (miJugador.hasScript) {
        int scriptLenJ;
        if (!readBytes(&scriptLenJ, sizeof(int))) return false;
        if (scriptLenJ > 0 && scriptLenJ < (int)sizeof(miJugador.scriptSource)) {
            if (!readBytes(miJugador.scriptSource, scriptLenJ)) return false;
            miJugador.scriptSource[scriptLenJ] = '\0';
        }
        else {
            miJugador.scriptSource[0] = '\0';
            if (scriptLenJ > 0) pos += scriptLenJ;
        }
        miJugador.scriptPath[0] = '\0';
    }
    else {
        miJugador.scriptSource[0] = '\0';
        miJugador.scriptPath[0] = '\0';
    }

    // Leer moneda
    if (!readBytes(&miMoneda.posicion, sizeof(Vector3))) return false;
    if (!readBytes(&miMoneda.radio, sizeof(float))) return false;
    if (!readBytes(&miMoneda.activa, sizeof(bool))) return false;

    // Leer script de la moneda
    if (!readBytes(&miMoneda.hasScript, sizeof(bool))) return false;
    if (miMoneda.hasScript) {
        int scriptLenC;
        if (!readBytes(&scriptLenC, sizeof(int))) return false;
        if (scriptLenC > 0 && scriptLenC < (int)sizeof(miMoneda.scriptSource)) {
            if (!readBytes(miMoneda.scriptSource, scriptLenC)) return false;
            miMoneda.scriptSource[scriptLenC] = '\0';
        }
        else {
            miMoneda.scriptSource[0] = '\0';
            if (scriptLenC > 0) pos += scriptLenC;
        }
        miMoneda.scriptPath[0] = '\0';
    }
    else {
        miMoneda.scriptSource[0] = '\0';
        miMoneda.scriptPath[0] = '\0';
    }

    // Leer enemigo
    if (!readBytes(&miEnemigo.posicion, sizeof(Vector3))) return false;
    if (!readBytes(&miEnemigo.tamano, sizeof(Vector3))) return false;

    // Leer script del enemigo
    if (!readBytes(&miEnemigo.hasScript, sizeof(bool))) return false;
    if (miEnemigo.hasScript) {
        int scriptLenE;
        if (!readBytes(&scriptLenE, sizeof(int))) return false;
        if (scriptLenE > 0 && scriptLenE < (int)sizeof(miEnemigo.scriptSource)) {
            if (!readBytes(miEnemigo.scriptSource, scriptLenE)) return false;
            miEnemigo.scriptSource[scriptLenE] = '\0';
        }
        else {
            miEnemigo.scriptSource[0] = '\0';
            if (scriptLenE > 0) pos += scriptLenE;
        }
        miEnemigo.scriptPath[0] = '\0';
    }
    else {
        miEnemigo.scriptSource[0] = '\0';
        miEnemigo.scriptPath[0] = '\0';
    }

    int forma;
    if (!readBytes(&forma, sizeof(int))) return false;
    formaActualCreacion = (ShapeType)forma;

    // Leer script global
    if (pos + sizeof(int) <= size) {
        int globalScriptLen;
        if (readBytes(&globalScriptLen, sizeof(int))) {
            std::vector<char> buffer(globalScriptLen + 1);
            buffer[globalScriptLen] = '\0';
            if (readBytes(buffer.data(), globalScriptLen)) {
                std::string loadedScript = std::string(buffer.data());
                std::string cleanScript = SanitizeScript(loadedScript);
                g_updateScriptCode = cleanScript;
            }
            else {
                g_updateScriptCode = "";
            }
        }
        else {
            g_updateScriptCode = "";
        }
    }
    else {
        g_updateScriptCode = "";
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
    const int screenWidth = 1366, screenHeight = 768;
    InitWindow(screenWidth, screenHeight, "Juego Exportado");
    SetWindowState(FLAG_WINDOW_RESIZABLE);

    // Inicializar sistema de scripting
    InitScripting();

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

    // ---- Inicializar cámara y física ----
    CameraGame miCamara;
    miCamara.camara.fovy = 45.0f;
    miCamara.camara.projection = CAMERA_PERSPECTIVE;
    miCamara.Inicializar(miJugador.posicion);

    float velocidadY = 0.0f;
    const float gravedad = -0.015f, fuerzaSalto = 0.35f;
    bool enElSuelo = false, puedeRecibirDano = true;
    float temporizadorDano = 0.0f;
    float camDistancia = 15.0f;

    SetScriptContext(
        &listaEscenas,
        &escenaActualIndex,
        &miJugador,
        &miMoneda,
        &miEnemigo,
        &miCamara,
        &camDistancia,
        &velocidadY,
        &enElSuelo,
        &puedeRecibirDano,
        &temporizadorDano
    );

    SetUpdateScript(g_updateScriptCode.c_str());
    if (g_updateScriptCode.empty()) g_updateScriptCode = "-- Sin script definido";

    Mesh meshCielo = GenMeshSphere(150.0f, 16, 16);
    Model modeloCielo = LoadModelFromMesh(meshCielo);
    modeloCielo.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = Color{ 110,180,240,255 };

    SetTargetFPS(60);
    DisableCursor();

    static std::unordered_map<int, Vector3> prevBlockPositions;

    while (!WindowShouldClose()) {
        // Script global
        UpdateScripts();

        std::vector<EditorBlock>& nivelEscenario = listaEscenas[escenaActualIndex];
        bool tieneObjetosEspeciales = (escenaActualIndex == 0);

        Vector3 adelante = { sinf(miCamara.anguloHorizontal), 0.0f, cosf(miCamara.anguloHorizontal) };
        Vector3 derecha = { cosf(miCamara.anguloHorizontal), 0.0f, -sinf(miCamara.anguloHorizontal) };
        Vector3 movimientoDeseado = { 0.0f, 0.0f, 0.0f };

        // ========== CONTROL POR SCRIPT SI EL JUGADOR TIENE UNO ==========
        if (!miJugador.hasScript) {
            if (IsKeyDown(KEY_W)) movimientoDeseado = Vector3Subtract(movimientoDeseado, adelante);
            if (IsKeyDown(KEY_S)) movimientoDeseado = Vector3Add(movimientoDeseado, adelante);
            if (IsKeyDown(KEY_A)) movimientoDeseado = Vector3Subtract(movimientoDeseado, derecha);
            if (IsKeyDown(KEY_D)) movimientoDeseado = Vector3Add(movimientoDeseado, derecha);
        }
        else {
            lua_State* scriptL = luaL_newstate();
            luaL_openlibs(scriptL);
            RegisterScriptingLuaFunctions(scriptL);

            lua_pushnumber(scriptL, miJugador.posicion.x); lua_setglobal(scriptL, "playerX");
            lua_pushnumber(scriptL, miJugador.posicion.y); lua_setglobal(scriptL, "playerY");
            lua_pushnumber(scriptL, miJugador.posicion.z); lua_setglobal(scriptL, "playerZ");
            lua_pushnumber(scriptL, enElSuelo ? 1 : 0);    lua_setglobal(scriptL, "enElSuelo");
            lua_pushnumber(scriptL, GetFrameTime());        lua_setglobal(scriptL, "dt");
            lua_pushnumber(scriptL, miCamara.anguloHorizontal);
            lua_setglobal(scriptL, "camAngle");

            int status;
            // Preferir el código fuente incrustado
            if (miJugador.scriptSource[0] != '\0') {
                status = luaL_dostring(scriptL, miJugador.scriptSource);
            }
            else {
                status = luaL_dofile(scriptL, miJugador.scriptPath);
            }

            if (status == LUA_OK) {
                // Leer movimiento (el script puede incluir rotación manual usando camAngle)
                lua_getglobal(scriptL, "moveX");
                if (lua_isnumber(scriptL, -1)) movimientoDeseado.x = (float)lua_tonumber(scriptL, -1);
                lua_pop(scriptL, 1);
                lua_getglobal(scriptL, "moveY");
                if (lua_isnumber(scriptL, -1)) movimientoDeseado.y = (float)lua_tonumber(scriptL, -1);
                lua_pop(scriptL, 1);
                lua_getglobal(scriptL, "moveZ");
                if (lua_isnumber(scriptL, -1)) movimientoDeseado.z = (float)lua_tonumber(scriptL, -1);
                lua_pop(scriptL, 1);

                // El script ya devuelve desplazamiento final (incluyendo dt y rotación)
                // No se aplica normalización adicional.

                // Leer salto
                lua_getglobal(scriptL, "jump");
                if (lua_isboolean(scriptL, -1) && lua_toboolean(scriptL, -1) && enElSuelo) {
                    velocidadY = fuerzaSalto;
                    enElSuelo = false;
                }
                lua_pop(scriptL, 1);
            }
            else {
                const char* err = lua_tostring(scriptL, -1);
                // Silencioso
                lua_pop(scriptL, 1);
            }
            lua_close(scriptL);
        }
        // =================================================================

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

        // Arrastre de bloques
        std::unordered_map<int, Vector3> currentBlockPositions;
        for (int i = 0; i < (int)nivelEscenario.size(); i++) {
            currentBlockPositions[i] = nivelEscenario[i].position;
        }
        if (!prevBlockPositions.empty()) {
            BoundingBox boxJ = getPlayerBox(miJugador);
            for (int i = 0; i < (int)nivelEscenario.size(); i++) {
                const auto& bloque = nivelEscenario[i];
                if (bloque.shapeType == SHAPE_PORTAL) continue;
                BoundingBox boxB = getBlockBox(bloque);
                if (CheckCollisionBoxes(boxJ, boxB)) {
                    auto it = prevBlockPositions.find(i);
                    if (it != prevBlockPositions.end()) {
                        Vector3 delta = Vector3Subtract(bloque.position, it->second);
                        if (Vector3Length(delta) > 0.001f) {
                            float tope = bloque.position.y + bloque.size.y / 2.0f;
                            float fondo = miJugador.posicion.y - miJugador.alturaTotal / 2.0f;
                            if (fabs(fondo - tope) < 0.3f && velocidadY >= 0) {
                                miJugador.posicion.x += delta.x;
                                miJugador.posicion.z += delta.z;
                                boxJ = getPlayerBox(miJugador);
                            }
                        }
                    }
                }
            }
        }
        prevBlockPositions = currentBlockPositions;

        if (!teletransportado) {
            // Calcular desplazamiento final (dx, dz)
            float dx = 0.0f, dz = 0.0f;

            if (Vector3Length(movimientoDeseado) > 0) {
                if (!miJugador.hasScript) {
                    // Movimiento por defecto (WASD): normalizar y aplicar velocidad
                    movimientoDeseado = Vector3Normalize(movimientoDeseado);
                    float vel = 0.08f;
                    if (IsKeyDown(KEY_LEFT_SHIFT)) vel = 0.2f;
                    dx = movimientoDeseado.x * vel;
                    dz = movimientoDeseado.z * vel;
                }
                else {
                    // Script: el script ya devuelve desplazamiento final (dirección * velocidad * dt)
                    dx = movimientoDeseado.x;
                    dz = movimientoDeseado.z;
                }

                // Aplicar movimiento en X con colisión
                miJugador.posicion.x += dx;
                {
                    BoundingBox boxJ = getPlayerBox(miJugador);
                    for (const auto& bloque : nivelEscenario) {
                        if (bloque.shapeType == SHAPE_CONE || bloque.shapeType == SHAPE_PORTAL) continue;
                        BoundingBox boxB = getBlockBox(bloque);
                        if (CheckCollisionBoxes(boxJ, boxB)) {
                            if (boxB.max.y > boxJ.min.y + 0.05f) {
                                miJugador.posicion.x -= dx;
                                break;
                            }
                        }
                    }
                }
                // Aplicar movimiento en Z con colisión
                miJugador.posicion.z += dz;
                {
                    BoundingBox boxJ = getPlayerBox(miJugador);
                    for (const auto& bloque : nivelEscenario) {
                        if (bloque.shapeType == SHAPE_CONE || bloque.shapeType == SHAPE_PORTAL) continue;
                        BoundingBox boxB = getBlockBox(bloque);
                        if (CheckCollisionBoxes(boxJ, boxB)) {
                            if (boxB.max.y > boxJ.min.y + 0.05f) {
                                miJugador.posicion.z -= dz;
                                break;
                            }
                        }
                    }
                }
            }

            // Salto (solo si no tiene script; el script controla su propio salto)
            if (IsKeyPressed(KEY_SPACE) && enElSuelo && !miJugador.hasScript) {
                velocidadY = fuerzaSalto;
                enElSuelo = false;
            }
            velocidadY += gravedad;
            miJugador.posicion.y += velocidadY;
            enElSuelo = false;

            // Colisión vertical (sin cambios)
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
                            float pBottom = miJugador.posicion.y - (miJugador.alturaTotal / 2);
                            float pTop = miJugador.posicion.y + (miJugador.alturaTotal / 2);
                            float surfaceY = topY - (distXZ / baseR) * (topY - bottomY);
                            if (velocidadY <= 0.0f && pBottom <= surfaceY + 0.3f && pTop >= surfaceY) {
                                miJugador.posicion.y += (surfaceY - miJugador.posicion.y) * 0.4f;
                            }
                            velocidadY = 0.0f;
                            enElSuelo = true;
                        }
                    }
                    else {
                        BoundingBox boxB = getBlockBox(bloque);
                        if (CheckCollisionBoxes(boxJ, boxB)) {
                            float tope = bloque.position.y + bloque.size.y / 2;
                            float fondo = bloque.position.y - bloque.size.y / 2;
                            float mitad = miJugador.alturaTotal / 2;
                            if (velocidadY <= 0.0f) {
                                miJugador.posicion.y = tope + mitad;
                                velocidadY = 0.0f;
                                enElSuelo = true;
                            }
                            else {
                                miJugador.posicion.y = fondo - mitad;
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

        // ============== Ejecutar scripts de los bloques ==============
        for (size_t i = 0; i < nivelEscenario.size(); i++) {
            EditorBlock& bloque = nivelEscenario[i];
            if (!bloque.hasScript || bloque.scriptSource[0] == '\0') continue;

            lua_State* L = luaL_newstate();
            luaL_openlibs(L);
            RegisterScriptingLuaFunctions(L);

            lua_pushnumber(L, bloque.position.x); lua_setglobal(L, "selfX");
            lua_pushnumber(L, bloque.position.y); lua_setglobal(L, "selfY");
            lua_pushnumber(L, bloque.position.z); lua_setglobal(L, "selfZ");
            lua_pushinteger(L, (int)i + 1);       lua_setglobal(L, "selfId");
            lua_pushnumber(L, GetFrameTime());    lua_setglobal(L, "dt");

            int status = luaL_dostring(L, bloque.scriptSource);
            if (status != LUA_OK) {
                const char* err = lua_tostring(L, -1);
                // Opcional: manejar el error o simplemente ignorar en el template
                lua_pop(L, 1);
            }
            lua_close(L);
        }
        // ============================================================

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

        auto dibujar = [](const EditorBlock& obj) {
            if (obj.hasTexture && obj.texture.id != 0) {
                Model m = GetCubeTexturedModel(obj.texture);
                Vector3 s = { obj.size.x, obj.size.y, obj.size.z };
                DrawModelEx(m, obj.position, { 1,0,0 }, 0, s, WHITE);
                DrawCubeWires(obj.position, obj.size.x, obj.size.y, obj.size.z, BLACK);
            }
            else {
                switch (obj.shapeType) {
                case SHAPE_CUBE: DrawCube(obj.position, obj.size.x, obj.size.y, obj.size.z, obj.color); break;
                case SHAPE_SPHERE: DrawSphere(obj.position, obj.size.x * 0.5f, obj.color); break;
                case SHAPE_CYLINDER: {
                    float r = obj.size.x * 0.5f;
                    DrawCylinderEx({ obj.position.x, obj.position.y - obj.size.y / 2, obj.position.z }, { obj.position.x, obj.position.y + obj.size.y / 2, obj.position.z }, r, r, 16, obj.color);
                    break;
                }
                case SHAPE_CONE: {
                    float r = obj.size.x * 0.5f;
                    DrawCylinderEx({ obj.position.x, obj.position.y - obj.size.y / 2, obj.position.z }, { obj.position.x, obj.position.y + obj.size.y / 2, obj.position.z }, r, 0, 16, obj.color);
                    break;
                }
                case SHAPE_PORTAL: DrawCube(obj.position, obj.size.x, obj.size.y, obj.size.z, Fade(PURPLE, 0.6f)); break;
                }
            }
            };
        for (auto& obj : nivelEscenario) dibujar(obj);

        float h = miJugador.alturaTotal - miJugador.radio * 2;
        Vector3 pi = { miJugador.posicion.x, miJugador.posicion.y - h / 2, miJugador.posicion.z };
        Vector3 ps = { miJugador.posicion.x, miJugador.posicion.y + h / 2, miJugador.posicion.z };
        DrawCylinderEx(pi, ps, miJugador.radio, miJugador.radio, 16, miJugador.color);
        DrawSphere(ps, miJugador.radio, miJugador.color);
        DrawSphere(pi, miJugador.radio, miJugador.color);

        if (tieneObjetosEspeciales) {
            if (miMoneda.activa) DrawSphere(miMoneda.posicion, miMoneda.radio, YELLOW);
            DrawCube(miEnemigo.posicion, miEnemigo.tamano.x, miEnemigo.tamano.y, miEnemigo.tamano.z, BLUE);
        }

        EndMode3D();
        DrawText(TextFormat("Escena: %i | Puntos: %i | Vidas: %i", escenaActualIndex + 1, miJugador.puntos, miJugador.vidas), 10, 10, 20, WHITE);
        DrawFPS(10, 60);
        EndDrawing();
    }

    for (auto& escena : listaEscenas) for (auto& b : escena) if (b.hasTexture && b.texture.id != 0) UnloadTexture(b.texture);
    UnloadCachedCubeModels();
    UnloadModel(modeloCielo);
    CloseWindow();
    return 0;
}