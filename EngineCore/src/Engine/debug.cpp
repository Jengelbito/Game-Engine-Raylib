#include "debug.h"
#include <cstdio>

bool g_debugMode = false;

void ToggleDebugMode() {
    g_debugMode = !g_debugMode;
}

void DrawDebugInfo(const DebugData& data) {
    if (!g_debugMode) return;

    int x = 10;
    int y = 700; // debajo del FPS
    int lineHeight = 24;
    int fontSize = 14;
    Color bg = { 0, 0, 0, 180 };
    int padding = 18;

    int lineCount = 0;
    lineCount++; // título
    lineCount += 2; // jugador
    if (data.camera) lineCount++; // cámara
    if (data.scenes) lineCount += 2; // escenas
    if (data.enemy) lineCount++; // enemigo
    if (data.coin) lineCount++; // moneda
    lineCount++; // consola
    lineCount++; // modo

    int maxWidth = 380;
    int height = lineCount * (lineHeight + 2) + padding * 2;
    DrawRectangle(x, y, maxWidth, height, bg);
    DrawRectangleLines(x, y, maxWidth, height, GOLD);

    int currentY = y + padding;
    int indent = 10;
    auto drawLine = [&](const char* text, Color color = WHITE) {
        DrawText(text, x + indent, currentY, fontSize, color);
        currentY += lineHeight + 2;
        };

    drawLine("=== DEBUG INFO (F10 para ocultar) ===", YELLOW);

    if (data.player) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Jugador: (%.2f, %.2f, %.2f) vel=%.2f",
            data.player->posicion.x, data.player->posicion.y, data.player->posicion.z,
            data.player->velocidadActual);
        drawLine(buf, LIME);
        snprintf(buf, sizeof(buf), "  Suelo: %s  Vidas: %d  Puntos: %d",
            data.enElSuelo ? "SI" : "NO", data.player->vidas, data.player->puntos);
        drawLine(buf, LIME);
    }

    if (data.camera) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Camara: (%.2f, %.2f, %.2f) Target: (%.2f, %.2f, %.2f)",
            data.camera->camara.position.x, data.camera->camara.position.y, data.camera->camara.position.z,
            data.camera->camara.target.x, data.camera->camara.target.y, data.camera->camara.target.z);
        drawLine(buf, SKYBLUE);
        if (data.mode == MODE_PLAY) {
            snprintf(buf, sizeof(buf), "  AngH=%.2f AngV=%.2f Dist=%.2f",
                data.camera->anguloHorizontal, data.camera->anguloVertical, data.camDistancia);
            drawLine(buf, SKYBLUE);
        }
    }

    if (data.scenes) {
        char buf[128];
        int totalBlocks = 0;
        for (const auto& scene : *data.scenes) totalBlocks += scene.size();
        snprintf(buf, sizeof(buf), "Escenas: %zu actual=%d  Bloques totales=%d",
            data.scenes->size(), data.currentSceneIndex, totalBlocks);
        drawLine(buf, ORANGE);
        if (!data.scenes->empty()) {
            const auto& currentScene = (*data.scenes)[data.currentSceneIndex];
            snprintf(buf, sizeof(buf), "  Bloques en escena actual: %zu", currentScene.size());
            drawLine(buf, ORANGE);
        }
    }

    if (data.enemy) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Enemigo: (%.2f, %.2f, %.2f) tam=(%.2f,%.2f,%.2f)",
            data.enemy->posicion.x, data.enemy->posicion.y, data.enemy->posicion.z,
            data.enemy->tamano.x, data.enemy->tamano.y, data.enemy->tamano.z);
        drawLine(buf, RED);
    }

    if (data.coin) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Moneda: (%.2f, %.2f, %.2f) radio=%.2f activa=%s",
            data.coin->posicion.x, data.coin->posicion.y, data.coin->posicion.z,
            data.coin->radio, data.coin->activa ? "SI" : "NO");
        drawLine(buf, GOLD);
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "Consola: offset=%d total=%d line=%d",
        data.consoleOffset, data.totalLines, data.lineCount);
    drawLine(buf, GRAY);

    drawLine(TextFormat("Modo: %s", (data.mode == MODE_EDITOR) ? "EDITOR" : "JUEGO"), SKYBLUE);
}