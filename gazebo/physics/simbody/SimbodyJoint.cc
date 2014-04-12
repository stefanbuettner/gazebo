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
#include "gazebo/common/Console.hh"
#include "gazebo/physics/Model.hh"
#include "gazebo/physics/World.hh"

#include "gazebo/physics/simbody/simbody_inc.h"
#include "gazebo/physics/simbody/SimbodyLink.hh"
#include "gazebo/physics/simbody/SimbodyJoint.hh"

using namespace gazebo;
using namespace physics;

//////////////////////////////////////////////////
SimbodyJoint::SimbodyJoint(BasePtr _parent)
  : Joint(_parent)
{
  this->isReversed = false;
  this->mustBreakLoopHere = false;
}

//////////////////////////////////////////////////
SimbodyJoint::~SimbodyJoint()
{
}

//////////////////////////////////////////////////
void SimbodyJoint::Load(sdf::ElementPtr _sdf)
{
  rml::Joint rmlJoint;
  rmlJoint.SetFromXML(_sdf);
  this->Load(rmlJoint);
}

//////////////////////////////////////////////////
bool SimbodyJoint::Load(const rml::Joint &_rml)
{
  if (!Joint::Load(_rml))
  {
    gzerr << "Unable to load simbody joint[" << _rml.name() << "]\n";
    return false;
  }

  // store a pointer to the simbody physics engine for convenience
  this->simbodyPhysics = boost::dynamic_pointer_cast<SimbodyPhysics>(
    this->model->GetWorld()->GetPhysicsEngine());

  // read must_be_loop_joint
  // \TODO: clean up
  if (_rml.has_physics() && _rml.physics().has_simbody())
    this->mustBreakLoopHere = _rml.physics().simbody().must_be_loop_joint();

  if (this->rml.has_axis())
  {
    if (this->rml.axis().has_dynamics())
    {
      /// \TODO: switch to GetElement so default values apply
      /// \TODO: check all physics engines
      if (this->rml.axis().dynamics().has_damping())
      {
        this->dissipationCoefficient[0] =
          this->rml.axis().dynamics().damping();
      }
    }
  }

  if (this->rml.has_axis2())
  {
    if (this->rml.axis2().has_dynamics())
    {
      /// \TODO: switch to GetElement so default values apply
      /// \TODO: check all physics engines
      if (this->rml.axis2().dynamics().has_damping())
      {
        this->dissipationCoefficient[1] = 
          this->rml.axis2().dynamics().damping();
      }
    }
  }

  // Read old style
  //    <pose>pose on child</pose>
  // or new style

  // to support alternative unassembled joint pose specification
  // check if the new style of pose specification exists
  //    <parent>
  //      <link>parentName</link>
  //      <pose>parentPose</pose>
  //    </parent>
  // as compared to old style
  //    <parent>parentName</parent>
  //
  // \TODO: consider storing the unassembled format parent pose when
  // calling Joint::Load(sdf::ElementPtr)

  math::Pose childPose(math::Vector3(
        this->rml.pose().pos.x,
        this->rml.pose().pos.y,
        this->rml.pose().pos.z),
      math::Quaternion(
        this->rml.pose().rot.x, this->rml.pose().rot.y,
        this->rml.pose().rot.z, this->rml.pose().rot.w));

  // The following is not supported.
  // if (this->rml.child().has_pose())
  // {
  //   childPose.Set(
  //       math::Vector3(
  //         this->rml.child().pose().pos.x,
  //         this->rml.child().pose().pos.y,
  //         this->rml.child().pose().pos.z),
  //       math::Quaternion(
  //         this->rml.child().pose().rot.x, this->rml.child().pose().rot.y,
  //         this->rml.child().pose().rot.z, this->rml.child().pose().rot.w));
  // }

  this->xCB = physics::SimbodyPhysics::Pose2Transform(childPose);

  math::Pose parentPose;

  // The following is not supported.
  // if (this->rml.parent().has_pose())
  //   this->xPA = physics::SimbodyPhysics::GetPose(this->rml.parent().pose());
  // else
  {
    SimTK::Transform X_MC, X_MP;
    if (this->parentLink)
    {
      X_MP = physics::SimbodyPhysics::Pose2Transform(
        this->parentLink->GetRelativePose());
    }
    else
    {
      // TODO: verify
      // parent frame is at the world frame
      X_MP = ~physics::SimbodyPhysics::Pose2Transform(
        this->model->GetWorldPose());
    }

    if (this->childLink)
    {
      X_MC = physics::SimbodyPhysics::Pose2Transform(
        this->childLink->GetRelativePose());
    }
    else
    {
      // TODO: verify
      X_MC = ~physics::SimbodyPhysics::Pose2Transform(
        this->model->GetWorldPose());
    }

    const SimTK::Transform X_PC = ~X_MP*X_MC;

    // i.e., A spatially coincident with B
    this->xPA = X_PC*this->xCB;
  }

  return true;
}

//////////////////////////////////////////////////
void SimbodyJoint::Reset()
{
  Joint::Reset();
}

//////////////////////////////////////////////////
void SimbodyJoint::CacheForceTorque()
{
  const SimTK::State &state = this->simbodyPhysics->integ->getAdvancedState();

  // force calculation of reaction forces
  this->simbodyPhysics->system.realize(state);

  // In simbody, parent is always inboard (closer to ground in the tree),
  //   child is always outboard (further away from ground in the tree).
  SimTK::SpatialVec spatialForceOnInboardBodyInGround =
    this->mobod.findMobilizerReactionOnParentAtFInGround(state);

  SimTK::SpatialVec spatialForceOnOutboardBodyInGround =
    this->mobod.findMobilizerReactionOnBodyAtMInGround(state);

  // determine if outboard body is parent or child based on isReversed flag.
  // determine if inboard body is parent or child based on isReversed flag.
  SimTK::SpatialVec spatialForceOnParentBodyInGround;
  SimTK::SpatialVec spatialForceOnChildBodyInGround;
  // parent and child mobods in gazebo's parent/child tree structure.
  SimTK::MobilizedBody parentMobod;
  SimTK::MobilizedBody childMobod;
  if (!this->isReversed)
  {
    spatialForceOnParentBodyInGround = spatialForceOnInboardBodyInGround;
    spatialForceOnChildBodyInGround = spatialForceOnOutboardBodyInGround;
    childMobod = this->mobod;
    parentMobod = this->mobod.getParentMobilizedBody();
  }
  else
  {
    spatialForceOnParentBodyInGround = spatialForceOnOutboardBodyInGround;
    spatialForceOnChildBodyInGround = spatialForceOnInboardBodyInGround;
    childMobod = this->mobod.getParentMobilizedBody();
    parentMobod = this->mobod;
  }

  // get rotation from ground to child/parent link frames
  const SimTK::Rotation& R_GC = childMobod.getBodyRotation(state);
  const SimTK::Rotation& R_GP = parentMobod.getBodyRotation(state);

  // re-express in child link frame
  SimTK::Vec3 reactionTorqueOnChildBody =
    ~R_GC * spatialForceOnChildBodyInGround[0];
  SimTK::Vec3 reactionForceOnChildBody =
    ~R_GC * spatialForceOnChildBodyInGround[1];

  SimTK::Vec3 reactionTorqueOnParentBody =
    ~R_GP * spatialForceOnParentBodyInGround[0];
  SimTK::Vec3 reactionForceOnParentBody =
    ~R_GP * spatialForceOnParentBodyInGround[1];

  // gzerr << "parent[" << this->GetName()
  //       << "]: t[" << reactionTorqueOnParentBody
  //       << "] f[" << reactionForceOnParentBody
  //       << "]\n";

  // gzerr << "child[" << this->GetName()
  //       << "]: t[" << reactionTorqueOnChildBody
  //       << "] f[" << reactionForceOnChildBody
  //       << "]\n";

  // Note minus sign indicates these are reaction forces
  // by the Link on the Joint in the target Link frame.
  this->wrench.body1Force =
    -SimbodyPhysics::Vec3ToVector3(reactionForceOnParentBody);
  this->wrench.body1Torque =
    -SimbodyPhysics::Vec3ToVector3(reactionTorqueOnParentBody);

  this->wrench.body2Force =
    -SimbodyPhysics::Vec3ToVector3(reactionForceOnChildBody);
  this->wrench.body2Torque =
    -SimbodyPhysics::Vec3ToVector3(reactionTorqueOnChildBody);
}

//////////////////////////////////////////////////
LinkPtr SimbodyJoint::GetJointLink(int _index) const
{
  LinkPtr result;

  if (_index == 0 || _index == 1)
  {
    SimbodyLinkPtr simbodyLink1 =
      boost::static_pointer_cast<SimbodyLink>(this->childLink);

    SimbodyLinkPtr simbodyLink2 =
      boost::static_pointer_cast<SimbodyLink>(this->parentLink);
  }

  return result;
}

//////////////////////////////////////////////////
bool SimbodyJoint::AreConnected(LinkPtr _one, LinkPtr _two) const
{
  return ((this->childLink.get() == _one.get() &&
           this->parentLink.get() == _two.get()) ||
          (this->childLink.get() == _two.get() &&
           this->parentLink.get() == _one.get()));
}

//////////////////////////////////////////////////
void SimbodyJoint::Detach()
{
  this->childLink.reset();
  this->parentLink.reset();
}

//////////////////////////////////////////////////
void SimbodyJoint::SetAxis(int _index, const math::Vector3 &/*_axis*/)
{
  math::Pose parentModelPose;
  if (this->parentLink)
    parentModelPose = this->parentLink->GetModel()->GetWorldPose();

  // Set joint axis
  // assuming incoming axis is defined in the model frame, so rotate them
  // into the inertial frame
  // TODO: switch so the incoming axis is defined in the child frame.
  math::Vector3 axis = parentModelPose.rot.RotateVector(
    math::Vector3(this->rml.axis().xyz().x, this->rml.axis().xyz().y,
                  this->rml.axis().xyz().z));

  if (_index == 0)
    this->rml.axis().set_xyz(sdf::Vector3(axis.x, axis.y, axis.z));
  else if (_index == 1)
    this->rml.axis2().set_xyz(sdf::Vector3(axis.x, axis.y, axis.z));
  else
    gzerr << "SetAxis index [" << _index << "] out of bounds\n";
}

//////////////////////////////////////////////////
JointWrench SimbodyJoint::GetForceTorque(unsigned int /*_index*/)
{
  return this->wrench;
}

//////////////////////////////////////////////////
void SimbodyJoint::SetForce(int _index, double _force)
{
  double force = Joint::CheckAndTruncateForce(_index, _force);
  this->SaveForce(_index, force);
  this->SetForceImpl(_index, force);

  // for engines that supports auto-disable of links
  if (this->childLink) this->childLink->SetEnabled(true);
  if (this->parentLink) this->parentLink->SetEnabled(true);
}

//////////////////////////////////////////////////
double SimbodyJoint::GetForce(unsigned int _index)
{
  if (_index < this->GetAngleCount())
  {
    return this->forceApplied[_index];
  }
  else
  {
    gzerr << "Invalid joint index [" << _index
          << "] when trying to get force\n";
    return 0;
  }
}

//////////////////////////////////////////////////
void SimbodyJoint::SaveForce(int _index, double _force)
{
  // this bit of code actually doesn't do anything physical,
  // it simply records the forces commanded inside forceApplied.
  if (_index >= 0 && static_cast<unsigned int>(_index) < this->GetAngleCount())
  {
    if (this->forceAppliedTime < this->GetWorld()->GetSimTime())
    {
      // reset forces if time step is new
      this->forceAppliedTime = this->GetWorld()->GetSimTime();
      this->forceApplied[0] = this->forceApplied[1] = 0;
    }

    this->forceApplied[_index] += _force;
  }
  else
    gzerr << "Something's wrong, joint [" << this->GetName()
          << "] index [" << _index
          << "] out of range.\n";
}

//////////////////////////////////////////////////
void SimbodyJoint::SaveSimbodyState(const SimTK::State &/*_state*/)
{
  // Not implemented
}

//////////////////////////////////////////////////
void SimbodyJoint::RestoreSimbodyState(SimTK::State &/*_state*/)
{
  // Not implemented
}

//////////////////////////////////////////////////
void SimbodyJoint::SetAnchor(int /*_index*/,
    const gazebo::math::Vector3 & /*_anchor*/)
{
  gzdbg << "Not implement in Simbody\n";
}

//////////////////////////////////////////////////
void SimbodyJoint::SetDamping(int _index, const double _damping)
{
  if (static_cast<unsigned int>(_index) < this->GetAngleCount())
  {
    this->SetStiffnessDamping(static_cast<unsigned int>(_index),
      this->stiffnessCoefficient[_index],
      _damping);
  }
  else
  {
     gzerr << "SimbodyJoint::SetDamping: index[" << _index
           << "] is out of bounds (GetAngleCount() = "
           << this->GetAngleCount() << ").\n";
     return;
  }
}

//////////////////////////////////////////////////
void SimbodyJoint::SetStiffness(int _index, const double _stiffness)
{
  if (static_cast<unsigned int>(_index) < this->GetAngleCount())
  {
    this->SetStiffnessDamping(static_cast<unsigned int>(_index),
      _stiffness,
      this->dissipationCoefficient[_index]);
  }
  else
  {
     gzerr << "SimbodyJoint::SetStiffness: index[" << _index
           << "] is out of bounds (GetAngleCount() = "
           << this->GetAngleCount() << ").\n";
     return;
  }
}

//////////////////////////////////////////////////
void SimbodyJoint::SetStiffnessDamping(unsigned int _index,
  double _stiffness, double _damping, double _reference)
{
  if (_index < this->GetAngleCount())
  {
    this->stiffnessCoefficient[_index] = _stiffness;
    this->dissipationCoefficient[_index] = _damping;
    this->springReferencePosition[_index] = _reference;

    /// \TODO: address multi-axis joints
    this->damper.setDamping(
      this->simbodyPhysics->integ->updAdvancedState(),
      _damping);

    /// \TODO: add spring force element
    gzdbg << "Joint [" << this->GetName()
           << "] stiffness not implement in Simbody\n";
  }
  else
    gzerr << "SetStiffnessDamping _index too large.\n";
}

//////////////////////////////////////////////////
math::Vector3 SimbodyJoint::GetAnchor(int /*_index*/) const
{
  gzdbg << "Not implement in Simbody\n";
  return math::Vector3();
}

//////////////////////////////////////////////////
math::Vector3 SimbodyJoint::GetLinkForce(unsigned int /*_index*/) const
{
  gzdbg << "Not implement in Simbody\n";
  return math::Vector3();
}

//////////////////////////////////////////////////
math::Vector3 SimbodyJoint::GetLinkTorque(unsigned int /*_index*/) const
{
  gzdbg << "Not implement in Simbody\n";
  return math::Vector3();
}

//////////////////////////////////////////////////
void SimbodyJoint::SetAttribute(Attribute, int /*_index*/, double /*_value*/)
{
  gzdbg << "Not implement in Simbody\n";
}

//////////////////////////////////////////////////
void SimbodyJoint::SetAttribute(const std::string &/*_key*/, int /*_index*/,
    const boost::any &/*_value*/)
{
  gzdbg << "Not implement in Simbody\n";
}

//////////////////////////////////////////////////
double SimbodyJoint::GetAttribute(const std::string &/*_key*/,
    unsigned int /*_index*/)
{
  gzdbg << "Not implement in Simbody\n";
  return 0;
}
