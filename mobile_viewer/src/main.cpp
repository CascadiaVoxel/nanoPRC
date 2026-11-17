/* Copyright (C) 2023-2026 CascadiaVoxel LLC

    nanoPRC is free software: you can redistribute it and/or modify it under
    the terms of the GNU Affero General Public License as published by the
    Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    nanoPRC is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
    License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with nanoPRC. If not, see <https://www.gnu.org/licenses/>.
*/

#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cinttypes>
#include <cstring>
#include <limits>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include <prc_api.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_opengl3.h>

#include "gles3_loader.h"
#include "scene.h"
#include "camera.h"
#include "product.h"
#include "mesh.h"
#include "skybox.h"
#include "shadow.h"
#include "composite.h"
#include "util.h"
#include "engine.h"

extern void releaseTextures();
static std::string g_loadErrorMessage;
static constexpr float kMoveSpeed = 4.0f;
static constexpr float kShiftMultiplier = 300.0f;
static constexpr float kTurnSpeed = 90.0f;

static const DirLight kDirLight = {
    Vector3(-0.174078f, -0.69311f, 0.696311f), // direction
    colorRGB(255, 255, 224),                   // color
    1.0f                                       // intensity
};

static constexpr PointLight kPointLight = {
    Vector3(14.0f, 7.0f, 3.0f), // position
    Vector3(1.0f, 1.0f, 1.0f),  // color
    0.0f                        // intensity
};

struct AtmosphereProperties
{
    float sunAltitude = 40.0f;
    float sunAzimuth = 0.0f;
    Vector3 sunColor = Vector3(1.0f, 1.0f, 1.0f);
    float sunIntensity = 1.0f;
    Vector3 skyColorHorizon = Vector3(56.0f / 255.0f);
    Vector3 skyColorZenith = Vector3(26.0f / 255.0f);
    float sunTightness = 500.0f;
};

static AtmosphereProperties g_atmosphere;
static Product *g_selectedProduct = nullptr;
static float g_renderScale = 1.0f;

static struct ActiveTouch {
    Uint64 id = 0;
    float x = 0.0f;
    float y = 0.0f;
    bool active = false;
} activeFingers[2];

/* Drag and drop support for Web */
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

static std::string g_pendingWebDropPath;

extern "C" {
    EMSCRIPTEN_KEEPALIVE
        void OnWebFileDropped(const char *virtualPath)
    {
        if (virtualPath && virtualPath[0] != '\0')
            g_pendingWebDropPath = virtualPath;
    }
}
#endif

bool ResolveCrossPlatformAssetPath(const char *relativeAssetName, std::string &outAbsolutePath)
{
#if defined(SDL_PLATFORM_ANDROID)
    const char *internalWriteDir = SDL_GetPrefPath("com.cascadvoxel", "nanoprc_viewer");
    if (!internalWriteDir) return false;

    outAbsolutePath = std::string(internalWriteDir) + relativeAssetName;

    SDL_PathInfo fileInfo;
    if (SDL_GetPathInfo(outAbsolutePath.c_str(), &fileInfo)) {
        return true;
    }

    std::string assetResourcePath = std::string("assets/") + relativeAssetName;
    SDL_IOStream *assetIn = SDL_IOFromFile(assetResourcePath.c_str(), "rb");
    if (!assetIn) return false;

    SDL_IOStream *cacheOut = SDL_IOFromFile(outAbsolutePath.c_str(), "wb");
    if (!cacheOut) {
        SDL_CloseIO(assetIn);
        return false;
    }

    const size_t bufferSize = 4096;
    std::vector<uint8_t> buffer(bufferSize);
    size_t bytesRead = 0;
    while ((bytesRead = SDL_ReadIO(assetIn, buffer.data(), bufferSize)) > 0) {
        SDL_WriteIO(cacheOut, buffer.data(), bytesRead);
    }
    SDL_CloseIO(assetIn);
    SDL_CloseIO(cacheOut);
    return true;
#else
    outAbsolutePath = relativeAssetName;
    return true;
#endif
}

static float arc_projection(float x, float y)
{
    float r = 1.0f;
    float dist2 = x * x + y * y;
    float r2 = r * r;
    if (dist2 * 2.0f <= r2)
    {
        float z_arg = r2 - dist2;
        return sqrtf(z_arg < 0.0f ? 0.0f : z_arg);
    }
    return (r2 / 2.0f) / sqrtf(dist2);
}

static Matrix4 compute_rotation2(float x1, float y1, float z1, float x2, float y2, float z2)
{
    float lenSq1 = x1 * x1 + y1 * y1;
    Vector3 v0 = normalize(Vector3(x1, y1, lenSq1 <= 1.0f ? sqrtf(1.0f - lenSq1) : 0.0f));

    float lenSq2 = x2 * x2 + y2 * y2;
    Vector3 v1 = normalize(Vector3(x2, y2, lenSq2 <= 1.0f ? sqrtf(1.0f - lenSq2) : 0.0f));

    Vector3 axis = cross(v0, v1);
    float angle = acosf(clamp(dot(v0, v1), -1.0f, 1.0f));

    float c = cosf(angle);
    float s = sinf(angle);
    float t = 1.0f - c;
    float mag = sqrtf(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);

    if (mag > 0.0f)
    {
        axis.x /= mag;
        axis.y /= mag;
        axis.z /= mag;
    }
    else
    {
        axis = Vector3(0.0f, 0.0f, 1.0f);
    }

    float r00 = c + axis.x * axis.x * t;
    float r01 = axis.x * axis.y * t - axis.z * s;
    float r02 = axis.x * axis.z * t + axis.y * s;
    float r10 = axis.y * axis.x * t + axis.z * s;
    float r11 = c + axis.y * axis.y * t;
    float r12 = axis.y * axis.z * t - axis.x * s;
    float r20 = axis.z * axis.x * t - axis.y * s;
    float r21 = axis.z * axis.y * t + axis.x * s;
    float r22 = c + axis.z * axis.z * t;

    return Matrix4(r00, r01, r02, 0.0f,
        r10, r11, r12, 0.0f,
        r20, r21, r22, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);
}

static void SetAllProductsEnabled(Product &prod, bool enabled)
{
    prod.setEnabled(enabled);
    for (uint32_t i = 0; i < prod.numChildren(); i++)
        SetAllProductsEnabled(*prod.children()[i], enabled);
}

static void EnableProductAndChildren(Product &prod)
{
    prod.setEnabled(true);
    for (uint32_t i = 0; i < prod.numChildren(); i++)
        EnableProductAndChildren(*prod.children()[i]);
}

static void EnableProductAndParents(Product &prod)
{
    prod.setEnabled(true);
    Product *parent = prod.parent();
    while (parent != nullptr)
    {
        parent->setEnabled(true);
        parent = parent->parent();
    }
}

static const char *ProductLabel(const Product &prod)
{
    return prod.name() ? prod.name() : "<unnamed product>";
}

static void DrawTree(Product &prod)
{
    if (prod.numChildren() == 1)
    {
        Product *onlyChild = prod.children()[0];
        if (onlyChild->isPartNode() && onlyChild->partNameSameAsProduct())
        {
            DrawTree(*onlyChild);
            return;
        }
    }

    ImGui::PushID(&prod);
    ImGuiTreeNodeFlags flag = 0;
    if (&prod == g_selectedProduct)
        flag |= ImGuiTreeNodeFlags_Selected;
    if (prod.numChildren() == 0)
        flag |= ImGuiTreeNodeFlags_Leaf;

    bool opened = ImGui::TreeNodeEx(ProductLabel(prod), flag);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        g_selectedProduct = &prod;

    ImGui::SameLine(0, -2);
    ImGui::PushID(prod.enabledPtr());
    ImGui::Checkbox("", prod.enabledPtr());
    ImGui::PopID();

    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Isolate Part"))
        {
            Product *root = &prod;
            while (root->parent() != nullptr)
                root = root->parent();
            SetAllProductsEnabled(*root, false);
            EnableProductAndChildren(prod);
            EnableProductAndParents(prod);
        }
        ImGui::EndPopup();
    }

    if (opened)
    {
        for (uint32_t k = 0; k < prod.numChildren(); k++)
            DrawTree(*prod.children()[k]);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

static bool drawDebugMenu(Scene &scene, Camera &camera, float deltaTime, int shadowRes, bool enableShadows)
{
    ImGui::Begin("Viewer Controls");

    if (ImGui::BeginTabBar("Tabs"))
    {
        if (ImGui::BeginTabItem("Main"))
        {
            ImGui::Text("Framerate: %.1f fps", deltaTime > 0.0f ? (1.0f / deltaTime) : 0.0f);
            ImGui::Text("Resolution: %dx%d", scene.width(), scene.height());

            float oldScale = g_renderScale;
            ImGui::SliderFloat("Render Scale", &g_renderScale, 0.25f, 1.0f);
            if (oldScale != g_renderScale)
                scene.resize(scene.width(), scene.height(), g_renderScale, shadowRes, enableShadows);

          //  ImGui::Checkbox("Backface Culling", &scene.backfaceCull());
           // ImGui::Checkbox("Wireframe", &scene.wireframe());
            bool showExtraWire = scene.showExtraWireOverlays();
            if (ImGui::Checkbox("Extra Wire Overlays", &showExtraWire))
                scene.setShowExtraWireOverlays(showExtraWire);
         //   ImGui::SliderInt("Normal Debug Mode", &scene.normalDebugMode(), 0, 7);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Lighting"))
        {
            ImGui::Checkbox("Fullbright", &scene.fullbright());
            if (ImGui::Button("Compute From File"))
            {
                scene.recommendLightingDefaults(&scene.ambientWeight(),
                    &scene.diffuseWeight(), &g_atmosphere.sunIntensity);
            }
            ImGui::SliderFloat("Ambient Weight", &scene.ambientWeight(), 0.0f, 2.5f, "%.3f");
            ImGui::SliderFloat("Diffuse Weight", &scene.diffuseWeight(), 0.0f, 2.5f, "%.3f");
            ImGui::SliderFloat("Sun Altitude", &g_atmosphere.sunAltitude, 0.0f, 360.0f);
            ImGui::SliderFloat("Sun Azimuth", &g_atmosphere.sunAzimuth, 0.0f, 360.0f);
            ImGui::SliderFloat("Sun Intensity", &g_atmosphere.sunIntensity, 0.0f, 10.0f);
            ImGui::ColorEdit3("Sun Color", g_atmosphere.sunColor.vec);
            ImGui::ColorEdit3("Horizon Color", g_atmosphere.skyColorHorizon.vec);
            ImGui::ColorEdit3("Zenith Color", g_atmosphere.skyColorZenith.vec);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Objects"))
        {
            if (scene.models() != nullptr && scene.mumProducts() > 0)
            {
                if (ImGui::Button("Show All Parts"))
                    SetAllProductsEnabled(scene.models()[0], true);
                DrawTree(scene.models()[0]);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Views"))
        {
            static int selectedValue = 0;
            uint32_t numViews = camera.getNumViews();

            if (numViews > 0)
            {
                for (uint32_t i = 0; i < numViews; i++)
                {
                    const char *name = camera.getViewName(i);
                    if (name)
                    {
                        ImGui::RadioButton(name, &selectedValue, i);
                        if (ImGui::IsItemClicked())
                        {
                            if (i != camera.getCurrentViewIndex())
                            {
                                camera.setNewViewIndex(i);
                            }
                        }
                    }
                }
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImVec2 mousePos = ImGui::GetMousePos();
    ImVec2 winPos = ImGui::GetWindowPos();
    ImVec2 winSize = ImGui::GetWindowSize();
    bool overWindow = (mousePos.x >= winPos.x && mousePos.x <= winPos.x + winSize.x &&
        mousePos.y >= winPos.y && mousePos.y <= winPos.y + winSize.y);
    bool mouseUsed = ImGui::IsMouseDown(ImGuiMouseButton_Left) && overWindow;
    ImGui::End();
    // Update Sun & Skybox each frame
    DirLight &sun = *scene.dirLight();
    Skybox *sky = scene.skybox();
    Quaternion q1 = mutil::rotateaxis(Vector3(0.0f, 1.0f, 0.0f), mutil::radians(g_atmosphere.sunAzimuth));
    Quaternion q2 = mutil::rotateaxis(Vector3(1.0f, 0.0f, 0.0f), mutil::radians(g_atmosphere.sunAltitude));
    sun.direction = mutil::rotatevector(q1 * q2, Vector3(0.0f, 0.0f, 1.0f));
    sun.color = g_atmosphere.sunColor;
    sun.intensity = g_atmosphere.sunIntensity;
    sky->setHorizonColor(g_atmosphere.skyColorHorizon);
    sky->setZenithColor(g_atmosphere.skyColorZenith);
    sky->setSunTightness(g_atmosphere.sunTightness);
    return mouseUsed;
}
int main(int argc, char *argv[])
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return -1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_Window *window = SDL_CreateWindow(
        "nanoPRC 3D PDF Viewer", 1024, 768,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );
    if (!window)
    {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }
    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext)
    {
        std::cerr << "GLES Context creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }


/* In desktop OpenGL ES 3.0 (like ANGLE on Windows), extensions like EXT_color_buffer_float
  and EXT_color_buffer_half_float are active automatically if the GPU supports them.
  In WebGL 2.0, extensions remain disabled until gl.getExtension(...) is explicitly called!*/
#if defined(__EMSCRIPTEN__)
    EM_ASM({
        const canvas = Module.canvas || document.getElementById('canvas');
        const gl = canvas ? canvas.getContext('webgl2') : null;
        if (gl) {
            gl.getExtension('EXT_color_buffer_float');
            gl.getExtension('EXT_color_buffer_half_float');
            gl.getExtension('OES_texture_float_linear');
            gl.getExtension('OES_texture_half_float_linear');
        }
        });
#endif

    SDL_GL_MakeCurrent(window, glContext);
    ResolveGLES3Extensions();
#if defined(SDL_PLATFORM_WIN32)
    if (!WinGLESGlobals<>::p_glGetUniformLocation || !WinGLESGlobals<>::p_glUniformMatrix4fv || !WinGLESGlobals<>::p_glEnable ||
        !WinGLESGlobals<>::p_glClear || !WinGLESGlobals<>::p_glClearColor || !WinGLESGlobals<>::p_glDrawElements)
    {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "FATAL: Graphics driver failed to export core OpenGL ES functions.");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
#endif
    // Initialize ImGui for OpenGL ES 3.0 (#version 300 es)
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplSDL3_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    MobileEngineContext engine;
    const char *inputFile = (argc > 1) ? argv[1] : "triangle.pdf";
    if (!ResolveCrossPlatformAssetPath(inputFile, engine.activeResolvedPath))
    {
        std::cerr << "Failed to resolve asset path: " << inputFile << std::endl;
        return -1;
    }

    int w, h;
    int shadowResolution = 1024;
    bool enableShadows = false;
    SDL_GetWindowSizeInPixels(window, &w, &h);

    // 1. Load scene & initialize camera
    bool loaded = engine.viewerScene.load(engine.activeResolvedPath.c_str(), &engine.viewerCamera, false, false);
    if (!loaded)
    {
        g_loadErrorMessage = "Could not load initial file: no valid 3D PRC data found.";
    }
    else
    {
        engine.viewerScene.setCameraInitialPosition(&engine.viewerCamera);
        // 2. Compute recommended lighting & initialize lights/skybox/postprocess
        engine.viewerScene.recommendLightingDefaults(
            &engine.viewerScene.ambientWeight(),
            &engine.viewerScene.diffuseWeight(),
            &g_atmosphere.sunIntensity);
        *engine.viewerScene.dirLight() = kDirLight;
        *engine.viewerScene.pointLight() = kPointLight;
        engine.viewerScene.enableBloom() = false;
        engine.viewerScene.enableToneMapping() = true;
        engine.viewerScene.compositor()->gamma() = 2.2f;
        engine.viewerScene.compositor()->exposure() = 1.0f;

        engine.viewerCamera.setAspect((float)w / (float)h);
        engine.viewerCamera.update();
        if (!engine.viewerScene.resize(w, h, g_renderScale, shadowResolution, enableShadows))
        {
            g_loadErrorMessage = "Graphics framebuffer allocation failed.";
        }
    }
    glEnable(GL_DEPTH_TEST);
    // Arcball state
    bool mouseDown = false;
    bool mouseMoved = false;
    int mouseUpX = 0, mouseUpY = 0;
    int mouseDownX = 0, mouseDownY = 0;
    bool mouseOverUI = false;
    Matrix4 currMat = Matrix4(1.0f);
    Matrix4 transToCenter = Matrix4(1.0f);
    Matrix4 transFromCenter = Matrix4(1.0f);
    Matrix4 cam2World = Matrix4(1.0f);
    Matrix4 world2Cam = Matrix4(1.0f);
    int32_t viewIndex = -1;
    float time = SDL_GetTicks() / 1000.0f;
    bool running = true;
    SDL_Event event;

    while (running)
    {

        /* Allow new files to be dropped into the webpag */
#if defined(__EMSCRIPTEN__)
        if (!g_pendingWebDropPath.empty())
        {
            std::string droppedPath = g_pendingWebDropPath;
            g_pendingWebDropPath.clear();

            printf("Loading dropped web file: %s\n", droppedPath.c_str());

            // 1. Unload triangle.pdf (or previous model) and reset camera/UI
            releaseTextures();
            engine.viewerScene.unload();
            engine.viewerCamera = Camera();
            g_selectedProduct = nullptr;
            viewIndex = -1;

            // 2. Load the newly dropped file from MEMFS
            bool loaded = engine.viewerScene.load(droppedPath.c_str(), &engine.viewerCamera, false, false);
            if (!loaded)
            {
                g_loadErrorMessage = "Could not load file: no valid 3D PRC data found.";
            }
            else
            {
                engine.viewerScene.setCameraInitialPosition(&engine.viewerCamera);

                // 3. Re-apply lighting, post-processing, and FBO dimensions
                engine.viewerScene.recommendLightingDefaults(
                    &engine.viewerScene.ambientWeight(),
                    &engine.viewerScene.diffuseWeight(),
                    &g_atmosphere.sunIntensity);

                *engine.viewerScene.dirLight() = kDirLight;
                *engine.viewerScene.pointLight() = kPointLight;
                engine.viewerScene.enableBloom() = false;
                engine.viewerScene.enableToneMapping() = true;
                engine.viewerScene.compositor()->gamma() = 2.2f;
                engine.viewerScene.compositor()->exposure() = 1.0f;

                SDL_GetWindowSizeInPixels(window, &w, &h);
                engine.viewerCamera.setAspect((float)w / (float)h);
                engine.viewerCamera.update();
                if (!engine.viewerScene.resize(w, h, g_renderScale, shadowResolution, enableShadows))
                {
                    g_loadErrorMessage = "Graphics framebuffer allocation failed.";
                }
            }
            // 4. Clean up temporary MEMFS file
            std::remove(droppedPath.c_str());
        }
#endif

        float lastTime = time;
        time = SDL_GetTicks() / 1000.0f;
        float deltaTime = time - lastTime;
        if (viewIndex != (int32_t)engine.viewerCamera.getCurrentViewIndex())
        {
            double camZCenter = engine.viewerCamera.getCurrentViewCenterOrbitZ();
            cam2World = engine.viewerCamera.getCurrentViewCameraMatrix();
            Vector4 modelWorldCenter = cam2World * Vector4(0.0, 0.0, camZCenter, 1.0);
            viewIndex = engine.viewerCamera.getCurrentViewIndex();
            transToCenter = mutil::translate(Matrix4(1.0f), Vector3(-modelWorldCenter.x, -modelWorldCenter.y, -modelWorldCenter.z));
            transFromCenter = mutil::translate(Matrix4(1.0f), Vector3(modelWorldCenter.x, modelWorldCenter.y, modelWorldCenter.z));
            cam2World.columns[3] = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
            world2Cam = mutil::transpose(cam2World);
        }
        float wheelDelta = 0.0f;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
            {
                running = false;
            }
            else if (event.type == SDL_EVENT_WINDOW_RESIZED)
            {
                SDL_GetWindowSizeInPixels(window, &w, &h);
                if (w > 0 && h > 0)
                {
                    engine.viewerCamera.setAspect((float)w / (float)h);
                    engine.viewerScene.resize(w, h, g_renderScale, shadowResolution, enableShadows);
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_MOTION)
            {
                mouseMoved = true;
                mouseDownX = (int)event.motion.x;
                mouseDownY = (int)event.motion.y;
                if (!mouseDown)
                {
                    mouseUpX = mouseDownX;
                    mouseUpY = mouseDownY;
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
            {
                if (engine.viewerScene.models() != nullptr && engine.viewerScene.mumProducts() > 0)
                    currMat = engine.viewerScene.models()[0].model();
                mouseDown = true;
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
            {
                mouseDown = false;
            }
            else if (event.type == SDL_EVENT_MOUSE_WHEEL && !mouseOverUI)
            {
                if (engine.viewerScene.models() != nullptr && engine.viewerScene.mumProducts() > 0)
                    currMat = engine.viewerScene.models()[0].model();
                wheelDelta = event.wheel.y;
            }
        }
        // Arcball rotation & wheel zoom on root product
        if (((mouseDown && mouseMoved && !mouseOverUI) || wheelDelta != 0.0f) &&
            engine.viewerScene.models() != nullptr && engine.viewerScene.mumProducts() > 0)
        {
            Matrix4 mat;
            if (wheelDelta == 0.0f)
            {
                int minDim = std::min(w, h) - 1;
                if (minDim > 0)
                {
                    float px = (1.0f / (float)minDim) * (2.0f * mouseUpX - w - 1);
                    float py = (1.0f / (float)minDim) * (2.0f * mouseUpY - h - 1);
                    float qx = (1.0f / (float)minDim) * (2.0f * mouseDownX - w - 1);
                    float qy = (1.0f / (float)minDim) * (2.0f * mouseDownY - h - 1);
                    Matrix4 matRot = compute_rotation2(px, py, arc_projection(px, py),
                        qx, qy, arc_projection(qx, qy));
                    Matrix4 matRotWorld = cam2World * matRot * world2Cam;
                    mat = transFromCenter * matRotWorld * transToCenter * currMat;
                    engine.viewerScene.models()[0].setModel(mat);
                }
            }
            else
            {
                float scaleFactor = 1.0f + wheelDelta * 0.1f;
                Matrix4 scaleMat = mutil::scale(Matrix4(1.0f), Vector3(scaleFactor, scaleFactor, scaleFactor));
                engine.viewerScene.models()[0].setModel(scaleMat * currMat);
            }
            mouseMoved = false;
        }
        // Clear default back buffer before scene + ImGui pass
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, w, h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        // Render 3D scene
        if (engine.viewerScene.models() != nullptr && engine.viewerScene.mumProducts() > 0)
        {
            glEnable(GL_DEPTH_TEST);
            engine.viewerScene.render(&engine.viewerCamera);
        }
        // Render ImGui overlay on top
        glDisable(GL_DEPTH_TEST);
        glViewport(0, 0, w, h);
        mouseOverUI = drawDebugMenu(engine.viewerScene, engine.viewerCamera, deltaTime, shadowResolution, enableShadows);

        if (!g_loadErrorMessage.empty())
        {
            mouseOverUI = true;
            ImGui::SetNextWindowPos(ImVec2(w * 0.5f, h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            if (ImGui::Begin("Load Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", g_loadErrorMessage.c_str());
                if (ImGui::Button("OK"))
                {
                    g_loadErrorMessage.clear();
                }
            }
            ImGui::End();
        }



        if (engine.viewerScene.models() != nullptr && engine.viewerScene.mumProducts() > 0)
            engine.viewerCamera.upDateView(engine.viewerScene.models()[0]);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    engine.viewerScene.unload();
    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}