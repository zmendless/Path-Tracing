/*******************************************************************************
 * Accumulation-based path tracer, C + raylib
 * -------------------------------------------------------------------------
 * Converted from a Shadertoy-style GLSL path tracer that took 64 samples in
 * a single fragment shader invocation every frame.
 *
 * Here the same path tracer runs ONE sample per pixel per frame. That single
 * noisy sample is added into a floating-point accumulation buffer (additive
 * blending), and a second "display" pass divides the running sum by the
 * number of frames accumulated so far to show the converging average.
 *
 * As long as the camera doesn't move, the image keeps getting cleaner every
 * frame (classic progressive/accumulation path tracing). Moving the camera
 * (drag mouse / scroll wheel) resets the accumulation buffer and starts over.
 *
 * Build (Linux/macOS, raylib installed via package manager or built from
 * source):
 *     gcc main.c -o pathtracer -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
 *
 * Build (using a local raylib build, adjust paths as needed):
 *     gcc main.c -o pathtracer -I/path/to/raylib/src -L/path/to/raylib/src \
 *         -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
 *
 * Run from the project root so the shader paths (resources/shaders/...)
 * resolve correctly.
 ******************************************************************************/

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stdio.h>

//------------------------------------------------------------------------------
// Float render texture helpers
//
// raylib's LoadRenderTexture() gives you an 8-bit-per-channel target, which
// isn't enough precision to sum hundreds/thousands of accumulated samples
// without banding. We build a float (R32G32B32A32) render texture manually
// via rlgl instead.
//------------------------------------------------------------------------------
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

//------------------------------------------------------------------------------
// Simple orbit camera (mouse drag to orbit, wheel to zoom)
//------------------------------------------------------------------------------
typedef struct {
    Vector3 target;
    float yaw;      // radians, around Y
    float pitch;    // radians, clamped to avoid flipping over the poles
    float distance; // orbit radius
} OrbitCam;

// Builds a camera-ray basis (origin + forward/right/up) from the orbit params.
static void OrbitCamBasis(const OrbitCam *cam, Vector3 *ro, Vector3 *forward, Vector3 *right, Vector3 *up)
{
    float cy = cosf(cam->yaw),   sy = sinf(cam->yaw);
    float cp = cosf(cam->pitch), sp = sinf(cam->pitch);

    // Direction from the camera position TO the target.
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

    // Path tracing is expensive per-pixel; let the GPU/driver pace us via
    // vsync rather than a fixed low target FPS, so we accumulate as fast as
    // the hardware allows.
    SetTargetFPS(0);

    Shader pathShader    = LoadShader(0, "resources/shaders/pathtrace.fs");
    Shader displayShader = LoadShader(0, "resources/shaders/display.fs");

    // --- pathtrace.fs uniforms ---
    int locResolution = GetShaderLocation(pathShader, "u_resolution");
    int locFrame      = GetShaderLocation(pathShader, "u_frame");
    int locTime       = GetShaderLocation(pathShader, "u_time");
    int locRo         = GetShaderLocation(pathShader, "u_ro");
    int locForward    = GetShaderLocation(pathShader, "u_forward");
    int locRight      = GetShaderLocation(pathShader, "u_right");
    int locUp         = GetShaderLocation(pathShader, "u_up");
    int locFov        = GetShaderLocation(pathShader, "u_fov");

    // --- display.fs uniforms ---
    int locDispResolution  = GetShaderLocation(displayShader, "u_resolution");
    int locSampleCount     = GetShaderLocation(displayShader, "u_sampleCount");

    int width  = GetScreenWidth();
    int height = GetScreenHeight();

    RenderTexture2D accum = LoadRenderTextureFloat(width, height);

    // Orbit camera, initialized to roughly match the original fixed
    // ro = vec3(0, 0.5, -6) looking toward +Z.
    OrbitCam cam = { 0 };
    cam.target   = (Vector3){ 0.0f, 1.0f, 0.0f };
    cam.yaw      = 1.5708f;  // ~+Z
    cam.pitch    = -0.06f;
    cam.distance = 6.6f;

    const float fov = 1.5f; // matches the "1.4" in the original vec3(sampleUV, 1.4)

    int  sampleCount = 0;
    bool dragging    = false;
    Vector2 lastMouse = { 0 };

    while (!WindowShouldClose())
    {
        int newWidth  = GetScreenWidth();
        int newHeight = GetScreenHeight();

        bool cameraChanged = false;

        // --- Orbit controls: left-drag to rotate, wheel to zoom ---
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

        // --- Handle window resize: reallocate the float accumulation buffer ---
        if (newWidth != width || newHeight != height)
        {
            width  = newWidth;
            height = newHeight;

            UnloadRenderTextureFloat(accum);
            accum = LoadRenderTextureFloat(width, height);
            cameraChanged = true;
        }

        // Any camera movement invalidates everything accumulated so far.
        if (cameraChanged) sampleCount = 0;

        Vector3 ro, forward, right, up;
        OrbitCamBasis(&cam, &ro, &forward, &right, &up);

        sampleCount++;

        // --- Update pathtrace.fs uniforms ---
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

        // --- Accumulation pass: render ONE new noisy sample and add it in ---
        BeginTextureMode(accum);
            if (sampleCount == 1)
            {
                // Fresh start (camera just moved, or first frame): clear the sum.
                ClearBackground(BLANK);
            }

            BeginBlendMode(BLEND_ADDITIVE); // src + dst, accumulated in a float buffer -> no clamping/banding
                BeginShaderMode(pathShader);
                    // A full-screen rectangle is enough to invoke the fragment
                    // shader for every pixel; raylib draws shapes with a 1x1
                    // white texture, so the shader's own math produces the image.
                    DrawRectangle(0, 0, width, height, WHITE);
                EndShaderMode();
            EndBlendMode();
        EndTextureMode();

        // --- Display pass: divide the sum by sampleCount and tonemap ---
        BeginDrawing();
            ClearBackground(BLACK);

            float res2[2] = { (float)width, (float)height };
            SetShaderValue(displayShader, locDispResolution, res2, SHADER_UNIFORM_VEC2);
            float sc = (float)sampleCount;
            SetShaderValue(displayShader, locSampleCount, &sc, SHADER_UNIFORM_FLOAT);

            BeginShaderMode(displayShader);
                // Render textures are stored flipped vertically in OpenGL;
                // draw with a negative-height source rect to flip it back.
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