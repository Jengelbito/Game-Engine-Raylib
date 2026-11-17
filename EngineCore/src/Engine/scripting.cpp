#define _CRT_SECURE_NO_WARNINGS

// ------------------------------------------------------------
// Declaraciones manuales de la API de Windows (evita incluir
// <windows.h> y <commdlg.h> que chocan con Raylib)
// ------------------------------------------------------------
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
}
#define OFN_EXPLORER 0x00080000
#define OFN_FILEMUSTEXIST 0x00001000
#define OFN_HIDEREADONLY 0x00000004
#define OFN_OVERWRITEPROMPT 0x00000002
#define OFN_PATHMUSTEXIST 0x00000800
#endif

// ------------------------------------------------------------
// Cabeceras del proyecto
// ------------------------------------------------------------
#include "scripting.h"   // Ya incluye raylib.h internamente
#include <cstdio>
#include <cstring>
#include <string>
#include <cmath>
#include <algorithm>
#include <vector>
#include <cctype>
#include <fstream>
#include <unordered_map>

#include "raylib.h"
#include "raymath.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

extern void LogMessage(const std::string& text, Color color);

// ------------------------------------------------------------
// Forward declaration del nodo seleccionado del motor
// (debe existir en main.cpp: EngineNode* selectedNode = nullptr;)
// ------------------------------------------------------------
extern struct EngineNode {
    std::string name;
} *selectedNode;

ScriptEditorState g_scriptState;
std::string g_updateScriptCode;
static bool arrastrandoScroll = false;

// ============================================================================
// ESTRUCTURAS PARA DESHACER / REHACER
// ============================================================================
static const size_t MAX_HISTORY = 30;
struct TextoEstado {
    char buffer[8192];
    int cursorPos;
    int scrollOffset;
    int selectionStart;
    int selectionEnd;
};
static std::vector<TextoEstado> undoStack;
static std::vector<TextoEstado> redoStack;

// ============================================================================
// GESTIÓN DE PESTAÑAS DE SCRIPTS (Sub-barra lateral)
// ============================================================================
static std::vector<std::string> openScriptsTabs;          // rutas reales o "virtual:..."
static int activeScriptTab = -1;                          // índice activo (-1 = ninguno)
static std::unordered_map<std::string, std::string> scriptBuffers; // contenido por ruta/clave
static std::unordered_map<std::string, std::vector<TextoEstado>> scriptUndoStacks;
static std::unordered_map<std::string, std::vector<TextoEstado>> scriptRedoStacks;
static std::string lastSyncedNodePath;                    // última ruta sincronizada

// Mapas para manejar scripts virtuales (aún no guardados en disco)
static std::unordered_map<std::string, bool> scriptIsVirtual;
static std::unordered_map<std::string, std::string> scriptVirtualDisplayName;

// ----------------------------------------------------------------------------
// Funciones de gestión de pestañas
// ----------------------------------------------------------------------------
static void SaveCurrentBuffer() {
    if (activeScriptTab >= 0 && activeScriptTab < (int)openScriptsTabs.size()) {
        const std::string& path = openScriptsTabs[activeScriptTab];
        scriptBuffers[path] = std::string(g_scriptState.codeBuffer);
    }
}

static void LoadBuffer(const std::string& path) {
    auto it = scriptBuffers.find(path);
    if (it != scriptBuffers.end()) {
        strcpy_s(g_scriptState.codeBuffer, sizeof(g_scriptState.codeBuffer), it->second.c_str());
    }
    else {
        g_scriptState.codeBuffer[0] = '\0';
    }
    g_scriptState.cursorPos = (int)strlen(g_scriptState.codeBuffer);
    g_scriptState.scrollOffset = 0;
    g_scriptState.selectionStart = -1;
    g_scriptState.selectionEnd = -1;
    // Cargar historial específico de este script
    undoStack = scriptUndoStacks[path];
    redoStack = scriptRedoStacks[path];
}

static void SaveCurrentUndoHistory() {
    if (activeScriptTab >= 0 && activeScriptTab < (int)openScriptsTabs.size()) {
        const std::string& path = openScriptsTabs[activeScriptTab];
        scriptUndoStacks[path] = undoStack;
        scriptRedoStacks[path] = redoStack;
    }
}

void FlushCurrentScriptBuffer() {
    SaveCurrentBuffer();
}

// Añadir un script a la lista y activarlo (si ya existe, solo lo activa)
void AddScriptTab(const char* path) {
    if (!path || path[0] == '\0') return;
    std::string spath(path);
    // Si ya existe, activarlo
    for (int i = 0; i < (int)openScriptsTabs.size(); ++i) {
        if (openScriptsTabs[i] == spath) {
            if (activeScriptTab != i) {
                SaveCurrentBuffer();
                SaveCurrentUndoHistory();
                activeScriptTab = i;
                LoadBuffer(spath);
            }
            return;
        }
    }
    // Nuevo: cargar el archivo desde disco o crear virtual vacío
    std::string content;
    bool isVirtual = (strncmp(path, "virtual:", 8) == 0);
    if (isVirtual) {
        content = "";
        scriptIsVirtual[spath] = true;
        // El nombre para mostrar es lo que sigue a "virtual:"
        scriptVirtualDisplayName[spath] = spath.substr(8);
    }
    else {
        std::ifstream in(spath);
        if (in.is_open()) {
            content = std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();
            content = SanitizeScript(content);
        }
        else {
            content = "";
        }
        scriptIsVirtual[spath] = false;
        // Nombre para mostrar se calculará bajo demanda
    }
    openScriptsTabs.push_back(spath);
    scriptBuffers[spath] = content;
    int newIndex = (int)openScriptsTabs.size() - 1;
    if (activeScriptTab >= 0) {
        SaveCurrentBuffer();
        SaveCurrentUndoHistory();
    }
    activeScriptTab = newIndex;
    LoadBuffer(spath);
    // Inicializar historial vacío para este script
    scriptUndoStacks[spath].clear();
    scriptRedoStacks[spath].clear();
}

// Cerrar un script (sin preguntar guardar, simplificado)
void CloseScriptTab(int index) {
    if (index < 0 || index >= (int)openScriptsTabs.size()) return;
    std::string path = openScriptsTabs[index];
    openScriptsTabs.erase(openScriptsTabs.begin() + index);
    scriptBuffers.erase(path);
    scriptUndoStacks.erase(path);
    scriptRedoStacks.erase(path);
    scriptIsVirtual.erase(path);
    scriptVirtualDisplayName.erase(path);
    if (openScriptsTabs.empty()) {
        activeScriptTab = -1;
        g_scriptState.codeBuffer[0] = '\0';
        g_scriptState.cursorPos = 0;
        g_scriptState.scrollOffset = 0;
        g_scriptState.selectionStart = -1;
        g_scriptState.selectionEnd = -1;
        undoStack.clear();
        redoStack.clear();
    }
    else {
        if (activeScriptTab >= index) {
            activeScriptTab--;
            if (activeScriptTab < 0) activeScriptTab = 0;
        }
        if (activeScriptTab >= (int)openScriptsTabs.size()) activeScriptTab = (int)openScriptsTabs.size() - 1;
        SaveCurrentBuffer();
        SaveCurrentUndoHistory();
        LoadBuffer(openScriptsTabs[activeScriptTab]);
    }
}

// Activar una pestaña por índice
void SetActiveScript(int index) {
    if (index < 0 || index >= (int)openScriptsTabs.size()) return;
    if (index == activeScriptTab) return;
    SaveCurrentBuffer();
    SaveCurrentUndoHistory();
    activeScriptTab = index;
    LoadBuffer(openScriptsTabs[index]);
}

// Sincronizar con el nodo seleccionado del motor (auto-abrir script)
void SyncEditorWithNode(const char* scriptPath) {
    if (!scriptPath || scriptPath[0] == '\0') {
        lastSyncedNodePath.clear();
        return;
    }
    std::string spath(scriptPath);
    if (spath == lastSyncedNodePath) return; // ya está sincronizado
    lastSyncedNodePath = spath;
    AddScriptTab(scriptPath); // lo abre o activa si ya existe
}

// ------------------------------------------------------------
// NUEVO: Crear script virtual en RAM basado en el nodo seleccionado
// ------------------------------------------------------------
void CreateVirtualScript(const char* suggestedName = nullptr) {
    std::string fileName;
    if (suggestedName && suggestedName[0] != '\0') {
        fileName = std::string("new_script_") + suggestedName + ".lua";
    }
    else {
        fileName = "new_script.lua";
    }
    std::string virtualPath = "virtual:" + fileName;
    AddScriptTab(virtualPath.c_str());
}

static void GuardarEstadoHistorial() {
    TextoEstado estado;
    memcpy(estado.buffer, g_scriptState.codeBuffer, sizeof(estado.buffer));
    estado.cursorPos = g_scriptState.cursorPos;
    estado.scrollOffset = g_scriptState.scrollOffset;
    estado.selectionStart = g_scriptState.selectionStart;
    estado.selectionEnd = g_scriptState.selectionEnd;
    undoStack.push_back(estado);
    if (undoStack.size() > MAX_HISTORY) undoStack.erase(undoStack.begin());
    redoStack.clear();
    SaveCurrentUndoHistory();
}

static void AplicarEstado(const TextoEstado& estado) {
    memcpy(g_scriptState.codeBuffer, estado.buffer, sizeof(estado.buffer));
    g_scriptState.cursorPos = estado.cursorPos;
    g_scriptState.scrollOffset = estado.scrollOffset;
    g_scriptState.selectionStart = estado.selectionStart;
    g_scriptState.selectionEnd = estado.selectionEnd;
}

// ============================================================================
// FUNCIONES AUXILIARES PARA EL BUFFER DE TEXTO
// ============================================================================
static int GetLineCount(const char* buffer) {
    int lines = 1;
    for (int i = 0; buffer[i]; i++) {
        if (buffer[i] == '\n') lines++;
    }
    return lines;
}

static int GetLineStart(const char* buffer, int line) {
    int currentLine = 0;
    for (int i = 0; buffer[i]; i++) {
        if (currentLine == line) return i;
        if (buffer[i] == '\n') currentLine++;
    }
    return (int)strlen(buffer);
}

static int GetLineEnd(const char* buffer, int line) {
    int currentLine = 0;
    for (int i = 0; buffer[i]; i++) {
        if (currentLine == line && buffer[i] == '\n') return i;
        if (buffer[i] == '\n') currentLine++;
    }
    return (int)strlen(buffer);
}

static int GetLineFromPos(const char* buffer, int pos) {
    int line = 0;
    for (int i = 0; i < pos && buffer[i]; i++) {
        if (buffer[i] == '\n') line++;
    }
    return line;
}

static int GetColFromPos(const char* buffer, int pos) {
    int line = GetLineFromPos(buffer, pos);
    int start = GetLineStart(buffer, line);
    return pos - start;
}

static void InsertChar(char* buffer, int pos, char ch) {
    int len = (int)strlen(buffer);
    if (len >= 8191) return;
    for (int i = len; i >= pos; i--) {
        buffer[i + 1] = buffer[i];
    }
    buffer[pos] = ch;
}

static void DeleteChar(char* buffer, int pos) {
    int len = (int)strlen(buffer);
    if (pos < 0 || pos >= len) return;
    for (int i = pos; i < len; i++) {
        buffer[i] = buffer[i + 1];
    }
}

static void DeleteRange(char* buffer, int start, int end) {
    if (start > end) std::swap(start, end);
    int len = (int)strlen(buffer);
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start == end) return;
    for (int i = end; i <= len; i++) {
        buffer[start + (i - end)] = buffer[i];
    }
}

static void ClearSelection() {
    g_scriptState.selectionStart = -1;
    g_scriptState.selectionEnd = -1;
}

static bool HasSelection() {
    return g_scriptState.selectionStart != -1 && g_scriptState.selectionEnd != -1 &&
        g_scriptState.selectionStart != g_scriptState.selectionEnd;
}

static std::string GetSelectedText() {
    if (!HasSelection()) return "";
    int start = std::min(g_scriptState.selectionStart, g_scriptState.selectionEnd);
    int end = std::max(g_scriptState.selectionStart, g_scriptState.selectionEnd);
    return std::string(g_scriptState.codeBuffer + start, g_scriptState.codeBuffer + end);
}

static void ReplaceSelection(const char* text) {
    if (!HasSelection()) return;
    int start = std::min(g_scriptState.selectionStart, g_scriptState.selectionEnd);
    int end = std::max(g_scriptState.selectionStart, g_scriptState.selectionEnd);
    DeleteRange(g_scriptState.codeBuffer, start, end);
    int len = (int)strlen(text);
    for (int i = 0; i < len; i++) {
        InsertChar(g_scriptState.codeBuffer, start + i, text[i]);
    }
    g_scriptState.cursorPos = start + len;
    ClearSelection();
}

std::string SanitizeScript(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (unsigned char c : input) {
        if (c == '\n') output.push_back((char)c);
        else if (c == '\t') output.append("    ");
        else if (c >= 32 && c <= 126) output.push_back((char)c);
    }
    std::string result;
    result.reserve(output.size());
    size_t pos = 0;
    while (pos < output.size()) {
        size_t end = output.find('\n', pos);
        if (end == std::string::npos) end = output.size();
        std::string line = output.substr(pos, end - pos);
        size_t lastNonSpace = line.find_last_not_of(" ");
        if (lastNonSpace != std::string::npos) line = line.substr(0, lastNonSpace + 1);
        else line.clear();
        result += line;
        if (end < output.size()) result += '\n';
        pos = end + 1;
    }
    return result;
}

// ============================================================================
// SISTEMA DE AUTOCOMPLETADO
// ============================================================================
struct Suggestion {
    std::string text;
    std::string description;
};

static std::vector<Suggestion> luaKeywords = {
    {"and", "keyword"}, {"break", "keyword"}, {"do", "keyword"}, {"else", "keyword"},
    {"elseif", "keyword"}, {"end", "keyword"}, {"false", "keyword"}, {"for", "keyword"},
    {"function", "keyword"}, {"if", "keyword"}, {"in", "keyword"}, {"local", "keyword"},
    {"nil", "keyword"}, {"not", "keyword"}, {"or", "keyword"}, {"repeat", "keyword"},
    {"return", "keyword"}, {"then", "keyword"}, {"true", "keyword"}, {"until", "keyword"},
    {"while", "keyword"}
};

static std::vector<Suggestion> apiFunctions = {
    {"print", "Imprime en consola"},
    {"GetFrameTime", "Tiempo del frame"},
    {"GetTime", "Tiempo global"},
    {"GetBlockCount", "Número de bloques"},
    {"CreateBlock", "Crea un bloque"},
    {"MoveBlock", "Mueve un bloque"},
    {"GetBlockPos", "Posición de un bloque"},
    {"SetBlockColor", "Cambia color de un bloque"},
    {"RemoveBlock", "Elimina un bloque"},
    {"GetBlockSize", "Tamaño de un bloque"},
    {"SetBlockSize", "Cambia tamaño de un bloque"},
    {"GetPlayerPos", "Posición del jugador"},
    {"SetPlayerPos", "Mueve al jugador"},
    {"GetPlayerVidas", "Vidas del jugador"},
    {"SetPlayerVidas", "Cambia vidas"},
    {"GetPlayerPuntos", "Puntos del jugador"},
    {"SetPlayerPuntos", "Cambia puntos"},
    {"GetPlayerRadio", "Radio del jugador"},
    {"SetPlayerRadio", "Cambia radio"},
    {"GetPlayerAltura", "Altura del jugador"},
    {"SetPlayerAltura", "Cambia altura"},
    {"GetCameraPos", "Posición de cámara"},
    {"SetCameraPos", "Mueve cámara"},
    {"GetCameraTarget", "Target de cámara"},
    {"SetCameraTarget", "Cambia target"},
    {"GetCameraDistance", "Distancia de cámara"},
    {"SetCameraDistance", "Cambia distancia"},
    {"GetCoinPos", "Posición de moneda"},
    {"SetCoinPos", "Mueve moneda"},
    {"GetCoinRadio", "Radio de moneda"},
    {"SetCoinRadio", "Cambia radio"},
    {"IsCoinActive", "Moneda activa?"},
    {"SetCoinActive", "Activa/desactiva moneda"},
    {"GetEnemyPos", "Posición de enemigo"},
    {"SetEnemyPos", "Mueve enemigo"},
    {"GetEnemySize", "Tamaño de enemigo"},
    {"SetEnemySize", "Cambia tamaño"},
    {"GetSceneCount", "Número de escenas"},
    {"GetCurrentScene", "Escena actual"},
    {"SwitchScene", "Cambia de escena"},
    {"LogMessage", "Mensaje en consola del motor"}
};

static std::vector<Suggestion> allSuggestions;

static void BuildSuggestionList() {
    if (!allSuggestions.empty()) return;
    allSuggestions.insert(allSuggestions.end(), luaKeywords.begin(), luaKeywords.end());
    allSuggestions.insert(allSuggestions.end(), apiFunctions.begin(), apiFunctions.end());
    std::sort(allSuggestions.begin(), allSuggestions.end(),
        [](const Suggestion& a, const Suggestion& b) { return a.text < b.text; });
}

static std::vector<Suggestion> GetSuggestions(const std::string& prefix) {
    std::vector<Suggestion> result;
    BuildSuggestionList();
    for (const auto& s : allSuggestions) {
        if (s.text.find(prefix) == 0) result.push_back(s);
    }
    return result;
}

// ============================================================================
// FUNCIONES DE LUA
// ============================================================================
static int lua_Print(lua_State* L) {
    int nargs = lua_gettop(L);
    std::string output;
    for (int i = 1; i <= nargs; i++) {
        const char* s = lua_tostring(L, i);
        if (s) output += s;
        if (i < nargs) output += "\t";
    }
    AddScriptOutput(output);
    return 0;
}

static int lua_GetFrameTime(lua_State* L) {
    lua_pushnumber(L, GetFrameTime());
    return 1;
}

static int lua_GetBlockCount(lua_State* L) {
    if (!g_scriptState.scenes || !g_scriptState.currentSceneIndex) {
        lua_pushinteger(L, 0);
        return 1;
    }
    int idx = *g_scriptState.currentSceneIndex;
    if (idx < 0 || idx >= (int)g_scriptState.scenes->size()) {
        lua_pushinteger(L, 0);
        return 1;
    }
    int count = (int)(*g_scriptState.scenes)[idx].size();
    lua_pushinteger(L, count);
    return 1;
}

static int lua_CreateBlock(lua_State* L) {
    if (!g_scriptState.scenes || !g_scriptState.currentSceneIndex) {
        lua_pushboolean(L, 0);
        return 1;
    }
    int idx = *g_scriptState.currentSceneIndex;
    if (idx < 0 || idx >= (int)g_scriptState.scenes->size()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float z = (float)luaL_checknumber(L, 3);
    float sx = (float)luaL_checknumber(L, 4);
    float sy = (float)luaL_checknumber(L, 5);
    float sz = (float)luaL_checknumber(L, 6);
    int r = (int)luaL_checkinteger(L, 7);
    int g = (int)luaL_checkinteger(L, 8);
    int b = (int)luaL_checkinteger(L, 9);
    EditorBlock block;
    block.position = { x, y, z };
    block.size = { sx, sy, sz };
    block.color = { (unsigned char)r, (unsigned char)g, (unsigned char)b, 255 };
    block.shapeType = SHAPE_CUBE;
    block.targetScene = 0;
    block.hasTexture = false;
    block.texturePath[0] = '\0';
    block.texture.id = 0;
    (*g_scriptState.scenes)[idx].push_back(block);
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_MoveBlock(lua_State* L) {
    if (!g_scriptState.scenes || !g_scriptState.currentSceneIndex) { lua_pushboolean(L, 0); return 1; }
    int idx = *g_scriptState.currentSceneIndex;
    if (idx < 0 || idx >= (int)g_scriptState.scenes->size()) { lua_pushboolean(L, 0); return 1; }
    int blockIndex = (int)luaL_checkinteger(L, 1) - 1;
    if (blockIndex < 0 || blockIndex >= (int)(*g_scriptState.scenes)[idx].size()) { lua_pushboolean(L, 0); return 1; }
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float z = (float)luaL_checknumber(L, 4);
    (*g_scriptState.scenes)[idx][blockIndex].position = { x, y, z };
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_GetBlockPos(lua_State* L) {
    if (!g_scriptState.scenes || !g_scriptState.currentSceneIndex) { lua_pushnil(L); return 1; }
    int idx = *g_scriptState.currentSceneIndex;
    if (idx < 0 || idx >= (int)g_scriptState.scenes->size()) { lua_pushnil(L); return 1; }
    int blockIndex = (int)luaL_checkinteger(L, 1) - 1;
    if (blockIndex < 0 || blockIndex >= (int)(*g_scriptState.scenes)[idx].size()) { lua_pushnil(L); return 1; }
    Vector3 pos = (*g_scriptState.scenes)[idx][blockIndex].position;
    lua_newtable(L);
    lua_pushnumber(L, pos.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, pos.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, pos.z); lua_setfield(L, -2, "z");
    return 1;
}

static int lua_SetBlockColor(lua_State* L) {
    if (!g_scriptState.scenes || !g_scriptState.currentSceneIndex) { lua_pushboolean(L, 0); return 1; }
    int idx = *g_scriptState.currentSceneIndex;
    if (idx < 0 || idx >= (int)g_scriptState.scenes->size()) { lua_pushboolean(L, 0); return 1; }
    int blockIndex = (int)luaL_checkinteger(L, 1) - 1;
    if (blockIndex < 0 || blockIndex >= (int)(*g_scriptState.scenes)[idx].size()) { lua_pushboolean(L, 0); return 1; }
    int r = (int)luaL_checkinteger(L, 2);
    int g = (int)luaL_checkinteger(L, 3);
    int b = (int)luaL_checkinteger(L, 4);
    (*g_scriptState.scenes)[idx][blockIndex].color = { (unsigned char)r, (unsigned char)g, (unsigned char)b, 255 };
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_RemoveBlock(lua_State* L) {
    if (!g_scriptState.scenes || !g_scriptState.currentSceneIndex) { lua_pushboolean(L, 0); return 1; }
    int idx = *g_scriptState.currentSceneIndex;
    if (idx < 0 || idx >= (int)g_scriptState.scenes->size()) { lua_pushboolean(L, 0); return 1; }
    int blockIndex = (int)luaL_checkinteger(L, 1) - 1;
    if (blockIndex < 0 || blockIndex >= (int)(*g_scriptState.scenes)[idx].size()) { lua_pushboolean(L, 0); return 1; }
    (*g_scriptState.scenes)[idx].erase((*g_scriptState.scenes)[idx].begin() + blockIndex);
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_GetBlockSize(lua_State* L) {
    if (!g_scriptState.scenes || !g_scriptState.currentSceneIndex) { lua_pushnil(L); return 1; }
    int idx = *g_scriptState.currentSceneIndex;
    if (idx < 0 || idx >= (int)g_scriptState.scenes->size()) { lua_pushnil(L); return 1; }
    int blockIndex = (int)luaL_checkinteger(L, 1) - 1;
    if (blockIndex < 0 || blockIndex >= (int)(*g_scriptState.scenes)[idx].size()) { lua_pushnil(L); return 1; }
    Vector3 size = (*g_scriptState.scenes)[idx][blockIndex].size;
    lua_newtable(L);
    lua_pushnumber(L, size.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, size.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, size.z); lua_setfield(L, -2, "z");
    return 1;
}

static int lua_SetBlockSize(lua_State* L) {
    if (!g_scriptState.scenes || !g_scriptState.currentSceneIndex) { lua_pushboolean(L, 0); return 1; }
    int idx = *g_scriptState.currentSceneIndex;
    if (idx < 0 || idx >= (int)g_scriptState.scenes->size()) { lua_pushboolean(L, 0); return 1; }
    int blockIndex = (int)luaL_checkinteger(L, 1) - 1;
    if (blockIndex < 0 || blockIndex >= (int)(*g_scriptState.scenes)[idx].size()) { lua_pushboolean(L, 0); return 1; }
    float sx = (float)luaL_checknumber(L, 2);
    float sy = (float)luaL_checknumber(L, 3);
    float sz = (float)luaL_checknumber(L, 4);
    if (sx < 0.1f) sx = 0.1f;
    if (sy < 0.1f) sy = 0.1f;
    if (sz < 0.1f) sz = 0.1f;
    (*g_scriptState.scenes)[idx][blockIndex].size = { sx, sy, sz };
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_GetPlayerPos(lua_State* L) {
    if (!g_scriptState.player) { lua_pushnil(L); return 1; }
    Vector3 pos = g_scriptState.player->posicion;
    lua_newtable(L);
    lua_pushnumber(L, pos.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, pos.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, pos.z); lua_setfield(L, -2, "z");
    return 1;
}

static int lua_SetPlayerPos(lua_State* L) {
    if (!g_scriptState.player) { lua_pushboolean(L, 0); return 1; }
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float z = (float)luaL_checknumber(L, 3);
    g_scriptState.player->posicion = { x, y, z };
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_GetPlayerVidas(lua_State* L) {
    if (!g_scriptState.player) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, g_scriptState.player->vidas);
    return 1;
}

static int lua_SetPlayerVidas(lua_State* L) {
    if (!g_scriptState.player) { lua_pushboolean(L, 0); return 1; }
    int v = (int)luaL_checkinteger(L, 1);
    g_scriptState.player->vidas = v;
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_GetPlayerPuntos(lua_State* L) {
    if (!g_scriptState.player) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, g_scriptState.player->puntos);
    return 1;
}

static int lua_SetPlayerPuntos(lua_State* L) {
    if (!g_scriptState.player) { lua_pushboolean(L, 0); return 1; }
    int p = (int)luaL_checkinteger(L, 1);
    g_scriptState.player->puntos = p;
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_GetPlayerRadio(lua_State* L) {
    if (!g_scriptState.player) { lua_pushnumber(L, 0); return 1; }
    lua_pushnumber(L, g_scriptState.player->radio);
    return 1;
}

static int lua_SetPlayerRadio(lua_State* L) {
    if (!g_scriptState.player) { lua_pushboolean(L, 0); return 1; }
    float r = (float)luaL_checknumber(L, 1);
    if (r < 0.1f) r = 0.1f;
    g_scriptState.player->radio = r;
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_GetPlayerAltura(lua_State* L) {
    if (!g_scriptState.player) { lua_pushnumber(L, 0); return 1; }
    lua_pushnumber(L, g_scriptState.player->alturaTotal);
    return 1;
}

static int lua_SetPlayerAltura(lua_State* L) {
    if (!g_scriptState.player) { lua_pushboolean(L, 0); return 1; }
    float h = (float)luaL_checknumber(L, 1);
    if (h < 0.2f) h = 0.2f;
    g_scriptState.player->alturaTotal = h;
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_GetCameraPos(lua_State* L) {
    if (!g_scriptState.camera) { lua_pushnil(L); return 1; }
    Vector3 pos = g_scriptState.camera->camara.position;
    lua_newtable(L);
    lua_pushnumber(L, pos.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, pos.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, pos.z); lua_setfield(L, -2, "z");
    return 1;
}

static int lua_SetCameraPos(lua_State* L) {
    if (!g_scriptState.camera) { lua_pushboolean(L, 0); return 1; }
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float z = (float)luaL_checknumber(L, 3);
    g_scriptState.camera->camara.position = { x, y, z };
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_GetCameraTarget(lua_State* L) {
    if (!g_scriptState.camera) { lua_pushnil(L); return 1; }
    Vector3 target = g_scriptState.camera->camara.target;
    lua_newtable(L);
    lua_pushnumber(L, target.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, target.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, target.z); lua_setfield(L, -2, "z");
    return 1;
}

static int lua_SetCameraTarget(lua_State* L) {
    if (!g_scriptState.camera) { lua_pushboolean(L, 0); return 1; }
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float z = (float)luaL_checknumber(L, 3);
    g_scriptState.camera->camara.target = { x, y, z };
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_GetCameraDistance(lua_State* L) {
    if (!g_scriptState.camDistancia) { lua_pushnumber(L, 15); return 1; }
    lua_pushnumber(L, *g_scriptState.camDistancia);
    return 1;
}

static int lua_SetCameraDistance(lua_State* L) {
    if (!g_scriptState.camDistancia) { lua_pushboolean(L, 0); return 1; }
    float d = (float)luaL_checknumber(L, 1);
    if (d < 1.0f) d = 1.0f;
    if (d > 50.0f) d = 50.0f;
    *g_scriptState.camDistancia = d;
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_GetCoinPos(lua_State* L) {
    if (!g_scriptState.coin) { lua_pushnil(L); return 1; }
    Vector3 pos = g_scriptState.coin->posicion;
    lua_newtable(L);
    lua_pushnumber(L, pos.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, pos.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, pos.z); lua_setfield(L, -2, "z");
    return 1;
}

static int lua_SetCoinPos(lua_State* L) {
    if (!g_scriptState.coin) { lua_pushboolean(L, 0); return 1; }
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float z = (float)luaL_checknumber(L, 3);
    g_scriptState.coin->posicion = { x, y, z };
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_GetCoinRadio(lua_State* L) {
    if (!g_scriptState.coin) { lua_pushnumber(L, 0); return 1; }
    lua_pushnumber(L, g_scriptState.coin->radio);
    return 1;
}

static int lua_SetCoinRadio(lua_State* L) {
    if (!g_scriptState.coin) { lua_pushboolean(L, 0); return 1; }
    float r = (float)luaL_checknumber(L, 1);
    if (r < 0.1f) r = 0.1f;
    g_scriptState.coin->radio = r;
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_IsCoinActive(lua_State* L) {
    if (!g_scriptState.coin) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, g_scriptState.coin->activa);
    return 1;
}

static int lua_SetCoinActive(lua_State* L) {
    if (!g_scriptState.coin) { lua_pushboolean(L, 0); return 1; }
    bool active = lua_toboolean(L, 1) != 0;
    g_scriptState.coin->activa = active;
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_GetEnemyPos(lua_State* L) {
    if (!g_scriptState.enemy) { lua_pushnil(L); return 1; }
    Vector3 pos = g_scriptState.enemy->posicion;
    lua_newtable(L);
    lua_pushnumber(L, pos.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, pos.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, pos.z); lua_setfield(L, -2, "z");
    return 1;
}

static int lua_SetEnemyPos(lua_State* L) {
    if (!g_scriptState.enemy) { lua_pushboolean(L, 0); return 1; }
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float z = (float)luaL_checknumber(L, 3);
    g_scriptState.enemy->posicion = { x, y, z };
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_GetEnemySize(lua_State* L) {
    if (!g_scriptState.enemy) { lua_pushnil(L); return 1; }
    Vector3 size = g_scriptState.enemy->tamano;
    lua_newtable(L);
    lua_pushnumber(L, size.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, size.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, size.z); lua_setfield(L, -2, "z");
    return 1;
}

static int lua_SetEnemySize(lua_State* L) {
    if (!g_scriptState.enemy) { lua_pushboolean(L, 0); return 1; }
    float sx = (float)luaL_checknumber(L, 1);
    float sy = (float)luaL_checknumber(L, 2);
    float sz = (float)luaL_checknumber(L, 3);
    if (sx < 0.1f) sx = 0.1f;
    if (sy < 0.1f) sy = 0.1f;
    if (sz < 0.1f) sz = 0.1f;
    g_scriptState.enemy->tamano = { sx, sy, sz };
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_GetSceneCount(lua_State* L) {
    if (!g_scriptState.scenes) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, (int)g_scriptState.scenes->size());
    return 1;
}

static int lua_GetCurrentScene(lua_State* L) {
    if (!g_scriptState.currentSceneIndex) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, *g_scriptState.currentSceneIndex + 1);
    return 1;
}

static int lua_SwitchScene(lua_State* L) {
    if (!g_scriptState.scenes || !g_scriptState.currentSceneIndex) {
        lua_pushboolean(L, 0);
        return 1;
    }
    int idx = (int)luaL_checkinteger(L, 1) - 1;
    if (idx < 0) idx = 0;
    if (idx >= (int)g_scriptState.scenes->size()) idx = (int)g_scriptState.scenes->size() - 1;
    *g_scriptState.currentSceneIndex = idx;
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_GetTime(lua_State* L) {
    lua_pushnumber(L, GetTime());
    return 1;
}

static int lua_GetScreenWidth(lua_State* L) {
    lua_pushinteger(L, GetScreenWidth());
    return 1;
}

static int lua_GetScreenHeight(lua_State* L) {
    lua_pushinteger(L, GetScreenHeight());
    return 1;
}

static int lua_LogMessage(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    Color color = WHITE;
    if (lua_gettop(L) >= 2) {
        const char* col = luaL_checkstring(L, 2);
        if (strcmp(col, "RED") == 0) color = RED;
        else if (strcmp(col, "GREEN") == 0) color = GREEN;
        else if (strcmp(col, "BLUE") == 0) color = BLUE;
        else if (strcmp(col, "YELLOW") == 0) color = YELLOW;
        else if (strcmp(col, "ORANGE") == 0) color = ORANGE;
        else if (strcmp(col, "PURPLE") == 0) color = PURPLE;
        else if (strcmp(col, "GRAY") == 0) color = GRAY;
        else if (strcmp(col, "LIGHTGRAY") == 0) color = LIGHTGRAY;
        else if (strcmp(col, "DARKGRAY") == 0) color = DARKGRAY;
        else if (strcmp(col, "SKYBLUE") == 0) color = SKYBLUE;
    }
    extern void LogMessage(const std::string & text, Color color);
    LogMessage(std::string("[SCRIPT] ") + msg, color);
    lua_pushboolean(L, 1);
    return 1;
}

static void RegisterLuaFunctions(lua_State* L) {
    lua_register(L, "print", lua_Print);
    lua_register(L, "GetFrameTime", lua_GetFrameTime);
    lua_register(L, "GetTime", lua_GetTime);
    lua_register(L, "GetScreenWidth", lua_GetScreenWidth);
    lua_register(L, "GetScreenHeight", lua_GetScreenHeight);
    lua_register(L, "GetPlayerPos", lua_GetPlayerPos);
    lua_register(L, "SetPlayerPos", lua_SetPlayerPos);
    lua_register(L, "GetPlayerVidas", lua_GetPlayerVidas);
    lua_register(L, "SetPlayerVidas", lua_SetPlayerVidas);
    lua_register(L, "GetPlayerPuntos", lua_GetPlayerPuntos);
    lua_register(L, "SetPlayerPuntos", lua_SetPlayerPuntos);
    lua_register(L, "GetPlayerRadio", lua_GetPlayerRadio);
    lua_register(L, "SetPlayerRadio", lua_SetPlayerRadio);
    lua_register(L, "GetPlayerAltura", lua_GetPlayerAltura);
    lua_register(L, "SetPlayerAltura", lua_SetPlayerAltura);
    lua_register(L, "GetCameraPos", lua_GetCameraPos);
    lua_register(L, "SetCameraPos", lua_SetCameraPos);
    lua_register(L, "GetCameraTarget", lua_GetCameraTarget);
    lua_register(L, "SetCameraTarget", lua_SetCameraTarget);
    lua_register(L, "GetCameraDistance", lua_GetCameraDistance);
    lua_register(L, "SetCameraDistance", lua_SetCameraDistance);
    lua_register(L, "GetCoinPos", lua_GetCoinPos);
    lua_register(L, "SetCoinPos", lua_SetCoinPos);
    lua_register(L, "GetCoinRadio", lua_GetCoinRadio);
    lua_register(L, "SetCoinRadio", lua_SetCoinRadio);
    lua_register(L, "IsCoinActive", lua_IsCoinActive);
    lua_register(L, "SetCoinActive", lua_SetCoinActive);
    lua_register(L, "GetEnemyPos", lua_GetEnemyPos);
    lua_register(L, "SetEnemyPos", lua_SetEnemyPos);
    lua_register(L, "GetEnemySize", lua_GetEnemySize);
    lua_register(L, "SetEnemySize", lua_SetEnemySize);
    lua_register(L, "GetSceneCount", lua_GetSceneCount);
    lua_register(L, "GetCurrentScene", lua_GetCurrentScene);
    lua_register(L, "SwitchScene", lua_SwitchScene);
    lua_register(L, "GetBlockCount", lua_GetBlockCount);
    lua_register(L, "CreateBlock", lua_CreateBlock);
    lua_register(L, "MoveBlock", lua_MoveBlock);
    lua_register(L, "GetBlockPos", lua_GetBlockPos);
    lua_register(L, "SetBlockColor", lua_SetBlockColor);
    lua_register(L, "RemoveBlock", lua_RemoveBlock);
    lua_register(L, "GetBlockSize", lua_GetBlockSize);
    lua_register(L, "SetBlockSize", lua_SetBlockSize);
    lua_register(L, "LogMessage", lua_LogMessage);
    lua_register(L, "IsKeyDown", [](lua_State* L) {
        int key = (int)luaL_checkinteger(L, 1);
        lua_pushboolean(L, IsKeyDown(key));
        return 1;
        });
}

// ============================================================================
// FUNCIONES DE GUARDADO DE SCRIPTS (con lógica postergada)
// ============================================================================
static std::string GetFileNameFromPath(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos) return path.substr(pos + 1);
    return path;
}

static void SaveScript() {
    if (activeScriptTab < 0 || activeScriptTab >= (int)openScriptsTabs.size()) return;
    std::string currentPath = openScriptsTabs[activeScriptTab];
    bool isVirtual = (scriptIsVirtual.find(currentPath) != scriptIsVirtual.end() && scriptIsVirtual[currentPath]);

    if (isVirtual) {
        // CASO A: Script virtual – guardar por primera vez con diálogo
        std::string suggestedName = scriptVirtualDisplayName[currentPath];
#ifdef _WIN32
        OPENFILENAMEA ofn = { 0 };
        char szFile[260] = { 0 };
        strncpy_s(szFile, sizeof(szFile), suggestedName.c_str(), _TRUNCATE);
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = GetActiveWindow();
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = "Scripts de Lua (*.lua)\0*.lua\0Todos los archivos (*.*)\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrDefExt = "lua";
        ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
        if (GetSaveFileNameA(&ofn)) {
            std::string newPath = szFile;
            std::ofstream out(newPath);
            if (out.is_open()) {
                out << g_scriptState.codeBuffer;
                out.close();
                // Actualizar todas las estructuras para reflejar la nueva ruta real
                openScriptsTabs[activeScriptTab] = newPath;
                scriptBuffers[newPath] = scriptBuffers[currentPath];
                scriptBuffers.erase(currentPath);
                scriptUndoStacks[newPath] = scriptUndoStacks[currentPath];
                scriptUndoStacks.erase(currentPath);
                scriptRedoStacks[newPath] = scriptRedoStacks[currentPath];
                scriptRedoStacks.erase(currentPath);
                scriptIsVirtual.erase(currentPath);
                scriptIsVirtual[newPath] = false;
                scriptVirtualDisplayName.erase(currentPath);
                scriptVirtualDisplayName[newPath] = GetFileNameFromPath(newPath);
                LogMessage("[SUCCESS] Script guardado correctamente en: " + newPath, GREEN);
            }
            else {
                LogMessage("[ERROR] No se pudo guardar el archivo.", RED);
            }
        }
#endif
    }
    else {
        // CASO B: Archivo real – guardado silencioso directo
        std::ofstream out(currentPath);
        if (out.is_open()) {
            out << g_scriptState.codeBuffer;
            out.close();
            scriptBuffers[currentPath] = std::string(g_scriptState.codeBuffer);
            LogMessage("[SUCCESS] Script guardado correctamente.", GREEN);
        }
        else {
            LogMessage("[ERROR] No se pudo guardar el archivo.", RED);
        }
    }
}

static void LoadScript() {
#ifdef _WIN32
    OPENFILENAMEA ofn = { 0 };
    char szFile[260] = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Scripts de Lua (*.lua)\0*.lua\0Todos los archivos (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = "lua";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameA(&ofn)) {
        AddScriptTab(szFile);
    }
#endif
}

// ============================================================================
// API PÚBLICA
// ============================================================================
void InitScripting() {
    g_scriptState.rect = { 200, 100, 800, 600 };
    g_scriptState.isFocused = false;
    g_scriptState.cursorPos = (int)strlen(g_scriptState.codeBuffer);
    g_scriptState.scenes = nullptr;
    g_scriptState.currentSceneIndex = nullptr;
    g_scriptState.player = nullptr;
    g_scriptState.coin = nullptr;
    g_scriptState.enemy = nullptr;
    g_scriptState.camera = nullptr;
    g_scriptState.camDistancia = nullptr;
    g_scriptState.velocidadY = nullptr;
    g_scriptState.enElSuelo = nullptr;
    g_scriptState.puedeRecibirDano = nullptr;
    g_scriptState.temporizadorDano = nullptr;
    g_scriptState.codeFont = LoadFontEx("resources/fonts/JetBrainsMono-Regular.ttf", 64, nullptr, 0);
    if (g_scriptState.codeFont.texture.id != 0) {
        SetTextureFilter(g_scriptState.codeFont.texture, 1);
        g_scriptState.fontLoaded = true;
        TraceLog(LOG_INFO, "Fuente monoespaciada cargada correctamente (tamaño base 64, filtro bilinear).");
    }
    else {
        g_scriptState.codeFont = GetFontDefault();
        g_scriptState.fontLoaded = false;
        TraceLog(LOG_WARNING, "No se pudo cargar la fuente monoespaciada. Usando la predeterminada.");
    }
    BuildSuggestionList();
    g_scriptState.showAutoComplete = false;
    g_scriptState.autoCompleteIndex = 0;
}

void SetScriptContext(
    std::vector<std::vector<EditorBlock>>* scenes,
    int* currentSceneIndex,
    Player* player,
    Coin* coin,
    Enemy* enemy,
    CameraGame* camera,
    float* camDistancia,
    float* velocidadY,
    bool* enElSuelo,
    bool* puedeRecibirDano,
    float* temporizadorDano
) {
    g_scriptState.scenes = scenes;
    g_scriptState.currentSceneIndex = currentSceneIndex;
    g_scriptState.player = player;
    g_scriptState.coin = coin;
    g_scriptState.enemy = enemy;
    g_scriptState.camera = camera;
    g_scriptState.camDistancia = camDistancia;
    g_scriptState.velocidadY = velocidadY;
    g_scriptState.enElSuelo = enElSuelo;
    g_scriptState.puedeRecibirDano = puedeRecibirDano;
    g_scriptState.temporizadorDano = temporizadorDano;
}

void AddScriptOutput(const std::string& text) {
    g_scriptState.outputBuffer.push_back(text);
    if (g_scriptState.outputBuffer.size() > 100) g_scriptState.outputBuffer.pop_front();
}

void ExecuteScript(const char* code) {
    g_scriptState.outputBuffer.clear();
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    RegisterLuaFunctions(L);
    int status = luaL_dostring(L, code);
    if (status != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AddScriptOutput(std::string("ERROR: ") + err);
        lua_pop(L, 1);
    }
    lua_close(L);
}

bool IsMouseOverScriptEditor() {
    if (!g_scriptState.open) return false;
    return CheckCollisionPointRec(GetMousePosition(), g_scriptState.rect);
}

void SetUpdateScript(const char* script) {
    g_updateScriptCode = script ? script : "";
}

void UpdateScripts() {
    if (g_updateScriptCode.empty()) return;
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    RegisterLuaFunctions(L);
    lua_pushnumber(L, GetFrameTime()); lua_setglobal(L, "dt");
    lua_pushnumber(L, GetTime()); lua_setglobal(L, "time");
    int status = luaL_dostring(L, g_updateScriptCode.c_str());
    if (status != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        AddScriptOutput(std::string("UPDATE ERROR: ") + err);
        lua_pop(L, 1);
    }
    lua_close(L);
}

// ============================================================================
// RESALTADO DE SINTAXIS
// ============================================================================
static Color GetSyntaxColor(const std::string& token, const std::string& fullLine, size_t startPos) {
    if (token.find("--") == 0) return GRAY;
    if (token.find("\"") == 0 || token.find("'") == 0) return GREEN;
    bool isNumber = true;
    for (char c : token) {
        unsigned char uc = (unsigned char)c;
        if (!isdigit(uc) && c != '.' && c != '-' && c != '+') { isNumber = false; break; }
    }
    if (isNumber && !token.empty()) return ORANGE;
    for (const auto& kw : luaKeywords) if (token == kw.text) return BLUE;
    for (const auto& api : apiFunctions) if (token == api.text) return SKYBLUE;
    if (token == "+" || token == "-" || token == "*" || token == "/" || token == "%" ||
        token == "=" || token == "==" || token == "<" || token == ">" || token == "~=" ||
        token == ".." || token == "..." || token == ":" || token == ";" || token == "(" || token == ")" ||
        token == "{" || token == "}" || token == "[" || token == "]") return YELLOW;
    return WHITE;
}

static std::vector<std::pair<std::string, Color>> TokenizeLine(const std::string& line) {
    std::vector<std::pair<std::string, Color>> tokens;
    std::string currentToken;
    bool inString = false, inComment = false;
    char stringChar = '\0';
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (!inString && !inComment && i + 1 < line.size() && line[i] == '-' && line[i + 1] == '-') {
            if (!currentToken.empty()) {
                tokens.push_back({ currentToken, GetSyntaxColor(currentToken, line, 0) });
                currentToken.clear();
            }
            tokens.push_back({ line.substr(i), GRAY });
            break;
        }
        if (!inComment && (c == '"' || c == '\'')) {
            if (!inString) {
                if (!currentToken.empty()) {
                    tokens.push_back({ currentToken, GetSyntaxColor(currentToken, line, 0) });
                    currentToken.clear();
                }
                inString = true;
                stringChar = c;
                currentToken += c;
                continue;
            }
            else if (c == stringChar) {
                currentToken += c;
                tokens.push_back({ currentToken, GREEN });
                currentToken.clear();
                inString = false;
                continue;
            }
        }
        if (inString || inComment) { currentToken += c; continue; }
        if (c == ' ') {
            if (!currentToken.empty()) {
                tokens.push_back({ currentToken, GetSyntaxColor(currentToken, line, 0) });
                currentToken.clear();
            }
            tokens.push_back({ " ", Color{ 100, 100, 100, 255 } });
            continue;
        }
        if (c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']' ||
            c == '=' || c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
            c == '<' || c == '>' || c == '~' || c == ':' || c == ';' || c == ',' || c == '.') {
            if (!currentToken.empty()) {
                tokens.push_back({ currentToken, GetSyntaxColor(currentToken, line, 0) });
                currentToken.clear();
            }
            if (i + 1 < line.size()) {
                std::string twoChars = line.substr(i, 2);
                if (twoChars == ".." || twoChars == "==" || twoChars == "~=" || twoChars == "<=" || twoChars == ">=") {
                    tokens.push_back({ twoChars, YELLOW });
                    i++;
                    continue;
                }
            }
            tokens.push_back({ std::string(1, c), YELLOW });
            continue;
        }
        currentToken += c;
    }
    if (!currentToken.empty()) {
        if (inString || inComment) tokens.push_back({ currentToken, inString ? GREEN : GRAY });
        else tokens.push_back({ currentToken, GetSyntaxColor(currentToken, line, 0) });
    }
    return tokens;
}

void SanitizeBuffer() {
    std::string current = std::string(g_scriptState.codeBuffer);
    std::string clean = SanitizeScript(current);
    strcpy_s(g_scriptState.codeBuffer, sizeof(g_scriptState.codeBuffer), clean.c_str());
    g_scriptState.cursorPos = (int)strlen(g_scriptState.codeBuffer);
    g_scriptState.selectionStart = -1;
    g_scriptState.selectionEnd = -1;
    AddScriptOutput("[SISTEMA] Script limpiado (caracteres invisibles eliminados)");
}

// ============================================================================
// DIBUJADO DEL EDITOR (con sub-barra lateral de pestañas)
// ============================================================================
void DrawScriptEditorInRect(Rectangle rect, float scaleFactor) {
    Vector2 mousePos = GetMousePosition();

    const float fontSize = 22.0f;
    int lineHeight = (int)(fontSize + 4.0f);
    float spacing = 0.0f;

    float charWidth = MeasureTextEx(g_scriptState.codeFont, "A", fontSize, spacing).x;
    if (charWidth <= 0.0f) charWidth = fontSize * 0.6f;

    DrawRectangleRec(rect, Color{ 40, 40, 50, 255 });
    DrawRectangleLinesEx(rect, 1.5f, g_scriptState.isFocused ? GOLD : DARKGRAY);

    float titleHeight = 28.0f;
    Rectangle titleRect = { rect.x, rect.y, rect.width, titleHeight };
    DrawRectangleRec(titleRect, Color{ 60, 60, 70, 255 });
    DrawText("Editor de Scripts (F7 para cerrar)", (int)(rect.x + 10), (int)(rect.y + 6), 14, WHITE);

    // Botones de deshacer / rehacer
    float undoredoBtnW = 20.0f, undoredoBtnH = 20.0f;
    float undoredoBtnY = rect.y + 4.0f;
    float saveBtnX = rect.x + rect.width - 60.0f - 10.0f;
    float openBtnX = saveBtnX - 60.0f - 5.0f;
    float redoBtnX = openBtnX - 20.0f - 5.0f;
    float undoBtnX = redoBtnX - 20.0f - 5.0f;
    Rectangle undoBtn = { undoBtnX, undoredoBtnY, undoredoBtnW, undoredoBtnH };
    Rectangle redoBtn = { redoBtnX, undoredoBtnY, undoredoBtnW, undoredoBtnH };

    DrawRectangleRec(undoBtn, (undoStack.empty() ? DARKGRAY : GRAY));
    DrawRectangleLinesEx(undoBtn, 1, LIGHTGRAY);
    DrawText("<", (int)(undoBtn.x + 5), (int)(undoBtn.y + 2), 14, undoStack.empty() ? GRAY : WHITE);

    DrawRectangleRec(redoBtn, (redoStack.empty() ? DARKGRAY : GRAY));
    DrawRectangleLinesEx(redoBtn, 1, LIGHTGRAY);
    DrawText(">", (int)(redoBtn.x + 6), (int)(redoBtn.y + 2), 14, redoStack.empty() ? GRAY : WHITE);

    // Botones Abrir / Guardar
    float btnW = 60.0f;
    float btnH = 20.0f;
    float btnY = rect.y + 4.0f;
    Rectangle openBtn = { openBtnX, btnY, btnW, btnH };
    Rectangle saveBtn = { saveBtnX, btnY, btnW, btnH };

    DrawRectangleRec(openBtn, DARKGRAY);
    DrawRectangleLinesEx(openBtn, 1, LIGHTGRAY);
    DrawText("Abrir", (int)(openBtn.x + 12), (int)(openBtn.y + 2), 14, WHITE);
    DrawRectangleRec(saveBtn, DARKGRAY);
    DrawRectangleLinesEx(saveBtn, 1, LIGHTGRAY);
    DrawText("Guardar", (int)(saveBtn.x + 8), (int)(saveBtn.y + 2), 14, WHITE);

    // ====== SUB-BARRA LATERAL IZQUIERDA ======
    float sidebarWidth = 180.0f;
    float sidebarX = rect.x + 6;
    float sidebarY = rect.y + titleHeight + 6;
    float sidebarHeight = rect.height - titleHeight - 6 - 70;
    Rectangle sidebarRect = { sidebarX, sidebarY, sidebarWidth, sidebarHeight };
    DrawRectangleRec(sidebarRect, Color{ 50, 50, 55, 255 });
    DrawRectangleLinesEx(sidebarRect, 1, DARKGRAY);
    DrawText("Scripts abiertos", (int)(sidebarX + 8), (int)(sidebarY + 4), 12, LIGHTGRAY);

    float listItemHeight = 20.0f;
    float listStartY = sidebarY + 22.0f;
    BeginScissorMode((int)sidebarX, (int)listStartY, (int)sidebarWidth, (int)(sidebarHeight - 22.0f));
    for (size_t i = 0; i < openScriptsTabs.size(); ++i) {
        float itemY = listStartY + i * listItemHeight;
        std::string path = openScriptsTabs[i];
        std::string displayName;
        if (path.find("virtual:") == 0) {
            displayName = scriptVirtualDisplayName[path];
        }
        else {
            displayName = path;
            size_t lastSlash = displayName.find_last_of("\\/");
            if (lastSlash != std::string::npos) displayName = displayName.substr(lastSlash + 1);
        }
        if (displayName.length() > 20) displayName = displayName.substr(0, 17) + "...";

        Rectangle itemRect = { sidebarX + 4, itemY, sidebarWidth - 8, listItemHeight };
        bool isActive = (i == activeScriptTab);
        Color bg = isActive ? Color{ 70, 70, 90, 255 } : Color{ 45, 45, 50, 255 };
        DrawRectangleRec(itemRect, bg);
        DrawText(displayName.c_str(), (int)(itemRect.x + 4), (int)(itemRect.y + 2), 12, isActive ? YELLOW : WHITE);

        Rectangle closeBtn = { itemRect.x + itemRect.width - 20, itemRect.y + 2, 16, 16 };
        DrawText("x", (int)(closeBtn.x), (int)(closeBtn.y), 12, RED);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mousePos, closeBtn)) {
            CloseScriptTab((int)i);
            break;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mousePos, itemRect) && !CheckCollisionPointRec(mousePos, closeBtn)) {
            SetActiveScript((int)i);
        }
    }
    EndScissorMode();

    // ====== ÁREA DE CÓDIGO (derecha de la sub-barra) ======
    float codeAreaX = sidebarX + sidebarWidth + 4;
    float codeAreaWidth = rect.width - 12 - sidebarWidth - 4;
    float codeY = rect.y + titleHeight + 6;
    float codeHeight = rect.height - titleHeight - 6 - 70;
    float codeX = codeAreaX;
    Rectangle codeRect = { codeX, codeY, codeAreaWidth, codeHeight };
    DrawRectangleRec(codeRect, Color{ 20, 20, 25, 255 });
    DrawRectangleLinesEx(codeRect, 1, DARKGRAY);

    float lineNumberWidth = 40.0f;
    Rectangle lineNumRect = { codeX, codeY, lineNumberWidth, codeHeight };
    DrawRectangleRec(lineNumRect, Color{ 30, 30, 35, 255 });
    DrawLine((int)(codeX + lineNumberWidth), (int)codeY, (int)(codeX + lineNumberWidth), (int)(codeY + codeHeight), DARKGRAY);

    int totalLines = GetLineCount(g_scriptState.codeBuffer);
    int visibleLines = (int)(codeHeight / lineHeight);
    int cursorLine = GetLineFromPos(g_scriptState.codeBuffer, g_scriptState.cursorPos);

    bool mouseWheelUsed = false;
    if (CheckCollisionPointRec(mousePos, rect) || g_scriptState.isFocused) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            mouseWheelUsed = true;
            int newScroll = g_scriptState.scrollOffset - (int)wheel;
            if (newScroll < 0) newScroll = 0;
            if (newScroll > totalLines - visibleLines) newScroll = totalLines - visibleLines;
            if (newScroll < 0) newScroll = 0;
            g_scriptState.scrollOffset = newScroll;
        }
    }

    static int lastCursorPos = -1;
    bool cursorMoved = (lastCursorPos != g_scriptState.cursorPos);
    if (cursorMoved) {
        lastCursorPos = g_scriptState.cursorPos;
        if (!mouseWheelUsed) {
            bool cursorVisible = (cursorLine >= g_scriptState.scrollOffset && cursorLine < g_scriptState.scrollOffset + visibleLines);
            if (!cursorVisible) {
                if (cursorLine < g_scriptState.scrollOffset) g_scriptState.scrollOffset = cursorLine;
                else if (cursorLine >= g_scriptState.scrollOffset + visibleLines) g_scriptState.scrollOffset = cursorLine - visibleLines + 1;
                if (g_scriptState.scrollOffset < 0) g_scriptState.scrollOffset = 0;
                int maxScroll = totalLines - visibleLines;
                if (maxScroll < 0) maxScroll = 0;
                if (g_scriptState.scrollOffset > maxScroll) g_scriptState.scrollOffset = maxScroll;
            }
        }
    }

    float lineNumY = codeY + 4;
    for (int i = g_scriptState.scrollOffset; i < g_scriptState.scrollOffset + visibleLines && i < totalLines; i++) {
        char num[8];
        snprintf(num, sizeof(num), "%d", i + 1);
        DrawText(num, (int)(codeX + 4), (int)lineNumY, (int)fontSize, GRAY);
        lineNumY += lineHeight;
    }

    Font font = g_scriptState.codeFont;
    float textX = codeX + lineNumberWidth + 4;
    float textY = codeY + 4;
    int startLine = g_scriptState.scrollOffset;
    int endLine = startLine + visibleLines;
    if (endLine > totalLines) endLine = totalLines;

    for (int i = startLine; i < endLine; i++) {
        int lineStartPos = GetLineStart(g_scriptState.codeBuffer, i);
        int lineEndPos = GetLineEnd(g_scriptState.codeBuffer, i);
        char lineBuffer[512];
        int tempLen = 0;
        for (int j = lineStartPos; j < lineEndPos && tempLen < 511; j++) {
            char c = g_scriptState.codeBuffer[j];
            if (c != '\r') lineBuffer[tempLen++] = c;
        }
        lineBuffer[tempLen] = '\0';
        std::string line(lineBuffer);

        if (HasSelection()) {
            int selStart = std::min(g_scriptState.selectionStart, g_scriptState.selectionEnd);
            int selEnd = std::max(g_scriptState.selectionStart, g_scriptState.selectionEnd);
            if (selStart < lineEndPos && selEnd > lineStartPos) {
                int startInLine = std::max(selStart, lineStartPos) - lineStartPos;
                int endInLine = std::min(selEnd, lineEndPos) - lineStartPos;
                std::string before = line.substr(0, startInLine);
                std::string selected = line.substr(startInLine, endInLine - startInLine);
                Vector2 szBefore = MeasureTextEx(font, before.c_str(), fontSize, spacing);
                Vector2 szSel = MeasureTextEx(font, selected.c_str(), fontSize, spacing);
                float x1 = textX + szBefore.x;
                float x2 = x1 + szSel.x;
                DrawRectangle((int)x1, (int)textY, (int)(x2 - x1), (int)fontSize, Fade(SKYBLUE, 0.4f));
            }
        }

        auto tokens = TokenizeLine(line);
        float currentX = textX;
        for (const auto& token : tokens) {
            DrawTextEx(font, token.first.c_str(), { currentX, textY }, fontSize, spacing, token.second);
            Vector2 measure = MeasureTextEx(font, token.first.c_str(), fontSize, spacing);
            currentX += measure.x;
        }
        textY += lineHeight;
    }

    if (g_scriptState.isFocused && cursorLine >= startLine && cursorLine < endLine) {
        static double lastBlinkTime = 0.0;
        double now = GetTime();
        if (now - lastBlinkTime > 1.0) lastBlinkTime = now;
        bool cursorVisible = fmod(now - lastBlinkTime, 1.0) < 0.5;
        if (cursorVisible) {
            int lineStartPos = GetLineStart(g_scriptState.codeBuffer, cursorLine);
            int lineEndPos = GetLineEnd(g_scriptState.codeBuffer, cursorLine);
            std::string line;
            line.reserve(lineEndPos - lineStartPos);
            for (int j = lineStartPos; j < lineEndPos; ++j) {
                char c = g_scriptState.codeBuffer[j];
                if (c != '\r') line.push_back(c);
            }
            int lineLen = (int)line.size();
            int col = g_scriptState.cursorPos - lineStartPos;
            if (col < 0) col = 0;
            if (col > lineLen) col = lineLen;
            float cursorX;
            if (col == lineLen) cursorX = textX + MeasureTextEx(font, line.c_str(), fontSize, spacing).x;
            else cursorX = textX + MeasureTextEx(font, line.substr(0, col).c_str(), fontSize, spacing).x;
            float cursorY = codeY + 4 + (cursorLine - startLine) * lineHeight;
            if (cursorX < codeX + codeAreaWidth) DrawRectangle((int)cursorX, (int)cursorY, 2, (int)fontSize, WHITE);
        }
    }

    Rectangle scrollbarRec = { 0,0,0,0 };
    if (totalLines > visibleLines) {
        float scrollBarHeight = codeRect.height * ((float)visibleLines / totalLines);
        float scrollBarY = codeRect.y + (codeRect.height - scrollBarHeight) * ((float)g_scriptState.scrollOffset / (totalLines - visibleLines));
        scrollbarRec = { codeRect.x + codeRect.width - 8, scrollBarY, 6, scrollBarHeight };
        DrawRectangleRec(scrollbarRec, Fade(LIGHTGRAY, 0.5f));
    }

    if (g_scriptState.showAutoComplete && !g_scriptState.autoCompleteSuggestions.empty()) {
        int maxWidth = 0;
        for (const auto& s : g_scriptState.autoCompleteSuggestions) {
            Vector2 sz = MeasureTextEx(font, s.text.c_str(), fontSize, spacing);
            if (sz.x > maxWidth) maxWidth = (int)sz.x;
        }
        int lineStartPos = GetLineStart(g_scriptState.codeBuffer, cursorLine);
        int col = g_scriptState.cursorPos - lineStartPos;
        if (col < 0) col = 0;
        std::string before = std::string(g_scriptState.codeBuffer + lineStartPos, col);
        Vector2 szBefore = MeasureTextEx(font, before.c_str(), fontSize, spacing);
        float popupX = textX + szBefore.x;
        float popupY = codeY + 4 + (cursorLine - startLine) * lineHeight + 4;
        if (popupX + maxWidth + 20 > codeX + codeAreaWidth) popupX = codeX + codeAreaWidth - maxWidth - 20;
        if (popupY + (g_scriptState.autoCompleteSuggestions.size() * lineHeight) > codeY + codeHeight)
            popupY = codeY + codeHeight - (g_scriptState.autoCompleteSuggestions.size() * lineHeight);
        if (popupY < codeY) popupY = codeY;
        int popupHeight = (int)g_scriptState.autoCompleteSuggestions.size() * lineHeight;
        DrawRectangle((int)popupX, (int)popupY, maxWidth + 20, popupHeight, Color{ 30, 30, 40, 240 });
        DrawRectangleLines((int)popupX, (int)popupY, maxWidth + 20, popupHeight, GRAY);
        for (size_t i = 0; i < g_scriptState.autoCompleteSuggestions.size(); i++) {
            const auto& s = g_scriptState.autoCompleteSuggestions[i];
            float yPos = popupY + i * lineHeight;
            Color bgColor = (i == g_scriptState.autoCompleteIndex) ? Color{ 60, 60, 80, 255 } : Color{ 0,0,0,0 };
            DrawRectangle((int)popupX, (int)yPos, maxWidth + 20, lineHeight, bgColor);
            DrawTextEx(font, s.text.c_str(), { popupX + 4, yPos + 2 }, fontSize, spacing, WHITE);
            if (!s.description.empty()) {
                Vector2 szText = MeasureTextEx(font, s.text.c_str(), fontSize, spacing);
                DrawTextEx(font, ("  " + s.description).c_str(), { popupX + 4 + szText.x, yPos + 2 }, fontSize - 2, spacing, GRAY);
            }
        }
    }

    // Barra inferior
    float bottomBarHeight = 70.0f;
    float bottomY = rect.y + rect.height - bottomBarHeight + 6;
    float btnH2 = 28;
    float btnY2 = bottomY;
    Rectangle execBtn = { rect.x + 10, btnY2, 90, btnH2 };
    DrawRectangleRec(execBtn, DARKGREEN);
    DrawRectangleLinesEx(execBtn, 1, LIME);
    DrawText("Ejecutar", (int)(execBtn.x + 12), (int)(execBtn.y + 4), 14, WHITE);
    if (CheckCollisionPointRec(mousePos, execBtn) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        ExecuteScript(g_scriptState.codeBuffer);
        SetUpdateScript(g_scriptState.codeBuffer);
        AddScriptOutput("[SISTEMA] Script activado como actualización automática.");
    }
    Rectangle stopBtn = { rect.x + 110, btnY2, 80, btnH2 };
    DrawRectangleRec(stopBtn, MAROON);
    DrawRectangleLinesEx(stopBtn, 1, RED);
    DrawText("Detener", (int)(stopBtn.x + 12), (int)(stopBtn.y + 4), 14, WHITE);
    if (CheckCollisionPointRec(mousePos, stopBtn) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        SetUpdateScript("");
        AddScriptOutput("[SISTEMA] Script de actualización detenido.");
    }
    float row2Y = btnY2 + btnH2 + 4;
    float consoleX = rect.x + 10;
    float consoleW = rect.width - 20;
    Rectangle consoleRect = { consoleX, row2Y, consoleW, 26 };
    DrawRectangleRec(consoleRect, Color{ 20, 20, 25, 200 });
    DrawRectangleLinesEx(consoleRect, 1, GRAY);
    if (!g_scriptState.outputBuffer.empty()) {
        std::string lastMsg = g_scriptState.outputBuffer.back();
        if (MeasureText(lastMsg.c_str(), 13) > consoleW - 10) {
            while (MeasureText((lastMsg + "...").c_str(), 13) > consoleW - 10 && lastMsg.length() > 3)
                lastMsg.pop_back();
            lastMsg += "...";
        }
        DrawText(lastMsg.c_str(), (int)(consoleRect.x + 8), (int)(consoleRect.y + 5), 13, LIME);
    }

    // Manejo de teclado
    if (g_scriptState.isFocused) {
        static int repeatDelay = 20, repeatInterval = 4, repeatCounter = 0, lastKey = -1;
        static bool keyHeld = false;
        auto checkKey = [&](int key) -> bool {
            if (IsKeyPressed(key)) { lastKey = key; keyHeld = true; repeatCounter = 0; return true; }
            if (IsKeyDown(key) && lastKey == key && keyHeld) {
                repeatCounter++;
                if (repeatCounter > repeatDelay && (repeatCounter - repeatDelay) % repeatInterval == 0) return true;
            }
            return false;
            };
        bool anySpecialKeyDown = false;
        for (int k : { KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_BACKSPACE, KEY_DELETE })
            if (IsKeyDown(k)) { anySpecialKeyDown = true; break; }
        if (!anySpecialKeyDown) { lastKey = -1; keyHeld = false; repeatCounter = 0; }

        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_SPACE)) {
            g_scriptState.showAutoComplete = true;
            g_scriptState.autoCompleteIndex = 0;
            int start = g_scriptState.cursorPos;
            while (start > 0 && isalnum((unsigned char)g_scriptState.codeBuffer[start - 1])) start--;
            g_scriptState.autoCompletePrefix = std::string(g_scriptState.codeBuffer + start, g_scriptState.cursorPos - start);
            g_scriptState.autoCompleteSuggestions = GetSuggestions(g_scriptState.autoCompletePrefix);
        }

        int key = GetCharPressed();
        while (key > 0) {
            if (HasSelection() && (key == '(' || key == '[' || key == '{' || key == '"' || key == '\'')) {
                GuardarEstadoHistorial();
                char closing = (key == '(') ? ')' : (key == '[') ? ']' : (key == '{') ? '}' : (char)key;
                int selStart = std::min(g_scriptState.selectionStart, g_scriptState.selectionEnd);
                int selEnd = std::max(g_scriptState.selectionStart, g_scriptState.selectionEnd);
                InsertChar(g_scriptState.codeBuffer, selEnd, closing);
                InsertChar(g_scriptState.codeBuffer, selStart, (char)key);
                g_scriptState.cursorPos = selStart + 1;
                ClearSelection();
                g_scriptState.showAutoComplete = false;
                lastCursorPos = g_scriptState.cursorPos;
            }
            else {
                if (key == '(' || key == '[' || key == '{' || key == '"' || key == '\'') {
                    GuardarEstadoHistorial();
                    char closing = (key == '(') ? ')' : (key == '[') ? ']' : (key == '{') ? '}' : (char)key;
                    if (g_scriptState.cursorPos < (int)sizeof(g_scriptState.codeBuffer) - 2) {
                        InsertChar(g_scriptState.codeBuffer, g_scriptState.cursorPos, (char)key);
                        InsertChar(g_scriptState.codeBuffer, g_scriptState.cursorPos + 1, closing);
                        g_scriptState.cursorPos++;
                    }
                    else if (g_scriptState.cursorPos < (int)sizeof(g_scriptState.codeBuffer) - 1) {
                        InsertChar(g_scriptState.codeBuffer, g_scriptState.cursorPos, (char)key);
                        g_scriptState.cursorPos++;
                    }
                }
                else if (key == ')' || key == ']' || key == '}') {
                    if (g_scriptState.codeBuffer[g_scriptState.cursorPos] == (char)key) {
                        g_scriptState.cursorPos++;
                    }
                    else {
                        GuardarEstadoHistorial();
                        if (g_scriptState.cursorPos < (int)sizeof(g_scriptState.codeBuffer) - 1) {
                            InsertChar(g_scriptState.codeBuffer, g_scriptState.cursorPos, (char)key);
                            g_scriptState.cursorPos++;
                        }
                    }
                }
                else {
                    GuardarEstadoHistorial();
                    if (HasSelection()) ReplaceSelection("");
                    if (g_scriptState.cursorPos < (int)sizeof(g_scriptState.codeBuffer) - 1) {
                        InsertChar(g_scriptState.codeBuffer, g_scriptState.cursorPos, (char)key);
                        g_scriptState.cursorPos++;
                    }
                }

                lastCursorPos = g_scriptState.cursorPos;

                if (key > 0 && key < 256 && (isalnum((unsigned char)key) || key == '_')) {
                    int start = g_scriptState.cursorPos;
                    while (start > 0 && isalnum((unsigned char)g_scriptState.codeBuffer[start - 1])) start--;
                    std::string prefix(g_scriptState.codeBuffer + start, g_scriptState.cursorPos - start);
                    if (prefix.length() >= 2) {
                        g_scriptState.showAutoComplete = true;
                        g_scriptState.autoCompleteIndex = 0;
                        g_scriptState.autoCompletePrefix = prefix;
                        g_scriptState.autoCompleteSuggestions = GetSuggestions(prefix);
                        if (g_scriptState.autoCompleteSuggestions.size() == 1 &&
                            g_scriptState.autoCompleteSuggestions[0].text == prefix)
                            g_scriptState.showAutoComplete = false;
                    }
                    else g_scriptState.showAutoComplete = false;
                }
                else g_scriptState.showAutoComplete = false;
            }
            key = GetCharPressed();
        }

        bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

        if (checkKey(KEY_BACKSPACE)) {
            if (HasSelection()) { GuardarEstadoHistorial(); ReplaceSelection(""); g_scriptState.showAutoComplete = false; }
            else if (g_scriptState.cursorPos > 0) {
                GuardarEstadoHistorial();
                int cursorLine = GetLineFromPos(g_scriptState.codeBuffer, g_scriptState.cursorPos);
                int lineStartPos = GetLineStart(g_scriptState.codeBuffer, cursorLine);
                int col = g_scriptState.cursorPos - lineStartPos;
                if (col == 0 && cursorLine > 0) {
                    int prevLineEnd = GetLineEnd(g_scriptState.codeBuffer, cursorLine - 1);
                    int prevLineStart = GetLineStart(g_scriptState.codeBuffer, cursorLine - 1);
                    std::string prevLine;
                    for (int j = prevLineStart; j < prevLineEnd; ++j) {
                        char c = g_scriptState.codeBuffer[j];
                        if (c != '\r' && c != '\n') prevLine.push_back(c);
                    }
                    int prevVisibleLen = (int)prevLine.size();
                    DeleteChar(g_scriptState.codeBuffer, prevLineEnd);
                    g_scriptState.cursorPos = prevLineStart + prevVisibleLen;
                }
                else {
                    int lineEndPos = GetLineEnd(g_scriptState.codeBuffer, cursorLine);
                    std::string line;
                    for (int j = lineStartPos; j < lineEndPos; ++j) {
                        char c = g_scriptState.codeBuffer[j];
                        if (c != '\r') line.push_back(c);
                    }
                    int lineLen = (int)line.size();
                    if (col > lineLen) g_scriptState.cursorPos = lineStartPos + lineLen;
                    if (g_scriptState.cursorPos > lineStartPos) {
                        g_scriptState.cursorPos--;
                        DeleteChar(g_scriptState.codeBuffer, g_scriptState.cursorPos);
                    }
                }
                g_scriptState.showAutoComplete = false;
                lastCursorPos = g_scriptState.cursorPos;
            }
        }

        if (checkKey(KEY_DELETE)) {
            if (HasSelection()) { GuardarEstadoHistorial(); ReplaceSelection(""); g_scriptState.showAutoComplete = false; }
            else {
                GuardarEstadoHistorial();
                DeleteChar(g_scriptState.codeBuffer, g_scriptState.cursorPos);
                g_scriptState.showAutoComplete = false;
                lastCursorPos = g_scriptState.cursorPos;
            }
        }

        if (IsKeyPressed(KEY_ENTER)) {
            if (g_scriptState.showAutoComplete && !g_scriptState.autoCompleteSuggestions.empty()) {
                GuardarEstadoHistorial();
                const Suggestion& sel = g_scriptState.autoCompleteSuggestions[g_scriptState.autoCompleteIndex];
                int start = g_scriptState.cursorPos;
                while (start > 0 && isalnum((unsigned char)g_scriptState.codeBuffer[start - 1])) start--;
                int prefixLen = g_scriptState.cursorPos - start;
                for (int i = 0; i < prefixLen; i++) DeleteChar(g_scriptState.codeBuffer, start);
                for (char c : sel.text) InsertChar(g_scriptState.codeBuffer, start++, c);
                g_scriptState.cursorPos = start;
                g_scriptState.showAutoComplete = false;
                lastCursorPos = g_scriptState.cursorPos;
            }
            else {
                GuardarEstadoHistorial();
                if (HasSelection()) ReplaceSelection("");
                if (g_scriptState.cursorPos < (int)sizeof(g_scriptState.codeBuffer) - 1) {
                    InsertChar(g_scriptState.codeBuffer, g_scriptState.cursorPos, '\n');
                    g_scriptState.cursorPos++;
                }
                g_scriptState.showAutoComplete = false;
                lastCursorPos = g_scriptState.cursorPos;
                int newCursorLine = GetLineFromPos(g_scriptState.codeBuffer, g_scriptState.cursorPos);
                if (newCursorLine >= g_scriptState.scrollOffset + visibleLines) {
                    g_scriptState.scrollOffset = newCursorLine - visibleLines + 1;
                    if (g_scriptState.scrollOffset < 0) g_scriptState.scrollOffset = 0;
                    int maxScroll = totalLines - visibleLines;
                    if (maxScroll < 0) maxScroll = 0;
                    if (g_scriptState.scrollOffset > maxScroll) g_scriptState.scrollOffset = maxScroll;
                }
            }
        }

        if (IsKeyPressed(KEY_TAB)) {
            if (g_scriptState.showAutoComplete && !g_scriptState.autoCompleteSuggestions.empty()) {
                GuardarEstadoHistorial();
                const Suggestion& sel = g_scriptState.autoCompleteSuggestions[g_scriptState.autoCompleteIndex];
                int start = g_scriptState.cursorPos;
                while (start > 0 && isalnum((unsigned char)g_scriptState.codeBuffer[start - 1])) start--;
                int prefixLen = g_scriptState.cursorPos - start;
                for (int i = 0; i < prefixLen; i++) DeleteChar(g_scriptState.codeBuffer, start);
                for (char c : sel.text) InsertChar(g_scriptState.codeBuffer, start++, c);
                g_scriptState.cursorPos = start;
                g_scriptState.showAutoComplete = false;
                lastCursorPos = g_scriptState.cursorPos;
            }
            else {
                GuardarEstadoHistorial();
                if (HasSelection()) ReplaceSelection("");
                const char* spaces = "    ";
                for (int i = 0; spaces[i]; i++) InsertChar(g_scriptState.codeBuffer, g_scriptState.cursorPos + i, spaces[i]);
                g_scriptState.cursorPos += 4;
                g_scriptState.showAutoComplete = false;
                lastCursorPos = g_scriptState.cursorPos;
            }
        }

        if (g_scriptState.showAutoComplete) {
            if (IsKeyPressed(KEY_UP)) {
                g_scriptState.autoCompleteIndex--;
                if (g_scriptState.autoCompleteIndex < 0) g_scriptState.autoCompleteIndex = (int)g_scriptState.autoCompleteSuggestions.size() - 1;
            }
            if (IsKeyPressed(KEY_DOWN)) {
                g_scriptState.autoCompleteIndex++;
                if (g_scriptState.autoCompleteIndex >= (int)g_scriptState.autoCompleteSuggestions.size()) g_scriptState.autoCompleteIndex = 0;
            }
            if (IsKeyPressed(KEY_ESCAPE)) g_scriptState.showAutoComplete = false;
        }

        // Movimiento del cursor
        if (checkKey(KEY_LEFT)) {
            int curLine = GetLineFromPos(g_scriptState.codeBuffer, g_scriptState.cursorPos);
            int ls = GetLineStart(g_scriptState.codeBuffer, curLine), le = GetLineEnd(g_scriptState.codeBuffer, curLine);
            std::string line;
            for (int j = ls; j < le; ++j) { char c = g_scriptState.codeBuffer[j]; if (c != '\r') line.push_back(c); }
            int len = (int)line.size();
            int col = g_scriptState.cursorPos - ls;
            if (col > len) { g_scriptState.cursorPos = ls + len; col = len; }
            if (shift && !HasSelection()) g_scriptState.selectionStart = g_scriptState.cursorPos;
            if (col > 0) {
                int np = g_scriptState.cursorPos - 1;
                while (np > ls && g_scriptState.codeBuffer[np] == '\r') np--;
                g_scriptState.cursorPos = np;
            }
            if (shift) g_scriptState.selectionEnd = g_scriptState.cursorPos; else ClearSelection();
            g_scriptState.showAutoComplete = false;
            lastCursorPos = g_scriptState.cursorPos;
        }
        if (checkKey(KEY_RIGHT)) {
            int curLine = GetLineFromPos(g_scriptState.codeBuffer, g_scriptState.cursorPos);
            int ls = GetLineStart(g_scriptState.codeBuffer, curLine), le = GetLineEnd(g_scriptState.codeBuffer, curLine);
            std::string line;
            for (int j = ls; j < le; ++j) { char c = g_scriptState.codeBuffer[j]; if (c != '\r') line.push_back(c); }
            int len = (int)line.size();
            int col = g_scriptState.cursorPos - ls;
            if (col > len) { g_scriptState.cursorPos = ls + len; col = len; }
            if (shift && !HasSelection()) g_scriptState.selectionStart = g_scriptState.cursorPos;
            if (col < len) {
                int np = g_scriptState.cursorPos + 1;
                while (np < le && g_scriptState.codeBuffer[np] == '\r') np++;
                if (np <= le) g_scriptState.cursorPos = np;
            }
            if (shift) g_scriptState.selectionEnd = g_scriptState.cursorPos; else ClearSelection();
            g_scriptState.showAutoComplete = false;
            lastCursorPos = g_scriptState.cursorPos;
        }
        if (checkKey(KEY_UP)) {
            int curLine = GetLineFromPos(g_scriptState.codeBuffer, g_scriptState.cursorPos);
            if (curLine > 0) {
                int ls = GetLineStart(g_scriptState.codeBuffer, curLine), le = GetLineEnd(g_scriptState.codeBuffer, curLine);
                std::string line; for (int j = ls; j < le; ++j) { char c = g_scriptState.codeBuffer[j]; if (c != '\r') line.push_back(c); }
                int len = (int)line.size();
                int col = g_scriptState.cursorPos - ls;
                if (col > len) col = len;
                int nls = GetLineStart(g_scriptState.codeBuffer, curLine - 1), nle = GetLineEnd(g_scriptState.codeBuffer, curLine - 1);
                std::string nline; for (int j = nls; j < nle; ++j) { char c = g_scriptState.codeBuffer[j]; if (c != '\r') nline.push_back(c); }
                int nlen = (int)nline.size();
                int ncol = (col < nlen) ? col : nlen;
                g_scriptState.cursorPos = nls + ncol;
                if (shift) {
                    if (g_scriptState.selectionStart == -1) g_scriptState.selectionStart = g_scriptState.cursorPos;
                    g_scriptState.selectionEnd = g_scriptState.cursorPos;
                }
                else ClearSelection();
                lastCursorPos = g_scriptState.cursorPos;
            }
            g_scriptState.showAutoComplete = false;
        }
        if (checkKey(KEY_DOWN)) {
            int curLine = GetLineFromPos(g_scriptState.codeBuffer, g_scriptState.cursorPos);
            if (curLine < totalLines - 1) {
                int ls = GetLineStart(g_scriptState.codeBuffer, curLine), le = GetLineEnd(g_scriptState.codeBuffer, curLine);
                std::string line; for (int j = ls; j < le; ++j) { char c = g_scriptState.codeBuffer[j]; if (c != '\r') line.push_back(c); }
                int len = (int)line.size();
                int col = g_scriptState.cursorPos - ls;
                if (col > len) col = len;
                int nls = GetLineStart(g_scriptState.codeBuffer, curLine + 1), nle = GetLineEnd(g_scriptState.codeBuffer, curLine + 1);
                std::string nline; for (int j = nls; j < nle; ++j) { char c = g_scriptState.codeBuffer[j]; if (c != '\r') nline.push_back(c); }
                int nlen = (int)nline.size();
                int ncol = (col < nlen) ? col : nlen;
                g_scriptState.cursorPos = nls + ncol;
                if (shift) {
                    if (g_scriptState.selectionStart == -1) g_scriptState.selectionStart = g_scriptState.cursorPos;
                    g_scriptState.selectionEnd = g_scriptState.cursorPos;
                }
                else ClearSelection();
                lastCursorPos = g_scriptState.cursorPos;
            }
            g_scriptState.showAutoComplete = false;
        }

        // --- Atajos de teclado (Ctrl) ---
        if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
            bool ctrlShiftZ = (IsKeyPressed(KEY_Z) && (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)));

            if (IsKeyPressed(KEY_Z) && !ctrlShiftZ) {
                if (!undoStack.empty()) {
                    TextoEstado actual;
                    memcpy(actual.buffer, g_scriptState.codeBuffer, sizeof(actual.buffer));
                    actual.cursorPos = g_scriptState.cursorPos;
                    actual.scrollOffset = g_scriptState.scrollOffset;
                    actual.selectionStart = g_scriptState.selectionStart;
                    actual.selectionEnd = g_scriptState.selectionEnd;
                    redoStack.push_back(actual);

                    TextoEstado anterior = undoStack.back();
                    undoStack.pop_back();
                    AplicarEstado(anterior);
                    lastCursorPos = g_scriptState.cursorPos;
                }
            }
            else if (IsKeyPressed(KEY_Y) || ctrlShiftZ) {
                if (!redoStack.empty()) {
                    TextoEstado actual;
                    memcpy(actual.buffer, g_scriptState.codeBuffer, sizeof(actual.buffer));
                    actual.cursorPos = g_scriptState.cursorPos;
                    actual.scrollOffset = g_scriptState.scrollOffset;
                    actual.selectionStart = g_scriptState.selectionStart;
                    actual.selectionEnd = g_scriptState.selectionEnd;
                    undoStack.push_back(actual);

                    TextoEstado siguiente = redoStack.back();
                    redoStack.pop_back();
                    AplicarEstado(siguiente);
                    lastCursorPos = g_scriptState.cursorPos;
                }
            }

            if (IsKeyPressed(KEY_C) && HasSelection()) SetClipboardText(GetSelectedText().c_str());
            if (IsKeyPressed(KEY_X) && HasSelection()) { SetClipboardText(GetSelectedText().c_str()); ReplaceSelection(""); }
            if (IsKeyPressed(KEY_V)) {
                const char* clip = GetClipboardText();
                if (clip && clip[0]) {
                    GuardarEstadoHistorial();
                    if (HasSelection()) ReplaceSelection("");
                    std::string raw(clip), normalized;
                    for (size_t i = 0; i < raw.size(); ++i) {
                        if (raw[i] == '\r') { if (i + 1 < raw.size() && raw[i + 1] == '\n') { normalized.push_back('\n'); i++; } }
                        else normalized.push_back(raw[i]);
                    }
                    std::string clean = SanitizeScript(normalized);
                    for (char c : clean) InsertChar(g_scriptState.codeBuffer, g_scriptState.cursorPos++, c);
                    SanitizeBuffer();
                    lastCursorPos = g_scriptState.cursorPos;
                    g_scriptState.showAutoComplete = false;
                }
            }
        }
        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_A)) {
            g_scriptState.selectionStart = 0;
            g_scriptState.selectionEnd = (int)strlen(g_scriptState.codeBuffer);
        }
        if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyDown(KEY_LEFT_SHIFT) && IsKeyPressed(KEY_L)) SanitizeBuffer();
        if (IsKeyPressed(KEY_ESCAPE)) { ClearSelection(); g_scriptState.showAutoComplete = false; }
    }

    // Interacción con ratón (scroll, selección, botones, sub-barra)
    static bool mouseDragging = false;
    static int mouseSelectionStart = -1;
    static float scrollGrabOffsetY = 0.0f;

    if (arrastrandoScroll) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && totalLines > visibleLines && scrollbarRec.height > 0.0f) {
            float desiredBarY = mousePos.y - scrollGrabOffsetY;
            float trackTop = codeRect.y, trackBottom = codeRect.y + codeRect.height - scrollbarRec.height;
            if (desiredBarY < trackTop) desiredBarY = trackTop;
            if (desiredBarY > trackBottom) desiredBarY = trackBottom;
            float t = (desiredBarY - codeRect.y) / (codeRect.height - scrollbarRec.height);
            int maxScroll = totalLines - visibleLines;
            if (maxScroll < 0) maxScroll = 0;
            g_scriptState.scrollOffset = (int)(t * maxScroll + 0.5f);
            if (g_scriptState.scrollOffset < 0) g_scriptState.scrollOffset = 0;
            if (g_scriptState.scrollOffset > maxScroll) g_scriptState.scrollOffset = maxScroll;
        }
        else arrastrandoScroll = false;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointRec(mousePos, undoBtn) && !undoStack.empty()) {
            TextoEstado actual;
            memcpy(actual.buffer, g_scriptState.codeBuffer, sizeof(actual.buffer));
            actual.cursorPos = g_scriptState.cursorPos;
            actual.scrollOffset = g_scriptState.scrollOffset;
            actual.selectionStart = g_scriptState.selectionStart;
            actual.selectionEnd = g_scriptState.selectionEnd;
            redoStack.push_back(actual);
            TextoEstado anterior = undoStack.back();
            undoStack.pop_back();
            AplicarEstado(anterior);
            lastCursorPos = g_scriptState.cursorPos;
        }
        else if (CheckCollisionPointRec(mousePos, redoBtn) && !redoStack.empty()) {
            TextoEstado actual;
            memcpy(actual.buffer, g_scriptState.codeBuffer, sizeof(actual.buffer));
            actual.cursorPos = g_scriptState.cursorPos;
            actual.scrollOffset = g_scriptState.scrollOffset;
            actual.selectionStart = g_scriptState.selectionStart;
            actual.selectionEnd = g_scriptState.selectionEnd;
            undoStack.push_back(actual);
            TextoEstado siguiente = redoStack.back();
            redoStack.pop_back();
            AplicarEstado(siguiente);
            lastCursorPos = g_scriptState.cursorPos;
        }
        else if (CheckCollisionPointRec(mousePos, openBtn)) LoadScript();
        else if (CheckCollisionPointRec(mousePos, saveBtn)) SaveScript();
        else if (totalLines > visibleLines && CheckCollisionPointRec(mousePos, scrollbarRec)) {
            arrastrandoScroll = true;
            scrollGrabOffsetY = mousePos.y - scrollbarRec.y;
        }
        else if (CheckCollisionPointRec(mousePos, codeRect)) {
            g_scriptState.isFocused = true;
            float mouseX = mousePos.x, mouseY = mousePos.y;
            int lineIndex = (int)((mouseY - (codeY + 4)) / lineHeight) + g_scriptState.scrollOffset;
            if (lineIndex < 0) lineIndex = 0;
            if (lineIndex >= totalLines) lineIndex = totalLines - 1;
            int lineStartPos = GetLineStart(g_scriptState.codeBuffer, lineIndex);
            int lineEndPos = GetLineEnd(g_scriptState.codeBuffer, lineIndex);
            int lineLen = lineEndPos - lineStartPos;
            int bestCol = 0; float bestDist = 1e9f;
            for (int col = 0; col <= lineLen; col++) {
                std::string substr(g_scriptState.codeBuffer + lineStartPos, col);
                float width = MeasureTextEx(font, substr.c_str(), fontSize, spacing).x;
                float dist = fabs(mouseX - (textX + width));
                if (dist < bestDist) { bestDist = dist; bestCol = col; }
            }
            g_scriptState.cursorPos = lineStartPos + bestCol;
            lastCursorPos = g_scriptState.cursorPos;
            if (!IsKeyDown(KEY_LEFT_SHIFT) && !IsKeyDown(KEY_RIGHT_SHIFT)) {
                ClearSelection();
                mouseSelectionStart = g_scriptState.cursorPos;
                mouseDragging = true;
            }
            else {
                if (g_scriptState.selectionStart == -1) g_scriptState.selectionStart = g_scriptState.cursorPos;
                g_scriptState.selectionEnd = g_scriptState.cursorPos;
                mouseDragging = false;
            }
        }
        else {
            if (!CheckCollisionPointRec(mousePos, sidebarRect)) {
                g_scriptState.isFocused = false;
                g_scriptState.showAutoComplete = false;
            }
            mouseDragging = false;
        }
    }

    if (g_scriptState.isFocused && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && mouseDragging) {
        float mouseX = mousePos.x, mouseY = mousePos.y;
        int lineIndex = (int)((mouseY - (codeY + 4)) / lineHeight) + g_scriptState.scrollOffset;
        if (lineIndex < 0) lineIndex = 0;
        if (lineIndex >= totalLines) lineIndex = totalLines - 1;
        int lineStartPos = GetLineStart(g_scriptState.codeBuffer, lineIndex);
        int lineEndPos = GetLineEnd(g_scriptState.codeBuffer, lineIndex);
        int lineLen = lineEndPos - lineStartPos;
        int bestCol = 0; float bestDist = 1e9f;
        for (int col = 0; col <= lineLen; col++) {
            std::string substr(g_scriptState.codeBuffer + lineStartPos, col);
            float width = MeasureTextEx(font, substr.c_str(), fontSize, spacing).x;
            float dist = fabs(mouseX - (textX + width));
            if (dist < bestDist) { bestDist = dist; bestCol = col; }
        }
        int newPos = lineStartPos + bestCol;
        g_scriptState.selectionStart = mouseSelectionStart;
        g_scriptState.selectionEnd = newPos;
        g_scriptState.cursorPos = newPos;
        lastCursorPos = newPos;
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        mouseDragging = false;
        arrastrandoScroll = false;
    }
}

const char* CreateAndGetVirtualScriptPath(const char* suggestedName = nullptr) {
    CreateVirtualScript(suggestedName);
    return openScriptsTabs[activeScriptTab].c_str();
}

const char* GetScriptBufferContent(const char* path) {
    auto it = scriptBuffers.find(std::string(path));
    if (it != scriptBuffers.end()) return it->second.c_str();
    return nullptr;
}

void RegisterScriptInEditor(const char* virtualPath, const char* source) {
    std::string spath(virtualPath);
    // Evitar duplicados
    if (scriptBuffers.find(spath) != scriptBuffers.end()) return;

    openScriptsTabs.push_back(spath);
    scriptBuffers[spath] = std::string(source);
    scriptIsVirtual[spath] = true;
    scriptVirtualDisplayName[spath] = spath.substr(8); // quitar "virtual:"
}

void RegisterScriptingLuaFunctions(lua_State* L) {
    RegisterLuaFunctions(L);
}