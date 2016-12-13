// The HiQP Control Framework, an optimal control framework targeted at robotics
// Copyright (C) 2016 Marcus A Johansson
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <hiqp/tasks/task_avoid_collisions_sdf.h>
#include <hiqp/utilities.h>
#include <hiqp_collision_check/sdf_collision_checker.h>
#include <iostream>

#define SAFETY_DISTANCE 0.005 //distance added to the gradient norm to act as a safety margin

namespace hiqp
{
  namespace tasks
  {
    //==================================================================================
    TaskAvoidCollisionsSDF::TaskAvoidCollisionsSDF(std::shared_ptr<GeometricPrimitiveMap> geom_prim_map, std::shared_ptr<Visualizer> visualizer)  : TaskDefinition(geom_prim_map, visualizer) 
    {
      printHiqpInfo("Initializing collision checker");
      collision_checker_ = std::make_shared<hiqp::SDFCollisionCheck> ();
      collision_checker_->init();
      collision_checker_->activate();
    }
    //==================================================================================
    TaskAvoidCollisionsSDF::~TaskAvoidCollisionsSDF()
    {
      collision_checker_->deactivate();
    }
    //==================================================================================

    int TaskAvoidCollisionsSDF::init(const std::vector<std::string>& parameters,
				     RobotStatePtr robot_state) {
      int size = parameters.size();
      if (size < 2) {
	printHiqpWarning("TaskAvoidCollisionsSDF requires at least 2 parameters, got " 
			 + std::to_string(size) + "! Initialization failed!");
	return -2;
      }

      reset();

      //loop through all the geometric primitives intended for the obstacle avoidance and extract the pointers
      std::shared_ptr<GeometricPrimitiveMap> gpm = this->getGeometricPrimitiveMap();
      for (unsigned int i=1; i<size; i++)
	{
	  //make sure the primitive is either a point or a sphere - avoidance with other primitives is not currently implemented
	  if (gpm->getGeometricPrimitive<GeometricPoint>(parameters.at(i)) != nullptr)
	    {
	      std::shared_ptr<GeometricPoint> point=gpm->getGeometricPrimitive<GeometricPoint>(parameters.at(i));
	      //make sure the avoidance point is attached to the manipulator
	      if(kdl_getQNrFromLinkName(robot_state->kdl_tree_, point->getFrameId()) == -1)
		{
		  printHiqpWarning("TaskAvoidCollisionsSDF::init, avoidance point '" + parameters.at(i) + "' is not attached to the manipulator! Initialization failed.");
		  return -2; 
		}
	      primitives_.push_back(point);
	      n_dimensions_++;
	    }
	  else if (gpm->getGeometricPrimitive<GeometricSphere>(parameters.at(i)) != nullptr)
	    {
	      std::shared_ptr<GeometricSphere> sphere = gpm->getGeometricPrimitive<GeometricSphere>(parameters.at(i));
	      //make sure the avoidance sphere is attached to the manipulator
	      if(kdl_getQNrFromLinkName(robot_state->kdl_tree_, sphere->getFrameId()) == -1)
		{
		  printHiqpWarning("TaskAvoidCollisionsSDF::init, avoidance sphere '" + parameters.at(i) + "' is not attached to the manipulator! Initialization failed.");
		  return -2; 
		}
	      primitives_.push_back(sphere);
	      n_dimensions_++;
	    }
	  else
	    { 
	      printHiqpWarning("TaskAvoidCollisionsSDF::init, couldn't find primitive '" + parameters.at(i) + "'! Initialization failed.");
	      return -2; 
	    }
	}

      performance_measures_.resize(0);
      task_types_.insert(task_types_.begin(), n_dimensions_, 1); // -1 leq, 0 eq, 1 geq

      //DEBUG ===========================================
      // std::cerr<<"TaskAvoidCollisionsSDF::init(.)"<<std::endl;
      // std::cerr<<"size e: "<<e_.size()<<std::endl;
      // std::cerr<<"size J: "<<J_.rows()<<" "<<J_.cols()<<std::endl;
      // std::cerr<<"size task_types: "<<task_types_.size()<<std::endl;
      //DEBUG END ===========================================

      fk_solver_pos_ = std::make_shared<KDL::TreeFkSolverPos_recursive>(robot_state->kdl_tree_);
      fk_solver_jac_ = std::make_shared<KDL::TreeJntToJacSolver>(robot_state->kdl_tree_);

      root_frame_id_=robot_state->kdl_tree_.getRootSegment()->second.segment.getName();
     grad_vis_pub_ = nh_.advertise<visualization_msgs::MarkerArray>( "gradient_marker", 1 );
      return 0;
    }
    //==================================================================================
    /// \bug Should rewrite to survive consistency check
    int TaskAvoidCollisionsSDF::update(RobotStatePtr robot_state) {

      e_.resize(0);
      J_.resize(0, robot_state->getNumJoints());

      for(unsigned int i=0; i<primitives_.size();i++)
	{
	  // compute forward kinematics for each primitive (yet unimplemented primitives such as capsules could have more than one ee_/J associated with them, hence the vector-valued argument
	  std::vector<KinematicQuantities> kin_q_list;
	  if(primitiveForwardKinematics(kin_q_list, primitives_[i], robot_state) < 0)
	    {
	      printHiqpWarning("TaskAvoidCollisionsSDF::update, primitive forward kinematics computation failed.");
	      return -2;
	    }

	  //get the gradient vectors associated with the ee points of the current primitive from the SDF map
	  SamplesVector test_pts;
	  for (unsigned int j=0; j<kin_q_list.size(); j++)
	    {
	      Eigen::Vector3d p(kin_q_list[j].ee_p_.x(),kin_q_list[j].ee_p_.y(),kin_q_list[j].ee_p_.z());
	      test_pts.push_back(p);
	    }
	  SamplesVector gradients;
	  if(!collision_checker_->obstacleGradientBulk(test_pts, gradients, root_frame_id_))
	    {
	      printHiqpWarning("TaskAvoidCollisionsSDF::update, collision checker failed.");
	      return -2;
	    }
	  assert(gradients.size() > 0); //make sure a gradient was found

	  //DEBUG ====================================
	  // for(int k=0; k<gradients.size();k++)
	  //   {
	  //     if(!collision_checker_->isValid(gradients[k]))
	  // 	continue;

	  //       std::cerr<<"Collision checker test point: "<<test_pts[k].transpose()<<std::endl;
	  //     std::cerr<<"Collision checker computed gradient: "<<gradients[k].transpose()<<", norm: "<<gradients[k].norm()<<std::endl;
	  //   }
	  publishGradientVisualization(gradients, test_pts);
	  //DEBUG END =====================================

	  //compute the task jacobian for the current geometric primitive
	  appendTaskJacobian(kin_q_list, gradients);
	  //compute the task function value vector for the current geometric primitive
	  appendTaskFunction(primitives_[i],kin_q_list, gradients);
	}

      return 0;
    }
    //==================================================================================
    void TaskAvoidCollisionsSDF::appendTaskJacobian(const std::vector<KinematicQuantities> kin_q_list ,const SamplesVector& gradients )
    {
      assert(kin_q_list.size()==gradients.size());
      for(unsigned int i=0; i<gradients.size();i++)
	{
	  Eigen::Vector3d gradient(gradients[i]);
	  J_.conservativeResize(J_.rows()+1, Eigen::NoChange);
	  //to check if a gradient to an obstacle is valid
	  if(!collision_checker_->isValid(gradient))
	    {
	      J_.row(J_.rows()-1).setZero();  //insert a zero-row
              continue;
	    }

	  //project the Jacobian onto the normalized gradient
	  gradient.normalize();
	  Eigen::MatrixXd ee_J_vel=kin_q_list[i].ee_J_.data.topRows<3>(); //velocity Jacobian
	  //DEBUG===============================
	  //std::cerr<<"Velocity Jacobian: "<<std::endl<<ee_J_vel<<std::endl;
	  //std::cerr<<"Normalized gradient transpose: "<<std::endl<<gradient.transpose()<<std::endl;
	  //DEBUG END===============================
	  J_.row(J_.rows()-1)= -gradient.transpose()*ee_J_vel;
	}
      //DEBUG===============================
      //std::cerr<<"Task Jacobian: "<<std::endl<<J_<<std::endl;
      //DEBUG END===============================
    }
    //==================================================================================
    void TaskAvoidCollisionsSDF::appendTaskFunction( const std::shared_ptr<geometric_primitives::GeometricPrimitive>& primitive, const std::vector<KinematicQuantities> kin_q_list, const SamplesVector& gradients)
    {
      assert(kin_q_list.size()==gradients.size());
      for(unsigned int i=0; i<gradients.size();i++)
       	{
    	  Eigen::Vector3d gradient(gradients[i]);
    	  e_.conservativeResize(e_.size()+1);          
    	  //check if a gradient to an obstacle is valid
    	  if(!collision_checker_->isValid(gradient))
    	    {
	      /// \bug this implicitly assumes that e*(e=0) = 0!
    	      e_(e_.size()-1) = 0.0; //insert zero
    	      continue;
    	    }

	  double d=gradient.norm()-SAFETY_DISTANCE;
    	  if(strcmp(primitive->getType().c_str(),"point")==0)
    	    {
    	      e_(e_.size()-1) = d;  //append the gradient length to the task function vector

    	    }
    	  else if(strcmp(primitive->getType().c_str(),"sphere")==0)
    	    {
    	      std::shared_ptr<GeometricSphere> sphere(std::static_pointer_cast<GeometricSphere>(primitive)); 
    	      e_(e_.size()-1) = d-sphere->getRadius();
    	    }
    	  else
	    {
	      printHiqpWarning("TaskAvoidCollisionsSDF::appendTaskFunction, appending task function for primitive type '" + primitive->getType() + "' not implemented yet. Setting task function value to 0!");
	      e_(e_.size()-1)=0.0;
	    }
	  
	}
      //DEBUG===============================
      //std::cerr<<"Task function value vector: "<<e_.transpose()<<std::endl;
      //DEBUG END===============================
      // if(e_(0)<0.0)
      // 	  ROS_WARN("negative e: %f",e_(0));
    }
    //==================================================================================
    int TaskAvoidCollisionsSDF::monitor() {
      return 0;
    }
    //==================================================================================
    void TaskAvoidCollisionsSDF::reset() {
      n_dimensions_=0;
      task_types_.clear();
      primitives_.clear();
    }
    //==================================================================================
    int TaskAvoidCollisionsSDF::forwardKinematics(KinematicQuantities& kin_q, RobotStatePtr const robot_state)const
    {
      if(fk_solver_pos_->JntToCart(robot_state->kdl_jnt_array_vel_.q, kin_q.ee_frame_, kin_q.frame_id_) < 0)
	{
	  printHiqpWarning("TaskAvoidCollisionsSDF::forwardKinematics, end-effector FK for link '" + kin_q.frame_id_ + "' failed.");
	  return -2;
	}
      //std::cout<<"ee_pose: "<<std::endl<<kin_q.ee_pose_<<std::endl;
      if(fk_solver_jac_->JntToJac(robot_state->kdl_jnt_array_vel_.q, kin_q.ee_J_, kin_q.frame_id_) < 0)
	{
	  printHiqpWarning("TaskAvoidCollisionsSDF::forwardKinematics, Jacobian computation for link '" + kin_q.frame_id_ + "' failed.");
	  return -2;
	}
      // std::cout<<"ee_J: "<<std::endl<<kin_q.ee_J_<<std::endl;

      //Not necesserily all joints between the end-effector and base are controlled, therefore the columns in the jacobian corresponding to these joints must be masked to zero to avoid unwanted contributions
      for(unsigned int i=0; i<robot_state->getNumJoints(); i++)
	if (!robot_state->isQNrWritable(i)) 
	  kin_q.ee_J_.setColumn(i,KDL::Twist::Zero());

      return 0;
    }
    //==================================================================================
    int TaskAvoidCollisionsSDF::primitiveForwardKinematics(std::vector<KinematicQuantities>& kin_q_list, const std::shared_ptr<geometric_primitives::GeometricPrimitive>& primitive, RobotStatePtr const robot_state )const
    {

      kin_q_list.clear();
      std::string type=primitive->getType();

      if(strcmp(type.c_str(),"point")==0)
	{
	  std::shared_ptr<GeometricPoint> point(std::static_pointer_cast<GeometricPoint>(primitive)); // downcast the primitive to the actual derived type
	  KDL::Vector coord=point->getPointKDL();
	  KinematicQuantities kin_q;
	  kin_q.ee_J_.resize(robot_state->getNumJoints());
	  kin_q.frame_id_=point->getFrameId();
	  if(forwardKinematics(kin_q,robot_state) < 0)
	    {
	      printHiqpWarning("TaskAvoidCollisionsSDF::primitiveForwardKinematics, primitive forward kinematics for GeometricPoint primitive '" + point->getName() + "' failed.");
	      return -2;
	    }  

	  //shift the Jacobian reference point
	  kin_q.ee_J_.changeRefPoint(kin_q.ee_frame_.M*coord);
	  //compute the ee position in the base frame
	  kin_q.ee_p_=kin_q.ee_frame_.p+kin_q.ee_frame_.M*coord;

	  kin_q_list.push_back(kin_q);

	  //DEBUG =========================================
	  // std::cerr<<"Point coordinates: "<<coord<<std::endl;
	  // std::cerr<<"frame id: "<<kin_q.frame_id_<<std::endl;
	  // std::cerr<<"After ref change - J: "<<std::endl<<kin_q.ee_J_.data<<std::endl;
	  // std::cerr<<"After ref change - ee: "<<kin_q.ee_pose_.p<<std::endl;
	  //DEBUG END =========================================

	}
      else if(strcmp(type.c_str(),"sphere")==0)
	{
	  std::shared_ptr<GeometricSphere> sphere(std::static_pointer_cast<GeometricSphere>(primitive)); // downcast the primitive to the actual derived type
	  KDL::Vector coord(sphere->getX(), sphere->getY(), sphere->getZ());
	  KinematicQuantities kin_q;
	  kin_q.ee_J_.resize(robot_state->kdl_jnt_array_vel_.q.rows());
	  kin_q.frame_id_=sphere->getFrameId();
	  if(forwardKinematics(kin_q,robot_state) < 0)
	    {
	      printHiqpWarning("TaskAvoidCollisionsSDF::primitiveForwardKinematics, primitive forward kinematics for GeometricSphere primitive '" + sphere->getName() + "' failed.");
	      return -2;
	    }   
	  //shift the Jacobian reference point
	  kin_q.ee_J_.changeRefPoint(kin_q.ee_frame_.M*coord);
	  //compute the ee position in the base frame
	  kin_q.ee_p_=kin_q.ee_frame_.p+kin_q.ee_frame_.M*coord;

	  kin_q_list.push_back(kin_q);
	}
      else{
	printHiqpWarning("TaskAvoidCollisionsSDF::forwardKinematics, FK for primitive type '" + type + "' not implemented yet.");
	return -2;
      }

      return 0;
    }
    //==================================================================================
    void TaskAvoidCollisionsSDF::publishGradientVisualization(const SamplesVector& gradients, const SamplesVector& test_pts)
    {
      assert(gradients.size()==test_pts.size());
      grad_markers_.markers.clear();
      for(unsigned int i=0;i<gradients.size();i++)
	{
	  if(!collision_checker_->isValid(gradients[i]))
	    continue;

          Eigen::Vector3d grad = gradients[i];
          Eigen::Vector3d test_pt = test_pts[i];
	  visualization_msgs::Marker  g_marker;
          g_marker.ns = "gradients";
	  g_marker.header.frame_id = root_frame_id_;
	  g_marker.header.stamp = ros::Time::now();
	  g_marker.type = visualization_msgs::Marker::ARROW;
	  g_marker.action = visualization_msgs::Marker::ADD;
          g_marker.lifetime = ros::Duration(0);
	  g_marker.id = grad_markers_.markers.size();

	  geometry_msgs::Point start, end;
	  //grad = 10*grad;
	  start.x = test_pt(0);
	  start.y = test_pt(1);
	  start.z = test_pt(2);
	  end.x = test_pt(0) + grad(0);
	  end.y = test_pt(1) + grad(1);
	  end.z = test_pt(2) + grad(2);
	  g_marker.points.push_back(start);
	  g_marker.points.push_back(end);

	  g_marker.scale.x = 0.003;
	  g_marker.scale.y = 0.005;
	  g_marker.scale.z = 0.005;
	  g_marker.color.r = 1.0;
	  g_marker.color.g = 0.0;
	  g_marker.color.b = 1.0;
	  g_marker.color.a = 1.0;
	  grad_markers_.markers.push_back(g_marker);
	}
      //publish
     grad_vis_pub_.publish(grad_markers_);
    }
    //==================================================================================
  } // namespace tasks

} // namespace hiqp
