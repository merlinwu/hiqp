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

#ifndef HIQP_GEOMETRIC_BOX_H
#define HIQP_GEOMETRIC_BOX_H

#include <hiqp/geometric_primitives/geometric_primitive.h>
#include <hiqp/utilities.h>

#include <kdl/frames.hpp>

#include <Eigen/Dense>

namespace hiqp
{
namespace geometric_primitives
{

  /*! \brief Parameters:<br />
   *         [c.x, c.y, c.z, dim.x, dim.y, dim.z] <br />
   *         [c.x, c.y, c.z, dim.x, dim.y, dim.z, angle.x, angle.y, angle.z] <br />
   *         [c.x, c.y, c.z, dim.x, dim.y, dim.z, q.w, q.x, q.y, q.z] <br />
   *  \author Marcus A Johansson */
  class GeometricBox : public GeometricPrimitive {
  public:
    GeometricBox(const std::string& name,
                 const std::string& frame_id,
                 bool visible,
                 const std::vector<double>& color)
     : GeometricPrimitive(name, frame_id, visible, color) {}

    ~GeometricBox() noexcept = default;

    /*! \brief Parses a set of parameters and initializes the box.
     *
     *  \param parameters : Should be of size 6, 9, or 10. <ol>
     *                      <li>Indices 0-2 (required) defines the position of the center of the box.</li>
     *                      <li>Indices 3-5 (required) defines the dimensions of the box.</li>
     *                      <li>Indices 6-8 (optional) defines euler angles of the orientation of the box (xyz).</li>
     *                      <li>Indices 6-9 (optional) defines a quaternion for the orientation of the box.</li>
     *                      </ol>
     * \return 0 on success, -1 if the wrong number of parameters was sent */
    int init(const std::vector<double>& parameters) {
      int size = parameters.size();
      if (size != 6 && size != 9 && size != 10) {
        printHiqpWarning("GeometricBox requires 6, 9 or 10 parameters, got " 
          + std::to_string(size) + "! Initialization failed!");
        return -1;
      }

      kdl_c_(0) = parameters.at(0);
      kdl_c_(1) = parameters.at(1);
      kdl_c_(2) = parameters.at(2);

      kdl_dim_(0) = parameters.at(3);
      kdl_dim_(1) = parameters.at(4);
      kdl_dim_(2) = parameters.at(5);

      scaling_matrix_eigen_ << 1/kdl_dim_(0), 0, 0,
                               0, 1/kdl_dim_(1), 0,
                               0, 0, 1/kdl_dim_(2);

      scaling_matrix_kdl_ = KDL::Rotation(1/kdl_dim_(0), 0, 0,
                                          0, 1/kdl_dim_(1), 0,
                                          0, 0, 1/kdl_dim_(2));

      // inversion of KDL::Rotate does not work the same as inverting a matrix
      scaling_matrix_inverted_kdl_ = 
        KDL::Rotation(kdl_dim_(0), 0, 0,
                      0, kdl_dim_(1), 0,
                      0, 0, kdl_dim_(2));

      if (size == 9) {
        double angle1 = parameters.at(6);
        double angle2 = parameters.at(7);
        double angle3 = parameters.at(8);

        Eigen::Matrix3d m;
        m = Eigen::AngleAxisd(angle1, Eigen::Vector3d::UnitX())
            * Eigen::AngleAxisd(angle2, Eigen::Vector3d::UnitY())
            * Eigen::AngleAxisd(angle3, Eigen::Vector3d::UnitZ());
        q_ = Eigen::Quaternion<double>(m);

      } else if (size == 10) {
        double w = parameters.at(6);
        double x = parameters.at(7);
        double y = parameters.at(8);
        double z = parameters.at(9);
        q_ = Eigen::Quaternion<double>(w, x, y, z);

      } else {
        q_ = Eigen::Quaternion<double>(1.0, 0.0, 0.0, 0.0);
      }

      eigen_c_ << kdl_c_(0), kdl_c_(1), kdl_c_(2);
      eigen_dim_ << kdl_dim_(0), kdl_dim_(1), kdl_dim_(2);
      rotation_kdl_ = KDL::Rotation::Quaternion(q_.x(), q_.y(), q_.z(), q_.w());

      return 0;
    }

    inline const KDL::Vector&     getCenterKDL() { return kdl_c_; }

    inline const Eigen::Vector3d& getCenterEigen() { return eigen_c_; }

    inline const KDL::Vector&     getDimensionsKDL() { return kdl_dim_; }

    inline const Eigen::Vector3d& getDimensionsEigen() { return eigen_dim_; }

    /// \brief Returns the scaling matrix of the box
    inline const KDL::Rotation&   getScalingKDL() { return scaling_matrix_kdl_; }

    /// \brief Returns the inverted scaling matrix of the box
    inline const KDL::Rotation&   getScalingInvertedKDL() { return scaling_matrix_inverted_kdl_; }

    inline const Eigen::Matrix3d& getScalingEigen() { return scaling_matrix_eigen_; }

    /// \brief Returns the rotation matrix that transforms from global world coordinates to local box coordinates
    inline const KDL::Rotation&   getRotationKDL() { return rotation_kdl_; }

    /// \brief Returns the quaternion that transforms from global world coordinates to local box coordinates
    inline const Eigen::Quaternion<double>& getQuaternionEigen() { return q_; }

    /// \brief Gets the quaternion components that transform from global world coordinates to local box coordinates
    inline void getQuaternion(double& w, double& x, double& y, double& z)
    { w = q_.w(); x = q_.x(); y = q_.y(); z = q_.z(); }

    inline double getCenterX() { return kdl_c_(0); }

    inline double getCenterY() { return kdl_c_(1); }

    inline double getCenterZ() { return kdl_c_(2); }

    inline double getDimX() { return kdl_dim_(0); }

    inline double getDimY() { return kdl_dim_(1); }

    inline double getDimZ() { return kdl_dim_(2); }

  protected:
    KDL::Vector                kdl_c_; // the geometrical center of the box
    Eigen::Vector3d            eigen_c_;

    KDL::Vector                kdl_dim_; // the dimensions of the box
    Eigen::Vector3d            eigen_dim_;

    KDL::Rotation              rotation_kdl_;
    Eigen::Quaternion<double>  q_;

    KDL::Rotation              scaling_matrix_kdl_;
    KDL::Rotation              scaling_matrix_inverted_kdl_;
    Eigen::Matrix3d            scaling_matrix_eigen_;

  private:
    GeometricBox(const GeometricBox& other) = delete;
    GeometricBox(GeometricBox&& other) = delete;
    GeometricBox& operator=(const GeometricBox& other) = delete;
    GeometricBox& operator=(GeometricBox&& other) noexcept = delete;
  };

} // namespace geometric_primitives

} // namespace hiqp

#endif // include guard