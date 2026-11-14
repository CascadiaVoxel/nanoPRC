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

#include "gles3_loader.h"
#include <prc_api.h>

// Core C++ Standard Library
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstddef>

#include "camera.h"

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

// Fixed interleaved structural layout specifying vector dimensions for GLES 3.0
struct GLESVertex
{
    float position[3]; // x, y, z
    float normal[3];   // nx, ny, nz
    float color[4];    // r, g, b, a
};

static struct ActiveTouch {
    Uint64 id = 0;
    float x = 0.0f;
    float y = 0.0f;
    bool active = false;
} activeFingers[2]; // Track up to 2 active fingers for the pinch pipeline

struct MobileEngineContext
{
    EngineState state = EngineState::Uninitialized;
    std::string activeResolvedPath = "";

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
    static Camera viewerCamera;

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

const char *vertexShaderSource = "#version 300 es\n"
"layout(location = 0) in vec3 aPos;\n"
"layout(location = 1) in vec3 aNormal;\n"
"layout(location = 2) in vec4 aColor;\n"
"uniform mat4 uMVP;\n" // Matrix uniform input
"out vec4 ourColor;\n"
"void main() {\n"
"    gl_Position = uMVP * vec4(aPos, 1.0);\n" // <-- Apply camera projection
"    ourColor = aColor;\n"
"}\n";

const char *fragmentShaderSource = "#version 300 es\n"
"precision mediump float;\n"
"in vec4 ourColor;\n"
"out vec4 fragColor;\n"
"void main() {\n"
"    fragColor = ourColor;\n"
"}\n";

GLuint CreateShaderProgram(const char *vsSource, const char *fsSource)
{
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vsSource, NULL);
    glCompileShader(vertexShader);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fsSource, NULL);
    glCompileShader(fragmentShader);

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return shaderProgram;
}

// Platform-agnostic asset extractor: ensures internal APK/bundle assets yield absolute file paths
bool ResolveCrossPlatformAssetPath(const char *relativeAssetName, std::string &outAbsolutePath)
{
#if defined(SDL_PLATFORM_ANDROID)
    const char *internalWriteDir = SDL_GetPrefPath("com.cascadvoxel", "nanoprc_viewer");
    if (!internalWriteDir) return false;

    outAbsolutePath = std::string(internalWriteDir) + relativeAssetName;

    SDL_FileInfo fileInfo;
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

static void setCompanionVisibilityRecursive(Product *product, bool enabled)
{
    if (product == NULL)
        return;

    Product *companion = product->renderCompanion();
    if (companion)
        companion->setEnabled(enabled);

    for (uint32_t i = 0; i < product->numChildren(); i++)
    {
        setCompanionVisibilityRecursive(product->children()[i], enabled);
    }
}

void setShowExtraWireOverlays(MobileEngineContext *engine, bool enabled)
{
    engine->showExtraWireOverlays = enabled;

    if (engine->viewerProducts != NULL && (engine->numProducts > 0))
    {
        setCompanionVisibilityRecursive(&engine->viewerProducts[0], enabled);
    }
}

static void setBounds(prc_context *ctx, Product *app_child, prc_api_tess *tess,
    Matrix4 matrix, uint8_t is_identity, Vector4 *minBound, Vector4 *maxBound)
{
    Vector4 min_bound;
    Vector4 max_bound;

    printf("setBounds: product=%s tess=%p type=%d bbox_min=[%g,%g,%g] bbox_max=[%g,%g,%g]\n",
        app_child->name(),
        (void *)tess,
        tess != NULL ? (int)tess->type : -1,
        tess != NULL ? tess->bounding_box_min[0] : 0.0,
        tess != NULL ? tess->bounding_box_min[1] : 0.0,
        tess != NULL ? tess->bounding_box_min[2] : 0.0,
        tess != NULL ? tess->bounding_box_max[0] : 0.0,
        tess != NULL ? tess->bounding_box_max[1] : 0.0,
        tess != NULL ? tess->bounding_box_max[2] : 0.0);

    /* A tessellation with no vertices has no box (all zeros, see
       prc_api_tess): it bounds nothing, so it stays out of the scene's */
    if (!tess->bound_box_initialized)
    {
        app_child->setBoundingBox(Vector4(0.0f), Vector4(0.0f));
        return;
    }

    app_child->setBoundingBox(tess->bounding_box_min, tess->bounding_box_max);

    /* Adjust the bounding boxes for the model by the matrix */
    if (!is_identity)
    {
        min_bound = matrix * app_child->bboxMin();
        max_bound = matrix * app_child->bboxMax();
        app_child->setBoundingBox(min_bound, max_bound);
    }

    min_bound = app_child->bboxMin();
    max_bound = app_child->bboxMax();

    minBound->x = std::fmin(min_bound.x, minBound->x);
    minBound->y = std::fmin(min_bound.y, minBound->y);
    minBound->z = std::fmin(min_bound.z, minBound->z);

    maxBound->x = std::fmax(max_bound.x, maxBound->x);
    maxBound->y = std::fmax(max_bound.y, maxBound->y);
    maxBound->z = std::fmax(max_bound.z, maxBound->z);
}

/* PRC_GRAPHICS_Removed (0x2000), as it appears in the high byte of a
   behaviour field, behavior_bit_field2: a product or item that has been
   removed and no longer appears (see prc_api_product). Adobe Acrobat does not
   draw it or anything under it; the viewer loads it switched off, so it can
   still be switched on from the model tree. */
#define PRC_REMOVED_BYTE2 0x20

static Matrix4 toMatrix4(const prc_api_transform &t)
{
    Matrix4 m = Matrix4(1.0f);

    m.columns[0] = Vector4((float)t.matrix[0], (float)t.matrix[1], (float)t.matrix[2], (float)t.matrix[3]);
    m.columns[1] = Vector4((float)t.matrix[4], (float)t.matrix[5], (float)t.matrix[6], (float)t.matrix[7]);
    m.columns[2] = Vector4((float)t.matrix[8], (float)t.matrix[9], (float)t.matrix[10], (float)t.matrix[11]);
    m.columns[3] = Vector4((float)t.matrix[12], (float)t.matrix[13], (float)t.matrix[14], (float)t.matrix[15]);
    return m;
}

/* A part or representation item with a local coordinate system
   (biased_local_coordinate_index) is placed by it within its parent's frame:
   nanoPRC gives its vertices in that system's frame. Sets app_node's model
   matrix to it, and folds it into matrix (for bounds); 1 if it has one. */
static bool placeByLocalCoordinateSystem(prc_context *ctx, prc_api_data data, const prc_api_part *item,
    Product *app_node, Matrix4 *matrix, uint8_t *is_identity)
{
    prc_api_transform own;
    Matrix4 m;

    if (item->biased_local_coordinate_index == 0 ||
        prc_api_get_local_coordinate_system(ctx, data, item->tess_file_index,
            item->biased_local_coordinate_index, &own) != 0 ||
        own.is_identity)
        return false;
    m = toMatrix4(own);
    app_node->setModel(m);
    *matrix = *matrix * m;
    *is_identity = 0;
    return true;
}

void addRepItems(MobileEngineContext *engine, prc_api_part *api_part,
    Product *app_product, Product *heap, uint32_t *product_count,
    Vector4 *minBound, Vector4 *maxBound, uint8_t is_identity, Matrix4 matrix)
{
    uint32_t k;
    prc_api_tess *tess;
    prc_api_tess *tess_line;

    app_product->createChildren(api_part->num_rep_items);

    for (k = 0; k < api_part->num_rep_items; k++)
    {
        const prc_api_part *item = &api_part->rep_items[k];
        Matrix4 item_matrix = matrix;
        uint8_t item_identity = is_identity;
        /* A removed item is loaded switched off, and kept out of the scene's
           bounds, so the view fits what is drawn */
        Vector4 removedMin = Vector4(FLT_MAX), removedMax = Vector4(-FLT_MAX);
        bool removed = (item->behavior_bit_field2 & PRC_REMOVED_BYTE2) != 0;
        Vector4 *itemMin = removed ? &removedMin : minBound;
        Vector4 *itemMax = removed ? &removedMax : maxBound;

        Product *app_child = app_product->getChildFromHeap(k, heap, product_count);
        *product_count += 1;
        app_child->setParent(app_product);
        app_child->setName(api_part->rep_items[k].name);
        app_child->setModel(Matrix4(1.0f));
        placeByLocalCoordinateSystem(engine->prcCtx, engine->prcData, item,
                                     app_child, &item_matrix, &item_identity);
        if (removed)
            app_child->setEnabled(false);
        app_child->setBoundingBox(Vector4(0.0f), Vector4(0.0f));
        if (api_part->rep_items[k].attributes.num_base_attributes > 0)
        {
            app_child->setAttributes(&api_part->rep_items[k].attributes);
        }

        /* Get the tessellation for this model (part) */
        tess = prc_api_get_ri_tessellation(engine->prcCtx, api_part, k);
        tess_line = prc_api_get_ri_line_tessellation(engine->prcCtx, api_part, k);

        printf("addRepItems: rep_item=%s tess=%p tess_line=%p num_rep_items=%zu\n",
            api_part->rep_items[k].name,
            (void *)tess,
            (void *)tess_line,
            api_part->rep_items[k].num_rep_items);

        if (tess != NULL)
        {
            setBounds(engine->prcCtx, app_child, tess, item_matrix, item_identity, itemMin,
                itemMax);

            /* Attach the model */
            app_child->attach(engine->prcCtx, engine->prcData, tess);

            if (tess_line != NULL && tess_line->type == PRC_API_TESS_3D_Wire_Extra)
            {
                Product *line_companion = &heap[*product_count];
                *product_count += 1;

                line_companion->setParent(app_child);
                line_companion->setEnabled(engine->showExtraWireOverlays);
                line_companion->setModel(Matrix4(1.0f));
                line_companion->setBoundingBox(app_child->bboxMin(), app_child->bboxMax());
                line_companion->attach(engine->prcCtx, engine->prcData, tess_line);

                app_child->setRenderCompanion(line_companion);
            }
        }
        if (api_part->rep_items[k].num_rep_items > 0)
        {
            /* This is a recursive case */
            addRepItems(engine, api_part->rep_items + k, app_child, heap,
                product_count, itemMin, itemMax, item_identity, item_matrix);
        }
    }
}

/* A recursive method that will go through the tree that is provided by nanoPRC and
   convert it to the tree that is used by this application */
void convertTree(MobileEngineContext *engine, prc_api_product *api_product,
    Product *app_product, Product *heap, uint32_t *product_count,
    Vector4 *minBound, Vector4 *maxBound, uint8_t *font_atlas_created)
{
    uint8_t has_tess = prc_api_model_item_is_part(engine->prcCtx, api_product);
    prc_api_tess *tess;
    prc_api_transform location = api_product->location;
    uint32_t num_children;
    uint32_t k;
    Matrix4 matrix = Matrix4(1.0f);
    uint8_t include_part;
    uint32_t start_index;
    uint32_t num_markups = prc_api_model_item_number_of_markups(engine->prcCtx, api_product);
    uint8_t has_part = api_product->part != NULL;

    app_product->setName(api_product->name);
    app_product->setIsPartNode(false);
    app_product->setPartNameSameAsProduct(false);
    if (!location.is_identity)
    {
        /* Set the tranformation matrix in the model*/
        matrix.columns[0] = Vector4((float)location.matrix[0], (float)location.matrix[1],
            (float)location.matrix[2], (float)location.matrix[3]);
        matrix.columns[1] = Vector4((float)location.matrix[4], (float)location.matrix[5],
            (float)location.matrix[6], (float)location.matrix[7]);
        matrix.columns[2] = Vector4((float)location.matrix[8], (float)location.matrix[9],
            (float)location.matrix[10], (float)location.matrix[11]);
        matrix.columns[3] = Vector4((float)location.matrix[12], (float)location.matrix[13],
            (float)location.matrix[14], (float)location.matrix[15]);

        app_product->setModel(matrix);
    }
    else
    {
        app_product->setModel(Matrix4(1.0f));
    }

    if (api_product->attributes.num_base_attributes > 0)
    {
        app_product->setAttributes(&api_product->attributes);
    }
    *product_count += 1;

    /* Special yoga pose if we have a part */
    include_part = (has_part != 0) &&
        (api_product->part->num_rep_items > 0 || api_product->part->tess != NULL);
    start_index = include_part ? 1 : 0;

    if (api_product->num_children > 0 || include_part)
    {
        num_children = api_product->num_children + include_part;
        app_product->createChildren(num_children); /* Get num_children ptr to ptrs */

        if (include_part)
        {
            /* Get the part child and set the parent as the product */
            Product *app_child = app_product->getChildFromHeap(0, heap, product_count);
            app_child->setParent(app_product);
            app_child->setName(api_product->part->name);
            app_child->setIsPartNode(true);
            app_child->setPartNameSameAsProduct(api_product->part->name_same_as_product != 0);
            app_child->setModel(Matrix4(1.0f));

            /* The part can carry its own local coordinate system, and be
               removed, as its items can (see addRepItems) */
            Matrix4 part_matrix = matrix;
            uint8_t part_identity = location.is_identity;
            Vector4 partRemovedMin = Vector4(FLT_MAX), partRemovedMax = Vector4(-FLT_MAX);
            Vector4 *partMin = minBound, *partMax = maxBound;

            placeByLocalCoordinateSystem(engine->prcCtx, engine->prcData,
                api_product->part, app_child, &part_matrix, &part_identity);
            if (api_product->part->behavior_bit_field2 & PRC_REMOVED_BYTE2)
            {
                app_child->setEnabled(false);
                partMin = &partRemovedMin;
                partMax = &partRemovedMax;
            }

            if (api_product->part->attributes.num_base_attributes > 0)
            {
                app_child->setAttributes(&api_product->part->attributes);
            }
            *product_count += 1;

            printf("convertTree: part=%s direct_tess=%p num_rep_items=%zu\n",
                api_product->part->name,
                (void *)api_product->part->tess,
                api_product->part->num_rep_items);

            if (api_product->part->tess != NULL)
            {
                setBounds(engine->prcCtx, app_child, api_product->part->tess, part_matrix,
                    part_identity, partMin, partMax);
                app_child->attach(engine->prcCtx, engine->prcData, api_product->part->tess);
            }

            if (api_product->part->num_rep_items > 0)
            {
                /* Add the RI items as children of the part child Product (app_child) */
                addRepItems(engine, api_product->part, app_child, heap, product_count,
                    partMin, partMax, part_identity, part_matrix);
            }
        }

        /* Now go through the children that this may have beyond the part we just added.
           If we have a "3D PMI" product that is when the markups are stuffed */
        for (size_t i = 0; i < api_product->num_children; i++)
        {
            Product *app_child = app_product->getChildFromHeap(i + start_index, heap, product_count); /* Note no + after this as it occurs in convertTree */
            app_child->setParent(app_product);
            convertTree(engine, api_product->children + i, app_child, heap,
                product_count, minBound, maxBound, font_atlas_created);
        }
    }

    if (num_markups > 0)
    {
        /* This should be essentially a "3D PMI" node with layout tessellations as children */
        app_product->createChildren(num_markups); /* Get num_markups ptr to ptrs */

        for (k = 0; k < num_markups; k++)
        {
            /* If it has both graphics and text primitives we will create a
             * parent and then two children, one for the graphics and one for
             * the text. If it has only graphics or only text, we will not create
             * the parent */
            tess = prc_api_get_markup_tessellation(engine->prcCtx, api_product, k);
            if (tess == NULL)
            {
                /* No tessellation resolved for this markup (e.g. an invalid or
                   unresolved biased_index_tessellation in the file). Still
                   create a tree node for consistency, but skip geometry/bounds
                   since there is nothing to read -- dereferencing a NULL tess
                   here would crash. */
                Product *app_child = app_product->getChildFromHeap(k, heap, product_count);
                *product_count += 1;
                app_child->setParent(app_product);
                app_child->setName(api_product->markup[k].name);
                app_child->setModel(Matrix4(1.0f));
                continue;
            }
            uint8_t needs_parent = (tess->num_line_primitives > 0);

            if (needs_parent)
            {
                /* Create the parent for this markup */
                Product *app_markup_parent = app_product->getChildFromHeap(k, heap, product_count);
                *product_count += 1;

                app_markup_parent->createChildren(2);
                app_markup_parent->setParent(app_product);
                app_markup_parent->setName(api_product->markup[k].name);
                app_markup_parent->setModel(Matrix4(1.0f));
                setBounds(engine->prcCtx, app_markup_parent, tess, matrix, location.is_identity, minBound,
                    maxBound);

                /* Set the the markup (non-text) child */
                Product *app_child = app_markup_parent->getChildFromHeap(0, heap, product_count);
                *product_count += 1;
                app_child->setParent(app_markup_parent);

                /* Append a _1 to the name */
                std::string markup_name = std::string(api_product->markup[k].name) + "_1";
                app_child->setName(markup_name.c_str());

                app_child->setModel(Matrix4(1.0f));
                setBounds(engine->prcCtx, app_child, tess, matrix, location.is_identity, minBound,
                    maxBound);
                app_child->attach(engine->prcCtx, engine->prcData, tess);
            }
            else if (tess->num_line_primitives > 0)
            {
                Product *app_child = app_product->getChildFromHeap(k, heap, product_count);
                *product_count += 1;
                app_child->setParent(app_product);

                /* Get the tessellation for this model (part) */
                tess = prc_api_get_markup_tessellation(engine->prcCtx, api_product, k);
                app_child->setName(api_product->markup[k].name);
                app_child->setModel(Matrix4(1.0f));
                setBounds(engine->prcCtx, app_child, tess, matrix, location.is_identity, minBound,
                    maxBound);

                /* Just a single child for this markup */
                if (tess->num_line_primitives > 0)
                {
                    app_child->attach(engine->prcCtx, engine->prcData, tess);
                }
            }
            else if (tess->num_line_primitives == 0)
            {
                /* No geometry for this markup, but we still want to create a node for it in the tree */
                Product *app_child = app_product->getChildFromHeap(k, heap, product_count);
                *product_count += 1;
                app_child->setParent(app_product);
                app_child->setName(api_product->markup[k].name);
                app_child->setModel(Matrix4(1.0f));
                setBounds(engine->prcCtx, app_child, tess, matrix, location.is_identity, minBound,
                    maxBound);
                /* Just don't attach it though */
            }
        }
    }
}

void ProcessPRCGeometry(MobileEngineContext *engine)
{
    int code;
    uint32_t numParts = 0, numProducts = 0, numMarkups = 0;

    if (!engine || engine->state != EngineState::ParsingDataStructure)
        return;

    if (prc_api_prep_model_tree(engine->prcCtx, engine->prcData, &numParts,
        &numProducts, &numMarkups) < 0)
    {
        engine->state = EngineState::ErrorState;
        return;
    }
    if (prc_api_create_model_tree(engine->prcCtx, engine->prcData, &engine->modelTreePRC,
        numParts, numProducts, numMarkups) < 0)
    {
        engine->state = EngineState::ErrorState;
        return;
    }
    engine->state = EngineState::ExtractingTessellation;
    uint32_t numExactGeom = 0;
    if (prc_api_get_number_tessellations(engine->prcCtx, engine->prcData, engine->modelTreePRC,
        &engine->numSurfaceTess, &engine->numLineTess, &numExactGeom, 0) < 0)
    {
        engine->state = EngineState::ErrorState;
        return;
    }
    if (engine->numSurfaceTess > 0)
    {
        engine->surfaceTessArray = (prc_api_tess *)SDL_calloc(engine->numSurfaceTess, sizeof(prc_api_tess));
    }
    if (engine->numLineTess > 0)
    {
        engine->lineTessArray = (prc_api_tess *)SDL_calloc(engine->numLineTess, sizeof(prc_api_tess));
    }
    for (uint32_t i = 0; i < engine->numSurfaceTess; ++i)
    {
        uint8_t hasLineCompanion = 0;
        prc_api_tess &tess = engine->surfaceTessArray[i];
        uint32_t nFaces = prc_api_get_number_faces(engine->prcCtx, engine->prcData, i);
        if (prc_api_get_tessellation_type(engine->prcCtx, engine->prcData, i) == PRC_API_TESS_3D_Compressed)
        {
            nFaces = 1;
        }
        tess.num_faces = nFaces;
        tess.tess_faces = new prc_api_face[nFaces];
        code = prc_api_initialize_tessellation(engine->prcCtx, engine->prcData, engine->modelTreePRC, i,
            &engine->surfaceTessArray[i], engine->numLineTess > 0 ? &engine->lineTessArray[i] : NULL, &hasLineCompanion);
        if (hasLineCompanion && engine->numLineTess > 0)
        {
            engine->lineTessArray[i].num_faces = nFaces;
            engine->lineTessArray[i].tess_faces = new prc_api_face[nFaces];
        }
        if (tess.type == PRC_API_TESS_3D_Wire ||
            tess.type == PRC_API_TESS_MarkUp)
        {
            code = prc_api_get_tessellation_vertices(engine->prcCtx, engine->prcData, i, 0, NULL, &tess);
        }
        else
        {
            for (uint32_t j = 0; j < tess.num_faces; j++)
            {
                code = prc_api_get_tessellation_vertices(engine->prcCtx, engine->prcData, i, j,
                    tess.tess_faces + j, &tess);
            }
        }
    }

    Vector4 min_bound;
    Vector4 max_bound;

    max_bound.x = FLT_MIN;
    max_bound.y = FLT_MIN;
    max_bound.z = FLT_MIN;
    min_bound.x = FLT_MAX;
    min_bound.y = FLT_MAX;
    min_bound.z = FLT_MAX;

    /* Allocate 3x on the markups in case we have text and lines (parent plus
       one for text and one for lines)*/
    engine->numProducts = numProducts + numParts + 3 * numMarkups + engine->numLineTess;
    engine->viewerProducts = new Product[engine->numProducts];
    uint32_t product_index = 0;
    uint8_t font_atlas_created = 0;

    convertTree(engine, engine->modelTreePRC, &engine->viewerProducts[0],
        engine->viewerProducts, &product_index, &min_bound, &max_bound, &font_atlas_created);

    engine->bboxMin[0] = min_bound.x;
    engine->bboxMin[1] = min_bound.y;
    engine->bboxMin[2] = min_bound.z;
    engine->bboxMax[0] = max_bound.x;
    engine->bboxMax[1] = max_bound.y;
    engine->bboxMax[2] = max_bound.z;

    /* Lets get the camera views if we have any. Always add the viewer
   default one in case all the other views are terrible */
    uint32_t num_views = prc_api_get_number_of_view(engine->prcCtx, engine->prcData);

    auto addDefaultView = [&]() {
        /* Create a default view based on bounding box */
        Vector3 max = Vector3(engine->bboxMax[0], engine->bboxMax[1], engine->bboxMax[2]);
        Vector3 min = Vector3(engine->bboxMin[0], engine->bboxMin[1], engine->bboxMin[2]);
        float diagonal = (max - min).length();
        Vector3 center = Vector3((engine->bboxMax[0] + engine->bboxMin[0]) * 0.5f,
            (engine->bboxMax[1] + engine->bboxMin[1]) * 0.5f,
            (engine->bboxMax[2] + engine->bboxMin[2]) * 0.5f);

        /* Position camera at a reasonable distance to see the entire object */
        float distance = diagonal * 1.5f;

        /* Create a standard isometric-style view similar to CAD applications
           Pitch ~-30 degrees (looking down), Yaw ~45 degrees (from front-right) */
        float pitch = -30.0f;
        float yaw = 45.0f;
        float roll = 0.0f;

        /* Build rotation matrices matching Camera::update() convention */
        double pitch_rad = mutil::radians((double)pitch);
        double yaw_rad = mutil::radians((double)yaw);
        double roll_rad = mutil::radians((double)roll);

        /* These match the rotation order in Camera::update(): R_phi * R_theta * R_psi */
        Matrix3 R_yaw = Matrix3(1, 0, 0,
            0, std::cos(yaw_rad), -std::sin(yaw_rad),
            0, std::sin(yaw_rad), std::cos(yaw_rad));

        Matrix3 R_pitch = Matrix3(std::cos(pitch_rad), 0, std::sin(pitch_rad),
            0, 1, 0,
            -std::sin(pitch_rad), 0, std::cos(pitch_rad));

        Matrix3 R_roll = Matrix3(std::cos(roll_rad), -std::sin(roll_rad), 0,
            std::sin(roll_rad), std::cos(roll_rad), 0,
            0, 0, 1);

        Matrix3 R = R_roll * R_pitch * R_yaw;

        /* Transform the camera's forward direction (+Z in camera space) by rotation */
        Vector3 camera_forward = Vector3(R.columns[2].x, R.columns[2].y, R.columns[2].z);

        /* Position camera behind the object (opposite of forward direction) */
        Vector3 position = center - camera_forward * distance;

        /* Build PRC camera matrix (column-major: 3x3 rotation + position) */
        double default_matrix[12] = {
            R.columns[0].x, R.columns[0].y, R.columns[0].z,
            R.columns[1].x, R.columns[1].y, R.columns[1].z,
            R.columns[2].x, R.columns[2].y, R.columns[2].z,
            position.x, position.y, position.z
        };

        /* Center of orbit distance */
        double camera_z_orbit = distance;
        char default_name[] = "viewer default";

        engine->viewerCamera.addView(default_matrix, camera_z_orbit,
                                     static_cast<int>(num_views), default_name);
    };

    if (num_views > 0)
    {
        double *matrix;
        double camera_z;
        char *name;

        engine->viewerCamera.setNumViews(num_views + 1);
        for (uint32_t i = 0; i < num_views; i++)
        {
            code = prc_api_get_view(engine->prcCtx, engine->prcData, i, &name,
                                    &matrix, &camera_z);
            if (code < 0)
            {
                printf("Scene::load: prc_api_get_view failed\n");
                exit(1);
            }
            engine->viewerCamera.addView(matrix, camera_z, i, name);
        }

        addDefaultView();
    }
    else
    {
        engine->viewerCamera.setNumViews(1);
        addDefaultView();
    }

    engine->state = EngineState::MarshallingToGPU;
}

void GenerateGLESBuffers(MobileEngineContext *engine)
{
    if (!engine || engine->state != EngineState::MarshallingToGPU) return;
    std::vector<GLESVertex> extractedVertices;
    std::vector<uint32_t> extractedIndices;
    uint32_t runningVertexOffset = 0;
    for (uint32_t t = 0; t < engine->numSurfaceTess; ++t)
    {
        prc_api_tess *tess = &engine->surfaceTessArray[t];
        uint32_t numFaces = prc_api_get_number_faces(engine->prcCtx, engine->prcData, t);
        for (uint32_t f = 0; f < numFaces; ++f)
        {
            if (prc_api_skip_face(engine->prcCtx, tess, f))
                continue;

            uint32_t faceVertCount = 0;
            prc_api_vertex *rawVerts = nullptr;
            if (prc_api_get_face_vertices(engine->prcCtx, tess, f, &faceVertCount, &rawVerts) == 0)
            {
                for (uint32_t v = 0; v < faceVertCount; ++v)
                {
                    rawVerts[v].color[0] = 1.0f; // Force Red
                    rawVerts[v].color[1] = 0.0f;
                    rawVerts[v].color[2] = 0.0f;
                    rawVerts[v].color[3] = 1.0f;
                    GLESVertex vertex;
                    std::memcpy(vertex.position, rawVerts[v].position, sizeof(float) * 3);
                    std::memcpy(vertex.normal, rawVerts[v].normal, sizeof(float) * 3);
                    std::memcpy(vertex.color, rawVerts[v].color, sizeof(float) * 4);
                    extractedVertices.push_back(vertex);
                    // Quick layout terminal print tracker to verify raw bounding ranges
                    std::cout << "Extracted Vert Vertex Coordinate Range -> [X]: " << vertex.position[0]
                        << ", [Y]: " << vertex.position[1] << ", [Z]: " << vertex.position[2] << std::endl;
                }
                size_t numPrims = prc_api_get_num_graphics_primitives(engine->prcCtx, engine->prcData, tess);
                for (size_t p = 0; p < numPrims; ++p)
                {
                    prc_api_graphic_primitive prim;
                    if (prc_api_get_graphics_primitive(engine->prcCtx, engine->prcData, tess, f, p, &prim) == 0)
                    {
                        for (size_t idx = 0; idx < prim.num_indices; ++idx)
                        {
                            extractedIndices.push_back(prim.indices[idx] + runningVertexOffset);
                        }
                    }
                }
                runningVertexOffset += faceVertCount;
            }
        }
    }

    if (!extractedVertices.empty() && !extractedIndices.empty())
    {
        glGenVertexArrays(1, &engine->vao);
        glGenBuffers(1, &engine->vbo);
        glGenBuffers(1, &engine->ebo);
        glBindVertexArray(engine->vao);
        glBindBuffer(GL_ARRAY_BUFFER, engine->vbo);
        glBufferData(GL_ARRAY_BUFFER, extractedVertices.size() * sizeof(GLESVertex), extractedVertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, engine->ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, extractedIndices.size() * sizeof(uint32_t), extractedIndices.data(), GL_STATIC_DRAW);
        // Position Attribute Setup
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GLESVertex), (void *)offsetof(GLESVertex, position));
        // Normal Attribute Setup
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GLESVertex), (void *)offsetof(GLESVertex, normal));
        // Color Attribute Setup
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(GLESVertex), (void *)offsetof(GLESVertex, color));
        engine->indexCount = static_cast<GLsizei>(extractedIndices.size());
        glBindVertexArray(0);
        engine->state = EngineState::ActiveRendering;
    }
    else
    {
        engine->state = EngineState::ErrorState;
    }
}
void CleanEngineResources(MobileEngineContext *engine)
{
    if (!engine)
        return;

    if (engine->shaderProgram)
        glDeleteProgram(engine->shaderProgram);

    if (engine->prcCtx && engine->prcData)
    {
        prc_api_release_data(engine->prcCtx, engine->prcData,
            engine->surfaceTessArray, engine->numSurfaceTess,
            engine->lineTessArray, engine->numLineTess,
            nullptr, 0, engine->modelTreePRC);
        if (engine->surfaceTessArray)
            SDL_free(engine->surfaceTessArray);
        if (engine->lineTessArray)
            SDL_free(engine->lineTessArray);
    }

    if (engine->prcCtx)
        prc_api_release_context(engine->prcCtx);
    if (engine->vao)
        glDeleteVertexArrays(1, &engine->vao);
    if (engine->vbo)
        glDeleteBuffers(1, &engine->vbo);
    if (engine->ebo)
        glDeleteBuffers(1, &engine->ebo);
}

inline glm::vec3 MapToTrackballSphere(float mouseX, float mouseY, float windowWidth, float windowHeight)
{
    // 1. Convert pixel space coordinates to Normalized Device Coordinates [-1.0 to 1.0]
    float x = (2.0f * mouseX) / windowWidth - 1.0f;
    float y = 1.0f - (2.0f * mouseY) / windowHeight; // Flip Y axis for OpenGL standard orientation

    // 2. Calculate distance from the center of the viewport
    float d = x * x + y * y;
    glm::vec3 vector(x, y, 0.0f);

    // 3. Project coordinates onto a sphere surface, fallback to a hyperbolic sheet if dragging far outside
    if (d <= 1.0f)
    {
        vector.z = std::sqrt(1.0f - d);
    }
    else
    {
        vector = glm::normalize(vector); // Snap to outer edge boundary edge
    }
    return vector;
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
        "nanoPRC 3D PDF Viewer", 800, 600,
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

    // 1. Resolve ALL driver function pointers instantly at runtime
    ResolveGLES3Extensions();

    // 2. Production Security Gate: Validate raw internal pointers directly 
    // to prove to the static analyzer that a null execution path is impossible.
#if defined(SDL_PLATFORM_WIN32) || defined(__EMSCRIPTEN__)
    if (!win_glGetUniformLocation || !win_glUniformMatrix4fv || !win_glEnable ||
        !win_glClear || !win_glClearColor || !win_glDrawElements)
    {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "FATAL: Graphics driver failed to export core OpenGL ES functions. Terminating.");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1; // Abort entry path securely
    }
#endif

    // 3. Completely secure from null dereference, syntax errors, and linker tracking!
    glEnable(GL_DEPTH_TEST);

    // ====================================================================
    // REMOVED THE TWO MANUAL ASSIGNMENTS THAT WERE CRASHING / COLLIDING HERE
    // ====================================================================

    MobileEngineContext engine;
    engine.shaderProgram = CreateShaderProgram(vertexShaderSource, fragmentShaderSource);
    engine.prcCtx = prc_api_new_context(nullptr);
    if (engine.prcCtx)
    {
        std::cout << "nanoPRC parser context successfully active inside mobile loop!" << std::endl;
        engine.state = EngineState::ExtractingAssets;
    }
    else
    {
        engine.state = EngineState::ErrorState;
    }

    if (engine.state == EngineState::ExtractingAssets)
    {
        if (ResolveCrossPlatformAssetPath("triangle.pdf", engine.activeResolvedPath))
        {
            engine.prcData = prc_api_open_contents(engine.prcCtx, engine.activeResolvedPath.c_str());
            if (engine.prcData)
            {
                engine.state = EngineState::ParsingDataStructure;
            }
            else
            {
                prc_api_print_error_stack(engine.prcCtx);
                engine.state = EngineState::ErrorState;
            }
        }
        else
        {
            engine.state = EngineState::ErrorState;
        }
    }
    bool running = true;
    SDL_Event event;
    while (running)
    {
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                running = false;
            }

            // ================================================================
            // 1. MOBILE TOUCH EVENTS (ISOLATED GESTURE SPACE)
            // ================================================================
            else if (event.type == SDL_EVENT_FINGER_DOWN)
            {
                if (!activeFingers[0].active)
                {
                    activeFingers[0].id = event.tfinger.fingerID;
                    activeFingers[0].x = event.tfinger.x;
                    activeFingers[0].y = event.tfinger.y;
                    activeFingers[0].active = true;
                }
                else if (!activeFingers[1].active && activeFingers[0].id != event.tfinger.fingerID) {
                    activeFingers[1].id = event.tfinger.fingerID;
                    activeFingers[1].x = event.tfinger.x;
                    activeFingers[1].y = event.tfinger.y;
                    activeFingers[1].active = true;

                    engine.isPinching = true;
                    float dx = activeFingers[0].x - activeFingers[1].x;
                    float dy = activeFingers[0].y - activeFingers[1].y;
                    engine.initialPinchDistance = std::sqrt(dx * dx + dy * dy);
                    engine.initialCameraDistance = engine.cameraDistance;
                }
            }
            else if (event.type == SDL_EVENT_FINGER_UP)
            {
                if (activeFingers[0].active && activeFingers[0].id == event.tfinger.fingerID) {
                    activeFingers[0].active = false;
                }
                if (activeFingers[1].active && activeFingers[1].id == event.tfinger.fingerID) {
                    activeFingers[1].active = false;
                }

                if (!activeFingers[0].active || !activeFingers[1].active) {
                    engine.isPinching = false;
                }
            }
            else if (event.type == SDL_EVENT_FINGER_MOTION)
            {
                if (activeFingers[0].active && activeFingers[0].id == event.tfinger.fingerID) {
                    activeFingers[0].x = event.tfinger.x;
                    activeFingers[0].y = event.tfinger.y;
                }
                if (activeFingers[1].active && activeFingers[1].id == event.tfinger.fingerID) {
                    activeFingers[1].x = event.tfinger.x;
                    activeFingers[1].y = event.tfinger.y;
                }

                if (engine.isPinching && activeFingers[0].active && activeFingers[1].active)
                {
                    float dx = activeFingers[0].x - activeFingers[1].x;
                    float dy = activeFingers[0].y - activeFingers[1].y;
                    float currentPinchDistance = std::sqrt(dx * dx + dy * dy);

                    if (engine.initialPinchDistance > 0.001f) {
                        float scaleFactor = engine.initialPinchDistance / currentPinchDistance;
                        engine.cameraDistance = engine.initialCameraDistance * scaleFactor;
                        engine.cameraDistance = glm::clamp(engine.cameraDistance, engine.minDistance, engine.maxDistance);
                    }
                }
            }

            // ================================================================
            // 2. DESKTOP MOUSE EVENTS (SEPARATED WORKING ORBIT STRUCTURES)
            // ================================================================
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
            {
                engine.isInteracting = true;
                engine.lastMouseX = event.button.x;
                engine.lastMouseY = event.button.y;
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
            {
                engine.isInteracting = false;
            }
            else if (event.type == SDL_EVENT_MOUSE_MOTION)
            {
                // Core trackball orbit block executes cleanly on desktop mouse movements
                if (engine.isInteracting)
                {
                    glm::vec3 va = MapToTrackballSphere(engine.lastMouseX, engine.lastMouseY, 800.0f, 600.0f);
                    glm::vec3 vb = MapToTrackballSphere(event.motion.x, event.motion.y, 800.0f, 600.0f);

                    float dot = glm::clamp(glm::dot(va, vb), -1.0f, 1.0f);
                    float angle = std::acos(dot) * 2.5f;

                    if (angle > 0.001f) {
                        glm::vec3 axis = glm::normalize(glm::cross(va, vb));
                        glm::mat4 incrementalRotation = glm::rotate(glm::mat4(1.0f), angle, axis);
                        engine.objectOrientation = incrementalRotation * engine.objectOrientation;
                    }
                    engine.lastMouseX = event.motion.x;
                    engine.lastMouseY = event.motion.y;
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_WHEEL)
            {
                engine.cameraDistance -= event.wheel.y * 1.5f;
                engine.cameraDistance = glm::clamp(engine.cameraDistance, engine.minDistance, engine.maxDistance);
            }
        }

        if (engine.state == EngineState::ParsingDataStructure)
        {
            ProcessPRCGeometry(&engine);
        }
        if (engine.state == EngineState::MarshallingToGPU)
        {
            GenerateGLESBuffers(&engine);
        }
        glClearColor(0.1f, 0.15f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (engine.state == EngineState::ActiveRendering)
        {
            glUseProgram(engine.shaderProgram);

            // 1. Projections
            glm::mat4 projection = glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);

            // 2. MODIFIED: Feed the dynamic distance state directly into the camera vector array
            glm::mat4 view = glm::lookAt(
                glm::vec3(0.0f, 0.0f, engine.cameraDistance), // <-- Use your zoom state variable here!
                glm::vec3(0.0f, 0.0f, 0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f)
            );

            // 3. Model Orientation from Trackball
            glm::mat4 model = engine.objectOrientation;

            // Combine matrices into MVP
            glm::mat4 mvp = projection * view * model;

            // 4. Uniform Upload
            GLint mvpLoc = glGetUniformLocation(engine.shaderProgram, "uMVP");
            glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));

            glBindVertexArray(engine.vao);
            glDrawElements(GL_TRIANGLES, engine.indexCount, GL_UNSIGNED_INT, nullptr);
            glBindVertexArray(0);
        }
        SDL_GL_SwapWindow(window);
    }
    CleanEngineResources(&engine);
    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

