#ifndef DEBUG_H
#define DEBUG_H

#include "raylib.h"
#include "player.h"
#include "cameraGame.h"
#include "enemy.h"
#include "coin.h"
#include "editor_types.h"
#include <vector>

struct DebugData {
    const Player* player;
    const CameraGame* camera;
    const Enemy* enemy;
    const Coin* coin;
    const std::vector<std::vector<EditorBlock>>* scenes;
    int currentSceneIndex;
    GameMode mode;
    int consoleOffset;
    int totalLines;
    int lineCount;
    Rectangle consoleRect;
    float camDistancia;
    bool enElSuelo;
};

extern bool g_debugMode;

void DrawDebugInfo(const DebugData& data);
void ToggleDebugMode();

#endif