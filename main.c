#include "raylib.h"
#include <stdio.h>
#include <sys/stat.h>

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 800

#define TILES_X 2
#define TILES_Y 2
#define TILE_WIDTH (SCREEN_WIDTH / TILES_X)
#define TILE_HEIGHT (SCREEN_HEIGHT / TILES_Y)

int main(void)
{
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Tiled Raytracer");
    SetTargetFPS(60);

    RenderTexture2D target = LoadRenderTexture(SCREEN_WIDTH, SCREEN_HEIGHT);
    Shader shader = LoadShader(0, "scene.fs");

    int resolutionLoc = GetShaderLocation(shader, "u_resolution");
    int mouseLoc = GetShaderLocation(shader, "u_mouse");
    int timeLoc = GetShaderLocation(shader, "u_time");
    int tileOffsetLoc = GetShaderLocation(shader, "u_tileOffset");

    Vector2 resolution = { SCREEN_WIDTH, SCREEN_HEIGHT };
    Vector2 mouse = { 0.0f, 0.0f };
    float time = 0.0f;

    SetShaderValue(shader, resolutionLoc, &resolution, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, mouseLoc, &mouse, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, timeLoc, &time, SHADER_UNIFORM_FLOAT);

    mkdir("renders", 0777);

    int imageIndex = 0;
    char filename[128];

    while (1)
    {
        snprintf(filename, sizeof(filename), "renders/render_%04d.png", imageIndex);

        if (!FileExists(filename))
            break;

        imageIndex++;
    }

    int tile = 0;
    double renderStart = GetTime();

    while (!WindowShouldClose())
    {
        if (tile < TILES_X * TILES_Y)
        {
            int tileX = tile % TILES_X;
            int tileY = tile / TILES_X;

            Vector2 tileOffset = {
                (float)(tileX * TILE_WIDTH),
                (float)(tileY * TILE_HEIGHT)
            };

            SetShaderValue(shader, tileOffsetLoc, &tileOffset, SHADER_UNIFORM_VEC2);

            BeginTextureMode(target);

            BeginShaderMode(shader);

            DrawRectangle(tileX * TILE_WIDTH,tileY * TILE_HEIGHT,TILE_WIDTH,TILE_HEIGHT,WHITE);

            EndShaderMode();

            EndTextureMode();

            tile++;

            if (tile == TILES_X * TILES_Y)
            {
                double renderTime = GetTime() - renderStart;

                printf("Rendering finished in %.3f seconds\n", renderTime);

                Image image = LoadImageFromTexture(target.texture);

                ImageFlipVertical(&image);

                ExportImage(image, filename);

                printf("Saved: %s\n", filename);

                UnloadImage(image);
            }
        }

        BeginDrawing();

        ClearBackground(BLACK);

        DrawTextureRec(target.texture,(Rectangle){0,0,(float)target.texture.width,-(float)target.texture.height},(Vector2){ 0, 0 },WHITE);

        EndDrawing();
    }

    UnloadShader(shader);
    UnloadRenderTexture(target);
    CloseWindow();

    return 0;
}