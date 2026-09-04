#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stdio.h>

static RenderTexture2D LoadRenderTextureFloat(int width, int height)
{
    RenderTexture2D target = { 0 };

    target.id = rlLoadFramebuffer();
    if (target.id > 0)
    {
        rlEnableFramebuffer(target.id);

        target.texture.id = rlLoadTexture(NULL, width, height, PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1);
        target.texture.width = width;
        target.texture.height = height;
        target.texture.mipmaps = 1;
        target.texture.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;

        target.depth.id = 0;
        target.depth.width = width;
        target.depth.height = height;

        rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);

        if (rlFramebufferComplete(target.id))
        {
            TraceLog(LOG_INFO, "FBO: [ID %i] Float accumulation buffer (%ix%i) created successfully", target.id, width, height);
        }
        else
        {
            TraceLog(LOG_WARNING, "FBO: [ID %i] Float accumulation buffer failed to complete", target.id);
        }

        rlDisableFramebuffer();
    }

    return target;
}

static void UnloadRenderTextureFloat(RenderTexture2D target)
{
    if (target.id > 0) rlUnloadFramebuffer(target.id);
}

typedef struct {
    Vector3 target;
    float yaw;
    float pitch;  
    float distance; 
} OrbitCam;

static void OrbitCamBasis(const OrbitCam *cam, Vector3 *ro, Vector3 *forward, Vector3 *right, Vector3 *up)
{
    float cy = cosf(cam->yaw),   sy = sinf(cam->yaw);
    float cp = cosf(cam->pitch), sp = sinf(cam->pitch);

    Vector3 dir = { cy * cp, sp, sy * cp };

    Vector3 pos = {
        cam->target.x - dir.x * cam->distance,
        cam->target.y - dir.y * cam->distance,
        cam->target.z - dir.z * cam->distance
    };

    *forward = Vector3Normalize(Vector3Subtract(cam->target, pos));

    Vector3 worldUp = { 0.0f, 1.0f, 0.0f };
    *right = Vector3Normalize(Vector3CrossProduct(*forward, worldUp));
    *up    = Vector3CrossProduct(*right, *forward);
    *ro    = pos;
}

int main(void)
{
    const int initialWidth  = 900;
    const int initialHeight = 900;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(initialWidth, initialHeight, "Accumulation Path Tracer (C + raylib)");

    SetTargetFPS(0);

    Shader pathShader    = LoadShader(0, "resources/shaders/pathtrace.fs");
    Shader displayShader = LoadShader(0, "resources/shaders/display.fs");

    int locResolution = GetShaderLocation(pathShader, "u_resolution");
    int locFrame      = GetShaderLocation(pathShader, "u_frame");
    int locTime       = GetShaderLocation(pathShader, "u_time");
    int locRo         = GetShaderLocation(pathShader, "u_ro");
    int locForward    = GetShaderLocation(pathShader, "u_forward");
    int locRight      = GetShaderLocation(pathShader, "u_right");
    int locUp         = GetShaderLocation(pathShader, "u_up");
    int locFov        = GetShaderLocation(pathShader, "u_fov");

    int locDispResolution  = GetShaderLocation(displayShader, "u_resolution");
    int locSampleCount     = GetShaderLocation(displayShader, "u_sampleCount");

    int width  = GetScreenWidth();
    int height = GetScreenHeight();

    RenderTexture2D accum = LoadRenderTextureFloat(width, height);

    OrbitCam cam = { 0 };
    cam.target   = (Vector3){ 0.0f, 1.0f, 0.0f };
    cam.yaw      = 1.5708f;
    cam.pitch    = -0.06f;
    cam.distance = 6.6f;

    const float fov = 1.5f;

    int  sampleCount = 0;
    bool dragging    = false;
    Vector2 lastMouse = { 0 };

    while (!WindowShouldClose())
    {
        int newWidth  = GetScreenWidth();
        int newHeight = GetScreenHeight();

        bool cameraChanged = false;

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            dragging  = true;
            lastMouse = GetMousePosition();
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) dragging = false;

        if (dragging)
        {
            Vector2 m = GetMousePosition();
            float dx = m.x - lastMouse.x;
            float dy = m.y - lastMouse.y;

            if (dx != 0.0f || dy != 0.0f)
            {
                cam.yaw   += dx * 0.005f;
                cam.pitch -= dy * 0.005f;
                if (cam.pitch >  1.5f) cam.pitch =  1.5f;
                if (cam.pitch < -1.5f) cam.pitch = -1.5f;

                lastMouse = m;
                cameraChanged = true;
            }
        }

        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
        {
            cam.distance -= wheel * 0.5f;
            if (cam.distance < 0.01f)  cam.distance = 0.01f;
            if (cam.distance > 25.0f) cam.distance = 25.0f;
            cameraChanged = true;
        }

        if (newWidth != width || newHeight != height)
        {
            width  = newWidth;
            height = newHeight;

            UnloadRenderTextureFloat(accum);
            accum = LoadRenderTextureFloat(width, height);
            cameraChanged = true;
        }

        if (cameraChanged) sampleCount = 0;

        Vector3 ro, forward, right, up;
        OrbitCamBasis(&cam, &ro, &forward, &right, &up);

        sampleCount++;

        float res[2] = { (float)width, (float)height };
        SetShaderValue(pathShader, locResolution, res, SHADER_UNIFORM_VEC2);
        SetShaderValue(pathShader, locFrame, &sampleCount, SHADER_UNIFORM_INT);
        float t = (float)GetTime();
        SetShaderValue(pathShader, locTime, &t, SHADER_UNIFORM_FLOAT);
        SetShaderValue(pathShader, locRo, &ro, SHADER_UNIFORM_VEC3);
        SetShaderValue(pathShader, locForward, &forward, SHADER_UNIFORM_VEC3);
        SetShaderValue(pathShader, locRight, &right, SHADER_UNIFORM_VEC3);
        SetShaderValue(pathShader, locUp, &up, SHADER_UNIFORM_VEC3);
        SetShaderValue(pathShader, locFov, &fov, SHADER_UNIFORM_FLOAT);

        BeginTextureMode(accum);
            if (sampleCount == 1)
            {
                ClearBackground(BLANK);
            }

            BeginBlendMode(BLEND_ADDITIVE);
                BeginShaderMode(pathShader);
                    DrawRectangle(0, 0, width, height, WHITE);
                EndShaderMode();
            EndBlendMode();
        EndTextureMode();

        BeginDrawing();
            ClearBackground(BLACK);

            float res2[2] = { (float)width, (float)height };
            SetShaderValue(displayShader, locDispResolution, res2, SHADER_UNIFORM_VEC2);
            float sc = (float)sampleCount;
            SetShaderValue(displayShader, locSampleCount, &sc, SHADER_UNIFORM_FLOAT);

            BeginShaderMode(displayShader);
                DrawTextureRec(
                    accum.texture,
                    (Rectangle){ 0, 0, (float)accum.texture.width, -(float)accum.texture.height },
                    (Vector2){ 0, 0 },
                    WHITE
                );
            EndShaderMode();

            DrawFPS(10, 10);
            DrawText(TextFormat("%d", sampleCount), 10, 34, 20, RAYWHITE);
        EndDrawing();
    }

    UnloadRenderTextureFloat(accum);
    UnloadShader(pathShader);
    UnloadShader(displayShader);

    CloseWindow();

    return 0;
}
