#define _CRT_SECURE_NO_WARNINGS

#include "raylib.h"
#include "raymath.h"
#include <cstdlib>
#include <cmath>
#include <vector>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <deque>
#include "scripting.h"

#ifdef _WIN32
struct OPENFILENAMEA {
    unsigned long lStructSize;
    void* hwndOwner;
    void* hInstance;
    const char* lpstrFilter;
    char* lpstrCustomFilter;
    unsigned long nMaxCustFilter;
    unsigned long nFilterIndex;
    char* lpstrFile;
    unsigned long nMaxFile;
    char* lpstrFileTitle;
    unsigned long nMaxFileTitle;
    const char* lpstrInitialDir;
    const char* lpstrTitle;
    unsigned long Flags;
    unsigned short nFileOffset;
    unsigned short nFileExtension;
    const char* lpstrDefExt;
    void* lCustData;
    void* lpfnHook;
    const char* lpstrTemplateName;
    void* pvReserved;
    unsigned long dwReserved;
    unsigned long FlagsEx;
};
extern "C" {
    unsigned long __stdcall GetSaveFileNameA(OPENFILENAMEA*);
    unsigned long __stdcall GetOpenFileNameA(OPENFILENAMEA*);
    void* __stdcall GetActiveWindow();
    int __stdcall ShowWindow(void* hWnd, int nCmdShow);
}
#define SW_MAXIMIZE 3
#define OFN_EXPLORER 0x00080000
#define OFN_FILEMUSTEXIST 0x00001000
#define OFN_HIDEREADONLY 0x00000004
#define OFN_OVERWRITEPROMPT 0x00000002
#endif

#include "player.h"
#include "coin.h"
#include "enemy.h"
#include "cameraGame.h"
#include "editor_types.h"
#include "debug.h"

extern const char* CreateAndGetVirtualScriptPath(const char* suggestedName = nullptr);
extern const char* GetScriptBufferContent(const char* path);
extern void FlushCurrentScriptBuffer();
extern void RegisterScriptInEditor(const char* virtualPath, const char* source);

struct EngineNode {
    std::string name;
    // Si tu EngineNode real tiene más miembros, agrégalos aquí
};

EngineNode* selectedNode = nullptr;

// ============================================================
// SISTEMA DE LOGS EN TIEMPO REAL (CONSOLA INTEGRADA)
// ============================================================
struct LogEntry {
    std::string text;
    Color color;
};

extern std::string g_updateScriptCode;

static std::deque<LogEntry> logMessages;
static const int MAX_LOG_MESSAGES = 200;
static int consoleScrollOffset = 0;
static bool consoleVisible = true;
static float consoleHeightBase = 150.0f;
static bool resizingConsole = false;

void LogMessage(const std::string& text, Color color = WHITE) {
    logMessages.push_back({ text, color });
    if ((int)logMessages.size() > MAX_LOG_MESSAGES) {
        logMessages.pop_front();
        if (consoleScrollOffset > 0) consoleScrollOffset--;
    }
}

void ToggleConsole() {
    consoleVisible = !consoleVisible;
}

void DrawConsole(Rectangle consoleRect, float scaleFactor) {
    if (!consoleVisible) return;
    if (consoleRect.width < 1 || consoleRect.height < 1) return;

    float fontSize = 13.0f * scaleFactor;
    float padding = 8.0f * scaleFactor;
    float headerHeight = 22.0f * scaleFactor;

    DrawRectangleRec(consoleRect, Color{ 30, 30, 35, 255 });
    DrawRectangleLinesEx(consoleRect, 1.5f, GRAY);

    Rectangle headerRect = { consoleRect.x, consoleRect.y, consoleRect.width, headerHeight };
    DrawRectangleRec(headerRect, Color{ 45, 45, 50, 255 });
    DrawLine(consoleRect.x, consoleRect.y + headerHeight, consoleRect.x + consoleRect.width, consoleRect.y + headerHeight, DARKGRAY);

    DrawText("CONSOLA", (int)(consoleRect.x + padding), (int)(consoleRect.y + 3 * scaleFactor), (int)(fontSize * 0.9f), LIGHTGRAY);

    char countMsg[64];
    snprintf(countMsg, sizeof(countMsg), "%zu mensajes", logMessages.size());
    DrawText(countMsg, (int)(consoleRect.x + consoleRect.width - padding - MeasureText(countMsg, (int)(fontSize * 0.8f))),
        (int)(consoleRect.y + 4 * scaleFactor), (int)(fontSize * 0.8f), GRAY);

    float textAreaY = consoleRect.y + headerHeight + 4 * scaleFactor;
    float textAreaHeight = consoleRect.height - headerHeight - 8 * scaleFactor;

    BeginScissorMode((int)consoleRect.x, (int)textAreaY, (int)consoleRect.width, (int)textAreaHeight);

    int lineCount = (int)(textAreaHeight / (fontSize + 3));
    int totalLines = (int)logMessages.size();

    Vector2 mousePos = GetMousePosition();
    if (CheckCollisionPointRec(mousePos, consoleRect)) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            consoleScrollOffset += (int)wheel;
            if (consoleScrollOffset < 0) consoleScrollOffset = 0;
            if (consoleScrollOffset > totalLines - lineCount) consoleScrollOffset = totalLines - lineCount;
            if (consoleScrollOffset < 0) consoleScrollOffset = 0;
        }
    }

    int startIdx = totalLines - lineCount - consoleScrollOffset;
    if (startIdx < 0) startIdx = 0;
    int endIdx = startIdx + lineCount;
    if (endIdx > totalLines) endIdx = totalLines;

    float textY = textAreaY + 2 * scaleFactor;
    for (int i = startIdx; i < endIdx; i++) {
        const LogEntry& entry = logMessages[i];
        std::string displayText = entry.text;
        int maxWidth = (int)(consoleRect.width - padding * 2);
        int textWidth = MeasureText(displayText.c_str(), (int)fontSize);
        if (textWidth > maxWidth) {
            while (textWidth > maxWidth - 20 && displayText.length() > 3) {
                displayText.pop_back();
                textWidth = MeasureText((displayText + "...").c_str(), (int)fontSize);
            }
            displayText += "...";
        }
        DrawText(displayText.c_str(), (int)(consoleRect.x + padding), (int)textY, (int)fontSize, entry.color);
        textY += fontSize + 3;
    }

    EndScissorMode();

    if (totalLines > lineCount) {
        float scrollBarHeight = textAreaHeight * ((float)lineCount / totalLines);
        float scrollBarY = textAreaY + (textAreaHeight - scrollBarHeight) * (1.0f - (float)consoleScrollOffset / (totalLines - lineCount));
        Rectangle scrollBar = { consoleRect.x + consoleRect.width - 8 * scaleFactor, scrollBarY, 6 * scaleFactor, scrollBarHeight };
        DrawRectangleRec(scrollBar, Fade(LIGHTGRAY, 0.5f));
    }

    // Flechas con triángulos
    float arrowSize = 6.0f * scaleFactor;
    float arrowOffsetX = 16.0f * scaleFactor;

    // Flecha ARRIBA
    if (consoleScrollOffset < totalLines - lineCount) {
        float cx = consoleRect.x + consoleRect.width - arrowOffsetX;
        float cy = textAreaY + 8.0f * scaleFactor;
        DrawTriangle(
            Vector2{ cx, cy - arrowSize },
            Vector2{ cx - arrowSize, cy + arrowSize },
            Vector2{ cx + arrowSize, cy + arrowSize },
            YELLOW
        );
    }

    // Flecha ABAJO
    if (consoleScrollOffset > 0) {
        float cx = consoleRect.x + consoleRect.width - arrowOffsetX;
        float cy = textAreaY + textAreaHeight - 14.0f * scaleFactor;
        DrawTriangle(
            Vector2{ cx + arrowSize, cy - arrowSize },
            Vector2{ cx - arrowSize, cy - arrowSize },
            Vector2{ cx, cy + arrowSize },
            YELLOW
        );
    }
}
// ============================================================

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

bool SaveProject(const std::vector<std::vector<EditorBlock>>& listaEscenas, int escenaActualIndex,
    const Player& miJugador, const Coin& miMoneda, const Enemy& miEnemigo,
    ShapeType formaActualCreacion, const char* filename) {
    FILE* file = fopen(filename, "wb");
    if (!file) return false;

    int numEscenas = (int)listaEscenas.size();
    fwrite(&numEscenas, sizeof(int), 1, file);

    for (const auto& escena : listaEscenas) {
        int numBloques = (int)escena.size();
        fwrite(&numBloques, sizeof(int), 1, file);
        for (const auto& bloque : escena) {
            fwrite(&bloque.position, sizeof(Vector3), 1, file);
            fwrite(&bloque.size, sizeof(Vector3), 1, file);
            fwrite(&bloque.color, sizeof(Color), 1, file);
            int shape = (int)bloque.shapeType;
            fwrite(&shape, sizeof(int), 1, file);
            fwrite(&bloque.targetScene, sizeof(int), 1, file);

            int hasTex = bloque.hasTexture ? 1 : 0;
            fwrite(&hasTex, sizeof(int), 1, file);
            if (bloque.hasTexture) fwrite(bloque.texturePath, sizeof(char), 512, file);

            // ✅ Guardar script del bloque (código fuente, no la ruta)
            fwrite(&bloque.hasScript, sizeof(bool), 1, file);
            if (bloque.hasScript) {
                int sourceLen = (int)strlen(bloque.scriptSource);
                fwrite(&sourceLen, sizeof(int), 1, file);
                if (sourceLen > 0) fwrite(bloque.scriptSource, sizeof(char), sourceLen, file);
            }
        }
    }

    fwrite(&escenaActualIndex, sizeof(int), 1, file);

    // Datos del jugador
    fwrite(&miJugador.posicion, sizeof(Vector3), 1, file);
    fwrite(&miJugador.radio, sizeof(float), 1, file);
    fwrite(&miJugador.alturaTotal, sizeof(float), 1, file);
    fwrite(&miJugador.color, sizeof(Color), 1, file);
    fwrite(&miJugador.puntos, sizeof(int), 1, file);
    fwrite(&miJugador.vidas, sizeof(int), 1, file);

    // ✅ Guardar script del jugador
    fwrite(&miJugador.hasScript, sizeof(bool), 1, file);
    if (miJugador.hasScript) {
        int sourceLen = (int)strlen(miJugador.scriptSource);
        fwrite(&sourceLen, sizeof(int), 1, file);
        if (sourceLen > 0) fwrite(miJugador.scriptSource, sizeof(char), sourceLen, file);
    }

    // Datos de la moneda
    fwrite(&miMoneda.posicion, sizeof(Vector3), 1, file);
    fwrite(&miMoneda.radio, sizeof(float), 1, file);
    fwrite(&miMoneda.activa, sizeof(bool), 1, file);

    // ✅ Guardar script de la moneda
    fwrite(&miMoneda.hasScript, sizeof(bool), 1, file);
    if (miMoneda.hasScript) {
        int sourceLen = (int)strlen(miMoneda.scriptSource);
        fwrite(&sourceLen, sizeof(int), 1, file);
        if (sourceLen > 0) fwrite(miMoneda.scriptSource, sizeof(char), sourceLen, file);
    }

    // Datos del enemigo
    fwrite(&miEnemigo.posicion, sizeof(Vector3), 1, file);
    fwrite(&miEnemigo.tamano, sizeof(Vector3), 1, file);

    // ✅ Guardar script del enemigo
    fwrite(&miEnemigo.hasScript, sizeof(bool), 1, file);
    if (miEnemigo.hasScript) {
        int sourceLen = (int)strlen(miEnemigo.scriptSource);
        fwrite(&sourceLen, sizeof(int), 1, file);
        if (sourceLen > 0) fwrite(miEnemigo.scriptSource, sizeof(char), sourceLen, file);
    }

    int forma = (int)formaActualCreacion;
    fwrite(&forma, sizeof(int), 1, file);

    // Script global (se sigue guardando como cadena, pues es el script de actualización)
    int globalScriptLen = (int)g_updateScriptCode.length();
    fwrite(&globalScriptLen, sizeof(int), 1, file);
    if (globalScriptLen > 0) fwrite(g_updateScriptCode.c_str(), sizeof(char), globalScriptLen, file);

    fclose(file);
    return true;
}

void CargarTexturaBloque(EditorBlock& bloque) {
    if (bloque.texturePath[0] == '\0') return;
    if (bloque.hasTexture) {
        if (bloque.texture.id != 0) UnloadTexture(bloque.texture);
        bloque.texture = LoadTexture(bloque.texturePath);
        if (bloque.texture.id == 0) {
            bloque.hasTexture = false;
            bloque.texturePath[0] = '\0';
        }
    }
}

bool LoadProject(std::vector<std::vector<EditorBlock>>& listaEscenas, int& escenaActualIndex,
    Player& miJugador, Coin& miMoneda, Enemy& miEnemigo,
    ShapeType& formaActualCreacion, const char* filename) {
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

            int hasTex;
            if (fread(&hasTex, sizeof(int), 1, file) != 1) { fclose(file); return false; }
            bloque.hasTexture = (hasTex == 1);
            if (bloque.hasTexture) {
                if (fread(bloque.texturePath, sizeof(char), 512, file) != 512) { fclose(file); return false; }
                bloque.texture.id = 0;
                CargarTexturaBloque(bloque);
            }
            else {
                bloque.texturePath[0] = '\0';
                bloque.texture.id = 0;
            }

            // ✅ Cargar script del bloque (código fuente, no ruta)
            if (fread(&bloque.hasScript, sizeof(bool), 1, file) != 1) { fclose(file); return false; }
            if (bloque.hasScript) {
                int scriptLen;
                if (fread(&scriptLen, sizeof(int), 1, file) != 1) { fclose(file); return false; }
                if (scriptLen > 0 && scriptLen < (int)sizeof(bloque.scriptSource)) {
                    if (fread(bloque.scriptSource, sizeof(char), scriptLen, file) != scriptLen) { fclose(file); return false; }
                    bloque.scriptSource[scriptLen] = '\0';
                }
                else {
                    bloque.scriptSource[0] = '\0';
                    if (scriptLen > 0) fseek(file, scriptLen, SEEK_CUR);
                }
                bloque.scriptPath[0] = '\0';  // ya no usamos ruta
            }
            else {
                bloque.scriptSource[0] = '\0';
                bloque.scriptPath[0] = '\0';
            }

            escena.push_back(bloque);
        }
        listaEscenas.push_back(escena);
    }

    if (fread(&escenaActualIndex, sizeof(int), 1, file) != 1) { fclose(file); return false; }

    // Cargar jugador
    if (fread(&miJugador.posicion, sizeof(Vector3), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miJugador.radio, sizeof(float), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miJugador.alturaTotal, sizeof(float), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miJugador.color, sizeof(Color), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miJugador.puntos, sizeof(int), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miJugador.vidas, sizeof(int), 1, file) != 1) { fclose(file); return false; }

    // ✅ Cargar script del jugador
    if (fread(&miJugador.hasScript, sizeof(bool), 1, file) != 1) { fclose(file); return false; }
    if (miJugador.hasScript) {
        int scriptLenJ;
        if (fread(&scriptLenJ, sizeof(int), 1, file) != 1) { fclose(file); return false; }
        if (scriptLenJ > 0 && scriptLenJ < (int)sizeof(miJugador.scriptSource)) {
            if (fread(miJugador.scriptSource, sizeof(char), scriptLenJ, file) != scriptLenJ) { fclose(file); return false; }
            miJugador.scriptSource[scriptLenJ] = '\0';
        }
        else {
            miJugador.scriptSource[0] = '\0';
            if (scriptLenJ > 0) fseek(file, scriptLenJ, SEEK_CUR);
        }
        miJugador.scriptPath[0] = '\0';
    }
    else {
        miJugador.scriptSource[0] = '\0';
        miJugador.scriptPath[0] = '\0';
    }

    // Cargar moneda
    if (fread(&miMoneda.posicion, sizeof(Vector3), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miMoneda.radio, sizeof(float), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miMoneda.activa, sizeof(bool), 1, file) != 1) { fclose(file); return false; }

    // ✅ Cargar script de la moneda
    if (fread(&miMoneda.hasScript, sizeof(bool), 1, file) != 1) { fclose(file); return false; }
    if (miMoneda.hasScript) {
        int scriptLenC;
        if (fread(&scriptLenC, sizeof(int), 1, file) != 1) { fclose(file); return false; }
        if (scriptLenC > 0 && scriptLenC < (int)sizeof(miMoneda.scriptSource)) {
            if (fread(miMoneda.scriptSource, sizeof(char), scriptLenC, file) != scriptLenC) { fclose(file); return false; }
            miMoneda.scriptSource[scriptLenC] = '\0';
        }
        else {
            miMoneda.scriptSource[0] = '\0';
            if (scriptLenC > 0) fseek(file, scriptLenC, SEEK_CUR);
        }
        miMoneda.scriptPath[0] = '\0';
    }
    else {
        miMoneda.scriptSource[0] = '\0';
        miMoneda.scriptPath[0] = '\0';
    }

    // Cargar enemigo
    if (fread(&miEnemigo.posicion, sizeof(Vector3), 1, file) != 1) { fclose(file); return false; }
    if (fread(&miEnemigo.tamano, sizeof(Vector3), 1, file) != 1) { fclose(file); return false; }

    // ✅ Cargar script del enemigo
    if (fread(&miEnemigo.hasScript, sizeof(bool), 1, file) != 1) { fclose(file); return false; }
    if (miEnemigo.hasScript) {
        int scriptLenE;
        if (fread(&scriptLenE, sizeof(int), 1, file) != 1) { fclose(file); return false; }
        if (scriptLenE > 0 && scriptLenE < (int)sizeof(miEnemigo.scriptSource)) {
            if (fread(miEnemigo.scriptSource, sizeof(char), scriptLenE, file) != scriptLenE) { fclose(file); return false; }
            miEnemigo.scriptSource[scriptLenE] = '\0';
        }
        else {
            miEnemigo.scriptSource[0] = '\0';
            if (scriptLenE > 0) fseek(file, scriptLenE, SEEK_CUR);
        }
        miEnemigo.scriptPath[0] = '\0';
    }
    else {
        miEnemigo.scriptSource[0] = '\0';
        miEnemigo.scriptPath[0] = '\0';
    }

    int forma;
    if (fread(&forma, sizeof(int), 1, file) != 1) { fclose(file); return false; }
    formaActualCreacion = (ShapeType)forma;

    // Script global (se sigue cargando como cadena)
    int globalScriptLen;
    if (fread(&globalScriptLen, sizeof(int), 1, file) != 1) {
        g_updateScriptCode = "";
        g_scriptState.codeBuffer[0] = '\0';
        g_scriptState.cursorPos = 0;
        g_scriptState.selectionStart = -1;
        g_scriptState.selectionEnd = -1;
    }
    else {
        std::vector<char> buffer(globalScriptLen + 1);
        buffer[globalScriptLen] = '\0';
        if (fread(buffer.data(), sizeof(char), globalScriptLen, file) == globalScriptLen) {
            std::string cleanScript = SanitizeScript(std::string(buffer.data()));
            g_updateScriptCode = cleanScript;
            strncpy(g_scriptState.codeBuffer, cleanScript.c_str(), sizeof(g_scriptState.codeBuffer) - 1);
            g_scriptState.codeBuffer[sizeof(g_scriptState.codeBuffer) - 1] = '\0';
            g_scriptState.cursorPos = (int)strlen(g_scriptState.codeBuffer);
            g_scriptState.selectionStart = -1;
            g_scriptState.selectionEnd = -1;
        }
        else {
            g_updateScriptCode = "";
            g_scriptState.codeBuffer[0] = '\0';
            g_scriptState.cursorPos = 0;
            g_scriptState.selectionStart = -1;
            g_scriptState.selectionEnd = -1;
        }
    }

    fclose(file);
    return true;
}

bool FileDialog(char* buffer, size_t bufferSize, bool save = false) {
#ifdef _WIN32
    OPENFILENAMEA ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = "Archivos SAV\0*.sav\0Todos los archivos\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = (unsigned long)bufferSize;
    ofn.lpstrTitle = save ? "Guardar proyecto" : "Cargar proyecto";
    if (save) {
        ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
        if (GetSaveFileNameA(&ofn)) {
            size_t len = strlen(buffer);
            if (len > 4 && strcmp(buffer + len - 4, ".sav") != 0)
                strcat_s(buffer, bufferSize, ".sav");
            return true;
        }
    }
    else {
        ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
        if (GetOpenFileNameA(&ofn)) return true;
    }
#else
    (void)buffer; (void)bufferSize; (void)save;
#endif
    return false;
}

bool OpenImageDialog(char* buffer, size_t bufferSize) {
#ifdef _WIN32
    OPENFILENAMEA ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = "Imágenes (*.png;*.jpg)\0*.png;*.jpg\0Todos los archivos (*.*)\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = (unsigned long)bufferSize;
    ofn.lpstrTitle = "Seleccionar textura";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameA(&ofn)) return true;
#else
    (void)buffer; (void)bufferSize;
#endif
    return false;
}

// Diálogo para scripts Lua
bool OpenScriptDialog(char* buffer, size_t bufferSize) {
#ifdef _WIN32
    OPENFILENAMEA ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = "Scripts de Lua (*.lua)\0*.lua\0Todos los archivos (*.*)\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = (unsigned long)bufferSize;
    ofn.lpstrTitle = "Seleccionar Script Lua";
    ofn.lpstrDefExt = "lua";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameA(&ofn)) return true;
#else
    (void)buffer; (void)bufferSize;
#endif
    return false;
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

static bool WritePackage(const char* outputPath,
    const std::vector<unsigned char>& projectData,
    const std::unordered_map<std::string, std::vector<unsigned char>>& imageMap) {
    FILE* f = fopen(outputPath, "wb");
    if (!f) return false;
    uint32_t magic = 0x41534554;
    uint32_t version = 1;
    uint32_t numFiles = 1 + (uint32_t)imageMap.size();
    fwrite(&magic, sizeof(uint32_t), 1, f);
    fwrite(&version, sizeof(uint32_t), 1, f);
    fwrite(&numFiles, sizeof(uint32_t), 1, f);
    {
        const char* name = "proyecto.sav";
        uint32_t nameLen = (uint32_t)strlen(name);
        uint32_t dataSize = (uint32_t)projectData.size();
        fwrite(&nameLen, sizeof(uint32_t), 1, f);
        fwrite(name, 1, nameLen, f);
        fwrite(&dataSize, sizeof(uint32_t), 1, f);
        fwrite(projectData.data(), 1, dataSize, f);
    }
    for (const auto& pair : imageMap) {
        const std::string& filename = pair.first;
        const std::vector<unsigned char>& data = pair.second;
        uint32_t nameLen = (uint32_t)filename.size();
        uint32_t dataSize = (uint32_t)data.size();
        fwrite(&nameLen, sizeof(uint32_t), 1, f);
        fwrite(filename.c_str(), 1, nameLen, f);
        fwrite(&dataSize, sizeof(uint32_t), 1, f);
        fwrite(data.data(), 1, dataSize, f);
    }
    fclose(f);
    return true;
}

bool ExportToEXE(const std::vector<std::vector<EditorBlock>>& listaEscenas, int escenaActualIndex,
    const Player& miJugador, const Coin& miMoneda, const Enemy& miEnemigo,
    ShapeType formaActualCreacion) {
    if (!SaveProject(listaEscenas, escenaActualIndex, miJugador, miMoneda, miEnemigo, formaActualCreacion, "proyecto.sav"))
        return false;
    std::vector<unsigned char> projectData = ReadFileToVector("proyecto.sav");
    if (projectData.empty()) return false;
    std::unordered_map<std::string, std::vector<unsigned char>> imageMap;
    std::unordered_set<std::string> uniquePaths;
    for (const auto& escena : listaEscenas) {
        for (const auto& bloque : escena) {
            if (bloque.hasTexture && std::filesystem::exists(bloque.texturePath)) {
                std::string path = bloque.texturePath;
                std::string filename = std::filesystem::path(path).filename().string();
                if (uniquePaths.find(filename) == uniquePaths.end()) {
                    std::vector<unsigned char> imgData = ReadFileToVector(path.c_str());
                    if (!imgData.empty()) {
                        imageMap[filename] = std::move(imgData);
                        uniquePaths.insert(filename);
                    }
                }
            }
        }
    }
    if (!WritePackage("export/assets.dat", projectData, imageMap)) return false;
    std::error_code ec;
    if (!std::filesystem::create_directories("export", ec)) {
        if (ec && ec != std::errc::file_exists) return false;
    }
    if (!std::filesystem::exists("template/Template.exe")) return false;
    if (!std::filesystem::copy_file("template/Template.exe", "export/JuegoEjecutable.exe",
        std::filesystem::copy_options::overwrite_existing, ec)) return false;
    if (std::filesystem::exists("raylib.dll")) {
        std::filesystem::copy_file("raylib.dll", "export/raylib.dll",
            std::filesystem::copy_options::overwrite_existing, ec);
    }
    std::filesystem::remove("proyecto.sav", ec);
    return true;
}

int main() {
    const int anchoPantalla = 1366;
    const int altoPantalla = 768;
    InitWindow(anchoPantalla, altoPantalla, "Proyecto 1 - CppGine Motor/Prototipo");
    SetWindowState(FLAG_WINDOW_RESIZABLE);
#ifdef _WIN32
    void* hwnd = GetActiveWindow();
    ShowWindow(hwnd, SW_MAXIMIZE);
#endif

    InitScripting();

    Player miJugador;
    Coin miMoneda;
    Enemy miEnemigo;
    CameraGame miCamara;
    miCamara.camara.fovy = 45.0f;
    miCamara.camara.projection = CAMERA_PERSPECTIVE;

    GameMode currentMode = MODE_EDITOR;
    std::vector<std::vector<EditorBlock>> listaEscenas;
    listaEscenas.push_back({});
    int escenaActualIndex = 0;

    SelectedObjectType selectedType = OBJ_NONE;
    int bloqueSeleccionadoIndex = -1;
    int ultimoBloqueSeleccionado = -1;
    float inspectorScrollY = 0.0f;
    bool arrastrandoBloqueConMouse = false;
    Vector3 offsetArrastre = { 0.0f, 0.0f, 0.0f };

    bool transformSectionOpen = true, colorSectionOpen = true;
    float transformHeaderY = 0.0f, colorHeaderY = 0.0f;
    float swatchSize = 0.0f, swatchSpacing = 0.0f, swatchStartX = 0.0f, swatchStartY = 0.0f;
    float rgbHeaderY = 0.0f, rgbRowsStartY = 0.0f, pickerHeaderY = 0.0f, pickerStartY = 0.0f;

    float currentHue = 0.0f, currentSat = 1.0f, currentVal = 1.0f, lastHueCache = -1.0f;
    bool arrastrandoSV = false, arrastrandoHue = false;
    int pickerTexSize = 128;

    Image imgSV = GenImageColor(pickerTexSize, pickerTexSize, WHITE);
    Texture2D texSV = LoadTextureFromImage(imgSV);
    UnloadImage(imgSV);
    Image imgHue = GenImageColor(20, pickerTexSize, WHITE);
    for (int y = 0; y < pickerTexSize; y++) {
        float h = (1.0f - (float)y / (pickerTexSize - 1)) * 360.0f;
        Color c = ColorFromHSV(h, 1.0f, 1.0f);
        for (int x = 0; x < 20; x++) ImageDrawPixel(&imgHue, x, y, c);
    }
    Texture2D texHue = LoadTextureFromImage(imgHue);
    UnloadImage(imgHue);

    int handleSeleccionado = -1;
    bool arrastrandoHandle = false;
    Vector2 mouseInicioArrastre = { 0 };
    Vector3 tamanoInicialBloque = { 0 };

    float previewYaw = 0.0f, previewPitch = 0.4f;
    bool arrastrandoPreview3D = false;
    RenderTexture2D previewTarget = LoadRenderTexture(220, 120);

    int activeTextField = -1;
    bool isScrubbing = false;
    int potentialScrubBox = -1;
    Vector2 scrubStartMouse = { 0 };
    float scrubInitialValue = 0.0f;
    char textInputBuffer[32] = "\0";
    bool showScriptView = false;

    ShapeType formaActualCreacion = SHAPE_CUBE;
    Color inspectorColors[] = { DARKGRAY, GRAY, WHITE, RED, ORANGE, YELLOW, GREEN, BLUE, PURPLE, MAROON, DARKBLUE, DARKBROWN };
    int numInspectorColors = 12;

    Mesh meshCielo = GenMeshSphere(150.0f, 16, 16);
    Model modeloCielo = LoadModelFromMesh(meshCielo);
    modeloCielo.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = Color{ 110, 180, 240, 255 };

    Vector3 editorCameraPos = { 0.0f, 5.0f, 8.0f };
    float editorYaw = 3.14159f, editorPitch = -0.4f;
    int indiceColorActual = 0;
    Vector3 tamanoActualBloque = { 2.0f, 2.0f, 2.0f };

    miCamara.Inicializar(miJugador.posicion);

    float velocidadY = 0.0f;
    const float gravedad = -0.015f, fuerzaSalto = 0.35f;
    bool enElSuelo = false, puedeRecibirDano = true;
    float temporizadorDano = 0.0f;

    const char* statusMessage = nullptr;
    double statusMessageTime = 0.0;

    int leftPanelTab = 0;
    bool showContextMenu = false;
    Vector2 contextMenuPos = { 0, 0 };
    EditorBlock copiedBlock;
    bool hasCopiedBlock = false;
    bool cutOperation = false;
    SelectedObjectType contextObjType = OBJ_NONE;
    int contextObjIndex = -1;
    bool showNodeTypeModal = false;

    // Redimensionamiento lateral
    float leftPanelAnchoBase = 160.0f;
    float panelAnchoBase = 170.0f;
    bool resizingLeft = false, resizingRight = false;
    int resizeZoneWidth = 8;

    // Redimensionamiento vertical (viewport)
    float viewportTopRatio = 0.0f;
    float viewportBottomRatio = 0.0f;
    bool resizingTop = false, resizingBottom = false;

    float camDistancia = 15.0f;

    consoleHeightBase = 104.0f;

    // Después de crear todas las variables (miJugador, miMoneda, miEnemigo, miCamara, etc.)
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

    LogMessage("[SISTEMA] Motor gráfico iniciado. Modo EDITOR.", LIGHTGRAY);

    SetTargetFPS(60);
    ShowCursor();

    while (!WindowShouldClose()) {
        // --- TECLAS GLOBALES (Siempre responden) ---

        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();

        if (IsKeyPressed(KEY_F10)) {
            ToggleDebugMode();
            LogMessage("[DEBUG] Modo debug " + std::string(g_debugMode ? "activado" : "desactivado"), LIGHTGRAY);
        }

        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_U)) {
            SetUpdateScript(g_scriptState.codeBuffer);
            LogMessage("[SCRIPT] Script de actualización establecido.", LIGHTGRAY);
        }

        // --- EDITOR DE SCRIPTS (manejo de tecla y estado) ---
        if (IsKeyPressed(KEY_F7)) {
            if (showScriptView) FlushCurrentScriptBuffer();
            showScriptView = !showScriptView;
            if (showScriptView) {
                g_scriptState.isFocused = true;
                g_scriptState.open = true;
                LogMessage("[SCRIPT] Editor de scripts abierto (viewport)", LIGHTGRAY);
            }
            else {
                g_scriptState.isFocused = false;
                g_scriptState.open = false;
                LogMessage("[SCRIPT] Editor de scripts cerrado", LIGHTGRAY);
            }
        }

        // Ejecutar script de actualización (si existe)
        UpdateScripts();

        // --- ESTADO DEL EDITOR DE SCRIPTS (se define aquí, ANTES de usarlo) ---
        bool scriptEditorFocused = g_scriptState.open && g_scriptState.isFocused;

        // --- NIVEL ESCENARIO (PUNTERO) ---
        std::vector<EditorBlock>* nivelEscenario = &listaEscenas[escenaActualIndex];
        static std::unordered_map<int, Vector3> prevBlockPositions; // Guarda posición anterior de cada bloque (por índice)
        bool tieneObjetosEspeciales = (escenaActualIndex == 0);

        // Detectar archivos arrastrados y soltados sobre la ventana
        if (IsFileDropped()) {
            FilePathList droppedFiles = LoadDroppedFiles();
            for (int i = 0; i < (int)droppedFiles.count; i++) {
                const char* filePath = droppedFiles.paths[i];
                const char* extWithDot = GetFileExtension(filePath);
                const char* ext = extWithDot;
                if (ext[0] == '.') ext++;

                if (TextIsEqual(ext, "obj") || TextIsEqual(ext, "gltf") ||
                    TextIsEqual(ext, "glb") || TextIsEqual(ext, "fbx") ||
                    TextIsEqual(ext, "iqm") || TextIsEqual(ext, "vox")) {

                    Model model = LoadModel(filePath);
                    if (model.meshCount > 0) {
                        // 1. Reset base transform
                        model.transform = MatrixIdentity();

                        // 2. Fix initial rotation (stand up the model)
                        float rotX = (TextIsEqual(ext, "gltf") || TextIsEqual(ext, "glb")) ? 90.0f : -90.0f;
                        if (rotX != 0.0f) {
                            model.transform = MatrixMultiply(model.transform, MatrixRotateX(rotX * DEG2RAD));
                        }

                        // 3. Get original bounding box (after rotation)
                        BoundingBox bbOriginal = GetModelBoundingBox(model);
                        Vector3 originalSize = {
                            fabsf(bbOriginal.max.x - bbOriginal.min.x),
                            fabsf(bbOriginal.max.y - bbOriginal.min.y),
                            fabsf(bbOriginal.max.z - bbOriginal.min.z)
                        };
                        if (originalSize.x < 0.001f) originalSize.x = 1.0f;
                        if (originalSize.y < 0.001f) originalSize.y = 1.0f;
                        if (originalSize.z < 0.001f) originalSize.z = 1.0f;

                        // 4. Desired hitbox size (max dimension = 5.0)
                        float targetSize = 5.0f;
                        float maxDimOriginal = fmaxf(originalSize.x, fmaxf(originalSize.y, originalSize.z));
                        float scaleFactor = (maxDimOriginal > 0.0001f) ? (targetSize / maxDimOriginal) : 1.0f;

                        // 5. Apply scale to model transform
                        model.transform = MatrixMultiply(model.transform, MatrixScale(scaleFactor, scaleFactor, scaleFactor));

                        // 6. Get bounding box AFTER scaling (for accurate hitbox and pivot)
                        BoundingBox bbScaled = GetModelBoundingBox(model);
                        Vector3 sizeScaled = {
                            bbScaled.max.x - bbScaled.min.x,
                            bbScaled.max.y - bbScaled.min.y,
                            bbScaled.max.z - bbScaled.min.z
                        };
                        Vector3 centerScaled = {
                            (bbScaled.min.x + bbScaled.max.x) / 2.0f,
                            (bbScaled.min.y + bbScaled.max.y) / 2.0f,
                            (bbScaled.min.z + bbScaled.max.z) / 2.0f
                        };

                        // 7. Materials
                        for (int m = 0; m < model.materialCount; m++) {
                            model.materials[m].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
                        }

                        EditorBlock nuevo;
                        nuevo.shapeType = SHAPE_CUSTOM_MODEL;

                        // Position in front of camera
                        Vector3 dir = Vector3Subtract(miCamara.camara.target, miCamara.camara.position);
                        dir = Vector3Normalize(dir);
                        Vector3 posCam = Vector3Add(miCamara.camara.position, Vector3Scale(dir, 3.0f));
                        nuevo.position = posCam;

                        // IMPORTANT: Store the SCALED size as reference for dynamic scaling
                        nuevo.modelOriginalSize = sizeScaled;      // reference size (already scaled)
                        nuevo.modelPivotOffset = centerScaled;     // pivot offset
                        nuevo.size = sizeScaled;                   // hitbox size

                        // Minimum size for selection
                        if (nuevo.size.x < 0.5f) nuevo.size.x = 0.5f;
                        if (nuevo.size.y < 0.5f) nuevo.size.y = 0.5f;
                        if (nuevo.size.z < 0.5f) nuevo.size.z = 0.5f;

                        nuevo.color = WHITE;
                        nuevo.hasTexture = false;
                        nuevo.texturePath[0] = '\0';
                        nuevo.texture.id = 0;
                        strcpy_s(nuevo.modelPath, sizeof(nuevo.modelPath), filePath);
                        nuevo.customModel = model;
                        nuevo.hasCustomModel = true;

                        nivelEscenario->push_back(nuevo);
                        TraceLog(LOG_INFO, "Model imported: %s (Hitbox: %.2f x %.2f x %.2f)",
                            filePath, nuevo.size.x, nuevo.size.y, nuevo.size.z);
                    }
                }
            }
            UnloadDroppedFiles(droppedFiles);
        }

        auto getSelectedNumRows = [&]() {
            if (selectedType == OBJ_BLOCK || selectedType == OBJ_ENEMY) return 6;
            if (selectedType == OBJ_PLAYER) return 5;
            if (selectedType == OBJ_COIN) return 4;
            return 0;
            };
        auto getObjectVal = [&](int r) -> float {
            if (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex >= 0 && bloqueSeleccionadoIndex < (int)nivelEscenario->size()) {
                const auto& b = (*nivelEscenario)[bloqueSeleccionadoIndex];
                if (r == 0) return b.position.x; if (r == 1) return b.position.y; if (r == 2) return b.position.z;
                if (r == 3) return b.size.x; if (r == 4) return b.size.y; if (r == 5) return b.size.z;
                if (r == 6) return (float)b.color.r; if (r == 7) return (float)b.color.g; if (r == 8) return (float)b.color.b;
            }
            else if (selectedType == OBJ_PLAYER) {
                if (r == 0) return miJugador.posicion.x; if (r == 1) return miJugador.posicion.y; if (r == 2) return miJugador.posicion.z;
                if (r == 3) return miJugador.radio; if (r == 4) return miJugador.alturaTotal;
                if (r == 5) return (float)miJugador.color.r; if (r == 6) return (float)miJugador.color.g; if (r == 7) return (float)miJugador.color.b;
            }
            else if (selectedType == OBJ_COIN && tieneObjetosEspeciales) {
                if (r == 0) return miMoneda.posicion.x; if (r == 1) return miMoneda.posicion.y; if (r == 2) return miMoneda.posicion.z;
                if (r == 3) return miMoneda.radio;
            }
            else if (selectedType == OBJ_ENEMY && tieneObjetosEspeciales) {
                if (r == 0) return miEnemigo.posicion.x; if (r == 1) return miEnemigo.posicion.y; if (r == 2) return miEnemigo.posicion.z;
                if (r == 3) return miEnemigo.tamano.x; if (r == 4) return miEnemigo.tamano.y; if (r == 5) return miEnemigo.tamano.z;
            }
            return 0.0f;
            };
        auto setObjectVal = [&](int r, float val) {
            if (val < 0.1f && r >= 3) val = 0.1f;
            if (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex >= 0 && bloqueSeleccionadoIndex < (int)nivelEscenario->size()) {
                auto& b = (*nivelEscenario)[bloqueSeleccionadoIndex];
                if (r >= 6) { if (val < 0) val = 0; if (val > 255) val = 255; }
                if (r == 0) b.position.x = val; else if (r == 1) b.position.y = val; else if (r == 2) b.position.z = val;
                else if (r == 3) b.size.x = val; else if (r == 4) b.size.y = val; else if (r == 5) b.size.z = val;
                else if (r == 6) b.color.r = (unsigned char)val; else if (r == 7) b.color.g = (unsigned char)val; else if (r == 8) b.color.b = (unsigned char)val;
            }
            else if (selectedType == OBJ_PLAYER) {
                if (r >= 5) { if (val < 0) val = 0; if (val > 255) val = 255; }
                if (r == 0) miJugador.posicion.x = val; else if (r == 1) miJugador.posicion.y = val; else if (r == 2) miJugador.posicion.z = val;
                else if (r == 3) miJugador.radio = val; else if (r == 4) miJugador.alturaTotal = val;
                else if (r == 5) miJugador.color.r = (unsigned char)val; else if (r == 6) miJugador.color.g = (unsigned char)val; else if (r == 7) miJugador.color.b = (unsigned char)val;
            }
            else if (selectedType == OBJ_COIN && tieneObjetosEspeciales) {
                if (r == 0) miMoneda.posicion.x = val; else if (r == 1) miMoneda.posicion.y = val; else if (r == 2) miMoneda.posicion.z = val;
                else if (r == 3) miMoneda.radio = val;
            }
            else if (selectedType == OBJ_ENEMY && tieneObjetosEspeciales) {
                if (r == 0) miEnemigo.posicion.x = val; else if (r == 1) miEnemigo.posicion.y = val; else if (r == 2) miEnemigo.posicion.z = val;
                else if (r == 3) miEnemigo.tamano.x = val; else if (r == 4) miEnemigo.tamano.y = val; else if (r == 5) miEnemigo.tamano.z = val;
            }
            };

        Vector2 mousePos = GetMousePosition();
        float anchoActualVentana = (float)GetScreenWidth();
        float altoActualVentana = (float)GetScreenHeight();
        float scaleFactor = (anchoActualVentana / 800.0f) * 0.86f;
        if (scaleFactor < 0.8f) scaleFactor = 0.8f;
        float rowH = 28.0f * scaleFactor;

        // HUD
        float hudAncho = 320.0f * scaleFactor, hudAlto = 75.0f * scaleFactor;
        Rectangle hudRect = { (anchoActualVentana - hudAncho) / 2.0f, 10.0f * scaleFactor, hudAncho, hudAlto };
        Rectangle botonModo = { anchoActualVentana - (185.0f * scaleFactor), 15.0f * scaleFactor, 170.0f * scaleFactor, 38.0f * scaleFactor };

        // --- PANEL INSPECTOR DERECHO ---
        float panely = 60.0f * scaleFactor;
        float panelAncho = panelAnchoBase * scaleFactor;
        float consoleHeight = consoleHeightBase * scaleFactor;
        float panelAlto = altoActualVentana - panely - consoleHeight - 5.0f * scaleFactor;
        if (panelAlto < 80.0f * scaleFactor) panelAlto = 80.0f * scaleFactor;
        float panelX = anchoActualVentana - panelAncho;
        Rectangle inspectorRect = { panelX, panely, panelAncho, panelAlto };

        // --- MENÚ LATERAL IZQUIERDO ---
        float leftPanelY = 60.0f * scaleFactor;
        float leftPanelAncho = leftPanelAnchoBase * scaleFactor;
        float leftPanelAlto = altoActualVentana - leftPanelY - consoleHeight - 5.0f * scaleFactor;
        if (leftPanelAlto < 80.0f * scaleFactor) leftPanelAlto = 80.0f * scaleFactor;
        float leftPanelX = 0.0f;
        Rectangle leftPanelRect = { leftPanelX, leftPanelY, leftPanelAncho, leftPanelAlto };

        // --- VIEWPORT ---
        float hudBottom = 10.0f * scaleFactor + hudAlto + 10.0f * scaleFactor;
        float marginBottom = 5.0f * scaleFactor + consoleHeight + 5.0f * scaleFactor;
        float totalSpace = altoActualVentana - hudBottom - marginBottom;
        if (viewportTopRatio < 0.0f) viewportTopRatio = 0.0f;
        if (viewportTopRatio > 1.0f) viewportTopRatio = 1.0f;
        if (viewportBottomRatio < 0.0f) viewportBottomRatio = 0.0f;
        if (viewportBottomRatio > 1.0f) viewportBottomRatio = 1.0f;
        if (viewportTopRatio + viewportBottomRatio > 1.0f) {
            if (viewportBottomRatio > 0.5f) viewportBottomRatio = 0.5f;
            viewportTopRatio = 1.0f - viewportBottomRatio;
        }
        float viewportY = hudBottom + totalSpace * viewportTopRatio;
        float viewportBottom = altoActualVentana - marginBottom - totalSpace * viewportBottomRatio;
        float viewportH = viewportBottom - viewportY;
        float minViewportH = 50.0f * scaleFactor;
        if (viewportH < minViewportH) {
            if (viewportTopRatio > 0.5f) {
                viewportTopRatio -= 0.1f;
                if (viewportTopRatio < 0.0f) viewportTopRatio = 0.0f;
            }
            else {
                viewportBottomRatio -= 0.1f;
                if (viewportBottomRatio < 0.0f) viewportBottomRatio = 0.0f;
            }
            viewportY = hudBottom + totalSpace * viewportTopRatio;
            viewportBottom = altoActualVentana - marginBottom - totalSpace * viewportBottomRatio;
            viewportH = viewportBottom - viewportY;
            if (viewportH < minViewportH) {
                viewportTopRatio = 0.0f;
                viewportBottomRatio = 0.0f;
                viewportY = hudBottom;
                viewportBottom = altoActualVentana - marginBottom;
                viewportH = viewportBottom - viewportY;
            }
        }

        int viewportX = (int)(leftPanelX + leftPanelAncho);
        int viewportY_int = (int)viewportY;
        int viewportW = (int)(panelX - (leftPanelX + leftPanelAncho));
        int viewportH_int = (int)viewportH;
        if (viewportW < 1) viewportW = 1;
        if (viewportH_int < 1) viewportH_int = 1;
        Rectangle viewportRect = { (float)viewportX, (float)viewportY_int, (float)viewportW, (float)viewportH_int };

        // --- CONSOLA ---
        float consoleY = viewportBottom + 5.0f * scaleFactor;
        float consoleWidth = panelX - (leftPanelX + leftPanelAncho);
        Rectangle consoleRect = { leftPanelX + leftPanelAncho, consoleY, consoleWidth, consoleHeight };

        float resizeZoneHeightCons = 10.0f;
        bool mouseOverConsoleResize = (mousePos.x >= consoleRect.x && mousePos.x <= consoleRect.x + consoleRect.width &&
            mousePos.y >= consoleRect.y - resizeZoneHeightCons && mousePos.y <= consoleRect.y + resizeZoneHeightCons);
        if (mouseOverConsoleResize && !resizingConsole) {
            SetMouseCursor(MOUSE_CURSOR_RESIZE_NS);
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouseOverConsoleResize) {
            resizingConsole = true;
        }
        if (resizingConsole && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            float newHeight = (consoleY + consoleHeight - mousePos.y) / scaleFactor;
            if (newHeight < 30.0f) newHeight = 30.0f;
            if (newHeight > 400.0f) newHeight = 400.0f;
            consoleHeightBase = newHeight;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            resizingConsole = false;
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        }

        // Recalcular después de redimensionar consola
        panelAlto = altoActualVentana - panely - consoleHeight - 5.0f * scaleFactor;
        if (panelAlto < 80.0f * scaleFactor) panelAlto = 80.0f * scaleFactor;
        leftPanelAlto = altoActualVentana - leftPanelY - consoleHeight - 5.0f * scaleFactor;
        if (leftPanelAlto < 80.0f * scaleFactor) leftPanelAlto = 80.0f * scaleFactor;
        marginBottom = 5.0f * scaleFactor + consoleHeight + 5.0f * scaleFactor;
        totalSpace = altoActualVentana - hudBottom - marginBottom;
        viewportY = hudBottom + totalSpace * viewportTopRatio;
        viewportBottom = altoActualVentana - marginBottom - totalSpace * viewportBottomRatio;
        viewportH = viewportBottom - viewportY;
        consoleY = viewportBottom + 5.0f * scaleFactor;
        consoleHeight = consoleHeightBase * scaleFactor;
        consoleWidth = panelX - (leftPanelX + leftPanelAncho);
        consoleRect = { leftPanelX + leftPanelAncho, consoleY, consoleWidth, consoleHeight };
        viewportX = (int)(leftPanelX + leftPanelAncho);
        viewportY_int = (int)viewportY;
        viewportW = (int)(panelX - (leftPanelX + leftPanelAncho));
        viewportH_int = (int)viewportH;
        if (viewportW < 1) viewportW = 1;
        if (viewportH_int < 1) viewportH_int = 1;
        viewportRect = { (float)viewportX, (float)viewportY_int, (float)viewportW, (float)viewportH_int };

        // --- Zonas de arrastre ---
        float resizeZoneHeight = 8.0f;
        float topResizeY = viewportY;
        bool mouseOverTopResize = (mousePos.x >= leftPanelX + leftPanelAncho &&
            mousePos.x <= panelX &&
            mousePos.y >= topResizeY - resizeZoneHeight &&
            mousePos.y <= topResizeY + resizeZoneHeight);
        float bottomResizeY = viewportY + viewportH;
        bool mouseOverBottomResize = (mousePos.x >= leftPanelX + leftPanelAncho &&
            mousePos.x <= panelX &&
            mousePos.y >= bottomResizeY - resizeZoneHeight &&
            mousePos.y <= bottomResizeY + resizeZoneHeight);

        float leftPanelRightEdge = leftPanelX + leftPanelAncho;
        float rightPanelLeftEdge = panelX;
        bool mouseOverLeftResize = (mousePos.x >= leftPanelRightEdge - resizeZoneWidth && mousePos.x <= leftPanelRightEdge + resizeZoneWidth && mousePos.y > leftPanelY && mousePos.y < leftPanelY + leftPanelAlto);
        bool mouseOverRightResize = (mousePos.x >= rightPanelLeftEdge - resizeZoneWidth && mousePos.x <= rightPanelLeftEdge + resizeZoneWidth && mousePos.y > panely && mousePos.y < panely + panelAlto);

        if (mouseOverLeftResize || mouseOverRightResize) {
            SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
        }
        else if (mouseOverTopResize || mouseOverBottomResize) {
            SetMouseCursor(MOUSE_CURSOR_RESIZE_NS);
        }
        else if (!resizingLeft && !resizingRight && !resizingTop && !resizingBottom && !resizingConsole) {
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (mouseOverLeftResize) resizingLeft = true;
            else if (mouseOverRightResize) resizingRight = true;
            else if (mouseOverTopResize) resizingTop = true;
            else if (mouseOverBottomResize) resizingBottom = true;
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            if (resizingLeft) {
                float newWidth = mousePos.x / scaleFactor;
                if (newWidth < 30.0f) newWidth = 30.0f;
                if (newWidth > 400.0f) newWidth = 400.0f;
                leftPanelAnchoBase = newWidth;
            }
            else if (resizingRight) {
                float newWidth = (anchoActualVentana - mousePos.x) / scaleFactor;
                if (newWidth < 30.0f) newWidth = 30.0f;
                if (newWidth > 400.0f) newWidth = 400.0f;
                panelAnchoBase = newWidth;
            }
            else if (resizingTop) {
                float delta = GetMouseDelta().y;
                float ratioDelta = delta / totalSpace;
                viewportTopRatio += ratioDelta;
                if (viewportTopRatio < 0.0f) viewportTopRatio = 0.0f;
                if (viewportTopRatio > 1.0f) viewportTopRatio = 1.0f;
                if (viewportTopRatio + viewportBottomRatio > 1.0f) {
                    viewportBottomRatio = 1.0f - viewportTopRatio;
                }
            }
            else if (resizingBottom) {
                float delta = -GetMouseDelta().y;
                float ratioDelta = delta / totalSpace;
                viewportBottomRatio += ratioDelta;
                if (viewportBottomRatio < 0.0f) viewportBottomRatio = 0.0f;
                if (viewportBottomRatio > 1.0f) viewportBottomRatio = 1.0f;
                if (viewportTopRatio + viewportBottomRatio > 1.0f) {
                    viewportTopRatio = 1.0f - viewportBottomRatio;
                }
            }
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            resizingLeft = false;
            resizingRight = false;
            resizingTop = false;
            resizingBottom = false;
            resizingConsole = false;
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        }

        leftPanelAncho = leftPanelAnchoBase * scaleFactor;
        panelAncho = panelAnchoBase * scaleFactor;
        panelX = anchoActualVentana - panelAncho;
        consoleWidth = panelX - (leftPanelX + leftPanelAncho);
        consoleRect = { leftPanelX + leftPanelAncho, consoleY, consoleWidth, consoleHeight };
        viewportX = (int)(leftPanelX + leftPanelAncho);
        viewportY_int = (int)viewportY;
        viewportW = (int)(panelX - (leftPanelX + leftPanelAncho));
        viewportH_int = (int)viewportH;
        if (viewportW < 1) viewportW = 1;
        if (viewportH_int < 1) viewportH_int = 1;
        viewportRect = { (float)viewportX, (float)viewportY_int, (float)viewportW, (float)viewportH_int };

        // --- UI de pestañas ---
        float tabHeight = 30.0f * scaleFactor;
        Rectangle tabBtnNodes = { leftPanelX, leftPanelY, leftPanelAncho / 2.0f, tabHeight };
        Rectangle tabBtnToolbox = { leftPanelX + leftPanelAncho / 2.0f, leftPanelY, leftPanelAncho / 2.0f, tabHeight };
        if (currentMode == MODE_EDITOR && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(mousePos, tabBtnNodes)) {
                leftPanelTab = 0;
                LogMessage("[UI] Cambiando a pestaña: Jerarquía", LIGHTGRAY);
            }
            if (CheckCollisionPointRec(mousePos, tabBtnToolbox)) {
                leftPanelTab = 1;
                LogMessage("[UI] Cambiando a pestaña: Toolbox", LIGHTGRAY);
            }
        }
        float contentStartY = leftPanelY + tabHeight + 4.0f * scaleFactor;
        float treeItemHeight = 20.0f * scaleFactor;
        float toolboxBaseY = contentStartY;

        float btnW = leftPanelAncho - (24.0f * scaleFactor);
        float btnH = 28.0f * scaleFactor;
        float startBtnY = toolboxBaseY + 32.0f * scaleFactor;
        Rectangle btnCube = { leftPanelX + 12.0f * scaleFactor, startBtnY, btnW, btnH };
        Rectangle btnSphere = { leftPanelX + 12.0f * scaleFactor, startBtnY + 34.0f * scaleFactor, btnW, btnH };
        Rectangle btnCylinder = { leftPanelX + 12.0f * scaleFactor, startBtnY + 68.0f * scaleFactor, btnW, btnH };
        Rectangle btnCone = { leftPanelX + 12.0f * scaleFactor, startBtnY + 102.0f * scaleFactor, btnW, btnH };
        Rectangle btnPortal = { leftPanelX + 12.0f * scaleFactor, startBtnY + 136.0f * scaleFactor, btnW, btnH };
        float sceneSectionY = startBtnY + 175.0f * scaleFactor;
        Rectangle btnNuevaEscena = { leftPanelX + 12.0f * scaleFactor, sceneSectionY, btnW, btnH };
        float arrowW = 30.0f * scaleFactor, middleW = btnW - (arrowW * 2.0f);
        Rectangle btnEscenaPrev = { leftPanelX + 12.0f * scaleFactor, sceneSectionY + 34.0f * scaleFactor, arrowW, btnH };
        Rectangle btnEscenaDisplay = { leftPanelX + 12.0f * scaleFactor + arrowW, sceneSectionY + 34.0f * scaleFactor, middleW, btnH };
        Rectangle btnEscenaNext = { leftPanelX + 12.0f * scaleFactor + arrowW + middleW, sceneSectionY + 34.0f * scaleFactor, arrowW, btnH };
        Rectangle btnBorrarEscena = { leftPanelX + 12.0f * scaleFactor, sceneSectionY + 68.0f * scaleFactor, btnW, btnH };
        float saveLoadY = sceneSectionY + 105.0f * scaleFactor;
        Rectangle btnGuardar = { leftPanelX + 12.0f * scaleFactor, saveLoadY, btnW, btnH };
        Rectangle btnCargar = { leftPanelX + 12.0f * scaleFactor, saveLoadY + 34.0f * scaleFactor, btnW, btnH };
        Rectangle btnExportar = { leftPanelX + 12.0f * scaleFactor, saveLoadY + 68.0f * scaleFactor, btnW, btnH };

        // Detectar UI
        bool clicEnHUD = CheckCollisionPointRec(mousePos, hudRect) || CheckCollisionPointRec(mousePos, botonModo);
        bool clickEnUI = clicEnHUD || CheckCollisionPointRec(mousePos, leftPanelRect) || CheckCollisionPointRec(mousePos, inspectorRect) ||
            CheckCollisionPointRec(mousePos, consoleRect) || CheckCollisionPointRec(mousePos, tabBtnNodes) || CheckCollisionPointRec(mousePos, tabBtnToolbox);
        if (leftPanelTab == 1) {
            clickEnUI = clickEnUI || CheckCollisionPointRec(mousePos, btnCube) || CheckCollisionPointRec(mousePos, btnSphere) ||
                CheckCollisionPointRec(mousePos, btnCylinder) || CheckCollisionPointRec(mousePos, btnCone) ||
                CheckCollisionPointRec(mousePos, btnPortal) || CheckCollisionPointRec(mousePos, btnNuevaEscena) ||
                CheckCollisionPointRec(mousePos, btnEscenaPrev) || CheckCollisionPointRec(mousePos, btnEscenaDisplay) ||
                CheckCollisionPointRec(mousePos, btnEscenaNext) || CheckCollisionPointRec(mousePos, btnBorrarEscena) ||
                CheckCollisionPointRec(mousePos, btnGuardar) || CheckCollisionPointRec(mousePos, btnCargar) || CheckCollisionPointRec(mousePos, btnExportar);
        }
        bool mouseOverUI = clickEnUI || showContextMenu || showNodeTypeModal || CheckCollisionPointRec(mousePos, consoleRect);
        if (showContextMenu) {
            Rectangle menuRect = { contextMenuPos.x, contextMenuPos.y, 160.0f * scaleFactor, 25.0f * 6.0f * scaleFactor };
            if (CheckCollisionPointRec(mousePos, menuRect)) mouseOverUI = true;
        }
        if (showNodeTypeModal) {
            float modalW = 180.0f * scaleFactor, modalH = 170.0f * scaleFactor;
            Rectangle modalRect = { (anchoActualVentana - modalW) / 2.0f, (altoActualVentana - modalH) / 2.0f, modalW, modalH };
            if (CheckCollisionPointRec(mousePos, modalRect)) mouseOverUI = true;
        }

        // --- Cursor ---
        if (currentMode == MODE_PLAY) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                ShowCursor();
                EnableCursor();
            }
        }
        else {
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !mouseOverUI && !scriptEditorFocused && !showScriptView)
                HideCursor();
            else
                ShowCursor();
        }

        bool clicEnBoton = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mousePos, botonModo) && !scriptEditorFocused;
        bool teclaAlternar = IsKeyPressed(KEY_TAB) && !scriptEditorFocused;

        // --- Menú contextual ---
        if (currentMode == MODE_EDITOR && leftPanelTab == 0 && !showNodeTypeModal && !scriptEditorFocused) {
            // ... (todo el código del menú contextual)
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && CheckCollisionPointRec(mousePos, leftPanelRect)) {
                showContextMenu = true;
                contextMenuPos = mousePos;
                LogMessage("[EDITOR] Menú contextual abierto.", LIGHTGRAY);

                float itemY = contentStartY + 8.0f * scaleFactor;
                Rectangle playerItem = { leftPanelX + 10.0f * scaleFactor, itemY, leftPanelAncho - 20.0f * scaleFactor, treeItemHeight };
                if (CheckCollisionPointRec(mousePos, playerItem)) { contextObjType = OBJ_PLAYER; contextObjIndex = -1; }
                itemY += treeItemHeight;
                if (tieneObjetosEspeciales) {
                    Rectangle coinItem = { leftPanelX + 10.0f * scaleFactor, itemY, leftPanelAncho - 20.0f * scaleFactor, treeItemHeight };
                    if (CheckCollisionPointRec(mousePos, coinItem)) { contextObjType = OBJ_COIN; contextObjIndex = -1; }
                    itemY += treeItemHeight;
                }
                if (tieneObjetosEspeciales) {
                    Rectangle enemyItem = { leftPanelX + 10.0f * scaleFactor, itemY, leftPanelAncho - 20.0f * scaleFactor, treeItemHeight };
                    if (CheckCollisionPointRec(mousePos, enemyItem)) { contextObjType = OBJ_ENEMY; contextObjIndex = -1; }
                    itemY += treeItemHeight;
                }
                for (int i = 0; i < (int)nivelEscenario->size(); i++) {
                    Rectangle blockItem = { leftPanelX + 10.0f * scaleFactor, itemY, leftPanelAncho - 20.0f * scaleFactor, treeItemHeight };
                    if (CheckCollisionPointRec(mousePos, blockItem)) { contextObjType = OBJ_BLOCK; contextObjIndex = i; break; }
                    itemY += treeItemHeight;
                }
                if (!showContextMenu) { contextObjType = OBJ_NONE; contextObjIndex = -1; }
            }

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mousePos, leftPanelRect)) {
                float itemY = contentStartY + 8.0f * scaleFactor;
                Rectangle playerItem = { leftPanelX + 10.0f * scaleFactor, itemY, leftPanelAncho - 20.0f * scaleFactor, treeItemHeight };
                if (CheckCollisionPointRec(mousePos, playerItem)) {
                    selectedType = OBJ_PLAYER;
                    bloqueSeleccionadoIndex = -1;
                    activeTextField = -1;
                    LogMessage("[EDITOR] Seleccionado nodo: Jugador", LIGHTGRAY);
                }
                itemY += treeItemHeight;
                if (tieneObjetosEspeciales) {
                    Rectangle coinItem = { leftPanelX + 10.0f * scaleFactor, itemY, leftPanelAncho - 20.0f * scaleFactor, treeItemHeight };
                    if (CheckCollisionPointRec(mousePos, coinItem)) {
                        selectedType = OBJ_COIN;
                        bloqueSeleccionadoIndex = -1;
                        activeTextField = -1;
                        LogMessage("[EDITOR] Seleccionado nodo: Moneda", LIGHTGRAY);
                    }
                    itemY += treeItemHeight;
                }
                if (tieneObjetosEspeciales) {
                    Rectangle enemyItem = { leftPanelX + 10.0f * scaleFactor, itemY, leftPanelAncho - 20.0f * scaleFactor, treeItemHeight };
                    if (CheckCollisionPointRec(mousePos, enemyItem)) {
                        selectedType = OBJ_ENEMY;
                        bloqueSeleccionadoIndex = -1;
                        activeTextField = -1;
                        LogMessage("[EDITOR] Seleccionado nodo: Enemigo", LIGHTGRAY);
                    }
                    itemY += treeItemHeight;
                }
                for (int i = 0; i < (int)nivelEscenario->size(); i++) {
                    Rectangle blockItem = { leftPanelX + 10.0f * scaleFactor, itemY, leftPanelAncho - 20.0f * scaleFactor, treeItemHeight };
                    if (CheckCollisionPointRec(mousePos, blockItem)) {
                        selectedType = OBJ_BLOCK;
                        bloqueSeleccionadoIndex = i;
                        activeTextField = -1;
                        const char* shapeName = "Bloque";
                        if ((*nivelEscenario)[i].shapeType == SHAPE_CUBE) shapeName = "Cubo";
                        else if ((*nivelEscenario)[i].shapeType == SHAPE_SPHERE) shapeName = "Esfera";
                        else if ((*nivelEscenario)[i].shapeType == SHAPE_CYLINDER) shapeName = "Cilindro";
                        else if ((*nivelEscenario)[i].shapeType == SHAPE_CONE) shapeName = "Cono";
                        else if ((*nivelEscenario)[i].shapeType == SHAPE_PORTAL) shapeName = "Portal";
                        char logMsg[128];
                        snprintf(logMsg, sizeof(logMsg), "[EDITOR] Seleccionado nodo: %s (ID %d)", shapeName, i);
                        LogMessage(logMsg, LIGHTGRAY);
                        break;
                    }
                    itemY += treeItemHeight;
                }
            }
        }

        // --- Modal de selección de tipo ---
        if (showNodeTypeModal) {
            float modalW = 180.0f * scaleFactor, modalH = 170.0f * scaleFactor;
            Rectangle modalRect = { (anchoActualVentana - modalW) / 2.0f, (altoActualVentana - modalH) / 2.0f, modalW, modalH };
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (!CheckCollisionPointRec(mousePos, modalRect)) { showNodeTypeModal = false; }
                else {
                    float itemHeight = 28.0f * scaleFactor;
                    const char* typeNames[] = { "Cubo", "Esfera", "Cilindro", "Cono", "Portal" };
                    ShapeType types[] = { SHAPE_CUBE, SHAPE_SPHERE, SHAPE_CYLINDER, SHAPE_CONE, SHAPE_PORTAL };
                    for (int i = 0; i < 5; i++) {
                        Rectangle itemRect = { modalRect.x + 10.0f * scaleFactor, modalRect.y + 25.0f * scaleFactor + i * itemHeight, modalRect.width - 20.0f * scaleFactor, itemHeight };
                        if (CheckCollisionPointRec(mousePos, itemRect)) {
                            EditorBlock nuevo; nuevo.shapeType = types[i]; nuevo.size = tamanoActualBloque; nuevo.color = inspectorColors[indiceColorActual];
                            nuevo.hasTexture = false;
                            nuevo.texturePath[0] = '\0';
                            nuevo.texture.id = 0;
                            Ray ray = GetMouseRay({ anchoActualVentana / 2.0f, altoActualVentana / 2.0f }, miCamara.camara);
                            Vector3 puntoImpacto = { 0,0,0 };
                            if (fabsf(ray.direction.y) > 0.0001f) { float t = -ray.position.y / ray.direction.y; if (t > 0) puntoImpacto = { ray.position.x + t * ray.direction.x, tamanoActualBloque.y / 2.0f, ray.position.z + t * ray.direction.z }; }
                            nuevo.position = puntoImpacto; nuevo.targetScene = (escenaActualIndex + 1);
                            nivelEscenario->push_back(nuevo);
                            showNodeTypeModal = false;
                            break;
                        }
                    }
                }
            }
        }

        // --- Menú contextual acciones ---
        if (showContextMenu && !showNodeTypeModal) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Rectangle menuRect = { contextMenuPos.x, contextMenuPos.y, 160.0f * scaleFactor, 25.0f * 8.0f * scaleFactor };
                if (!CheckCollisionPointRec(mousePos, menuRect)) showContextMenu = false;
            }
        }

        if (currentMode == MODE_EDITOR && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !scriptEditorFocused) {
            if (showContextMenu && !showNodeTypeModal) {
                float menuX = contextMenuPos.x, menuY = contextMenuPos.y;
                float menuWidth = 160.0f * scaleFactor, itemHeight = 25.0f * scaleFactor;
                bool isVacío = (contextObjType == OBJ_NONE);
                std::vector<const char*> items;
                if (isVacío) {
                    if (hasCopiedBlock) items.push_back("Pegar");
                    items.push_back("Añadir Nodo...");
                }
                else {
                    items.push_back("Copiar");
                    items.push_back("Cortar");
                    if (hasCopiedBlock) items.push_back("Pegar");
                    items.push_back("Borrar");
                    items.push_back("Duplicar");
                    items.push_back("Crear Script Virtual");
                    items.push_back("Cargar Script Existente...");
                }
                int itemCount = (int)items.size();
                bool clicked = false;
                for (int i = 0; i < itemCount; i++) {
                    Rectangle itemRect = { menuX, menuY + i * itemHeight, menuWidth, itemHeight };
                    if (CheckCollisionPointRec(mousePos, itemRect)) {
                        clicked = true; showContextMenu = false;
                        if (isVacío) {
                            if (i == 0 && hasCopiedBlock) {
                                Ray ray = GetMouseRay(contextMenuPos, miCamara.camara);
                                Vector3 puntoImpacto = { 0,0,0 };
                                if (fabsf(ray.direction.y) > 0.0001f) { float t = -ray.position.y / ray.direction.y; if (t > 0) puntoImpacto = { ray.position.x + t * ray.direction.x, tamanoActualBloque.y / 2, ray.position.z + t * ray.direction.z }; }
                                EditorBlock nuevo = copiedBlock; nuevo.position = puntoImpacto; nivelEscenario->push_back(nuevo);
                                if (cutOperation) { hasCopiedBlock = false; cutOperation = false; }
                            }
                            else { showNodeTypeModal = true; }
                        }
                        else {
                            if (contextObjType == OBJ_BLOCK && contextObjIndex >= 0 && contextObjIndex < (int)nivelEscenario->size()) {
                                EditorBlock& targetBlock = (*nivelEscenario)[contextObjIndex];
                                if (items[i] == "Copiar") { copiedBlock = targetBlock; hasCopiedBlock = true; cutOperation = false; }
                                else if (items[i] == "Cortar") { copiedBlock = targetBlock; hasCopiedBlock = true; cutOperation = true; nivelEscenario->erase(nivelEscenario->begin() + contextObjIndex); if (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex == contextObjIndex) { selectedType = OBJ_NONE; bloqueSeleccionadoIndex = -1; } }
                                else if (items[i] == "Pegar") { if (hasCopiedBlock) { EditorBlock nuevo = copiedBlock; nuevo.position.x += 1.5f; nuevo.position.z += 1.5f; nivelEscenario->push_back(nuevo); if (cutOperation) { hasCopiedBlock = false; cutOperation = false; } } }
                                else if (items[i] == "Borrar") { nivelEscenario->erase(nivelEscenario->begin() + contextObjIndex); if (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex == contextObjIndex) { selectedType = OBJ_NONE; bloqueSeleccionadoIndex = -1; } }
                                else if (items[i] == "Duplicar") { EditorBlock dupe = targetBlock; dupe.position.x += 1.5f; dupe.position.z += 1.5f; nivelEscenario->push_back(dupe); }
                                else if (items[i] == "Crear Script Virtual") {
                                    char nombreScript[64];
                                    snprintf(nombreScript, sizeof(nombreScript), "bloque_%d", contextObjIndex);
                                    const char* virtualPath = CreateAndGetVirtualScriptPath(nombreScript);
                                    strcpy_s(targetBlock.scriptPath, sizeof(targetBlock.scriptPath), virtualPath);
                                    targetBlock.hasScript = true;

                                    const char* code = GetScriptBufferContent(virtualPath);
                                    if (code) {
                                        strncpy(targetBlock.scriptSource, code, sizeof(targetBlock.scriptSource) - 1);
                                        targetBlock.scriptSource[sizeof(targetBlock.scriptSource) - 1] = '\0';
                                    }
                                    else {
                                        targetBlock.scriptSource[0] = '\0';
                                    }

                                    g_scriptState.open = true;
                                    g_scriptState.isFocused = true;
                                    showScriptView = true;
                                    LogMessage("[SCRIPT] Script virtual asignado al bloque", GREEN);
                                }
                                else if (items[i] == "Cargar Script Existente...") {
                                    char filePath[512] = "";
                                    if (OpenScriptDialog(filePath, sizeof(filePath))) {
                                        std::ifstream in(filePath);
                                        if (in.is_open()) {
                                            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                                            in.close();
                                            std::string clean = SanitizeScript(content);
                                            strncpy(targetBlock.scriptSource, clean.c_str(), sizeof(targetBlock.scriptSource) - 1);
                                            targetBlock.scriptSource[sizeof(targetBlock.scriptSource) - 1] = '\0';
                                            targetBlock.hasScript = true;
                                            strcpy_s(targetBlock.scriptPath, sizeof(targetBlock.scriptPath), filePath);
                                            LogMessage("[SCRIPT] Script cargado desde archivo y asignado al bloque", GREEN);
                                        }
                                        else {
                                            LogMessage("[ERROR] No se pudo leer el archivo de script", RED);
                                        }
                                    }
                                }
                            }
                            else if (contextObjType == OBJ_PLAYER) {
                                if (items[i] == "Crear Script Virtual") {
                                    const char* virtualPath = CreateAndGetVirtualScriptPath("jugador");
                                    strcpy_s(miJugador.scriptPath, sizeof(miJugador.scriptPath), virtualPath);
                                    miJugador.hasScript = true;

                                    const char* code = GetScriptBufferContent(virtualPath);
                                    if (code) {
                                        strncpy(miJugador.scriptSource, code, sizeof(miJugador.scriptSource) - 1);
                                        miJugador.scriptSource[sizeof(miJugador.scriptSource) - 1] = '\0';
                                    }
                                    else {
                                        miJugador.scriptSource[0] = '\0';
                                    }

                                    g_scriptState.open = true;
                                    g_scriptState.isFocused = true;
                                    showScriptView = true;
                                    LogMessage("[SCRIPT] Script virtual asignado al jugador", GREEN);
                                }
                                else if (items[i] == "Cargar Script Existente...") {
                                    char filePath[512] = "";
                                    if (OpenScriptDialog(filePath, sizeof(filePath))) {
                                        std::ifstream in(filePath);
                                        if (in.is_open()) {
                                            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                                            in.close();
                                            std::string clean = SanitizeScript(content);
                                            strncpy(miJugador.scriptSource, clean.c_str(), sizeof(miJugador.scriptSource) - 1);
                                            miJugador.scriptSource[sizeof(miJugador.scriptSource) - 1] = '\0';
                                            miJugador.hasScript = true;
                                            strcpy_s(miJugador.scriptPath, sizeof(miJugador.scriptPath), filePath);
                                            LogMessage("[SCRIPT] Script cargado desde archivo y asignado al jugador", GREEN);
                                        }
                                        else {
                                            LogMessage("[ERROR] No se pudo leer el archivo de script", RED);
                                        }
                                    }
                                }
                            }
                            else if (contextObjType == OBJ_COIN) {
                                if (items[i] == "Crear Script Virtual") {
                                    const char* virtualPath = CreateAndGetVirtualScriptPath("moneda");
                                    strcpy_s(miMoneda.scriptPath, sizeof(miMoneda.scriptPath), virtualPath);
                                    miMoneda.hasScript = true;

                                    const char* code = GetScriptBufferContent(virtualPath);
                                    if (code) {
                                        strncpy(miMoneda.scriptSource, code, sizeof(miMoneda.scriptSource) - 1);
                                        miMoneda.scriptSource[sizeof(miMoneda.scriptSource) - 1] = '\0';
                                    }
                                    else {
                                        miMoneda.scriptSource[0] = '\0';
                                    }

                                    g_scriptState.open = true;
                                    g_scriptState.isFocused = true;
                                    showScriptView = true;
                                    LogMessage("[SCRIPT] Script virtual asignado a la moneda", GREEN);
                                }
                                else if (items[i] == "Cargar Script Existente...") {
                                    char filePath[512] = "";
                                    if (OpenScriptDialog(filePath, sizeof(filePath))) {
                                        std::ifstream in(filePath);
                                        if (in.is_open()) {
                                            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                                            in.close();
                                            std::string clean = SanitizeScript(content);
                                            strncpy(miMoneda.scriptSource, clean.c_str(), sizeof(miMoneda.scriptSource) - 1);
                                            miMoneda.scriptSource[sizeof(miMoneda.scriptSource) - 1] = '\0';
                                            miMoneda.hasScript = true;
                                            strcpy_s(miMoneda.scriptPath, sizeof(miMoneda.scriptPath), filePath);
                                            LogMessage("[SCRIPT] Script cargado desde archivo y asignado a la moneda", GREEN);
                                        }
                                        else {
                                            LogMessage("[ERROR] No se pudo leer el archivo de script", RED);
                                        }
                                    }
                                }
                            }
                            else if (contextObjType == OBJ_ENEMY) {
                                if (items[i] == "Crear Script Virtual") {
                                    const char* virtualPath = CreateAndGetVirtualScriptPath("enemigo");
                                    strcpy_s(miEnemigo.scriptPath, sizeof(miEnemigo.scriptPath), virtualPath);
                                    miEnemigo.hasScript = true;

                                    const char* code = GetScriptBufferContent(virtualPath);
                                    if (code) {
                                        strncpy(miEnemigo.scriptSource, code, sizeof(miEnemigo.scriptSource) - 1);
                                        miEnemigo.scriptSource[sizeof(miEnemigo.scriptSource) - 1] = '\0';
                                    }
                                    else {
                                        miEnemigo.scriptSource[0] = '\0';
                                    }

                                    g_scriptState.open = true;
                                    g_scriptState.isFocused = true;
                                    showScriptView = true;
                                    LogMessage("[SCRIPT] Script virtual asignado al enemigo", GREEN);
                                }
                                else if (items[i] == "Cargar Script Existente...") {
                                    char filePath[512] = "";
                                    if (OpenScriptDialog(filePath, sizeof(filePath))) {
                                        std::ifstream in(filePath);
                                        if (in.is_open()) {
                                            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                                            in.close();
                                            std::string clean = SanitizeScript(content);
                                            strncpy(miEnemigo.scriptSource, clean.c_str(), sizeof(miEnemigo.scriptSource) - 1);
                                            miEnemigo.scriptSource[sizeof(miEnemigo.scriptSource) - 1] = '\0';
                                            miEnemigo.hasScript = true;
                                            strcpy_s(miEnemigo.scriptPath, sizeof(miEnemigo.scriptPath), filePath);
                                            LogMessage("[SCRIPT] Script cargado desde archivo y asignado al enemigo", GREEN);
                                        }
                                        else {
                                            LogMessage("[ERROR] No se pudo leer el archivo de script", RED);
                                        }
                                    }
                                }
                            }
                        }
                        break;
                    }
                }
                if (clicked) continue;
            }

            // Toolbox
            if (leftPanelTab == 1 && !showContextMenu && !showNodeTypeModal) {
                if (CheckCollisionPointRec(mousePos, btnCube)) formaActualCreacion = SHAPE_CUBE;
                if (CheckCollisionPointRec(mousePos, btnSphere)) formaActualCreacion = SHAPE_SPHERE;
                if (CheckCollisionPointRec(mousePos, btnCylinder)) formaActualCreacion = SHAPE_CYLINDER;
                if (CheckCollisionPointRec(mousePos, btnCone)) formaActualCreacion = SHAPE_CONE;
                if (CheckCollisionPointRec(mousePos, btnPortal)) formaActualCreacion = SHAPE_PORTAL;
                if (CheckCollisionPointRec(mousePos, btnNuevaEscena)) {
                    listaEscenas.push_back({});
                    escenaActualIndex = (int)listaEscenas.size() - 1;
                    nivelEscenario = &listaEscenas[escenaActualIndex];
                    selectedType = OBJ_NONE;
                    bloqueSeleccionadoIndex = -1;
                }
                if (CheckCollisionPointRec(mousePos, btnBorrarEscena) && listaEscenas.size() > 1) {
                    listaEscenas.erase(listaEscenas.begin() + escenaActualIndex);
                    if (escenaActualIndex >= (int)listaEscenas.size()) escenaActualIndex = (int)listaEscenas.size() - 1;
                    nivelEscenario = &listaEscenas[escenaActualIndex];
                    selectedType = OBJ_NONE;
                    bloqueSeleccionadoIndex = -1;
                }
                if (CheckCollisionPointRec(mousePos, btnEscenaPrev)) {
                    escenaActualIndex--;
                    if (escenaActualIndex < 0) escenaActualIndex = (int)listaEscenas.size() - 1;
                    nivelEscenario = &listaEscenas[escenaActualIndex];
                    selectedType = OBJ_NONE;
                    bloqueSeleccionadoIndex = -1;
                }
                if (CheckCollisionPointRec(mousePos, btnEscenaNext) || CheckCollisionPointRec(mousePos, btnEscenaDisplay)) {
                    escenaActualIndex = (escenaActualIndex + 1) % listaEscenas.size();
                    nivelEscenario = &listaEscenas[escenaActualIndex];
                    selectedType = OBJ_NONE;
                    bloqueSeleccionadoIndex = -1;
                }
                if (CheckCollisionPointRec(mousePos, btnGuardar)) {
                    // Sincronizar scripts virtuales antes de guardar
                    if (miJugador.hasScript && strncmp(miJugador.scriptPath, "virtual:", 8) == 0) {
                        const char* code = GetScriptBufferContent(miJugador.scriptPath);
                        if (code) { strncpy(miJugador.scriptSource, code, sizeof(miJugador.scriptSource) - 1); miJugador.scriptSource[sizeof(miJugador.scriptSource) - 1] = '\0'; }
                    }
                    if (miMoneda.hasScript && strncmp(miMoneda.scriptPath, "virtual:", 8) == 0) {
                        const char* code = GetScriptBufferContent(miMoneda.scriptPath);
                        if (code) { strncpy(miMoneda.scriptSource, code, sizeof(miMoneda.scriptSource) - 1); miMoneda.scriptSource[sizeof(miMoneda.scriptSource) - 1] = '\0'; }
                    }
                    if (miEnemigo.hasScript && strncmp(miEnemigo.scriptPath, "virtual:", 8) == 0) {
                        const char* code = GetScriptBufferContent(miEnemigo.scriptPath);
                        if (code) { strncpy(miEnemigo.scriptSource, code, sizeof(miEnemigo.scriptSource) - 1); miEnemigo.scriptSource[sizeof(miEnemigo.scriptSource) - 1] = '\0'; }
                    }
                    for (auto& escena : listaEscenas) {
                        for (auto& bloque : escena) {
                            if (bloque.hasScript && strncmp(bloque.scriptPath, "virtual:", 8) == 0) {
                                const char* code = GetScriptBufferContent(bloque.scriptPath);
                                if (code) { strncpy(bloque.scriptSource, code, sizeof(bloque.scriptSource) - 1); bloque.scriptSource[sizeof(bloque.scriptSource) - 1] = '\0'; }
                            }
                        }
                    }

                    char filePath[256] = "proyecto.sav";
                    bool saved = false;
                    if (FileDialog(filePath, sizeof(filePath), true)) saved = SaveProject(listaEscenas, escenaActualIndex, miJugador, miMoneda, miEnemigo, formaActualCreacion, filePath);
                    statusMessage = saved ? "¡Proyecto guardado!" : "Error al guardar";
                    statusMessageTime = GetTime();
                }
                if (CheckCollisionPointRec(mousePos, btnCargar)) {
                    char filePath[256] = "proyecto.sav";
                    bool loaded = false;
                    if (FileDialog(filePath, sizeof(filePath), false)) loaded = LoadProject(listaEscenas, escenaActualIndex, miJugador, miMoneda, miEnemigo, formaActualCreacion, filePath);
                    if (loaded) {
                        nivelEscenario = &listaEscenas[escenaActualIndex];
                        selectedType = OBJ_NONE;
                        bloqueSeleccionadoIndex = -1;
                        activeTextField = -1;
                        arrastrandoBloqueConMouse = false;
                        arrastrandoHandle = false;
                        inspectorScrollY = 0;
                        velocidadY = 0;
                        enElSuelo = false;
                        miCamara.Inicializar(miJugador.posicion);

                        // ---------------------------------------------------
                        // Sincronizar scripts virtuales en el editor
                        // ---------------------------------------------------
                        if (miJugador.hasScript && miJugador.scriptSource[0] != '\0') {
                            RegisterScriptInEditor("virtual:jugador.lua", miJugador.scriptSource);
                        }
                        if (miMoneda.hasScript && miMoneda.scriptSource[0] != '\0') {
                            RegisterScriptInEditor("virtual:moneda.lua", miMoneda.scriptSource);
                        }
                        if (miEnemigo.hasScript && miEnemigo.scriptSource[0] != '\0') {
                            RegisterScriptInEditor("virtual:enemigo.lua", miEnemigo.scriptSource);
                        }
                        for (size_t e = 0; e < listaEscenas.size(); ++e) {
                            for (size_t i = 0; i < listaEscenas[e].size(); ++i) {
                                auto& bloque = listaEscenas[e][i];
                                if (bloque.hasScript && bloque.scriptSource[0] != '\0') {
                                    char name[64];
                                    snprintf(name, sizeof(name), "virtual:bloque_%zu_%zu.lua", e, i);
                                    RegisterScriptInEditor(name, bloque.scriptSource);
                                }
                            }
                        }

                        statusMessage = "¡Proyecto cargado!";
                    }
                    else statusMessage = "Error al cargar";
                    statusMessageTime = GetTime();
                }
                if (CheckCollisionPointRec(mousePos, btnExportar)) {
                    // Sincronizar scripts virtuales antes de exportar
                    if (miJugador.hasScript && strncmp(miJugador.scriptPath, "virtual:", 8) == 0) {
                        const char* code = GetScriptBufferContent(miJugador.scriptPath);
                        if (code) { strncpy(miJugador.scriptSource, code, sizeof(miJugador.scriptSource) - 1); miJugador.scriptSource[sizeof(miJugador.scriptSource) - 1] = '\0'; }
                    }
                    if (miMoneda.hasScript && strncmp(miMoneda.scriptPath, "virtual:", 8) == 0) {
                        const char* code = GetScriptBufferContent(miMoneda.scriptPath);
                        if (code) { strncpy(miMoneda.scriptSource, code, sizeof(miMoneda.scriptSource) - 1); miMoneda.scriptSource[sizeof(miMoneda.scriptSource) - 1] = '\0'; }
                    }
                    if (miEnemigo.hasScript && strncmp(miEnemigo.scriptPath, "virtual:", 8) == 0) {
                        const char* code = GetScriptBufferContent(miEnemigo.scriptPath);
                        if (code) { strncpy(miEnemigo.scriptSource, code, sizeof(miEnemigo.scriptSource) - 1); miEnemigo.scriptSource[sizeof(miEnemigo.scriptSource) - 1] = '\0'; }
                    }
                    for (auto& escena : listaEscenas) {
                        for (auto& bloque : escena) {
                            if (bloque.hasScript && strncmp(bloque.scriptPath, "virtual:", 8) == 0) {
                                const char* code = GetScriptBufferContent(bloque.scriptPath);
                                if (code) { strncpy(bloque.scriptSource, code, sizeof(bloque.scriptSource) - 1); bloque.scriptSource[sizeof(bloque.scriptSource) - 1] = '\0'; }
                            }
                        }
                    }

                    bool exito = ExportToEXE(listaEscenas, escenaActualIndex, miJugador, miMoneda, miEnemigo, formaActualCreacion);
                    if (!exito) {
                        if (!std::filesystem::exists("template/Template.exe")) {
                            statusMessage = "Error: No se encuentra template/Template.exe";
                        }
                        else if (!std::filesystem::exists("proyecto.sav")) {
                            statusMessage = "Error: No se pudo guardar el proyecto";
                        }
                        else {
                            statusMessage = "Error al exportar EXE (copia de archivos falló)";
                        }
                    }
                    else {
                        statusMessage = "¡EXE exportado correctamente!";
                    }
                    statusMessageTime = GetTime();

                }
            }
        }
        // --- Alternar modos ---
        if (clicEnBoton || teclaAlternar) {
            if (currentMode == MODE_EDITOR) {
                currentMode = MODE_PLAY;
                DisableCursor();
                HideCursor();
                miCamara.anguloHorizontal = 0.0f;
                miCamara.anguloVertical = 0.3f;
                LogMessage("[SISTEMA] Cambiando a modo JUEGO.", LIGHTGRAY);
            }
            else {
                currentMode = MODE_EDITOR;
                ShowCursor();
                EnableCursor();
                editorCameraPos = miCamara.camara.position;
                selectedType = OBJ_NONE;
                bloqueSeleccionadoIndex = -1;
                activeTextField = -1;
                arrastrandoBloqueConMouse = false;
                arrastrandoHandle = false;
                arrastrandoPreview3D = false;
                LogMessage("[SISTEMA] Cambiando a modo EDITOR.", LIGHTGRAY);
            }
        }

        auto dibujarObjeto3D = [](const EditorBlock& obj, bool isSelected) {
            // 1. Modelo 3D personalizado (importado)
            if (obj.hasCustomModel && obj.customModel.meshCount > 0) {
                // Calcular la posición de dibujado compensando el pivote
                Vector3 drawPos = Vector3Subtract(obj.position, obj.modelPivotOffset);

                // Escala: queremos que el modelo ocupe obj.size, así que la escala es
                // obj.size / tamaño original (que obtendremos de GetModelBoundingBox)
                // pero podemos aprovechar que ya tenemos modelOriginalSize.
                // De todas formas, si no lo tienes, lo calculamos con el bounding box actual.
                // Usaremos el tamaño real actual (obj.size) y el tamaño original guardado.
                Vector3 scaleFactor = {
                    obj.size.x / obj.modelOriginalSize.x,
                    obj.size.y / obj.modelOriginalSize.y,
                    obj.size.z / obj.modelOriginalSize.z
                };
                if (obj.modelOriginalSize.x < 0.0001f) scaleFactor.x = 1.0f;
                if (obj.modelOriginalSize.y < 0.0001f) scaleFactor.y = 1.0f;
                if (obj.modelOriginalSize.z < 0.0001f) scaleFactor.z = 1.0f;

                DrawModelEx(obj.customModel, drawPos, { 0,0,0 }, 0.0f, scaleFactor, WHITE);

                if (isSelected) {
                    // La caja de selección se calcula con el tamaño actual del bloque
                    BoundingBox bb = {
                        { obj.position.x - obj.size.x / 2.0f, obj.position.y - obj.size.y / 2.0f, obj.position.z - obj.size.z / 2.0f },
                        { obj.position.x + obj.size.x / 2.0f, obj.position.y + obj.size.y / 2.0f, obj.position.z + obj.size.z / 2.0f }
                    };
                    DrawBoundingBox(bb, GOLD);
                }
                return;
            }

            // 2. Cubo con textura
            if (obj.hasTexture && obj.texture.id != 0) {
                Model model = GetCubeTexturedModel(obj.texture);
                Vector3 scale = { obj.size.x, obj.size.y, obj.size.z };
                DrawModelEx(model, obj.position, Vector3{ 0,0,0 }, 0, scale, WHITE);
                if (isSelected) DrawCubeWires(obj.position, obj.size.x + 0.1f, obj.size.y + 0.1f, obj.size.z + 0.1f, GOLD);
                else DrawCubeWires(obj.position, obj.size.x, obj.size.y, obj.size.z, BLACK);
                return;
            }

            // 3. Formas básicas
            switch (obj.shapeType) {
            case SHAPE_CUBE:
                DrawCube(obj.position, obj.size.x, obj.size.y, obj.size.z, obj.color);
                if (isSelected) DrawCubeWires(obj.position, obj.size.x + 0.1f, obj.size.y + 0.1f, obj.size.z + 0.1f, GOLD);
                else DrawCubeWires(obj.position, obj.size.x, obj.size.y, obj.size.z, BLACK);
                break;
            case SHAPE_SPHERE: {
                float radio = obj.size.x * 0.5f;
                DrawSphere(obj.position, radio, obj.color);
                if (isSelected) DrawSphereWires(obj.position, radio + 0.05f, 10, 10, GOLD);
                else DrawSphereWires(obj.position, radio, 10, 10, BLACK);
                break;
            }
            case SHAPE_CYLINDER: {
                float radio = obj.size.x * 0.5f;
                float altura = obj.size.y;
                Vector3 pInf = { obj.position.x, obj.position.y - (altura / 2.0f), obj.position.z };
                Vector3 pSup = { obj.position.x, obj.position.y + (altura / 2.0f), obj.position.z };
                DrawCylinderEx(pInf, pSup, radio, radio, 16, obj.color);
                if (isSelected) DrawCylinderWiresEx(pInf, pSup, radio + 0.05f, radio + 0.05f, 16, GOLD);
                else DrawCylinderWiresEx(pInf, pSup, radio, radio, 16, BLACK);
                break;
            }
            case SHAPE_CONE: {
                float radio = obj.size.x * 0.5f;
                float altura = obj.size.y;
                Vector3 pInf = { obj.position.x, obj.position.y - (altura / 2.0f), obj.position.z };
                Vector3 pSup = { obj.position.x, obj.position.y + (altura / 2.0f), obj.position.z };
                DrawCylinderEx(pInf, pSup, radio, 0.0f, 16, obj.color);
                if (isSelected) DrawCylinderWiresEx(pInf, pSup, radio + 0.05f, 0.05f, 16, GOLD);
                else DrawCylinderWiresEx(pInf, pSup, radio, 0.0f, 16, BLACK);
                break;
            }
            case SHAPE_PORTAL:
                DrawCube(obj.position, obj.size.x, obj.size.y, obj.size.z, Fade(PURPLE, 0.6f));
                if (isSelected) DrawCubeWires(obj.position, obj.size.x + 0.1f, obj.size.y + 0.1f, obj.size.z + 0.1f, GOLD);
                else DrawCubeWires(obj.position, obj.size.x, obj.size.y, obj.size.z, MAGENTA);
                break;
            default: break;
            }
        };

        // --- Lógica de juego (PLAY) ---
        if (currentMode == MODE_PLAY) {
            // Movimiento del jugador
            if (IsKeyDown(KEY_LEFT_SHIFT)) miJugador.velocidadActual = miJugador.velocidadBase * 2.5f;
            else miJugador.velocidadActual = miJugador.velocidadBase;

            Vector3 adelante = { sinf(miCamara.anguloHorizontal), 0.0f, cosf(miCamara.anguloHorizontal) };
            Vector3 derecha = { cosf(miCamara.anguloHorizontal), 0.0f, -sinf(miCamara.anguloHorizontal) };
            Vector3 movimientoDeseado = { 0.0f, 0.0f, 0.0f };

            // =============== Control por script si el jugador lo tiene ===============
            if (!miJugador.hasScript || miJugador.scriptSource[0] == '\0') {
                if (IsKeyDown(KEY_W)) movimientoDeseado = Vector3Subtract(movimientoDeseado, adelante);
                if (IsKeyDown(KEY_S)) movimientoDeseado = Vector3Add(movimientoDeseado, adelante);
                if (IsKeyDown(KEY_A)) movimientoDeseado = Vector3Subtract(movimientoDeseado, derecha);
                if (IsKeyDown(KEY_D)) movimientoDeseado = Vector3Add(movimientoDeseado, derecha);
            }
            else {
                // Ejecutar el script Lua asociado al jugador
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
                // 1. Preferir siempre el código fuente incrustado
                if (miJugador.scriptSource[0] != '\0') {
                    status = luaL_dostring(scriptL, miJugador.scriptSource);
                }
                // 2. Si la ruta es virtual, intentar obtener el contenido del buffer (para proyectos viejos)
                else if (strncmp(miJugador.scriptPath, "virtual:", 8) == 0) {
                    const char* code = GetScriptBufferContent(miJugador.scriptPath);
                    if (code) {
                        status = luaL_dostring(scriptL, code);
                    }
                    else {
                        status = LUA_ERRERR;
                    }
                }
                // 3. Si la ruta es un archivo real, cargarlo
                else if (miJugador.scriptPath[0] != '\0') {
                    status = luaL_dofile(scriptL, miJugador.scriptPath);
                }
                // 4. Sin ruta y sin código, no hacer nada
                else {
                    status = LUA_OK;
                }

                if (status == LUA_OK) {
                    lua_getglobal(scriptL, "moveX");
                    if (lua_isnumber(scriptL, -1)) movimientoDeseado.x = (float)lua_tonumber(scriptL, -1);
                    lua_pop(scriptL, 1);
                    lua_getglobal(scriptL, "moveY");
                    if (lua_isnumber(scriptL, -1)) movimientoDeseado.y = (float)lua_tonumber(scriptL, -1);
                    lua_pop(scriptL, 1);
                    lua_getglobal(scriptL, "moveZ");
                    if (lua_isnumber(scriptL, -1)) movimientoDeseado.z = (float)lua_tonumber(scriptL, -1);
                    lua_pop(scriptL, 1);
                    lua_getglobal(scriptL, "jump");
                    if (lua_isboolean(scriptL, -1) && lua_toboolean(scriptL, -1) && enElSuelo) {
                        velocidadY = fuerzaSalto;
                        enElSuelo = false;
                    }
                    lua_pop(scriptL, 1);
                }
                else {
                    const char* err = lua_tostring(scriptL, -1);
                    LogMessage(std::string("[SCRIPT ERROR] ") + err, RED);
                    lua_pop(scriptL, 1);
                }
                lua_close(scriptL);
            }
            // =============================================================================

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
            int destinoPortal = -1;
            // Primero, detectar si hay colisión con un portal sin modificar nada
            for (const auto& bloque : *nivelEscenario) {
                BoundingBox boxBloque = getBlockBox(bloque);
                if (CheckCollisionBoxes(boxJugador, boxBloque)) {
                    if (bloque.shapeType == SHAPE_PORTAL) {
                        destinoPortal = bloque.targetScene;
                        teletransportado = true;
                        break;
                    }
                }
            }

            // Procesar teletransporte fuera del bucle
            if (teletransportado) {
                if (destinoPortal < 0) destinoPortal = 0;
                if (destinoPortal >= (int)listaEscenas.size()) listaEscenas.resize(destinoPortal + 1);
                escenaActualIndex = destinoPortal;
                nivelEscenario = &listaEscenas[escenaActualIndex];
                // Reiniciar selección para evitar índices inválidos
                selectedType = OBJ_NONE;
                bloqueSeleccionadoIndex = -1;
                miJugador.posicion = Vector3{ 0.0f, 5.0f, 0.0f };
                velocidadY = 0.0f;
                // Saltamos el resto del movimiento
            }
            else {
                // Guardar posiciones anteriores de los bloques y detectar arrastre
                std::unordered_map<int, Vector3> currentBlockPositions;
                for (int i = 0; i < (int)nivelEscenario->size(); i++) {
                    currentBlockPositions[i] = (*nivelEscenario)[i].position;
                }

                // Si hay una posición anterior, verificar si el jugador está sobre algún bloque y arrastrarlo
                if (!prevBlockPositions.empty()) {
                    // Obtener la caja del jugador
                    BoundingBox boxJugador = getPlayerBox(miJugador);

                    for (int i = 0; i < (int)nivelEscenario->size(); i++) {
                        const auto& bloque = (*nivelEscenario)[i];
                        if (bloque.shapeType == SHAPE_PORTAL) continue;

                        BoundingBox boxBloque = getBlockBox(bloque);

                        // Verificar si el jugador está encima del bloque (colisión con el bloque, y el jugador está arriba)
                        if (CheckCollisionBoxes(boxJugador, boxBloque)) {
                            // Verificar si el bloque se movió desde el frame anterior
                            auto it = prevBlockPositions.find(i);
                            if (it != prevBlockPositions.end()) {
                                Vector3 prevPos = it->second;
                                Vector3 delta = Vector3Subtract(bloque.position, prevPos);

                                // Si el bloque se movió horizontalmente, arrastrar al jugador
                                if (Vector3Length(delta) > 0.001f) {
                                    // Verificar que el jugador esté realmente parado sobre el bloque (tope del bloque)
                                    float topeBloque = bloque.position.y + (bloque.size.y / 2.0f);
                                    float fondoJugador = miJugador.posicion.y - (miJugador.alturaTotal / 2.0f);

                                    // Si el jugador está justo encima del bloque (con un margen pequeño)
                                    if (fabs(fondoJugador - topeBloque) < 0.3f && velocidadY >= 0) {
                                        // Aplicar el desplazamiento al jugador (solo en X y Z)
                                        miJugador.posicion.x += delta.x;
                                        miJugador.posicion.z += delta.z;

                                        // Recalcular la caja del jugador después del arrastre
                                        boxJugador = getPlayerBox(miJugador);
                                    }
                                }
                            }
                        }
                    }
                }

                // Después de procesar, actualizar las posiciones anteriores para el próximo frame
                prevBlockPositions = currentBlockPositions;

                // Solo si no hubo teletransporte, procesamos el movimiento normal
                if (Vector3Length(movimientoDeseado) > 0) {
                    movimientoDeseado = Vector3Normalize(movimientoDeseado);
                    miJugador.posicion.x += movimientoDeseado.x * miJugador.velocidadActual;
                    {
                        BoundingBox boxJ = getPlayerBox(miJugador);
                        for (const auto& bloque : *nivelEscenario) {
                            if (bloque.shapeType == SHAPE_CONE || bloque.shapeType == SHAPE_PORTAL) continue;
                            BoundingBox boxBloque = getBlockBox(bloque);
                            if (CheckCollisionBoxes(boxJ, boxBloque)) {
                                if (boxBloque.max.y > boxJ.min.y + 0.05f) {
                                    miJugador.posicion.x -= movimientoDeseado.x * miJugador.velocidadActual;
                                    break;
                                }
                            }
                        }
                    }
                    miJugador.posicion.z += movimientoDeseado.z * miJugador.velocidadActual;
                    {
                        BoundingBox boxJ = getPlayerBox(miJugador);
                        for (const auto& bloque : *nivelEscenario) {
                            if (bloque.shapeType == SHAPE_CONE || bloque.shapeType == SHAPE_PORTAL) continue;
                            BoundingBox boxBloque = getBlockBox(bloque);
                            if (CheckCollisionBoxes(boxJ, boxBloque)) {
                                if (boxBloque.max.y > boxJ.min.y + 0.05f) {
                                    miJugador.posicion.z -= movimientoDeseado.z * miJugador.velocidadActual;
                                    break;
                                }
                            }
                        }
                    }
                }

                // Permitir salto manual incluso si hasScript es true pero el script está vacío
                if (IsKeyPressed(KEY_SPACE) && enElSuelo && (!miJugador.hasScript || miJugador.scriptSource[0] == '\0')) {
                    velocidadY = fuerzaSalto;
                    enElSuelo = false;
                    LogMessage("[PLAYER] Jugador ha realizado un salto.", WHITE);
                }
                velocidadY += gravedad;
                miJugador.posicion.y += velocidadY;
                enElSuelo = false;

                {
                    BoundingBox boxJ = getPlayerBox(miJugador);
                    for (const auto& bloque : *nivelEscenario) {
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
                miJugador.posicion = Vector3{ 0.0f, 5.0f, 0.0f };
                velocidadY = 0.0f;
                LogMessage("[GAME] Jugador cayó al vacío. Reapareciendo en el origen...", RED);
            }

            // ---- Cámara en modo juego ----
            Vector2 mouseDelta = GetMouseDelta();
            float sensibilidad = 0.002f;
            miCamara.anguloHorizontal -= mouseDelta.x * sensibilidad;
            miCamara.anguloVertical += mouseDelta.y * sensibilidad;
            if (miCamara.anguloVertical > 1.55f) miCamara.anguloVertical = 1.55f;
            if (miCamara.anguloVertical < -1.55f) miCamara.anguloVertical = -1.55f;

            float wheel = GetMouseWheelMove();
            if (wheel != 0.0f) {
                camDistancia -= wheel * 2.0f;
                if (camDistancia < 5.0f) camDistancia = 5.0f;
                if (camDistancia > 30.0f) camDistancia = 30.0f;
            }

            Vector3 offset = {
                camDistancia * cosf(miCamara.anguloVertical) * sinf(miCamara.anguloHorizontal),
                camDistancia * sinf(miCamara.anguloVertical),
                camDistancia * cosf(miCamara.anguloVertical) * cosf(miCamara.anguloHorizontal)
            };
            miCamara.camara.position = Vector3Add(miJugador.posicion, offset);
            miCamara.camara.target = miJugador.posicion;
            miCamara.camara.up = Vector3{ 0.0f, 1.0f, 0.0f };

            // ============== Ejecutar scripts de los bloques ==============
            for (size_t i = 0; i < nivelEscenario->size(); i++) {
                EditorBlock& bloque = (*nivelEscenario)[i];
                if (!bloque.hasScript || bloque.scriptSource[0] == '\0') continue;

                lua_State* L = luaL_newstate();
                luaL_openlibs(L);
                RegisterScriptingLuaFunctions(L);

                // Pasar variables globales útiles para el bloque
                lua_pushnumber(L, bloque.position.x); lua_setglobal(L, "selfX");
                lua_pushnumber(L, bloque.position.y); lua_setglobal(L, "selfY");
                lua_pushnumber(L, bloque.position.z); lua_setglobal(L, "selfZ");
                lua_pushinteger(L, (int)i + 1);       lua_setglobal(L, "selfId");   // ID para usar con MoveBlock etc.
                lua_pushnumber(L, GetFrameTime());    lua_setglobal(L, "dt");

                int status;
                if (bloque.scriptSource[0] != '\0') {
                    status = luaL_dostring(L, bloque.scriptSource);
                }
                else if (strncmp(bloque.scriptPath, "virtual:", 8) == 0) {
                    const char* code = GetScriptBufferContent(bloque.scriptPath);
                    if (code) status = luaL_dostring(L, code);
                    else      status = LUA_ERRERR;
                }
                else if (bloque.scriptPath[0] != '\0') {
                    status = luaL_dofile(L, bloque.scriptPath);
                }
                else {
                    status = LUA_OK;
                }

                if (status != LUA_OK) {
                    const char* err = lua_tostring(L, -1);
                    LogMessage(("[SCRIPT BLOQUE " + std::to_string(i) + " ERROR] ") + err, RED);
                    lua_pop(L, 1);
                }

                lua_close(L);
            }
            // ============================================================

            SetMousePosition(GetScreenWidth() / 2, GetScreenHeight() / 2);

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
                        LogMessage("[DANGER] ¡Colisión detectada con un enemigo!", ORANGE);
                    }
                }
                if (miMoneda.activa && CheckCollisionBoxSphere(getPlayerBox(miJugador), miMoneda.posicion, miMoneda.radio)) {
                    miJugador.puntos += 10;
                    miMoneda.Reaparecer();
                    LogMessage("[EVENT] ¡Moneda recolectada con éxito!", GREEN);
                }
            }
        }
        else {
            // --- MODO EDITOR ---
            // Cámara libre (bloqueada si el editor de scripts está enfocado)
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !mouseOverUI && !scriptEditorFocused) {
                Vector2 deltaMouse = GetMouseDelta();
                editorYaw -= deltaMouse.x * 0.005f;
                editorPitch -= deltaMouse.y * 0.005f;
                if (editorPitch > 1.4f) editorPitch = 1.4f;
                if (editorPitch < -1.4f) editorPitch = -1.4f;
            }
            Vector3 camAdelante = {
                cosf(editorPitch) * sinf(editorYaw),
                sinf(editorPitch),
                cosf(editorPitch) * cosf(editorYaw)
            };
            Vector3 camDerecha = { -cosf(editorYaw), 0.0f, sinf(editorYaw) };
            float velocidadCamaraLibre = 0.15f;
            if (IsKeyDown(KEY_LEFT_SHIFT)) velocidadCamaraLibre = 0.35f;
            if (IsKeyDown(KEY_W) && !scriptEditorFocused) editorCameraPos = Vector3Add(editorCameraPos, Vector3Scale(camAdelante, velocidadCamaraLibre));
            if (IsKeyDown(KEY_S) && !scriptEditorFocused) editorCameraPos = Vector3Subtract(editorCameraPos, Vector3Scale(camAdelante, velocidadCamaraLibre));
            if (IsKeyDown(KEY_A) && !scriptEditorFocused) editorCameraPos = Vector3Subtract(editorCameraPos, Vector3Scale(camDerecha, velocidadCamaraLibre));
            if (IsKeyDown(KEY_D) && !scriptEditorFocused) editorCameraPos = Vector3Add(editorCameraPos, Vector3Scale(camDerecha, velocidadCamaraLibre));
            if (IsKeyDown(KEY_SPACE) && !scriptEditorFocused) editorCameraPos.y += velocidadCamaraLibre;
            if (IsKeyDown(KEY_LEFT_CONTROL) && !scriptEditorFocused) editorCameraPos.y -= velocidadCamaraLibre;
            miCamara.camara.position = editorCameraPos;
            miCamara.camara.target = Vector3Add(editorCameraPos, camAdelante);
            miCamara.camara.up = Vector3{ 0.0f, 1.0f, 0.0f };

            // Atajos (bloqueados si el editor está enfocado)
            if (IsKeyPressed(KEY_C) && !scriptEditorFocused) {
                indiceColorActual = (indiceColorActual + 1) % 6;
                if (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex != -1)
                    (*nivelEscenario)[bloqueSeleccionadoIndex].color = inspectorColors[indiceColorActual];
                else if (selectedType == OBJ_PLAYER)
                    miJugador.color = inspectorColors[indiceColorActual];
            }
            if (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex != -1 && (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) && activeTextField == -1 && !scriptEditorFocused) {
                if ((*nivelEscenario)[bloqueSeleccionadoIndex].hasTexture && (*nivelEscenario)[bloqueSeleccionadoIndex].texture.id != 0)
                    UnloadTexture((*nivelEscenario)[bloqueSeleccionadoIndex].texture);
                nivelEscenario->erase(nivelEscenario->begin() + bloqueSeleccionadoIndex);
                selectedType = OBJ_NONE;
                bloqueSeleccionadoIndex = -1;
                arrastrandoBloqueConMouse = false;
                arrastrandoHandle = false;
                arrastrandoPreview3D = false;
            }
            if (selectedType != OBJ_NONE && IsKeyPressed(KEY_ESCAPE) && !scriptEditorFocused) {
                if (activeTextField != -1) activeTextField = -1;
                else { selectedType = OBJ_NONE; bloqueSeleccionadoIndex = -1; arrastrandoBloqueConMouse = false; arrastrandoHandle = false; arrastrandoPreview3D = false; }
            }
            if (selectedType != OBJ_NONE) {
                int currentSelectionKey = (selectedType == OBJ_BLOCK) ? bloqueSeleccionadoIndex : (int)selectedType;
                if (currentSelectionKey != ultimoBloqueSeleccionado) {
                    inspectorScrollY = 0.0f;
                    ultimoBloqueSeleccionado = currentSelectionKey;
                    Color cCheck = WHITE;
                    if (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex != -1) cCheck = (*nivelEscenario)[bloqueSeleccionadoIndex].color;
                    else if (selectedType == OBJ_PLAYER) cCheck = miJugador.color;
                    Vector3 hsv = ColorToHSV(cCheck);
                    currentHue = hsv.x;
                    currentSat = hsv.y;
                    currentVal = hsv.z;
                }
            }

            // Inspector
            if (CheckCollisionPointRec(mousePos, inspectorRect)) {
                float wheel = GetMouseWheelMove();
                if (wheel != 0.0f) inspectorScrollY -= wheel * 30.0f;
            }

            float previewBoxHeight = 130.0f * scaleFactor;
            float pickerHeight = 130.0f * scaleFactor;
            int numRows = getSelectedNumRows();
            float contenidoEstimadoAlto = (28.0f * scaleFactor) + previewBoxHeight + (16.0f * scaleFactor) + (24.0f * scaleFactor);
            if (transformSectionOpen) contenidoEstimadoAlto += (numRows * rowH) + (10.0f * scaleFactor);
            contenidoEstimadoAlto += (24.0f * scaleFactor);
            bool hasColor = (selectedType == OBJ_BLOCK || selectedType == OBJ_PLAYER);
            if (hasColor && colorSectionOpen) {
                float swatchTotalH = 3 * (22.0f * scaleFactor + 5.0f * scaleFactor);
                contenidoEstimadoAlto += (10.0f * scaleFactor) + swatchTotalH + (24.0f * scaleFactor) + pickerHeight + (24.0f * scaleFactor) + (3 * rowH);
            }
            // Añadir espacio extra para los botones de textura/script (siempre visibles)
            contenidoEstimadoAlto += 80.0f * scaleFactor;
            float altoVisiblePanel = panelAlto - (28.0f * scaleFactor);
            float maxScroll = fmaxf(0.0f, contenidoEstimadoAlto - altoVisiblePanel);
            if (inspectorScrollY < 0.0f) inspectorScrollY = 0.0f;
            if (inspectorScrollY > maxScroll) inspectorScrollY = maxScroll;

            float currentY = panely + 34.0f * scaleFactor - inspectorScrollY;
            Rectangle previewBox = { panelX + (12.0f * scaleFactor), currentY, panelAncho - (24.0f * scaleFactor), previewBoxHeight };
            currentY += previewBox.height + (12.0f * scaleFactor);

            transformHeaderY = currentY;
            Rectangle transformHeaderRect = { panelX + (12.0f * scaleFactor), transformHeaderY, panelAncho - (24.0f * scaleFactor), 22.0f * scaleFactor };
            currentY += 24.0f * scaleFactor;
            float transformRowsStartY = currentY;
            if (transformSectionOpen) currentY += numRows * rowH + (10.0f * scaleFactor);

            colorHeaderY = currentY;
            Rectangle colorHeaderRect = { panelX + (12.0f * scaleFactor), colorHeaderY, panelAncho - (24.0f * scaleFactor), 22.0f * scaleFactor };
            currentY += 24.0f * scaleFactor;
            if (hasColor && colorSectionOpen) {
                swatchSize = 22.0f * scaleFactor;
                swatchSpacing = 5.0f * scaleFactor;
                swatchStartX = panelX + (14.0f * scaleFactor);
                swatchStartY = currentY;
                float swatchTotalH = 3 * (swatchSize + swatchSpacing);
                currentY += swatchTotalH + (12.0f * scaleFactor);
            }
            pickerHeaderY = currentY;
            currentY += 24.0f * scaleFactor;
            pickerStartY = currentY;
            currentY += pickerHeight + (12.0f * scaleFactor);
            rgbHeaderY = currentY;
            currentY += 24.0f * scaleFactor;
            rgbRowsStartY = currentY;

            // Interacciones del inspector
            if (selectedType != OBJ_NONE) {
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mousePos, previewBox))
                    arrastrandoPreview3D = true;
            }
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                arrastrandoPreview3D = false;
                arrastrandoSV = false;
                arrastrandoHue = false;
            }
            if (arrastrandoPreview3D) {
                Vector2 deltaMouse = GetMouseDelta();
                previewYaw -= deltaMouse.x * 0.015f;
                previewPitch += deltaMouse.y * 0.015f;
                if (previewPitch > 1.4f) previewPitch = 1.4f;
                if (previewPitch < -1.4f) previewPitch = -1.4f;
            }

            float pickerW = 100.0f * scaleFactor;
            float pickerH = 120.0f * scaleFactor;
            float pickerX = panelX + (14.0f * scaleFactor);
            float hueBarW = 20.0f * scaleFactor;
            float hueBarX = pickerX + pickerW + (10.0f * scaleFactor);
            Rectangle rectSVBox = { pickerX, pickerStartY, pickerW, pickerH };
            Rectangle rectHueBar = { hueBarX, pickerStartY, hueBarW, pickerH };

            if (selectedType != OBJ_NONE && CheckCollisionPointRec(mousePos, inspectorRect)) {
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    if (CheckCollisionPointRec(mousePos, transformHeaderRect)) {
                        transformSectionOpen = !transformSectionOpen;
                        activeTextField = -1;
                    }
                    else if (hasColor && CheckCollisionPointRec(mousePos, colorHeaderRect)) {
                        colorSectionOpen = !colorSectionOpen;
                        activeTextField = -1;
                    }
                    else if (hasColor && colorSectionOpen) {
                        if (CheckCollisionPointRec(mousePos, rectSVBox)) {
                            arrastrandoSV = true;
                            activeTextField = -1;
                        }
                        else if (CheckCollisionPointRec(mousePos, rectHueBar)) {
                            arrastrandoHue = true;
                        }
                    }
                }
            }

            if (arrastrandoSV && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                currentSat = (mousePos.x - rectSVBox.x) / rectSVBox.width;
                currentVal = 1.0f - ((mousePos.y - rectSVBox.y) / rectSVBox.height);
                if (currentSat < 0.0f) currentSat = 0.0f; if (currentSat > 1.0f) currentSat = 1.0f;
                if (currentVal < 0.0f) currentVal = 0.0f; if (currentVal > 1.0f) currentVal = 1.0f;
                Color newCol = ColorFromHSV(currentHue, currentSat, currentVal);
                if (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex != -1) (*nivelEscenario)[bloqueSeleccionadoIndex].color = newCol;
                else if (selectedType == OBJ_PLAYER) miJugador.color = newCol;
            }
            if (arrastrandoHue && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                float relY = (mousePos.y - rectHueBar.y) / rectHueBar.height;
                if (relY < 0.0f) relY = 0.0f; if (relY > 1.0f) relY = 1.0f;
                currentHue = (1.0f - relY) * 360.0f;
                Color newCol = ColorFromHSV(currentHue, currentSat, currentVal);
                if (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex != -1) (*nivelEscenario)[bloqueSeleccionadoIndex].color = newCol;
                else if (selectedType == OBJ_PLAYER) miJugador.color = newCol;
            }

            // Handles de escalado
            BoundingBox boxXHandle = { {0}, {0} };
            BoundingBox boxYHandle = { {0}, {0} };
            BoundingBox boxZHandle = { {0}, {0} };
            Vector3 posH_X = { 0 }, posH_Y = { 0 }, posH_Z = { 0 };
            if (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex != -1 && bloqueSeleccionadoIndex < (int)nivelEscenario->size()) {
                EditorBlock& selBlock = (*nivelEscenario)[bloqueSeleccionadoIndex];
                posH_X = { selBlock.position.x + selBlock.size.x / 2.0f, selBlock.position.y, selBlock.position.z };
                posH_Y = { selBlock.position.x, selBlock.position.y + selBlock.size.y / 2.0f, selBlock.position.z };
                posH_Z = { selBlock.position.x, selBlock.position.y, selBlock.position.z + selBlock.size.z / 2.0f };
                float hs = 0.35f;
                boxXHandle = { {posH_X.x - hs, posH_X.y - hs, posH_X.z - hs}, {posH_X.x + hs, posH_X.y + hs, posH_X.z + hs} };
                boxYHandle = { {posH_Y.x - hs, posH_Y.y - hs, posH_Y.z - hs}, {posH_Y.x + hs, posH_Y.y + hs, posH_Y.z + hs} };
                boxZHandle = { {posH_Z.x - hs, posH_Z.y - hs, posH_Z.z - hs}, {posH_Z.x + hs, posH_Z.y + hs, posH_Z.z + hs} };
            }

            // Manejo de campos de texto y scrubbing
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && selectedType != OBJ_NONE && CheckCollisionPointRec(mousePos, inspectorRect)) {
                int cols = 4;
                bool clicEnPaleta = false;
                if (hasColor && colorSectionOpen) {
                    for (int c = 0; c < numInspectorColors; c++) {
                        int colIdx = c % cols;
                        int rowIdx = c / cols;
                        float rx = swatchStartX + colIdx * (swatchSize + swatchSpacing);
                        float ry = swatchStartY + rowIdx * (swatchSize + swatchSpacing);
                        Rectangle swatchRect = { rx, ry, swatchSize, swatchSize };
                        if (CheckCollisionPointRec(mousePos, swatchRect)) {
                            Color chosenCol = inspectorColors[c];
                            if (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex != -1) (*nivelEscenario)[bloqueSeleccionadoIndex].color = chosenCol;
                            else if (selectedType == OBJ_PLAYER) miJugador.color = chosenCol;
                            Vector3 hsv = ColorToHSV(chosenCol);
                            currentHue = hsv.x; currentSat = hsv.y; currentVal = hsv.z;
                            clicEnPaleta = true;
                            break;
                        }
                    }
                }

                if (!clicEnPaleta && !CheckCollisionPointRec(mousePos, previewBox) &&
                    !CheckCollisionPointRec(mousePos, transformHeaderRect) &&
                    !CheckCollisionPointRec(mousePos, colorHeaderRect) &&
                    (!hasColor || !colorSectionOpen || (!CheckCollisionPointRec(mousePos, rectSVBox) && !CheckCollisionPointRec(mousePos, rectHueBar)))) {

                    float boxW = 85.0f * scaleFactor;
                    float boxH = 20.0f * scaleFactor;
                    float boxX = panelX + panelAncho - boxW - (12.0f * scaleFactor);
                    potentialScrubBox = -1;

                    if (transformSectionOpen) {
                        for (int r = 0; r < numRows; r++) {
                            float currentYRow = transformRowsStartY + (r * rowH);
                            Rectangle valBoxRect = { boxX, currentYRow, boxW, boxH };
                            if (CheckCollisionPointRec(mousePos, valBoxRect)) {
                                potentialScrubBox = r;
                                break;
                            }
                        }
                    }

                    int rgbStartIndex = (selectedType == OBJ_PLAYER) ? 5 : (selectedType == OBJ_BLOCK ? 6 : -1);
                    if (potentialScrubBox == -1 && hasColor && colorSectionOpen && rgbStartIndex != -1) {
                        for (int r = rgbStartIndex; r <= rgbStartIndex + 2; r++) {
                            int localR = r - rgbStartIndex;
                            float currentYRow = rgbRowsStartY + (localR * rowH);
                            Rectangle valBoxRect = { boxX, currentYRow, boxW, boxH };
                            if (CheckCollisionPointRec(mousePos, valBoxRect)) {
                                potentialScrubBox = r;
                                break;
                            }
                        }
                    }

                    if (potentialScrubBox != -1) {
                        if (activeTextField != -1 && activeTextField != potentialScrubBox) {
                            float val = (float)atof(textInputBuffer);
                            setObjectVal(activeTextField, val);
                        }
                        isScrubbing = false;
                        scrubStartMouse = mousePos;
                        scrubInitialValue = getObjectVal(potentialScrubBox);
                    }
                }
            }
            else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && selectedType != OBJ_NONE && !CheckCollisionPointRec(mousePos, inspectorRect)) {
                if (activeTextField != -1) {
                    float val = (float)atof(textInputBuffer);
                    setObjectVal(activeTextField, val);
                    activeTextField = -1;
                }
            }

            if (potentialScrubBox != -1 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                Vector2 deltaMouse = Vector2Subtract(mousePos, scrubStartMouse);
                if (!isScrubbing && Vector2Length(deltaMouse) > 4.0f) {
                    isScrubbing = true;
                    activeTextField = -1;
                }
                if (isScrubbing) {
                    int rgbStartIndexCheck = (selectedType == OBJ_PLAYER) ? 5 : 6;
                    float sensitivity = 0.05f;
                    if (potentialScrubBox >= rgbStartIndexCheck && selectedType != OBJ_COIN) sensitivity = 0.5f;
                    if (IsKeyDown(KEY_LEFT_SHIFT)) sensitivity *= 0.1f;
                    float newVal = scrubInitialValue + (deltaMouse.x * sensitivity);
                    setObjectVal(potentialScrubBox, newVal);
                    int rgbStartCheck = (selectedType == OBJ_PLAYER) ? 5 : 6;
                    if (potentialScrubBox >= rgbStartCheck && selectedType != OBJ_COIN) {
                        snprintf(textInputBuffer, sizeof(textInputBuffer), "%.0f", newVal);
                        Color cCheck = (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex != -1) ? (*nivelEscenario)[bloqueSeleccionadoIndex].color : miJugador.color;
                        Vector3 hsv = ColorToHSV(cCheck);
                        currentHue = hsv.x; currentSat = hsv.y; currentVal = hsv.z;
                    }
                    else {
                        snprintf(textInputBuffer, sizeof(textInputBuffer), "%.1f", newVal);
                    }
                }
            }

            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                if (potentialScrubBox != -1 && !isScrubbing && selectedType != OBJ_NONE) {
                    activeTextField = potentialScrubBox;
                    int rgbStartCheck = (selectedType == OBJ_PLAYER) ? 5 : 6;
                    if (activeTextField >= rgbStartCheck && selectedType != OBJ_COIN) {
                        snprintf(textInputBuffer, sizeof(textInputBuffer), "%.0f", getObjectVal(activeTextField));
                    }
                    else {
                        snprintf(textInputBuffer, sizeof(textInputBuffer), "%.1f", getObjectVal(activeTextField));
                    }
                }
                isScrubbing = false;
                potentialScrubBox = -1;
            }

            if (activeTextField != -1 && selectedType != OBJ_NONE && !isScrubbing) {
                int key = GetCharPressed();
                while (key > 0) {
                    bool permitir = false;
                    if ((key >= '0' && key <= '9') || key == '.') permitir = true;
                    else if (key == '-' && activeTextField < 3) permitir = true;
                    if (permitir) {
                        int len = (int)strlen(textInputBuffer);
                        if (len < (int)sizeof(textInputBuffer) - 1) {
                            textInputBuffer[len] = (char)key;
                            textInputBuffer[len + 1] = '\0';
                        }
                    }
                    key = GetCharPressed();
                }
                if (IsKeyPressed(KEY_BACKSPACE)) {
                    int len = (int)strlen(textInputBuffer);
                    if (len > 0) textInputBuffer[len - 1] = '\0';
                }
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                    float val = (float)atof(textInputBuffer);
                    setObjectVal(activeTextField, val);
                    int rgbStartCheck = (selectedType == OBJ_PLAYER) ? 5 : 6;
                    if (activeTextField >= rgbStartCheck && selectedType != OBJ_COIN) {
                        Color cCheck = (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex != -1) ? (*nivelEscenario)[bloqueSeleccionadoIndex].color : miJugador.color;
                        Vector3 hsv = ColorToHSV(cCheck);
                        currentHue = hsv.x; currentSat = hsv.y; currentVal = hsv.z;
                    }
                }
            }
            if (IsKeyPressed(KEY_ESCAPE)) activeTextField = -1;

            // -----------------------------------------------------------------
            // SELECCIÓN Y CREACIÓN DE OBJETOS (SOLO DENTRO DEL VIEWPORT)
            // -----------------------------------------------------------------

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !mouseOverUI && !showContextMenu && !showNodeTypeModal &&
                !resizingLeft && !resizingRight && !resizingTop && !resizingBottom && !resizingConsole &&
                CheckCollisionPointRec(mousePos, viewportRect) && !IsMouseOverScriptEditor() && !scriptEditorFocused &&
                !showScriptView) {

                Ray ray = GetMouseRay(mousePos, miCamara.camara);

                // --- DETECCIÓN DE HANDLES DE ESCALADO ---
                bool handleActivado = false;
                if (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex != -1 && bloqueSeleccionadoIndex < (int)nivelEscenario->size()) {
                    EditorBlock& selBlock = (*nivelEscenario)[bloqueSeleccionadoIndex];
                    Vector3 posH_X = { selBlock.position.x + selBlock.size.x / 2.0f, selBlock.position.y, selBlock.position.z };
                    Vector3 posH_Y = { selBlock.position.x, selBlock.position.y + selBlock.size.y / 2.0f, selBlock.position.z };
                    Vector3 posH_Z = { selBlock.position.x, selBlock.position.y, selBlock.position.z + selBlock.size.z / 2.0f };
                    float hs = 0.35f;
                    BoundingBox boxXHandle = { {posH_X.x - hs, posH_X.y - hs, posH_X.z - hs}, {posH_X.x + hs, posH_X.y + hs, posH_X.z + hs} };
                    BoundingBox boxYHandle = { {posH_Y.x - hs, posH_Y.y - hs, posH_Y.z - hs}, {posH_Y.x + hs, posH_Y.y + hs, posH_Y.z + hs} };
                    BoundingBox boxZHandle = { {posH_Z.x - hs, posH_Z.y - hs, posH_Z.z - hs}, {posH_Z.x + hs, posH_Z.y + hs, posH_Z.z + hs} };

                    RayCollision colX = GetRayCollisionBox(ray, boxXHandle);
                    RayCollision colY = GetRayCollisionBox(ray, boxYHandle);
                    RayCollision colZ = GetRayCollisionBox(ray, boxZHandle);

                    if (colX.hit) {
                        handleSeleccionado = 0;
                        arrastrandoHandle = true;
                        mouseInicioArrastre = mousePos;
                        tamanoInicialBloque = selBlock.size;
                        handleActivado = true;
                    }
                    else if (colY.hit) {
                        handleSeleccionado = 1;
                        arrastrandoHandle = true;
                        mouseInicioArrastre = mousePos;
                        tamanoInicialBloque = selBlock.size;
                        handleActivado = true;
                    }
                    else if (colZ.hit) {
                        handleSeleccionado = 2;
                        arrastrandoHandle = true;
                        mouseInicioArrastre = mousePos;
                        tamanoInicialBloque = selBlock.size;
                        handleActivado = true;
                    }
                }

                // Si se activó un handle, salir sin seleccionar/crear objetos
                if (!handleActivado) {
                    // --- Resto del código de selección/creación (todo lo que ya tenías) ---
                    SelectedObjectType encontradoTipo = OBJ_NONE;
                    int encontradoIndex = -1;
                    float distanciaMinima = 10000.0f;

                    // Player
                    BoundingBox playerBox = {
                        Vector3{ miJugador.posicion.x - miJugador.radio, miJugador.posicion.y - miJugador.alturaTotal / 2.0f, miJugador.posicion.z - miJugador.radio },
                        Vector3{ miJugador.posicion.x + miJugador.radio, miJugador.posicion.y + miJugador.alturaTotal / 2.0f, miJugador.posicion.z + miJugador.radio }
                    };
                    RayCollision colPlayer = GetRayCollisionBox(ray, playerBox);
                    if (colPlayer.hit && colPlayer.distance < distanciaMinima) {
                        distanciaMinima = colPlayer.distance;
                        encontradoTipo = OBJ_PLAYER;
                        encontradoIndex = -1;
                    }

                    // Moneda
                    if (tieneObjetosEspeciales && miMoneda.activa) {
                        RayCollision colCoin = GetRayCollisionSphere(ray, miMoneda.posicion, miMoneda.radio);
                        if (colCoin.hit && colCoin.distance < distanciaMinima) {
                            distanciaMinima = colCoin.distance;
                            encontradoTipo = OBJ_COIN;
                            encontradoIndex = -1;
                        }
                    }

                    // Enemigo
                    if (tieneObjetosEspeciales) {
                        BoundingBox enemyBox = {
                            Vector3{ miEnemigo.posicion.x - miEnemigo.tamano.x / 2.0f, miEnemigo.posicion.y - miEnemigo.tamano.y / 2.0f, miEnemigo.posicion.z - miEnemigo.tamano.z / 2.0f },
                            Vector3{ miEnemigo.posicion.x + miEnemigo.tamano.x / 2.0f, miEnemigo.posicion.y + miEnemigo.tamano.y / 2.0f, miEnemigo.posicion.z + miEnemigo.tamano.z / 2.0f }
                        };
                        RayCollision colEnemy = GetRayCollisionBox(ray, enemyBox);
                        if (colEnemy.hit && colEnemy.distance < distanciaMinima) {
                            distanciaMinima = colEnemy.distance;
                            encontradoTipo = OBJ_ENEMY;
                            encontradoIndex = -1;
                        }
                    }

                    // Bloques
                    for (int i = 0; i < (int)nivelEscenario->size(); i++) {
                        const auto& b = (*nivelEscenario)[i];
                        Vector3 minB = { b.position.x - b.size.x / 2.0f, b.position.y - b.size.y / 2.0f, b.position.z - b.size.z / 2.0f };
                        Vector3 maxB = { b.position.x + b.size.x / 2.0f, b.position.y + b.size.y / 2.0f, b.position.z + b.size.z / 2.0f };
                        BoundingBox box = { minB, maxB };
                        RayCollision collision = GetRayCollisionBox(ray, box);
                        if (collision.hit && collision.distance < distanciaMinima) {
                            distanciaMinima = collision.distance;
                            encontradoTipo = OBJ_BLOCK;
                            encontradoIndex = i;
                        }
                    }

                    if (encontradoTipo != OBJ_NONE) {
                        if (activeTextField != -1) activeTextField = -1;
                        selectedType = encontradoTipo;
                        bloqueSeleccionadoIndex = encontradoIndex;
                        arrastrandoBloqueConMouse = true;

                        Color cTarget = WHITE;
                        if (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex != -1) cTarget = (*nivelEscenario)[bloqueSeleccionadoIndex].color;
                        else if (selectedType == OBJ_PLAYER) cTarget = miJugador.color;
                        Vector3 hsv = ColorToHSV(cTarget);
                        currentHue = hsv.x; currentSat = hsv.y; currentVal = hsv.z;

                        Vector3 objPos = { 0 };
                        if (selectedType == OBJ_BLOCK) objPos = (*nivelEscenario)[bloqueSeleccionadoIndex].position;
                        else if (selectedType == OBJ_PLAYER) objPos = miJugador.posicion;
                        else if (selectedType == OBJ_COIN) objPos = miMoneda.posicion;
                        else if (selectedType == OBJ_ENEMY) objPos = miEnemigo.posicion;

                        if (fabsf(ray.direction.y) > 0.0001f) {
                            float t = (objPos.y - ray.position.y) / ray.direction.y;
                            if (t > 0.0f) {
                                Vector3 puntoImpacto = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
                                offsetArrastre = Vector3Subtract(objPos, puntoImpacto);
                            }
                        }
                        else {
                            offsetArrastre = { 0.0f, 0.0f, 0.0f };
                        }
                    }
                    else {
                        if (activeTextField != -1) activeTextField = -1;
                        selectedType = OBJ_BLOCK;
                        bloqueSeleccionadoIndex = -1;
                        arrastrandoBloqueConMouse = false;

                        Vector3 puntoImpacto = { 0.0f, 0.0f, 0.0f };
                        if (fabsf(ray.direction.y) > 0.0001f) {
                            float t = -ray.position.y / ray.direction.y;
                            if (t > 0.0f) {
                                puntoImpacto = {
                                    ray.position.x + t * ray.direction.x,
                                    tamanoActualBloque.y / 2.0f,
                                    ray.position.z + t * ray.direction.z
                                };
                            }
                        }

                        EditorBlock nuevoObjeto;
                        nuevoObjeto.position = puntoImpacto;
                        nuevoObjeto.size = tamanoActualBloque;
                        nuevoObjeto.color = (formaActualCreacion == SHAPE_PORTAL) ? PURPLE : inspectorColors[indiceColorActual];
                        nuevoObjeto.shapeType = formaActualCreacion;
                        nuevoObjeto.targetScene = (escenaActualIndex + 1);
                        nuevoObjeto.hasTexture = false;
                        nuevoObjeto.texturePath[0] = '\0';
                        nuevoObjeto.texture.id = 0;
                        nivelEscenario->push_back(nuevoObjeto);
                        bloqueSeleccionadoIndex = (int)nivelEscenario->size() - 1;
                        selectedType = OBJ_BLOCK;
                        Vector3 hsv = ColorToHSV(nuevoObjeto.color);
                        currentHue = hsv.x; currentSat = hsv.y; currentVal = hsv.z;
                        arrastrandoBloqueConMouse = true;
                        offsetArrastre = { 0.0f, 0.0f, 0.0f };
                    }
                }
            }

            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                arrastrandoBloqueConMouse = false;
                arrastrandoHandle = false;
                handleSeleccionado = -1;
            }

            if (arrastrandoHandle && selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex != -1 && bloqueSeleccionadoIndex < (int)nivelEscenario->size()) {
                Vector2 deltaMouse = Vector2Subtract(mousePos, mouseInicioArrastre);
                EditorBlock& selBlock = (*nivelEscenario)[bloqueSeleccionadoIndex];
                float sensibilidad = 0.05f;
                if (handleSeleccionado == 0) selBlock.size.x = fmaxf(0.1f, tamanoInicialBloque.x + (deltaMouse.x * sensibilidad));
                else if (handleSeleccionado == 1) selBlock.size.y = fmaxf(0.1f, tamanoInicialBloque.y - (deltaMouse.y * sensibilidad));
                else if (handleSeleccionado == 2) selBlock.size.z = fmaxf(0.1f, tamanoInicialBloque.z - (deltaMouse.x * sensibilidad));
            }

            if (!arrastrandoHandle && selectedType != OBJ_NONE && arrastrandoBloqueConMouse && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mousePos, previewBox) && CheckCollisionPointRec(mousePos, viewportRect) && !showScriptView) {
                Ray ray = GetMouseRay(mousePos, miCamara.camara);
                if (IsKeyDown(KEY_LEFT_SHIFT)) {
                    Vector2 deltaMouse = GetMouseDelta();
                    if (selectedType == OBJ_BLOCK) (*nivelEscenario)[bloqueSeleccionadoIndex].position.y -= deltaMouse.y * 0.03f;
                    else if (selectedType == OBJ_PLAYER) miJugador.posicion.y -= deltaMouse.y * 0.03f;
                    else if (selectedType == OBJ_COIN) miMoneda.posicion.y -= deltaMouse.y * 0.03f;
                    else if (selectedType == OBJ_ENEMY) miEnemigo.posicion.y -= deltaMouse.y * 0.03f;
                }
                else {
                    if (fabsf(ray.direction.y) > 0.0001f) {
                        float alturaActual = 0.0f;
                        if (selectedType == OBJ_BLOCK) alturaActual = (*nivelEscenario)[bloqueSeleccionadoIndex].position.y;
                        else if (selectedType == OBJ_PLAYER) alturaActual = miJugador.posicion.y;
                        else if (selectedType == OBJ_COIN) alturaActual = miMoneda.posicion.y;
                        else if (selectedType == OBJ_ENEMY) alturaActual = miEnemigo.posicion.y;
                        float t = (alturaActual - ray.position.y) / ray.direction.y;
                        if (t > 0.0f) {
                            Vector3 puntoImpacto = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
                            Vector3 nuevaPos = { puntoImpacto.x + offsetArrastre.x, alturaActual, puntoImpacto.z + offsetArrastre.z };
                            if (selectedType == OBJ_BLOCK) (*nivelEscenario)[bloqueSeleccionadoIndex].position = nuevaPos;
                            else if (selectedType == OBJ_PLAYER) miJugador.posicion = nuevaPos;
                            else if (selectedType == OBJ_COIN) miMoneda.posicion = nuevaPos;
                            else if (selectedType == OBJ_ENEMY) miEnemigo.posicion = nuevaPos;
                        }
                    }
                }
            }
        }

        // --- RENDERIZADO ---
        BeginDrawing();
        ClearBackground(Color{ 200, 215, 230, 255 });

        // Líneas de guía
        DrawLine((int)(leftPanelX + leftPanelAncho), (int)viewportY, (int)panelX, (int)viewportY,
            (mouseOverTopResize || resizingTop) ? YELLOW : Color{ 80, 80, 80, 200 });
        DrawLine((int)(leftPanelX + leftPanelAncho), (int)(viewportY + viewportH),
            (int)panelX, (int)(viewportY + viewportH),
            (mouseOverBottomResize || resizingBottom) ? YELLOW : Color{ 80, 80, 80, 200 });

        if (!showScriptView) {
            // Viewport 3D
            BeginScissorMode(viewportX, viewportY_int, viewportW, viewportH_int);
            BeginMode3D(miCamara.camara);
            DrawModel(modeloCielo, miCamara.camara.position, 1.0f, WHITE);

            for (int i = 0; i < (int)nivelEscenario->size(); i++) {
                const auto& obj = (*nivelEscenario)[i];
                bool seleccionado = (currentMode == MODE_EDITOR && selectedType == OBJ_BLOCK && i == bloqueSeleccionadoIndex);
                dibujarObjeto3D(obj, seleccionado);
                if (seleccionado) {
                    Vector3 gX = { obj.position.x + obj.size.x / 2.0f, obj.position.y, obj.position.z };
                    Vector3 gY = { obj.position.x, obj.position.y + obj.size.y / 2.0f, obj.position.z };
                    Vector3 gZ = { obj.position.x, obj.position.y, obj.position.z + obj.size.z / 2.0f };
                    DrawCube(gX, 0.4f, 0.4f, 0.4f, RED);
                    DrawCube(gY, 0.4f, 0.4f, 0.4f, GREEN);
                    DrawCube(gZ, 0.4f, 0.4f, 0.4f, BLUE);
                    DrawCubeWires(gX, 0.4f, 0.4f, 0.4f, BLACK);
                    DrawCubeWires(gY, 0.4f, 0.4f, 0.4f, BLACK);
                    DrawCubeWires(gZ, 0.4f, 0.4f, 0.4f, BLACK);
                }
            }

            // Jugador
            bool jugadorSeleccionado = (currentMode == MODE_EDITOR && selectedType == OBJ_PLAYER);
            float alturaCilindro = miJugador.alturaTotal - (miJugador.radio * 2.0f);
            Vector3 puntoInferiorCilindro = { miJugador.posicion.x, miJugador.posicion.y - (alturaCilindro / 2.0f), miJugador.posicion.z };
            Vector3 puntoSuperiorCilindro = { miJugador.posicion.x, miJugador.posicion.y + (alturaCilindro / 2.0f), miJugador.posicion.z };
            DrawCylinderEx(puntoInferiorCilindro, puntoSuperiorCilindro, miJugador.radio, miJugador.radio, 16, miJugador.color);
            DrawSphere(puntoSuperiorCilindro, miJugador.radio, miJugador.color);
            DrawSphere(puntoInferiorCilindro, miJugador.radio, miJugador.color);
            DrawSphereWires(puntoSuperiorCilindro, miJugador.radio, 10, 10, jugadorSeleccionado ? GOLD : MAROON);
            DrawSphereWires(puntoInferiorCilindro, miJugador.radio, 10, 10, jugadorSeleccionado ? GOLD : MAROON);

            // Objetos especiales
            if (tieneObjetosEspeciales) {
                if (miMoneda.activa) {
                    bool monedaSeleccionada = (currentMode == MODE_EDITOR && selectedType == OBJ_COIN);
                    DrawSphere(miMoneda.posicion, miMoneda.radio, YELLOW);
                    DrawSphereWires(miMoneda.posicion, miMoneda.radio, 8, 8, monedaSeleccionada ? GOLD : GOLD);
                }
                bool enemigoSeleccionado = (currentMode == MODE_EDITOR && selectedType == OBJ_ENEMY);
                DrawCube(miEnemigo.posicion, miEnemigo.tamano.x, miEnemigo.tamano.y, miEnemigo.tamano.z, BLUE);
                DrawCubeWires(miEnemigo.posicion, miEnemigo.tamano.x, miEnemigo.tamano.y, miEnemigo.tamano.z, enemigoSeleccionado ? GOLD : DARKBLUE);
            }

            DrawGrid(100, 1.0f);
            EndMode3D();
            EndScissorMode();
        }
        else {

            // Editor de scripts en el viewport
            ShowCursor();
            Rectangle scriptRect = { (float)viewportX, (float)viewportY_int, (float)viewportW, (float)viewportH_int };
            DrawScriptEditorInRect(scriptRect, scaleFactor);

        }

        // --- UI 2D (fondos, paneles) ---
        int leftPanelWidthInt = (int)ceilf(leftPanelAncho);
        int rightPanelWidthInt = (int)ceilf(panelAncho);
        int viewportTopInt = (int)ceilf(viewportY);
        int viewportBottomInt = (int)ceilf(viewportY + viewportH);

        DrawRectangle(0, 0, leftPanelWidthInt, (int)altoActualVentana, Color{ 40, 40, 40, 255 });
        DrawRectangle((int)(anchoActualVentana - panelAncho), 0, rightPanelWidthInt, (int)altoActualVentana, Color{ 40, 40, 40, 255 });
        DrawRectangle(leftPanelWidthInt, 0, (int)(panelX - leftPanelAncho), viewportTopInt, Color{ 40, 40, 40, 255 });
        DrawRectangle(leftPanelWidthInt, viewportBottomInt, (int)(panelX - leftPanelAncho), (int)(altoActualVentana - viewportBottomInt), Color{ 40, 40, 40, 255 });

        // HUD
        DrawRectangleRec(hudRect, Fade(BLACK, 0.75f));
        DrawRectangleLines((int)hudRect.x, (int)hudRect.y, (int)hudRect.width, (int)hudRect.height, DARKGRAY);
        int fSizeTitle = (int)(12 * scaleFactor);
        int fSizeText = (int)(11 * scaleFactor);
        int fSizeSmall = (int)(10 * scaleFactor);

        // Panel izquierdo
        DrawRectangle((int)leftPanelX, (int)leftPanelY, (int)leftPanelAncho, (int)leftPanelAlto, Color{ 35, 35, 40, 255 });
        DrawRectangleLines((int)leftPanelX, (int)leftPanelY, (int)leftPanelAncho, (int)leftPanelAlto, DARKGRAY);
        Color tab0Color = (leftPanelTab == 0) ? DARKGRAY : GRAY;
        Color tab1Color = (leftPanelTab == 1) ? DARKGRAY : GRAY;
        DrawRectangleRec(tabBtnNodes, tab0Color);
        DrawRectangleRec(tabBtnToolbox, tab1Color);
        DrawRectangleLines((int)tabBtnNodes.x, (int)tabBtnNodes.y, (int)tabBtnNodes.width, (int)tabBtnNodes.height, BLACK);
        DrawRectangleLines((int)tabBtnToolbox.x, (int)tabBtnToolbox.y, (int)tabBtnToolbox.width, (int)tabBtnToolbox.height, BLACK);
        DrawText("Jerarquia", (int)(tabBtnNodes.x + 8 * scaleFactor), (int)(tabBtnNodes.y + 6 * scaleFactor), (int)(12 * scaleFactor), WHITE);
        DrawText("Toolbox", (int)(tabBtnToolbox.x + 8 * scaleFactor), (int)(tabBtnToolbox.y + 6 * scaleFactor), (int)(12 * scaleFactor), WHITE);

        if (leftPanelTab == 0) {
            DrawText("Arbol de Nodos", (int)(leftPanelX + 10 * scaleFactor), (int)(contentStartY), (int)(12 * scaleFactor), GOLD);
            float itemY = contentStartY + 20.0f * scaleFactor;
            bool playerSel = (selectedType == OBJ_PLAYER);
            DrawRectangle((int)(leftPanelX + 10 * scaleFactor), (int)itemY, (int)(leftPanelAncho - 20 * scaleFactor), (int)treeItemHeight, playerSel ? Fade(SKYBLUE, 0.3f) : BLANK);
            DrawText("Jugador", (int)(leftPanelX + 15 * scaleFactor), (int)(itemY + 2 * scaleFactor), (int)(11 * scaleFactor), playerSel ? YELLOW : WHITE);
            // Icono de script si el jugador tiene uno
            if (miJugador.hasScript) {
                DrawText("[LUA]", (int)(leftPanelX + leftPanelAncho - 55 * scaleFactor), (int)(itemY + 2 * scaleFactor), (int)(9 * scaleFactor), YELLOW);
            }
            itemY += treeItemHeight;
            if (tieneObjetosEspeciales) {
                bool coinSel = (selectedType == OBJ_COIN);
                DrawRectangle((int)(leftPanelX + 10 * scaleFactor), (int)itemY, (int)(leftPanelAncho - 20 * scaleFactor), (int)treeItemHeight, coinSel ? Fade(SKYBLUE, 0.3f) : BLANK);
                DrawText("Moneda", (int)(leftPanelX + 15 * scaleFactor), (int)(itemY + 2 * scaleFactor), (int)(11 * scaleFactor), coinSel ? YELLOW : WHITE);
                if (miMoneda.hasScript) {
                    DrawText("[LUA]", (int)(leftPanelX + leftPanelAncho - 55 * scaleFactor), (int)(itemY + 2 * scaleFactor), (int)(9 * scaleFactor), YELLOW);
                }
                itemY += treeItemHeight;
            }
            if (tieneObjetosEspeciales) {
                bool enemySel = (selectedType == OBJ_ENEMY);
                DrawRectangle((int)(leftPanelX + 10 * scaleFactor), (int)itemY, (int)(leftPanelAncho - 20 * scaleFactor), (int)treeItemHeight, enemySel ? Fade(SKYBLUE, 0.3f) : BLANK);
                DrawText("Enemigo", (int)(leftPanelX + 15 * scaleFactor), (int)(itemY + 2 * scaleFactor), (int)(11 * scaleFactor), enemySel ? YELLOW : WHITE);
                if (miEnemigo.hasScript) {
                    DrawText("[LUA]", (int)(leftPanelX + leftPanelAncho - 55 * scaleFactor), (int)(itemY + 2 * scaleFactor), (int)(9 * scaleFactor), YELLOW);
                }
                itemY += treeItemHeight;
            }
            for (int i = 0; i < (int)nivelEscenario->size(); i++) {
                bool blockSel = (selectedType == OBJ_BLOCK && i == bloqueSeleccionadoIndex);
                DrawRectangle((int)(leftPanelX + 10 * scaleFactor), (int)itemY, (int)(leftPanelAncho - 20 * scaleFactor), (int)treeItemHeight, blockSel ? Fade(SKYBLUE, 0.3f) : BLANK);
                const char* shapeName = "Bloque";
                if ((*nivelEscenario)[i].shapeType == SHAPE_CUBE) shapeName = "Cubo";
                else if ((*nivelEscenario)[i].shapeType == SHAPE_SPHERE) shapeName = "Esfera";
                else if ((*nivelEscenario)[i].shapeType == SHAPE_CYLINDER) shapeName = "Cilindro";
                else if ((*nivelEscenario)[i].shapeType == SHAPE_CONE) shapeName = "Cono";
                else if ((*nivelEscenario)[i].shapeType == SHAPE_PORTAL) shapeName = "Portal";
                DrawText(shapeName, (int)(leftPanelX + 15 * scaleFactor), (int)(itemY + 2 * scaleFactor), (int)(11 * scaleFactor), blockSel ? YELLOW : WHITE);
                // Icono de script si el bloque tiene uno
                if ((*nivelEscenario)[i].hasScript) {
                    DrawText("[LUA]", (int)(leftPanelX + leftPanelAncho - 55 * scaleFactor), (int)(itemY + 2 * scaleFactor), (int)(9 * scaleFactor), YELLOW);
                }
                itemY += treeItemHeight;
            }
        }
        else {
            DrawText("Toolbox & Escenas", (int)(leftPanelX + 10 * scaleFactor), (int)(contentStartY), (int)(12 * scaleFactor), GOLD);
            DrawRectangleRec(btnCube, (formaActualCreacion == SHAPE_CUBE) ? DARKGRAY : BLACK);
            DrawRectangleLines((int)btnCube.x, (int)btnCube.y, (int)btnCube.width, (int)btnCube.height, (formaActualCreacion == SHAPE_CUBE) ? GOLD : DARKGRAY);
            DrawText("Cubo", (int)btnCube.x + (12 * scaleFactor), (int)btnCube.y + (6 * scaleFactor), fSizeText, (formaActualCreacion == SHAPE_CUBE) ? GOLD : WHITE);
            DrawRectangleRec(btnSphere, (formaActualCreacion == SHAPE_SPHERE) ? DARKGRAY : BLACK);
            DrawRectangleLines((int)btnSphere.x, (int)btnSphere.y, (int)btnSphere.width, (int)btnSphere.height, (formaActualCreacion == SHAPE_SPHERE) ? GOLD : DARKGRAY);
            DrawText("Esfera", (int)btnSphere.x + (12 * scaleFactor), (int)btnSphere.y + (6 * scaleFactor), fSizeText, (formaActualCreacion == SHAPE_SPHERE) ? GOLD : WHITE);
            DrawRectangleRec(btnCylinder, (formaActualCreacion == SHAPE_CYLINDER) ? DARKGRAY : BLACK);
            DrawRectangleLines((int)btnCylinder.x, (int)btnCylinder.y, (int)btnCylinder.width, (int)btnCylinder.height, (formaActualCreacion == SHAPE_CYLINDER) ? GOLD : DARKGRAY);
            DrawText("Cilindro", (int)btnCylinder.x + (12 * scaleFactor), (int)btnCylinder.y + (6 * scaleFactor), fSizeText, (formaActualCreacion == SHAPE_CYLINDER) ? GOLD : WHITE);
            DrawRectangleRec(btnCone, (formaActualCreacion == SHAPE_CONE) ? DARKGRAY : BLACK);
            DrawRectangleLines((int)btnCone.x, (int)btnCone.y, (int)btnCone.width, (int)btnCone.height, (formaActualCreacion == SHAPE_CONE) ? GOLD : DARKGRAY);
            DrawText("Cono", (int)btnCone.x + (12 * scaleFactor), (int)btnCone.y + (6 * scaleFactor), fSizeText, (formaActualCreacion == SHAPE_CONE) ? GOLD : WHITE);
            DrawRectangleRec(btnPortal, (formaActualCreacion == SHAPE_PORTAL) ? DARKGRAY : BLACK);
            DrawRectangleLines((int)btnPortal.x, (int)btnPortal.y, (int)btnPortal.width, (int)btnPortal.height, (formaActualCreacion == SHAPE_PORTAL) ? GOLD : DARKGRAY);
            DrawText("Portal (Meta)", (int)btnPortal.x + (12 * scaleFactor), (int)btnPortal.y + (6 * scaleFactor), fSizeText, (formaActualCreacion == SHAPE_PORTAL) ? GOLD : MAGENTA);

            DrawText("Gestión de Escenas", (int)leftPanelX + (12 * scaleFactor), (int)sceneSectionY - (10.5 * scaleFactor), (int)(11 * scaleFactor), GRAY);
            DrawRectangle((int)(leftPanelX + 12 * scaleFactor), (int)(sceneSectionY - 12 * scaleFactor), (int)(leftPanelAncho - 24 * scaleFactor), 1, DARKGRAY);
            DrawRectangleRec(btnNuevaEscena, BLACK);
            DrawRectangleLines((int)btnNuevaEscena.x, (int)btnNuevaEscena.y, (int)btnNuevaEscena.width, (int)btnNuevaEscena.height, DARKGRAY);
            DrawText("+ Nueva Escena", (int)btnNuevaEscena.x + (10 * scaleFactor), (int)btnNuevaEscena.y + (6 * scaleFactor), fSizeText, GREEN);
            DrawRectangleRec(btnEscenaPrev, BLACK);
            DrawRectangleLines((int)btnEscenaPrev.x, (int)btnEscenaPrev.y, (int)btnEscenaPrev.width, (int)btnEscenaPrev.height, DARKGRAY);
            DrawText("<", (int)btnEscenaPrev.x + (10 * scaleFactor), (int)btnEscenaPrev.y + (6 * scaleFactor), fSizeText, YELLOW);
            DrawRectangleRec(btnEscenaDisplay, BLACK);
            DrawRectangleLines((int)btnEscenaDisplay.x, (int)btnEscenaDisplay.y, (int)btnEscenaDisplay.width, (int)btnEscenaDisplay.height, DARKGRAY);
            DrawText(TextFormat("Escena: %i/%i", escenaActualIndex + 1, (int)listaEscenas.size()), (int)btnEscenaDisplay.x + (6 * scaleFactor), (int)btnEscenaDisplay.y + (6 * scaleFactor), fSizeText, YELLOW);
            DrawRectangleRec(btnEscenaNext, BLACK);
            DrawRectangleLines((int)btnEscenaNext.x, (int)btnEscenaNext.y, (int)btnEscenaNext.width, (int)btnEscenaNext.height, DARKGRAY);
            DrawText(">", (int)btnEscenaNext.x + (10 * scaleFactor), (int)btnEscenaNext.y + (6 * scaleFactor), fSizeText, YELLOW);
            DrawRectangleRec(btnBorrarEscena, BLACK);
            DrawRectangleLines((int)btnBorrarEscena.x, (int)btnBorrarEscena.y, (int)btnBorrarEscena.width, (int)btnBorrarEscena.height, DARKGRAY);
            DrawText("- Borrar Escena", (int)btnBorrarEscena.x + (10 * scaleFactor), (int)btnBorrarEscena.y + (6 * scaleFactor), fSizeText, (listaEscenas.size() > 1) ? RED : GRAY);
            DrawText("Proyecto", (int)leftPanelX + (12 * scaleFactor), (int)saveLoadY - (10.5 * scaleFactor), (int)(11 * scaleFactor), GRAY);
            DrawRectangle((int)(leftPanelX + 12 * scaleFactor), (int)(saveLoadY - 12 * scaleFactor), (int)(leftPanelAncho - 24 * scaleFactor), 1, DARKGRAY);
            DrawRectangleRec(btnGuardar, DARKGREEN);
            DrawRectangleLines((int)btnGuardar.x, (int)btnGuardar.y, (int)btnGuardar.width, (int)btnGuardar.height, LIME);
            DrawText("Guardar Proyecto", (int)btnGuardar.x + (10 * scaleFactor), (int)btnGuardar.y + (6 * scaleFactor), fSizeText, GREEN);
            DrawRectangleRec(btnCargar, DARKBLUE);
            DrawRectangleLines((int)btnCargar.x, (int)btnCargar.y, (int)btnCargar.width, (int)btnCargar.height, SKYBLUE);
            DrawText("Cargar Proyecto", (int)btnCargar.x + (10 * scaleFactor), (int)btnCargar.y + (6 * scaleFactor), fSizeText, BLUE);
            DrawRectangleRec(btnExportar, DARKPURPLE);
            DrawRectangleLines((int)btnExportar.x, (int)btnExportar.y, (int)btnExportar.width, (int)btnExportar.height, MAGENTA);
            DrawText("Exportar a .exe", (int)btnExportar.x + (10 * scaleFactor), (int)btnExportar.y + (6 * scaleFactor), fSizeText, MAGENTA);
        }

        // Panel inspector (COMPLETO)
        DrawRectangle((int)panelX, (int)panely, (int)panelAncho, (int)panelAlto, Color{ 35, 35, 40, 255 });
        DrawRectangleLines((int)panelX, (int)panely, (int)panelAncho, (int)panelAlto, DARKGRAY);
        DrawRectangle((int)panelX, (int)panely, (int)panelAncho, (int)(28 * scaleFactor), Fade(DARKGRAY, 0.9f));
        const char* inspectorTitle = "Node Inspector";
        if (selectedType == OBJ_BLOCK) {
            if (bloqueSeleccionadoIndex != -1 && bloqueSeleccionadoIndex < (int)nivelEscenario->size() && (*nivelEscenario)[bloqueSeleccionadoIndex].shapeType == SHAPE_PORTAL)
                inspectorTitle = "PortalNode (Teleport)";
            else
                inspectorTitle = "MeshInstance3D (Block)";
        }
        else if (selectedType == OBJ_PLAYER) inspectorTitle = "PlayerNode (Player)";
        else if (selectedType == OBJ_COIN) inspectorTitle = "CoinNode (Collectible)";
        else if (selectedType == OBJ_ENEMY) inspectorTitle = "EnemyNode (Enemy)";
        else inspectorTitle = "Node Inspector (vacío)";
        DrawText(inspectorTitle, (int)panelX + (10 * scaleFactor), (int)panely + (8 * scaleFactor), (int)(12 * scaleFactor), GOLD);

        if (selectedType != OBJ_NONE) {
            // Visor 3D
            float previewBoxHeight = 130.0f * scaleFactor;
            float currentY = panely + 34.0f * scaleFactor - inspectorScrollY;
            Rectangle previewBox = { panelX + (12.0f * scaleFactor), currentY, panelAncho - (24.0f * scaleFactor), previewBoxHeight };
            int targetW = (int)previewBox.width;
            int targetH = (int)previewBox.height;
            if (targetW < 1) targetW = 1;
            if (targetH < 1) targetH = 1;

            if (previewTarget.texture.width != targetW || previewTarget.texture.height != targetH) {
                UnloadRenderTexture(previewTarget);
                previewTarget = LoadRenderTexture(targetW, targetH);
            }

            BeginTextureMode(previewTarget);
            ClearBackground(Color{ 30, 35, 45, 255 });
            Camera3D previewCam = { 0 };
            previewCam.up = Vector3{ 0.0f, 1.0f, 0.0f };
            previewCam.fovy = 45.0f;
            previewCam.projection = CAMERA_PERSPECTIVE;

            float maxDim = 2.0f;
            if (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex != -1) {
                const auto& selB = (*nivelEscenario)[bloqueSeleccionadoIndex];
                maxDim = fmaxf(fmaxf(selB.size.x, selB.size.y), selB.size.z);
            }
            else if (selectedType == OBJ_PLAYER) {
                maxDim = fmaxf(miJugador.radio * 2.0f, miJugador.alturaTotal);
            }
            else if (selectedType == OBJ_COIN) {
                maxDim = miMoneda.radio * 2.0f;
            }
            else if (selectedType == OBJ_ENEMY) {
                maxDim = fmaxf(fmaxf(miEnemigo.tamano.x, miEnemigo.tamano.y), miEnemigo.tamano.z);
            }
            float distanciaCamara = maxDim * 1.8f;
            previewCam.position = Vector3{
                cosf(previewPitch) * sinf(previewYaw) * distanciaCamara,
                sinf(previewPitch) * distanciaCamara,
                cosf(previewPitch) * cosf(previewYaw) * distanciaCamara
            };
            previewCam.target = Vector3{ 0.0f, 0.0f, 0.0f };

            BeginMode3D(previewCam);
            if (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex != -1) {
                const auto& selB = (*nivelEscenario)[bloqueSeleccionadoIndex];
                if (selB.hasTexture && selB.texture.id != 0) {
                    Model model = GetCubeTexturedModel(selB.texture);
                    Vector3 scale = { selB.size.x, selB.size.y, selB.size.z };
                    DrawModelEx(model, Vector3{ 0,0,0 }, Vector3{ 0,0,0 }, 0, scale, WHITE);
                    DrawCubeWires(Vector3{ 0,0,0 }, selB.size.x, selB.size.y, selB.size.z, BLACK);
                }
                else if (selB.shapeType == SHAPE_CUBE) {
                    DrawCube(Vector3{ 0, 0, 0 }, selB.size.x, selB.size.y, selB.size.z, selB.color);
                    DrawCubeWires(Vector3{ 0, 0, 0 }, selB.size.x, selB.size.y, selB.size.z, BLACK);
                }
                else if (selB.shapeType == SHAPE_SPHERE) {
                    float rad = selB.size.x * 0.5f;
                    DrawSphere(Vector3{ 0, 0, 0 }, rad, selB.color);
                    DrawSphereWires(Vector3{ 0, 0, 0 }, rad, 10, 10, BLACK);
                }
                else if (selB.shapeType == SHAPE_CYLINDER) {
                    float rad = selB.size.x * 0.5f;
                    float alt = selB.size.y;
                    DrawCylinderEx(Vector3{ 0, -alt / 2.0f, 0 }, Vector3{ 0, alt / 2.0f, 0 }, rad, rad, 16, selB.color);
                    DrawCylinderWiresEx(Vector3{ 0, -alt / 2.0f, 0 }, Vector3{ 0, alt / 2.0f, 0 }, rad, rad, 16, BLACK);
                }
                else if (selB.shapeType == SHAPE_CONE) {
                    float rad = selB.size.x * 0.5f;
                    float alt = selB.size.y;
                    DrawCylinderEx(Vector3{ 0, -alt / 2.0f, 0 }, Vector3{ 0, alt / 2.0f, 0 }, rad, 0.0f, 16, selB.color);
                    DrawCylinderWiresEx(Vector3{ 0, -alt / 2.0f, 0 }, Vector3{ 0, alt / 2.0f, 0 }, rad, 0.0f, 16, BLACK);
                }
                else if (selB.shapeType == SHAPE_PORTAL) {
                    DrawCube(Vector3{ 0, 0, 0 }, selB.size.x, selB.size.y, selB.size.z, PURPLE);
                    DrawCubeWires(Vector3{ 0, 0, 0 }, selB.size.x, selB.size.y, selB.size.z, MAGENTA);
                }
            }
            else if (selectedType == OBJ_PLAYER) {
                float pAlt = miJugador.alturaTotal - (miJugador.radio * 2.0f);
                DrawCylinderEx(Vector3{ 0, -pAlt / 2.0f, 0 }, Vector3{ 0, pAlt / 2.0f, 0 }, miJugador.radio, miJugador.radio, 16, miJugador.color);
                DrawSphere(Vector3{ 0, pAlt / 2.0f, 0 }, miJugador.radio, miJugador.color);
                DrawSphere(Vector3{ 0, -pAlt / 2.0f, 0 }, miJugador.radio, miJugador.color);
            }
            else if (selectedType == OBJ_COIN) {
                DrawSphere(Vector3{ 0, 0, 0 }, miMoneda.radio, YELLOW);
                DrawSphereWires(Vector3{ 0, 0, 0 }, miMoneda.radio, 8, 8, GOLD);
            }
            else if (selectedType == OBJ_ENEMY) {
                DrawCube(Vector3{ 0, 0, 0 }, miEnemigo.tamano.x, miEnemigo.tamano.y, miEnemigo.tamano.z, BLUE);
                DrawCubeWires(Vector3{ 0, 0, 0 }, miEnemigo.tamano.x, miEnemigo.tamano.y, miEnemigo.tamano.z, DARKBLUE);
            }
            EndMode3D();
            EndTextureMode();

            // --- Scissor para contenido del inspector ---
            BeginScissorMode((int)panelX, (int)(panely + 28.0f * scaleFactor), (int)panelAncho, (int)(panelAlto - 28.0f * scaleFactor));
            {
                DrawTextureRec(previewTarget.texture, Rectangle{ 0, 0, (float)previewTarget.texture.width, (float)-previewTarget.texture.height }, Vector2{ previewBox.x, previewBox.y }, WHITE);
                DrawRectangleLines((int)previewBox.x, (int)previewBox.y, (int)previewBox.width, (int)previewBox.height, GRAY);
                DrawText("3D Viewport (Arrastra para girar)", (int)previewBox.x + (8 * scaleFactor), (int)previewBox.y + (6 * scaleFactor), (int)(10 * scaleFactor), LIGHTGRAY);

                float transformHeaderYLocal = (panely + 34.0f * scaleFactor - inspectorScrollY) + previewBox.height + 12.0f;
                DrawRectangle((int)(panelX + (10 * scaleFactor)), (int)transformHeaderYLocal, (int)(panelAncho - (20.0f * scaleFactor)), (int)(22.0f * scaleFactor), Fade(DARKGRAY, 0.6f));
                const char* flechaTrans = transformSectionOpen ? "v" : ">";
                DrawText(flechaTrans, (int)panelX + (16 * scaleFactor), (int)transformHeaderYLocal + (4 * scaleFactor), (int)(11 * scaleFactor), GOLD);
                DrawText("Transform", (int)panelX + (32 * scaleFactor), (int)transformHeaderYLocal + (4 * scaleFactor), (int)(12 * scaleFactor), GOLD);

                float transformRowsStartY = transformHeaderYLocal + 28.0f * scaleFactor;
                float boxW = 85.0f * scaleFactor;
                float boxH = 20.0f * scaleFactor;
                float boxX = panelX + panelAncho - boxW - (12.0f * scaleFactor);
                int numRows = getSelectedNumRows();

                // Posición Y actual: después de Transform (abierto o cerrado)
                float currentY = transformSectionOpen ? (transformRowsStartY + numRows * rowH + 6.0f * scaleFactor) : (transformHeaderYLocal + 28.0f * scaleFactor + 6.0f * scaleFactor);

                if (transformSectionOpen) {
                    const char* etiquetas[6] = { "Pos X", "Pos Y", "Pos Z", "Tam X", "Tam Y", "Tam Z" };
                    if (selectedType == OBJ_PLAYER) {
                        etiquetas[3] = "Radio";
                        etiquetas[4] = "Altura";
                    }
                    else if (selectedType == OBJ_COIN) {
                        etiquetas[3] = "Radio";
                    }
                    for (int r = 0; r < numRows; r++) {
                        float currentYRow = transformRowsStartY + (r * rowH);
                        Color colorEje = WHITE;
                        if (r % 3 == 0 && r < 3) colorEje = RED;
                        else if (r % 3 == 1 && r < 3) colorEje = GREEN;
                        else if (r % 3 == 2 && r < 3) colorEje = BLUE;
                        DrawText(etiquetas[r], (int)panelX + (14 * scaleFactor), (int)currentYRow + (2 * scaleFactor), (int)(11 * scaleFactor), colorEje);
                        Rectangle valBoxRect = { boxX, currentYRow, boxW, boxH };
                        Color boxBgColor = (activeTextField == r) ? Color{ 40, 45, 55, 255 } : DARKGRAY;
                        Color boxBorderColor = (activeTextField == r) ? GOLD : GRAY;
                        DrawRectangleRec(valBoxRect, boxBgColor);
                        DrawRectangleLines((int)valBoxRect.x, (int)valBoxRect.y, (int)valBoxRect.width, (int)valBoxRect.height, boxBorderColor);
                        const char* displayText = "";
                        char tempBuffer[32];
                        if (activeTextField == r) {
                            displayText = textInputBuffer;
                        }
                        else {
                            float val = getObjectVal(r);
                            snprintf(tempBuffer, sizeof(tempBuffer), "%.1f", val);
                            displayText = tempBuffer;
                        }
                        DrawText(displayText, (int)valBoxRect.x + (8 * scaleFactor), (int)valBoxRect.y + (4 * scaleFactor), (int)(11 * scaleFactor), WHITE);
                        if (activeTextField == r && isScrubbing) {
                            DrawText("<->", (int)valBoxRect.x + valBoxRect.width + (4 * scaleFactor), (int)valBoxRect.y + (4 * scaleFactor), (int)(10 * scaleFactor), YELLOW);
                        }
                    }
                }

                bool hasColor = (selectedType == OBJ_BLOCK || selectedType == OBJ_PLAYER);
                float colorHeaderYLocal = currentY;

                if (hasColor) {
                    DrawRectangle((int)(panelX + (10 * scaleFactor)), (int)colorHeaderYLocal, (int)(panelAncho - (20.0f * scaleFactor)), (int)(22.0f * scaleFactor), Fade(DARKGRAY, 0.6f));
                    const char* flechaColor = colorSectionOpen ? "v" : ">";
                    DrawText(flechaColor, (int)panelX + (16 * scaleFactor), (int)colorHeaderYLocal + (4 * scaleFactor), (int)(11 * scaleFactor), GOLD);
                    DrawText("Color Settings", (int)panelX + (32 * scaleFactor), (int)colorHeaderYLocal + (4 * scaleFactor), (int)(12 * scaleFactor), GOLD);

                    if (colorSectionOpen) {
                        float contentStartY = colorHeaderYLocal + 28.0f * scaleFactor;
                        int cols = 4;
                        for (int c = 0; c < numInspectorColors; c++) {
                            int colIdx = c % cols;
                            int rowIdx = c / cols;
                            float rx = swatchStartX + colIdx * (swatchSize + swatchSpacing);
                            float ry = contentStartY + rowIdx * (swatchSize + swatchSpacing);
                            Rectangle swatchRect = { rx, ry, swatchSize, swatchSize };
                            DrawRectangleRec(swatchRect, inspectorColors[c]);
                            Color colCheck = (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex != -1) ? (*nivelEscenario)[bloqueSeleccionadoIndex].color : miJugador.color;
                            bool esColorActual = (colCheck.r == inspectorColors[c].r &&
                                colCheck.g == inspectorColors[c].g &&
                                colCheck.b == inspectorColors[c].b);
                            DrawRectangleLines((int)rx, (int)ry, (int)swatchSize, (int)swatchSize, esColorActual ? GOLD : GRAY);
                        }
                        float swatchTotalH = 3 * (swatchSize + swatchSpacing);
                        float pickerHeaderYLocal = contentStartY + swatchTotalH + (10.0f * scaleFactor);
                        DrawText("Visual Color Picker", (int)panelX + (14 * scaleFactor), (int)pickerHeaderYLocal, (int)(11 * scaleFactor), LIGHTGRAY);
                        float pickerStartYLocal = pickerHeaderYLocal + 18.0f * scaleFactor;
                        float pickerWLocal = 100.0f * scaleFactor;
                        float pickerHLocal = 120.0f * scaleFactor;
                        float pickerXLocal = panelX + (14.0f * scaleFactor);
                        float hueBarWLocal = 20.0f * scaleFactor;
                        float hueBarXLocal = pickerXLocal + pickerWLocal + (10.0f * scaleFactor);

                        if (fabsf(currentHue - lastHueCache) > 0.01f) {
                            Image tempImgSV = GenImageColor(pickerTexSize, pickerTexSize, WHITE);
                            for (int y = 0; y < pickerTexSize; y++) {
                                float v = 1.0f - (float)y / (pickerTexSize - 1);
                                for (int x = 0; x < pickerTexSize; x++) {
                                    float s = (float)x / (pickerTexSize - 1);
                                    Color c = ColorFromHSV(currentHue, s, v);
                                    ImageDrawPixel(&tempImgSV, x, y, c);
                                }
                            }
                            UpdateTexture(texSV, tempImgSV.data);
                            UnloadImage(tempImgSV);
                            lastHueCache = currentHue;
                        }

                        DrawTexturePro(texSV, Rectangle{ 0, 0, (float)pickerTexSize, (float)pickerTexSize }, Rectangle{ pickerXLocal, pickerStartYLocal, pickerWLocal, pickerHLocal }, Vector2{ 0, 0 }, 0.0f, WHITE);
                        DrawRectangleLines((int)pickerXLocal, (int)pickerStartYLocal, (int)pickerWLocal, (int)pickerHLocal, GRAY);
                        DrawTexturePro(texHue, Rectangle{ 0, 0, 20.0f, (float)pickerTexSize }, Rectangle{ hueBarXLocal, pickerStartYLocal, hueBarWLocal, pickerHLocal }, Vector2{ 0, 0 }, 0.0f, WHITE);
                        DrawRectangleLines((int)hueBarXLocal, (int)pickerStartYLocal, (int)hueBarWLocal, (int)pickerHLocal, GRAY);

                        float cursorX = pickerXLocal + currentSat * pickerWLocal;
                        float cursorY = pickerStartYLocal + (1.0f - currentVal) * pickerHLocal;
                        DrawCircleLines((int)cursorX, (int)cursorY, 5.0f * scaleFactor, BLACK);
                        DrawCircle((int)cursorX, (int)cursorY, 3.5f * scaleFactor, WHITE);
                        float hueCursorY = pickerStartYLocal + (1.0f - (currentHue / 360.0f)) * pickerHLocal;
                        DrawRectangle((int)hueBarXLocal - 2, (int)hueCursorY - 2, (int)hueBarWLocal + 4, 4, WHITE);
                        DrawRectangleLines((int)hueBarXLocal - 2, (int)hueCursorY - 2, (int)hueBarWLocal + 4, 4, BLACK);

                        float rgbHeaderYLocal = pickerStartYLocal + pickerHLocal + (10.0f * scaleFactor);
                        DrawText("RGB Values", (int)panelX + (14 * scaleFactor), (int)rgbHeaderYLocal, (int)(11 * scaleFactor), LIGHTGRAY);
                        float rgbRowsStartYLocal = rgbHeaderYLocal + 18.0f * scaleFactor;
                        const char* rgbEtiquetas[3] = { "R (Rojo)", "G (Verde)", "B (Azul)" };
                        Color rgbColores[3] = { RED, GREEN, BLUE };
                        int rgbStartIndex = (selectedType == OBJ_PLAYER) ? 5 : 6;
                        for (int r = 0; r < 3; r++) {
                            int fieldIndex = rgbStartIndex + r;
                            float currentYRow = rgbRowsStartYLocal + (r * rowH);
                            DrawText(rgbEtiquetas[r], (int)panelX + (14 * scaleFactor), (int)currentYRow + (2 * scaleFactor), (int)(11 * scaleFactor), rgbColores[r]);
                            Rectangle valBoxRect = { boxX, currentYRow, boxW, boxH };
                            Color boxBgColor = (activeTextField == fieldIndex) ? Color{ 40, 45, 55, 255 } : DARKGRAY;
                            Color boxBorderColor = (activeTextField == fieldIndex) ? GOLD : GRAY;
                            DrawRectangleRec(valBoxRect, boxBgColor);
                            DrawRectangleLines((int)valBoxRect.x, (int)valBoxRect.y, (int)valBoxRect.width, (int)valBoxRect.height, boxBorderColor);
                            const char* displayText = "";
                            char tempBuffer[32];
                            if (activeTextField == fieldIndex) {
                                displayText = textInputBuffer;
                            }
                            else {
                                float val = getObjectVal(fieldIndex);
                                snprintf(tempBuffer, sizeof(tempBuffer), "%.0f", val);
                                displayText = tempBuffer;
                            }
                            DrawText(displayText, (int)valBoxRect.x + (8 * scaleFactor), (int)valBoxRect.y + (4 * scaleFactor), (int)(11 * scaleFactor), WHITE);
                            if (activeTextField == fieldIndex && isScrubbing) {
                                DrawText("<->", (int)valBoxRect.x + valBoxRect.width + (4 * scaleFactor), (int)valBoxRect.y + (4 * scaleFactor), (int)(10 * scaleFactor), YELLOW);
                            }
                        }

                        // Actualizar currentY al final de la sección de color abierta
                        currentY = rgbRowsStartYLocal + 3 * rowH + 12.0f * scaleFactor;
                    }
                    else {
                        // Sección de color cerrada: currentY justo debajo del header
                        currentY = colorHeaderYLocal + 28.0f * scaleFactor;
                    }
                }

                // --- Botón de Textura (solo bloques, siempre visible) ---
                if (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex != -1 && bloqueSeleccionadoIndex < (int)nivelEscenario->size()) {
                    Rectangle btnTextura = { panelX + (14.0f * scaleFactor), currentY + 4.0f * scaleFactor, panelAncho - (28.0f * scaleFactor), 28.0f * scaleFactor };
                    DrawRectangleRec(btnTextura, DARKBLUE);
                    DrawRectangleLines((int)btnTextura.x, (int)btnTextura.y, (int)btnTextura.width, (int)btnTextura.height, LIGHTGRAY);
                    DrawText("Cambiar Textura", (int)btnTextura.x + (10 * scaleFactor), (int)btnTextura.y + (6 * scaleFactor), (int)(12 * scaleFactor), WHITE);
                    EditorBlock& bloque = (*nivelEscenario)[bloqueSeleccionadoIndex];
                    if (bloque.hasTexture) {
                        DrawText(TextFormat("Texto: %s", bloque.texturePath), (int)panelX + (14 * scaleFactor), (int)btnTextura.y + btnTextura.height + 4.0f, (int)(10 * scaleFactor), LIGHTGRAY);
                    }
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mousePos, btnTextura)) {
                        char filePath[512] = "";
                        if (OpenImageDialog(filePath, sizeof(filePath))) {
                            if (bloque.hasTexture && bloque.texture.id != 0) UnloadTexture(bloque.texture);
                            strcpy(bloque.texturePath, filePath);
                            bloque.texture = LoadTexture(filePath);
                            if (bloque.texture.id != 0) {
                                bloque.hasTexture = true;
                                statusMessage = "Textura cargada correctamente";
                            }
                            else {
                                bloque.hasTexture = false;
                                bloque.texturePath[0] = '\0';
                                statusMessage = "Error al cargar textura";
                            }
                            statusMessageTime = GetTime();
                        }
                    }
                    currentY += 28.0f * scaleFactor + 8.0f * scaleFactor; // avanzar tras el botón de textura
                }

                // --- Botón "Asignar Script" (siempre visible para cualquier objeto seleccionado) ---
                            // --- Botones de Script (dos opciones) ---
                if (selectedType != OBJ_NONE) {
                    // Primer botón: "Nuevo Script Virtual"
                    Rectangle btnNewScript = { panelX + (14.0f * scaleFactor), currentY + 4.0f * scaleFactor, panelAncho - (28.0f * scaleFactor), 28.0f * scaleFactor };
                    DrawRectangleRec(btnNewScript, DARKPURPLE);
                    DrawRectangleLines((int)btnNewScript.x, (int)btnNewScript.y, (int)btnNewScript.width, (int)btnNewScript.height, YELLOW);
                    DrawText("Nuevo Script", (int)btnNewScript.x + (10 * scaleFactor), (int)btnNewScript.y + (6 * scaleFactor), (int)(12 * scaleFactor), WHITE);

                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mousePos, btnNewScript)) {
                        if (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex >= 0 && bloqueSeleccionadoIndex < (int)nivelEscenario->size()) {
                            EditorBlock& selBlock = (*nivelEscenario)[bloqueSeleccionadoIndex];
                            char nombreScript[64];
                            snprintf(nombreScript, sizeof(nombreScript), "bloque_%d", bloqueSeleccionadoIndex);
                            const char* virtualPath = CreateAndGetVirtualScriptPath(nombreScript);
                            strcpy_s(selBlock.scriptPath, sizeof(selBlock.scriptPath), virtualPath);
                            selBlock.hasScript = true;
                            const char* code = GetScriptBufferContent(virtualPath);
                            if (code) { strncpy(selBlock.scriptSource, code, sizeof(selBlock.scriptSource) - 1); selBlock.scriptSource[sizeof(selBlock.scriptSource) - 1] = '\0'; }
                            else selBlock.scriptSource[0] = '\0';
                        }
                        else if (selectedType == OBJ_PLAYER) {
                            const char* virtualPath = CreateAndGetVirtualScriptPath("jugador");
                            strcpy_s(miJugador.scriptPath, sizeof(miJugador.scriptPath), virtualPath);
                            miJugador.hasScript = true;
                            const char* code = GetScriptBufferContent(virtualPath);
                            if (code) { strncpy(miJugador.scriptSource, code, sizeof(miJugador.scriptSource) - 1); miJugador.scriptSource[sizeof(miJugador.scriptSource) - 1] = '\0'; }
                            else miJugador.scriptSource[0] = '\0';
                        }
                        else if (selectedType == OBJ_COIN) {
                            const char* virtualPath = CreateAndGetVirtualScriptPath("moneda");
                            strcpy_s(miMoneda.scriptPath, sizeof(miMoneda.scriptPath), virtualPath);
                            miMoneda.hasScript = true;
                            const char* code = GetScriptBufferContent(virtualPath);
                            if (code) { strncpy(miMoneda.scriptSource, code, sizeof(miMoneda.scriptSource) - 1); miMoneda.scriptSource[sizeof(miMoneda.scriptSource) - 1] = '\0'; }
                            else miMoneda.scriptSource[0] = '\0';
                        }
                        else if (selectedType == OBJ_ENEMY) {
                            const char* virtualPath = CreateAndGetVirtualScriptPath("enemigo");
                            strcpy_s(miEnemigo.scriptPath, sizeof(miEnemigo.scriptPath), virtualPath);
                            miEnemigo.hasScript = true;
                            const char* code = GetScriptBufferContent(virtualPath);
                            if (code) { strncpy(miEnemigo.scriptSource, code, sizeof(miEnemigo.scriptSource) - 1); miEnemigo.scriptSource[sizeof(miEnemigo.scriptSource) - 1] = '\0'; }
                            else miEnemigo.scriptSource[0] = '\0';
                        }
                        g_scriptState.open = true;
                        g_scriptState.isFocused = true;
                        showScriptView = true;
                    }

                    currentY += 28.0f * scaleFactor + 4.0f * scaleFactor;

                    // Segundo botón: "Cargar Script Existente..."
                    Rectangle btnLoadScript = { panelX + (14.0f * scaleFactor), currentY + 4.0f * scaleFactor, panelAncho - (28.0f * scaleFactor), 28.0f * scaleFactor };
                    DrawRectangleRec(btnLoadScript, DARKBLUE);
                    DrawRectangleLines((int)btnLoadScript.x, (int)btnLoadScript.y, (int)btnLoadScript.width, (int)btnLoadScript.height, LIGHTGRAY);
                    DrawText("Cargar Script...", (int)btnLoadScript.x + (10 * scaleFactor), (int)btnLoadScript.y + (6 * scaleFactor), (int)(12 * scaleFactor), WHITE);

                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mousePos, btnLoadScript)) {
                        char filePath[512] = "";
                        if (OpenScriptDialog(filePath, sizeof(filePath))) {
                            std::ifstream in(filePath);
                            if (in.is_open()) {
                                std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                                in.close();
                                std::string clean = SanitizeScript(content);
                                if (selectedType == OBJ_BLOCK && bloqueSeleccionadoIndex >= 0 && bloqueSeleccionadoIndex < (int)nivelEscenario->size()) {
                                    EditorBlock& selBlock = (*nivelEscenario)[bloqueSeleccionadoIndex];
                                    strncpy(selBlock.scriptSource, clean.c_str(), sizeof(selBlock.scriptSource) - 1);
                                    selBlock.scriptSource[sizeof(selBlock.scriptSource) - 1] = '\0';
                                    selBlock.hasScript = true;
                                    strcpy_s(selBlock.scriptPath, sizeof(selBlock.scriptPath), filePath);
                                }
                                else if (selectedType == OBJ_PLAYER) {
                                    strncpy(miJugador.scriptSource, clean.c_str(), sizeof(miJugador.scriptSource) - 1);
                                    miJugador.scriptSource[sizeof(miJugador.scriptSource) - 1] = '\0';
                                    miJugador.hasScript = true;
                                    strcpy_s(miJugador.scriptPath, sizeof(miJugador.scriptPath), filePath);
                                }
                                else if (selectedType == OBJ_COIN) {
                                    strncpy(miMoneda.scriptSource, clean.c_str(), sizeof(miMoneda.scriptSource) - 1);
                                    miMoneda.scriptSource[sizeof(miMoneda.scriptSource) - 1] = '\0';
                                    miMoneda.hasScript = true;
                                    strcpy_s(miMoneda.scriptPath, sizeof(miMoneda.scriptPath), filePath);
                                }
                                else if (selectedType == OBJ_ENEMY) {
                                    strncpy(miEnemigo.scriptSource, clean.c_str(), sizeof(miEnemigo.scriptSource) - 1);
                                    miEnemigo.scriptSource[sizeof(miEnemigo.scriptSource) - 1] = '\0';
                                    miEnemigo.hasScript = true;
                                    strcpy_s(miEnemigo.scriptPath, sizeof(miEnemigo.scriptPath), filePath);
                                }
                                LogMessage("[SCRIPT] Script cargado desde archivo y asignado", GREEN);
                            }
                            else {
                                LogMessage("[ERROR] No se pudo leer el archivo de script", RED);
                            }
                        }
                    }
                    currentY += 28.0f * scaleFactor + 8.0f * scaleFactor;
                }
            }
            EndScissorMode();
        }

        // HUD info
        if (currentMode == MODE_EDITOR) {
            if (selectedType != OBJ_NONE) {
                const char* nombreObj = "Objeto";
                if (selectedType == OBJ_BLOCK) nombreObj = "Bloque Escenario";
                else if (selectedType == OBJ_PLAYER) nombreObj = "Jugador";
                else if (selectedType == OBJ_COIN) nombreObj = "Moneda";
                else if (selectedType == OBJ_ENEMY) nombreObj = "Enemigo";
                DrawText(TextFormat("SEL: %s", nombreObj), (int)hudRect.x + (10 * scaleFactor), (int)hudRect.y + (8 * scaleFactor), fSizeTitle, YELLOW);
                DrawText("C: Color | DEL/BKSP: Borrar", (int)hudRect.x + (10 * scaleFactor), (int)hudRect.y + (28 * scaleFactor), fSizeSmall, LIGHTGRAY);
                DrawText("F11: Fullscreen | [TAB]: Probar", (int)hudRect.x + (10 * scaleFactor), (int)hudRect.y + (48 * scaleFactor), fSizeSmall, GREEN);
            }
            else {
                DrawText(TextFormat("ESCENA ACTUAL: %i (Total: %i)", escenaActualIndex + 1, (int)listaEscenas.size()), (int)hudRect.x + (10 * scaleFactor), (int)hudRect.y + (8 * scaleFactor), fSizeText, YELLOW);
                DrawText("Usa menú izq para cambiar escenas", (int)hudRect.x + (10 * scaleFactor), (int)hudRect.y + (28 * scaleFactor), fSizeSmall, LIGHTGRAY);
                DrawText("F11: Fullscreen | [TAB]: Probar", (int)hudRect.x + (10 * scaleFactor), (int)hudRect.y + (48 * scaleFactor), fSizeSmall, GREEN);
            }
        }
        else {
            DrawText(TextFormat("MODO JUEGO - Escena: %i | W,A,S,D | SPACE: Saltar", escenaActualIndex), (int)hudRect.x + (12 * scaleFactor), (int)hudRect.y + (14 * scaleFactor), fSizeText, LIGHTGRAY);
            DrawText("F11: Pantalla Completa | [TAB]: Editar | ESC: Liberar cursor", (int)hudRect.x + (12 * scaleFactor), (int)hudRect.y + (34 * scaleFactor), fSizeText, GREEN);
            DrawText(TextFormat("PUNTOS: %03i/50 | VIDAS: %i", miJugador.puntos, miJugador.vidas), (int)hudRect.x + (12 * scaleFactor), (int)hudRect.y + (58 * scaleFactor), (int)(14 * scaleFactor), GREEN);
        }

        // Notificación
        if (statusMessage != nullptr && (GetTime() - statusMessageTime) < 2.5) {
            DrawRectangle(10, 10, 500, 40, Fade(BLACK, 0.7f));
            DrawText(statusMessage, 20, 20, 20, RAYWHITE);
        }
        else {
            statusMessage = nullptr;
        }

        // Menú contextual y modal
        if (showContextMenu && currentMode == MODE_EDITOR && !showNodeTypeModal) {
            float menuX = contextMenuPos.x;
            float menuY = contextMenuPos.y;
            float menuWidth = 160.0f * scaleFactor;
            float itemHeight = 25.0f * scaleFactor;
            bool isVacío = (contextObjType == OBJ_NONE);
            std::vector<const char*> items;

            if (isVacío) {
                if (hasCopiedBlock) items.push_back("Pegar");
                items.push_back("Añadir Nodo...");
            }
            else {
                items.push_back("Copiar");
                items.push_back("Cortar");
                if (hasCopiedBlock) items.push_back("Pegar");
                items.push_back("Borrar");
                items.push_back("Duplicar");
                items.push_back("Crear Script Virtual");
                items.push_back("Cargar Script Existente...");
            }

            int itemCount = (int)items.size();
            // Fondo del menú con altura exacta
            DrawRectangle(menuX, menuY, menuWidth, itemHeight * itemCount, Fade(DARKGRAY, 0.95f));
            DrawRectangleLines(menuX, menuY, menuWidth, itemHeight * itemCount, GRAY);

            for (int i = 0; i < itemCount; i++) {
                Rectangle itemRect = { menuX, menuY + i * itemHeight, menuWidth, itemHeight };
                bool hovered = CheckCollisionPointRec(mousePos, itemRect);
                bool disabled = false;
                if (isVacío) {
                    disabled = (i == 0 && !hasCopiedBlock && items[0] != "Pegar");
                }
                else {
                    if (items[i] == "Pegar" && !hasCopiedBlock) disabled = true;
                }
                Color bgColor = hovered && !disabled ? SKYBLUE : Fade(DARKGRAY, 0.9f);
                DrawRectangleRec(itemRect, bgColor);
                Color textColor = disabled ? GRAY : (hovered ? BLACK : WHITE);
                DrawText(items[i], menuX + 10, menuY + i * itemHeight + 5, 12 * scaleFactor, textColor);
            }
        }

        if (showNodeTypeModal && currentMode == MODE_EDITOR) {
            float modalW = 180.0f * scaleFactor;
            float modalH = 170.0f * scaleFactor;
            float modalX = (anchoActualVentana - modalW) / 2.0f;
            float modalY = (altoActualVentana - modalH) / 2.0f;
            DrawRectangle(modalX, modalY, modalW, modalH, Fade(DARKGRAY, 0.95f));
            DrawRectangleLines(modalX, modalY, modalW, modalH, GOLD);
            DrawText("Elegir tipo de nodo", modalX + 10 * scaleFactor, modalY + 5 * scaleFactor, 10 * scaleFactor, YELLOW);
            float itemHeight = 28.0f * scaleFactor;
            const char* typeNames[] = { "Cubo", "Esfera", "Cilindro", "Cono", "Portal" };
            for (int i = 0; i < 5; i++) {
                Rectangle itemRect = { modalX + 10.0f * scaleFactor, modalY + 25.0f * scaleFactor + i * itemHeight, modalW - 20.0f * scaleFactor, itemHeight };
                bool hovered = CheckCollisionPointRec(mousePos, itemRect);
                DrawRectangleRec(itemRect, hovered ? SKYBLUE : DARKGRAY);
                DrawText(typeNames[i], modalX + 20 * scaleFactor, modalY + 25.0f * scaleFactor + i * itemHeight + 4 * scaleFactor, 11 * scaleFactor, WHITE);
            }
        }

        // Botón de modo
        Color colorBoton = (currentMode == MODE_EDITOR) ? DARKGREEN : MAROON;
        DrawRectangleRec(botonModo, colorBoton);
        DrawRectangleLines((int)botonModo.x, (int)botonModo.y, (int)botonModo.width, (int)botonModo.height, WHITE);
        int fSizeButton = (int)(12 * scaleFactor);
        if (currentMode == MODE_EDITOR) {
            DrawText("PLAY [TAB]", (int)botonModo.x + (28 * scaleFactor), (int)botonModo.y + (11 * scaleFactor), fSizeButton, WHITE);
        }
        else {
            DrawText("EDIT [TAB]", (int)botonModo.x + (30 * scaleFactor), (int)botonModo.y + (11 * scaleFactor), fSizeButton, WHITE);
        }

        DrawFPS(10, (int)GetScreenHeight() - 25);

        // Botón "Script" (esquina superior izquierda, encima del panel izquierdo)
        Rectangle botonScript = { 10.0f * scaleFactor, 10.0f * scaleFactor, 80.0f * scaleFactor, 38.0f * scaleFactor };
        DrawRectangleRec(botonScript, showScriptView ? DARKGRAY : DARKBLUE);
        DrawRectangleLines((int)botonScript.x, (int)botonScript.y, (int)botonScript.width, (int)botonScript.height, WHITE);
        DrawText("Script", (int)botonScript.x + 12 * scaleFactor, (int)botonScript.y + 10 * scaleFactor, 14 * scaleFactor, WHITE);
        if (CheckCollisionPointRec(mousePos, botonScript) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (showScriptView) FlushCurrentScriptBuffer();
            showScriptView = !showScriptView;
            if (showScriptView) {
                g_scriptState.isFocused = true;
                g_scriptState.open = true;
                LogMessage("[SCRIPT] Editor de scripts abierto (viewport)", LIGHTGRAY);
            }
            else {
                g_scriptState.isFocused = false;
                g_scriptState.open = false;
                LogMessage("[SCRIPT] Editor de scripts cerrado", LIGHTGRAY);
            }
        }

        // Consola integrada
        DrawConsole(consoleRect, scaleFactor);

        // --- DEPURACIÓN ---
        float debugFontSize = 13.0f * scaleFactor;
        float debugHeaderHeight = 22.0f * scaleFactor;
        float debugTextAreaHeight = consoleRect.height - debugHeaderHeight - 8 * scaleFactor;
        int debugLineCount = (int)(debugTextAreaHeight / (debugFontSize + 3));
        int debugTotalLines = (int)logMessages.size();

        DebugData debugData;
        debugData.player = &miJugador;
        debugData.camera = &miCamara;
        debugData.enemy = &miEnemigo;
        debugData.coin = &miMoneda;
        debugData.scenes = &listaEscenas;
        debugData.currentSceneIndex = escenaActualIndex;
        debugData.mode = currentMode;
        debugData.consoleOffset = consoleScrollOffset;
        debugData.totalLines = debugTotalLines;
        debugData.lineCount = debugLineCount;
        debugData.consoleRect = consoleRect;
        debugData.camDistancia = camDistancia;
        debugData.enElSuelo = enElSuelo;

        DrawDebugInfo(debugData);

        EndDrawing();
    }

    // Liberar recursos
    for (auto& escena : listaEscenas) {
        for (auto& bloque : escena) {
            if (bloque.hasTexture && bloque.texture.id != 0) UnloadTexture(bloque.texture);
        }
    }

    for (auto& escena : listaEscenas)
        for (auto& bloque : escena)
            if (bloque.hasCustomModel && bloque.customModel.meshCount > 0)
                UnloadModel(bloque.customModel);
    UnloadCachedCubeModels();
    UnloadTexture(texSV);
    UnloadTexture(texHue);
    UnloadRenderTexture(previewTarget);
    UnloadModel(modeloCielo);
    CloseWindow();
    return 0;
}