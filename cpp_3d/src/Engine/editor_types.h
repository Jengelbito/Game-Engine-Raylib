#ifndef EDITOR_TYPES_H
#define EDITOR_TYPES_H

#include "raylib.h"

enum GameMode { MODE_EDITOR, MODE_PLAY };
enum ShapeType { SHAPE_CUBE, SHAPE_SPHERE, SHAPE_CYLINDER, SHAPE_CONE, SHAPE_PORTAL, SHAPE_CUSTOM_MODEL};
enum SelectedObjectType { OBJ_NONE = -1, OBJ_BLOCK, OBJ_PLAYER, OBJ_COIN, OBJ_ENEMY };

struct EditorBlock {
    Vector3 position;
    Vector3 size;
    Color color;
    ShapeType shapeType;
    int targetScene = 0;
    char texturePath[512] = { 0 };
    bool hasTexture = false;
    bool hasScript = false;
    char scriptPath[512] = "";
    Texture2D texture;
    char scriptSource[8192];
    bool hasCustomModel = false;
    Model customModel;
    char modelPath[512];
    Vector3 modelOriginalSize;
    Vector3 modelPivotOffset;
};

#endif