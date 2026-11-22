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

#include <cmath>
#include <string.h>
#include "camera.h"

static Quaternion matrixToQuaternion(const Matrix4 &m)
{
    // m.columns[col].{x,y,z} corresponds to row {0,1,2}, column {col}
    float m00 = m.columns[0].x, m01 = m.columns[1].x, m02 = m.columns[2].x;
    float m10 = m.columns[0].y, m11 = m.columns[1].y, m12 = m.columns[2].y;
    float m20 = m.columns[0].z, m21 = m.columns[1].z, m22 = m.columns[2].z;

    float trace = m00 + m11 + m22;
    Quaternion q;

    if (trace > 0.0f)
    {
        float s = 0.5f / std::sqrt(trace + 1.0f);
        q.w = 0.25f / s;
        q.x = (m21 - m12) * s;
        q.y = (m02 - m20) * s;
        q.z = (m10 - m01) * s;
    }
    else if (m00 > m11 && m00 > m22)
    {
        float s = 2.0f * std::sqrt(1.0f + m00 - m11 - m22);
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    }
    else if (m11 > m22)
    {
        float s = 2.0f * std::sqrt(1.0f + m11 - m00 - m22);
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    }
    else
    {
        float s = 2.0f * std::sqrt(1.0f + m22 - m00 - m11);
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }

    return mutil::normalize(q);
}

static Quaternion safeSlerp(const Quaternion &a, const Quaternion &b, float t)
{
    Quaternion target = b;
    float d = mutil::dot(a, b);
    if (d < 0.0f)
    {
        target = -b;
        d = -d;
    }
    if (d > 0.9995f)
    {
        return mutil::normalize(a * (1.0f - t) + target * t);
    }
    float theta = std::acos(d);
    float sinTheta = std::sin(theta);
    float w0 = std::sin((1.0f - t) * theta) / sinTheta;
    float w1 = std::sin(t * theta) / sinTheta;
    return mutil::normalize(a * w0 + target * w1);
}

void Camera::update()
{
    Vector3 front3 = mutil::rotatevector(_orientation, Vector3(0.0f, 0.0f, 1.0f));
    Vector3 up3 = mutil::rotatevector(_orientation, Vector3(0.0f, 1.0f, 0.0f));

    _front = normalize(front3);
    _up = normalize(up3);
    _right = normalize(cross(_front, _up));
    _view = lookAt(_position, _position + _front, _up);
    _projection = perspective(mutil::radians(_fov), _aspect, _near, _far);
}

double Camera::getCurrentViewCenterOrbitZ(void)
{
    if (_views == nullptr || _currentViewIndex >= _numViews)
    {
        return 0.0f;
    }
    return _views[_currentViewIndex].center_orbit_z;
}

Matrix4 Camera::getCurrentViewCameraMatrix(void)
{
    if (_views == nullptr || _currentViewIndex >= _numViews)
    {
        return Matrix4(1.0f);
    }
    return _views[_currentViewIndex].cam_pos;
}

void Camera::addView(double *in_matrix, double z_center, int index, char *name)
{
    if (index < 0 || index >= _numViews)
    {
        return;
    }
    if (_views == nullptr)
    {
        _views = new View[_numViews];
    }
    if (_views == nullptr)
    {
        return;
    }
    _views[index].cam_pos.columns[0] = Vector4(in_matrix[0], in_matrix[1], in_matrix[2], 0);
    _views[index].cam_pos.columns[1] = Vector4(in_matrix[3], in_matrix[4], in_matrix[5], 0);
    _views[index].cam_pos.columns[2] = Vector4(in_matrix[6], in_matrix[7], in_matrix[8], 0);
    _views[index].cam_pos.columns[3] = Vector4(in_matrix[9], in_matrix[10], in_matrix[11], 1);
    _views[index].center_orbit_z = z_center;
    _views[index].name = new char[strlen(name) + 1];
    if (_views[index].name == nullptr)
    {
        return;
    }
    strcpy(_views[index].name, name);

    _views[index].position = Vector3(_views[index].cam_pos.columns[3].x, _views[index].cam_pos.columns[3].y, _views[index].cam_pos.columns[3].z);

    /* Compute pitch and yaw from the rotation portion of the matrix */
    double r31 = _views[index].cam_pos.columns[0].z;
    double pitch, yaw, roll;
    double temp;

    if (!(r31 == 1.0 || r31 == -1))
    {
        pitch = -asin(r31);
        temp = cos(pitch);
        yaw = atan2(_views[index].cam_pos.columns[1].z / temp, _views[index].cam_pos.columns[2].z / temp);
        roll = atan2(_views[index].cam_pos.columns[0].y / temp, _views[index].cam_pos.columns[0].x / temp);
    }
    else
    {
        roll = 0.0f;
        if (r31 == -1.0)
        {
            pitch = MUTIL_PI / 2.0f;
            yaw = atan2(_views[index].cam_pos.columns[1].x, _views[index].cam_pos.columns[2].x);
        }
        else
        {
            pitch = -MUTIL_PI / 2.0f;
            yaw = atan2(-_views[index].cam_pos.columns[1].x, -_views[index].cam_pos.columns[2].x);
        }
    }

    _views[index].pitch = mutil::degrees(pitch);
    _views[index].yaw = mutil::degrees(yaw);
    _views[index].roll = mutil::degrees(roll);
    _views[index].orientation = matrixToQuaternion(_views[index].cam_pos);
    Vector4 offset_vector = Vector4(0.0f, 0.0f, (float)z_center, 1.0f);
    Vector4 world_pivot = _views[index].cam_pos * offset_vector;
    _views[index].orbitCenter = Vector3(world_pivot.x, world_pivot.y, world_pivot.z);

    if (index == 0)
    {
        _modelWorldCenter = world_pivot;
    }
}

void Camera::upDateView(Product &product)
{
    if (_newViewIndex == _currentViewIndex)
    {
        return;
    }

    if (_viewConvexStep == 0.0)
    {
        _productModel = product.model();
    }

    _viewConvexStep += 1.0 / NUM_VIEW_STEPS;
    float t = static_cast<float>(_viewConvexStep);
    if (t > 1.0f)
    {
        t = 1.0f;
    }

    // Smooth ease-in / ease-out
    float smoothT = t * t * (3.0f - 2.0f * t);

    const View &v0 = _views[_currentViewIndex];
    const View &v1 = _views[_newViewIndex];

    // 1. Spherical interpolation of camera orientation
    _orientation = safeSlerp(v0.orientation, v1.orientation, smoothT);

    // 2. Orbit around the interpolated target center so the object stays framed
    Vector3 pivot = v0.orbitCenter * (1.0f - smoothT) + v1.orbitCenter * smoothT;
    float r0 = (v0.position - v0.orbitCenter).length();
    float r1 = (v1.position - v1.orbitCenter).length();
    float radius = r0 * (1.0f - smoothT) + r1 * smoothT;

    Vector3 forward = mutil::rotatevector(_orientation, Vector3(0.0f, 0.0f, 1.0f));
    _position = pivot - forward * radius;

    // 3. Smoothly reset any arcball rotation/scale applied to the root product
    //    (Using rigid quaternion + scale interpolation instead of matrix lerping)
    Vector3 startScale(
        Vector3(_productModel.columns[0].x, _productModel.columns[0].y, _productModel.columns[0].z).length(),
        Vector3(_productModel.columns[1].x, _productModel.columns[1].y, _productModel.columns[1].z).length(),
        Vector3(_productModel.columns[2].x, _productModel.columns[2].y, _productModel.columns[2].z).length()
    );
    Matrix4 rotOnly = _productModel;
    if (startScale.x > 1e-6f) rotOnly.columns[0] /= startScale.x;
    if (startScale.y > 1e-6f) rotOnly.columns[1] /= startScale.y;
    if (startScale.z > 1e-6f) rotOnly.columns[2] /= startScale.z;

    Quaternion qModelStart = matrixToQuaternion(rotOnly);
    Quaternion qModelCurr = safeSlerp(qModelStart, Quaternion(), smoothT);

    Vector3 transStart(_productModel.columns[3].x, _productModel.columns[3].y, _productModel.columns[3].z);
    Vector3 transCurr = transStart * (1.0f - smoothT);
    Vector3 scaleCurr = startScale * (1.0f - smoothT) + Vector3(1.0f, 1.0f, 1.0f) * smoothT;

    Matrix4 modelMat = qModelCurr.torotation();
    modelMat.columns[0] *= scaleCurr.x;
    modelMat.columns[1] *= scaleCurr.y;
    modelMat.columns[2] *= scaleCurr.z;
    modelMat.columns[3] = Vector4(transCurr.x, transCurr.y, transCurr.z, 1.0f);
    product.setModel(modelMat);

    if (t >= 1.0f)
    {
        _viewConvexStep = 0.0;
        _currentViewIndex = _newViewIndex;
        _position = v1.position;
        _orientation = v1.orientation;
        _pitch = v1.pitch;
        _yaw = v1.yaw;
        _roll = v1.roll;
        _productModel = Matrix4(1.0f);
        product.setModel(_productModel);
    }

    update();
}

void Camera::setView(int index)
{
    if (index < 0 || index >= (int)_numViews)
    {
        return;
    }
    _position = _views[index].position;
    _pitch = _views[index].pitch;
    _yaw = _views[index].yaw;
    _roll = _views[index].roll;
    _orientation = _views[index].orientation;
    _currentViewIndex = index;
    _newViewIndex = index;
    _viewConvexStep = 0.0;
    update();
}

Camera::Camera() : _pitch(0.0f), _yaw(0.0f), _roll(0.0f), _numViews(0), _views(nullptr),
                   _fov(45.0f), _near(0.1f), _far(400.0f), _aspect(16.0f / 9.0f)
{
    _orientation = Quaternion();
    update();
}
