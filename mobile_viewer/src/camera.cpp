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

#include <string.h>
#include "camera.h"

void Camera::update()
{
    // Data given in row centric manner
    Matrix3 R_psi = Matrix3(1, 0, 0, 0, cos((double)mutil::radians(_yaw)),
        -sin((double)mutil::radians(_yaw)), 0, sin((double)mutil::radians(_yaw)),
        cos((double)mutil::radians(_yaw)));

    Matrix3 R_theta = Matrix3(cos((double)mutil::radians(_pitch)), 0, sin((double)mutil::radians(_pitch)),
        0, 1, 0, -sin((double)mutil::radians(_pitch)), 0, cos((double)mutil::radians(_pitch)));
    Matrix3 R_phi = Matrix3(cos((double)mutil::radians(_roll)), -sin((double)mutil::radians(_roll)), 0,
        sin((double)mutil::radians(_roll)), cos((double)mutil::radians(_roll)), 0, 0, 0, 1);

    Matrix3 Rtemp = R_phi * R_theta;
    Matrix3 Rtemp2 = Rtemp * R_psi;
    Matrix3 R = R_phi * R_theta * R_psi;

    // Load R into _view
    _view = Matrix4(1.0f);
    _view.columns[0] = Vector4(R.columns[0].x, R.columns[0].y, R.columns[0].z, 0.0f);
    _view.columns[1] = Vector4(R.columns[1].x, R.columns[1].y, R.columns[1].z, 0.0f);
    _view.columns[2] = Vector4(R.columns[2].x, R.columns[2].y, R.columns[2].z, 0.0f);
    _view.columns[3] = Vector4(_position.x, _position.y, _position.z, 1.0f);

    /* Find the front vector and the up vector */
    Vector4 front  = _view * Vector4(0.0f, 0.0f, 1.0f, 0.0f);
    Vector3 front3 = Vector3(front.x, front.y, front.z);
    Vector4 up = _view * Vector4(0.0f, 1.0f, 0.0f, 0.0f);
    Vector3 up3 = Vector3(up.x, up.y, up.z);

    _front = front3;
    _up = up3;
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

    if (index == 0)
    {
        Vector4 offset_vector = Vector4(0.0, 0.0, z_center, 1.0);
        _modelWorldCenter = _views[index].cam_pos * offset_vector;
    }
}

void Camera::setView(int index)
{
    if (index >= _numViews)
    {
        return;
    }
    _position = _views[index].position;
    _pitch = _views[index].pitch;
    _yaw = _views[index].yaw;
    _roll = _views[index].roll;
    _currentViewIndex = index;
    _newViewIndex = index;
    update();
}

Camera::Camera() : _pitch(0.0f), _yaw(0.0f), _roll(0.0f), _numViews(0), _views(nullptr),
                   _fov(45.0f), _near(0.1f), _far(400.0f), _aspect(16.0f / 9.0f)
{
    update();
}
