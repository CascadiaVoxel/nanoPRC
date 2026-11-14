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

#ifndef _CAMERA_H_
#define _CAMERA_H_

#include <mutil/mutil.h>
#include "product.h"

#define NUM_VIEW_STEPS 100.0

using namespace mutil;

constexpr Vector3 kWorldUp = {0.0f, 1.0f, 0.0f};

typedef struct View_s View;
struct View_s
{
    Matrix4 cam_pos;
    double center_orbit_z;
    char *name;
    Vector3 position;
    float pitch, yaw, roll;
};

class Camera
{
public:
    constexpr const Vector3 &position() const { return _position; }
    constexpr const float &pitch() const { return _pitch; }
    constexpr const float &yaw() const { return _yaw; }

    constexpr const float &fov() const { return _fov; }
    constexpr const float &near() const { return _near; }
    constexpr const float &far() const { return _far; }
    constexpr const float &aspect() const { return _aspect; }

    constexpr const Vector3 &front() const { return _front; }
    constexpr const Vector3 &up() const { return _up; }
    constexpr const Vector3 &right() const { return _right; }

    constexpr const Matrix4 &view() const { return _view; }
    constexpr const Matrix4 &projection() const { return _projection; }

    constexpr void setPosition(const Vector3 &position) { _position = position; }
    constexpr void setPitch(float pitch) { _pitch = pitch; }
    constexpr void setYaw(float yaw) { _yaw = yaw; }

    constexpr void setFov(float fov) { _fov = fov; }
    constexpr void setNear(float near) { _near = near; }
    constexpr void setFar(float far) { _far = far; }
    constexpr void setAspect(float aspect) { _aspect = aspect; }

    constexpr void setNumViews(uint32_t numViews) { _numViews = numViews; }
    constexpr uint32_t &getNumViews() { return _numViews; }
    constexpr char* getViewName(int index) { return _views[index].name; }

    void update();
    void addView(double *in_matrix, double z_center, int index, char *name);
    void setView(int index);
    double getCurrentViewCenterOrbitZ(void);
    Matrix4 getCurrentViewCameraMatrix(void);


    constexpr void setNewViewIndex(uint32_t index) { _newViewIndex = index; }
    constexpr void setCurrentViewIndex(uint32_t index) { _currentViewIndex = index; }
    constexpr uint32_t getCurrentViewIndex() const { return _currentViewIndex; }
    constexpr uint32_t getNewViewIndex() const { return _newViewIndex; }

    constexpr void upDateView(Product &product)
    {
        if (_newViewIndex != _currentViewIndex)
        {
            if (_viewConvexStep == 0)
            {
                _productModel = product.model();
            }
            _viewConvexStep = _viewConvexStep + 1.0 / NUM_VIEW_STEPS;
            if (_viewConvexStep >= 1.0)
            {
                _viewConvexStep = 0.0;
                _currentViewIndex = _newViewIndex;
                _productModel = Matrix4(1.0f); // Reset the model transformation
                product.setModel(_productModel);
            }

            /* Lets do the translation from the center point */
            Vector3 pos1 = { _views[_currentViewIndex].position.x - _modelWorldCenter.x,
                             _views[_currentViewIndex].position.y - _modelWorldCenter.y,
                             _views[_currentViewIndex].position.z - _modelWorldCenter.z };
            Vector3 pos2 = { _views[_newViewIndex].position.x - _modelWorldCenter.x,
                             _views[_newViewIndex].position.y - _modelWorldCenter.y,
                             _views[_newViewIndex].position.z - _modelWorldCenter.z };
            _position = {pos1.x * (1.0f - float(_viewConvexStep)) + pos2.x * float(_viewConvexStep) + _modelWorldCenter.x,
                           pos1.y * (1.0f - float(_viewConvexStep)) + pos2.y * float(_viewConvexStep) + _modelWorldCenter.y,
                           pos1.z * (1.0f - float(_viewConvexStep)) + pos2.z * float(_viewConvexStep) + _modelWorldCenter.z };


            _pitch = _views[_currentViewIndex].pitch * (1.0 - _viewConvexStep) +
                _views[_newViewIndex].pitch * _viewConvexStep;
            _yaw = _views[_currentViewIndex].yaw * (1.0 - _viewConvexStep) +
                _views[_newViewIndex].yaw * _viewConvexStep;
            _roll = _views[_currentViewIndex].roll * (1.0 - _viewConvexStep) +
                _views[_newViewIndex].roll * _viewConvexStep;

            /* Move the model transformation back to identity */
            Matrix4 ident_matrix = Matrix4(1.0f);
            Matrix4 current_model = product.model();
            current_model = current_model * (1 - _viewConvexStep) + ident_matrix * _viewConvexStep;
            product.setModel(current_model);

            update();
        }
    }

    Camera();

private:
    Vector3 _position;
    float _pitch, _yaw, _roll;

    Matrix4 _productModel;

    float _fov, _near, _far;
    float _aspect;

    Vector3 _front, _up, _right;
    Matrix4 _view, _projection;

    View *_views;
    uint32_t _numViews;
    uint32_t _currentViewIndex;
    uint32_t _newViewIndex;

    double _viewConvexStep;
    Vector4 _modelWorldCenter; /* This is what we will rotate the camera about */
};

#endif // _CAMERA_H_
