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

#ifndef _ENGINE_H_
#define _ENGINE_H_

#include "gles3_loader.h"
#include <prc_api.h>

// Core C++ Standard Library
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstddef>
#include "camera.h"
#include "scene.h"

// Modern GLM Setup
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/type_ptr.hpp>

// ============================================================================

// Unified Engine Runtime States
enum class EngineState
{
    Uninitialized,
    ExtractingAssets,
    ParsingDataStructure,
    ExtractingTessellation,
    MarshallingToGPU,
    ActiveRendering,
    ErrorState
};

class Product;

struct MobileEngineContext
{
    EngineState state = EngineState::Uninitialized;
    std::string activeResolvedPath = "";

    Scene viewerScene;

    // Public C API instances
    prc_context *prcCtx = nullptr;
    prc_api_data prcData = nullptr;
    prc_api_product *modelTreePRC = nullptr;
    prc_api_tess *surfaceTessArray = nullptr;
    prc_api_tess *lineTessArray = nullptr;
    uint32_t numSurfaceTess = 0;
    uint32_t numLineTess = 0;

    // OpenGL ES Vertex Object Registry
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;
    GLuint shaderProgram = 0; // Modern GLES program handler reference

    /* This viewers products */
    Product *viewerProducts;
    uint32_t numProducts;

    /* Scene bounding box */
    double bboxMin[3];
    double bboxMax[3];

    /* Camera */
    Camera viewerCamera;

    /* UI */
    uint8_t showExtraWireOverlays;

    // --- 3D Trackball Interaction Space ---
    bool isInteracting = false;
    float lastMouseX = 0.0f;
    float lastMouseY = 0.0f;
    glm::mat4 objectOrientation = glm::mat4(1.0f);

    // --- Dynamic Zoom Space ---
    float cameraDistance = 20.0f; // Matches your initial 20.0f Z-position
    float minDistance = 2.0f;     // Prevents zooming inside the mesh
    float maxDistance = 80.0f;    // Prevents zooming infinitely far away

    // Multi-touch tracking tracking states
    bool isPinching = false;
    float initialPinchDistance = 0.0f;
    float initialCameraDistance = 0.0f;
};

#endif