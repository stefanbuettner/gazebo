/*
 * Copyright (C) 2012-2013 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/
#include "gazebo/common/Exception.hh"
#include "gazebo/msgs/msgs.hh"
#include "gazebo/physics/MultiRayShape.hh"

using namespace gazebo;
using namespace physics;

//////////////////////////////////////////////////
MultiRayShape::MultiRayShape(CollisionPtr _parent)
  : Shape(_parent)
{
  this->AddType(MULTIRAY_SHAPE);
  this->SetName("multiray");
}

//////////////////////////////////////////////////
MultiRayShape::~MultiRayShape()
{
  this->rays.clear();
}

//////////////////////////////////////////////////
void MultiRayShape::Init()
{
  math::Vector3 start, end, axis;
  double yawAngle, pitchAngle;
  math::Quaternion ray;
  double yDiff;
  double horzMinAngle, horzMaxAngle;
  int horzSamples = 1;
  // double horzResolution = 1.0;

  double pDiff = 0;
  int vertSamples = 1;
  // double vertResolution = 1.0;
  double vertMinAngle = 0;
  double vertMaxAngle = 0;

  double minRange, maxRange;

  if (this->rml.geometry().ray().scan().has_vertical())
  {
    vertMinAngle = this->rml.geometry().ray().scan().vertical().min_angle();
    vertMaxAngle = this->rml.geometry().ray().scan().vertical().max_angle();
    vertSamples = this->rml.geometry().ray().scan().vertical().samples();
    // vertResolution =
    // this->rml.geometry().ray().scan().vertical().resolution();
    pDiff = vertMaxAngle - vertMinAngle;
  }

  horzMinAngle = this->rml.geometry().ray().scan().horizontal().min_angle();
  horzMaxAngle = this->rml.geometry().ray().scan().horizontal().max_angle();
  horzSamples = this->rml.geometry().ray().scan().horizontal().samples();
  // horzResolution =
  // this->rml.geometry().ray().scan().horizontal().resolution();
  yDiff = horzMaxAngle - horzMinAngle;

  minRange = this->rml.geometry().ray().range().min();
  maxRange = this->rml.geometry().ray().range().max();

  this->offset = this->collisionParent->GetRelativePose();

  // Create an array of ray collisions
  for (unsigned int j = 0; j < (unsigned int)vertSamples; ++j)
  {
    for (unsigned int i = 0; i < (unsigned int)horzSamples; ++i)
    {
      yawAngle = (horzSamples == 1) ? 0 :
        i * yDiff / (horzSamples - 1) + horzMinAngle;

      pitchAngle = (vertSamples == 1)? 0 :
        j * pDiff / (vertSamples - 1) + vertMinAngle;

      // since we're rotating a unit x vector, a pitch rotation will now be
      // around the negative y axis
      ray.SetFromEuler(math::Vector3(0.0, -pitchAngle, yawAngle));
      axis = this->offset.rot * ray * math::Vector3(1.0, 0.0, 0.0);

      start = (axis * minRange) + this->offset.pos;
      end = (axis * maxRange) + this->offset.pos;

      this->AddRay(start, end);
    }
  }
}

//////////////////////////////////////////////////
void MultiRayShape::SetScale(const math::Vector3 &_scale)
{
  if (this->scale == _scale)
    return;

  this->scale = _scale;

  for (unsigned int i = 0; i < this->rays.size(); ++i)
  {
    this->rays[i]->SetScale(this->scale);
  }
}

//////////////////////////////////////////////////
double MultiRayShape::GetRange(int _index)
{
  if (_index < 0 || _index >= static_cast<int>(this->rays.size()))
  {
    std::ostringstream stream;
    stream << "index[" << _index << "] out of range[0-"
      << this->rays.size() << "]";
    gzthrow(stream.str());
  }

  // Add min range, because we measured from min range.
  return this->GetMinRange() + this->rays[_index]->GetLength();
}

//////////////////////////////////////////////////
double MultiRayShape::GetRetro(int _index)
{
  if (_index < 0 || _index >= static_cast<int>(this->rays.size()))
  {
    std::ostringstream stream;
    stream << "index[" << _index << "] out of range[0-"
      << this->rays.size() << "]";
    gzthrow(stream.str());
  }

  return this->rays[_index]->GetRetro();
}

//////////////////////////////////////////////////
int MultiRayShape::GetFiducial(int _index)
{
  if (_index < 0 || _index >= static_cast<int>(this->rays.size()))
  {
    std::ostringstream stream;
    stream << "index[" << _index << "] out of range[0-"
      << this->rays.size() << "]";
    gzthrow(stream.str());
  }

  return this->rays[_index]->GetFiducial();
}

//////////////////////////////////////////////////
void MultiRayShape::Update()
{
  // The measurable range is (max-min)
  double fullRange = this->GetMaxRange() - this->GetMinRange();

  // Reset the ray lengths and mark the collisions as dirty (so they get
  // redrawn)
  unsigned int ray_size = this->rays.size();
  for (unsigned int i = 0; i < ray_size; i++)
  {
    this->rays[i]->SetLength(fullRange);
    this->rays[i]->SetRetro(0.0);

    // Get the global points of the line
    this->rays[i]->Update();
  }

  // do actual collision checks
  this->UpdateRays();

  // for plugin
  this->newLaserScans();
}

//////////////////////////////////////////////////
void MultiRayShape::AddRay(const math::Vector3 &/*_start*/,
                           const math::Vector3 &/*_end*/)
{
  // robot_msgs::Vector3d *pt = NULL;

  // FIXME: need to lock this when spawning models with ray.
  // This fails because RaySensor::laserShape->Update()
  // is called before rays could be constructed.
}


//////////////////////////////////////////////////
double MultiRayShape::GetMinRange() const
{
  return this->rml.geometry().ray().range().min();
}

//////////////////////////////////////////////////
double MultiRayShape::GetMaxRange() const
{
  return this->rml.geometry().ray().range().max();
}

//////////////////////////////////////////////////
double MultiRayShape::GetResRange() const
{
  return this->rml.geometry().ray().range().resolution();
}

//////////////////////////////////////////////////
int MultiRayShape::GetSampleCount() const
{
  return this->rml.geometry().ray().scan().horizontal().samples();
}

//////////////////////////////////////////////////
double MultiRayShape::GetScanResolution() const
{
  return this->rml.geometry().ray().scan().horizontal().resolution();
}

//////////////////////////////////////////////////
math::Angle MultiRayShape::GetMinAngle() const
{
  return this->rml.geometry().ray().scan().horizontal().min_angle();
}

//////////////////////////////////////////////////
math::Angle MultiRayShape::GetMaxAngle() const
{
  return this->rml.geometry().ray().scan().horizontal().max_angle();
}

//////////////////////////////////////////////////
int MultiRayShape::GetVerticalSampleCount() const
{
  if (this->rml.geometry().ray().scan().has_vertical())
    return this->rml.geometry().ray().scan().vertical().samples();
  else
    return 1;
}

//////////////////////////////////////////////////
double MultiRayShape::GetVerticalScanResolution() const
{
  if (this->rml.geometry().ray().scan().has_vertical())
    return this->rml.geometry().ray().scan().vertical().resolution();
  else
    return 1;
}

//////////////////////////////////////////////////
math::Angle MultiRayShape::GetVerticalMinAngle() const
{
  if (this->rml.geometry().ray().scan().has_vertical())
    return this->rml.geometry().ray().scan().vertical().min_angle();
  else
    return math::Angle(0);
}

//////////////////////////////////////////////////
math::Angle MultiRayShape::GetVerticalMaxAngle() const
{
  if (this->rml.geometry().ray().scan().has_vertical())
    return this->rml.geometry().ray().scan().vertical().max_angle();
  else
    return math::Angle(0);
}

//////////////////////////////////////////////////
void MultiRayShape::FillMsg(msgs::Geometry &/*_msg*/)
{
}

//////////////////////////////////////////////////
void MultiRayShape::ProcessMsg(const msgs::Geometry &/*_msg*/)
{
}
