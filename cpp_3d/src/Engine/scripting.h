#ifndef SCRIPTING_H
#define SCRIPTING_H

#include "raylib.h"
#include "editor_types.h"
#include "player.h"
#include "coin.h"
#include "enemy.h"
#include "cameraGame.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <deque>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

extern std::string g_updateScriptCode;

extern const char* CreateAndGetVirtualScriptPath();

std::string SanitizeScript(const std::string& input);

struct ScriptEditorState {
    bool open = false;
    Rectangle rect;
    char codeBuffer[8192] = "print('Hello World!')\n-- Escribe tu codigo aqui";
    int cursorPos = 0;
    int selectionStart = -1;
    int selectionEnd = -1;
    int scrollOffset = 0;
    bool isFocused = false;
    std::deque<std::string> outputBuffer;

    Font codeFont;
    bool fontLoaded = false;

    bool showAutoComplete = false;
    int autoCompleteIndex = 0;
    std::string autoCompletePrefix;
    std::vector<struct Suggestion> autoCompleteSuggestions;

    std::vector<std::vector<EditorBlock>>* scenes = nullptr;
    int* currentSceneIndex = nullptr;
    Player* player = nullptr;
    Coin* coin = nullptr;
    Enemy* enemy = nullptr;
    CameraGame* camera = nullptr;
    float* camDistancia = nullptr;
    float* velocidadY = nullptr;
    bool* enElSuelo = nullptr;
    bool* puedeRecibirDano = nullptr;
    float* temporizadorDano = nullptr;
};

extern ScriptEditorState g_scriptState;

void DrawScriptEditorInRect(Rectangle rect, float scaleFactor);
void InitScripting();
void SetScriptContext(
    std::vector<std::vector<EditorBlock>>* scenes,
    int* currentSceneIndex,
    Player* player,
    Coin* coin,
    Enemy* enemy,
    CameraGame* camera,
    float* camDistancia,
    float* velocidadY = nullptr,
    bool* enElSuelo = nullptr,
    bool* puedeRecibirDano = nullptr,
    float* temporizadorDano = nullptr
);
void RegisterScriptingLuaFunctions(lua_State* L);
void RegisterScriptInEditor(const char* virtualPath, const char* source);
void SetUpdateScript(const char* script);
void UpdateScripts();
void DrawScriptEditor();
void ExecuteScript(const char* code);
void AddScriptOutput(const std::string& text);
void AddScriptTab(const char* path);
void CloseScriptTab(int index);
void SetActiveScript(int index);
void SyncEditorWithNode(const char* scriptPath);
bool IsMouseOverScriptEditor();

#endif