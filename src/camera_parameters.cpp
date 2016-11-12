#include <image_undistort/camera_parameters.h>

BaseCameraParameters::BaseCameraParameters(
    const ros::NodeHandle& nh, const std::string& camera_namespace) {
  ROS_INFO("Loading camera parameters");

  XmlRpc::XmlRpcValue K_in;
  bool K_loaded = false;
  if (nh.getParam(camera_namespace + "/K", K_in)) {
    xmlRpcToMatrix(K_in, &K_);
    K_loaded = true;
  }

  std::vector<double> intrinsics_in;
  if (nh.getParam(camera_namespace + "/intrinsics", intrinsics_in)) {
    if (K_loaded) {
      ROS_WARN(
          "Both K and intrinsics vector given, ignoring intrinsics "
          "vector");
    } else if (intrinsics_in.size() != 4) {
      throw std::runtime_error(
          "Intrinsics vector must have exactly 4 values (Fx,Fy,Cx,Cy)");
    }

    K_ = Eigen::Matrix3d::Identity();
    K_(0, 0) = intrinsics_in[0];
    K_(1, 1) = intrinsics_in[1];
    K_(0, 2) = intrinsics_in[2];
    K_(1, 2) = intrinsics_in[3];
  } else if (!K_loaded) {
    throw std::runtime_error("Could not find K or camera intrinsics vector");
  }

  std::vector<double> resolution_in;
  if (nh.getParam(camera_namespace + "/resolution", resolution_in)) {
    if (resolution_in.size() != 2) {
      throw std::runtime_error("Resolution must have exactly 2 values (x,y)");
    }
    resolution_.width = resolution_in[0];
    resolution_.height = resolution_in[1];
  } else {
    throw std::runtime_error("Could not find camera resolution");
  }

  XmlRpc::XmlRpcValue T_in;
  bool T_loaded = false;
  if (nh.getParam(camera_namespace + "/T_cn_cnm1", T_in) ||
      nh.getParam(camera_namespace + "/T", T_in)) {
    xmlRpcToMatrix(T_in, &T_);
    T_loaded = true;
  } else {
    T_ = Eigen::Matrix4d::Identity();
  }

  XmlRpc::XmlRpcValue P_in;
  if (nh.getParam(camera_namespace + "/P", P_in)) {
    xmlRpcToMatrix(P_in, &P_);

    if (!T_loaded) {
      T_.topLeftCorner<3, 3>() = K_.inverse() * P_.topLeftCorner<3, 3>();
      T_.topRightCorner<3, 1>() = K_.inverse() * P_.topRightCorner<3, 1>();
      T_(3, 3) = 1;
    } else if (!P_.isApprox(
                   (Eigen::Matrix<double, 3, 4>() << K_, 0, 0, 0).finished() *
                   T_)) {
      throw std::runtime_error("For given K, T and P ([K,[0;0;0]]*T != P)");
    }
  } else {
    P_ = (Eigen::Matrix<double, 3, 4>() << K_, 0, 0, 0).finished() * T_;
  }
}

BaseCameraParameters::BaseCameraParameters(
    const sensor_msgs::CameraInfo& camera_info) {
  resolution_.height = camera_info.height;
  resolution_.width = camera_info.width;

  K_ = Eigen::Map<const Eigen::Matrix<double, 3, 3, Eigen::RowMajor>>(
      camera_info.K.data());

  T_.topLeftCorner<3, 3>() =
      Eigen::Map<const Eigen::Matrix<double, 3, 3, Eigen::RowMajor>>(
          camera_info.R.data());
  T_(3, 3) = 1;

  P_ = Eigen::Map<const Eigen::Matrix<double, 3, 4, Eigen::RowMajor>>(
      camera_info.P.data());

  T_.topRightCorner<3, 1>() = K_.inverse() * P_.topRightCorner<3, 1>();

  if (!P.topLeftCorner<3, 3>().isApprox(K * T_.topLeftCorner<3, 3>())) {
    throw std::runtime_error("For given K, T and P ([K,[0;0;0]]*T != P)");
  }
}

BaseCameraParameters::BaseCameraParameters(const cv::Size& resolution,
                                           const Eigen::Matrix<double, 4, 4>& T,
                                           const Eigen::Matrix<double, 3, 4>& K)
    : resolution_(resolution),
      T_(T),
      P((Eigen::Matrix<double, 3, 4>() << K, 0, 0, 0).finished() * T),
      K_(K) {}

const cv::Size& BaseCameraParameters::resolution() const {return resolution_};

const Eigen::Matrix<double, 4, 4>& BaseCameraParameters::T() const {
  return T_;
}
const Eigen::Matrix<double, 3, 3>& BaseCameraParameters::R() const {
  return T_.topLeftCorner<3, 3>();
}
const Eigen::Matrix<double, 3, 1>& BaseCameraParameters::p() const {
  return T_.topRightCorner<3, 1>();
}

const Eigen::Matrix<double, 3, 4>& BaseCameraParameters::P() const {
  return P_;
};
const Eigen::Matrix<double, 3, 3>& BaseCameraParameters::K() const {return K_};

bool BaseCameraParameters::operator==(const BaseCameraParameters& B) const {
  return (resolution() == B.resolution()) && (T() == B.T()) && (P() == B.P()) &&
         (K() == B.K());
}

InputCameraParameters::InputCameraParameters(
    const ros::NodeHandle& nh, const std::string& camera_namespace)
    : BaseCameraParameters(nh, camera_namespace) {
  std::string distortion_model_in;
  if (!nh.getParam(camera_namespace + "/distortion_model",
                   distortion_model_in)) {
    ROS_WARN("No distortion model given, assuming radtan");
    radtan_distortion_ = true;
  } else {
    radtan_distortion_ = is_radtan_distortion(distortion_model_in);
  }

  if (!nh.getParam(camera_namespace + "/distortion_coeffs", D_)) {
    ROS_WARN(
        "No distortion coefficients found, assuming images are "
        "undistorted");
    D_ = std::vector<double>(0, 5);
  }

  // ensure D always has at least 5 elements
  while (D_.size() < 5) {
    D_.push_back(0);
  }
}

InputCameraParameters::InputCameraParameters(
    const sensor_msgs::CameraInfo& camera_info)
    : BaseCameraParameters(camera_info),
      D_(D),
      radtan_distortion_(is_radtan_distortion(camera_info->distortion_model)) {
  // ensure D always has at least 5 elements
  while (D_.size() < 5) {
    D_.push_back(0);
  }
}

InputCameraParameters::InputCameraParameters(
    const cv::Size& resolution, const Eigen::Matrix<double, 4, 4>& T,
    const Eigen::Matrix<double, 3, 4>& K, const std::Vector<double>& D,
    const bool R_loadedradtan_distortion)
    : BaseCameraParameters(resolution_, T, K), D_(D) {
  // ensure D always has at least 5 elements
  while (D_.size() < 5) {
    D_.push_back(0);
  }
}

const std::Vector<double>& InputCameraParameters::D() const {return D_};
const bool& InputCameraParameters::usingRadtanDistortion(){
    return radtan_distortion_};

static bool InputCameraParameters::is_radtan_distortion(
    const std::string& distortion_model) {
  std::string lower_case_distortion_model = distortion_model;

  std::transform(lower_case_distortion_model.begin(),
                 lower_case_distortion_model.end(),
                 lower_case_distortion_model.begin(), ::tolower);
  if ((lower_case_distortion_model == std::string("plumb bob")) ||
      (lower_case_distortion_model == std::string("radtan"))) {
    return true;
  } else if (distortion_model_ == std::string("equidistant")) {
    return false;
  } else {
    throw std::runtime_error(
        "Unrecognized distortion model. Valid options are 'radtan', 'Plumb "
        "Bob' and 'equidistant'");
  }
}

bool InputCameraParameters::operator==(const IntputCameraParameters& B) const {
  return (dynamic_cast<BaseCameraParameters>(*this) ==
          dynamic_cast<BaseCameraParameters>(B)) &&
         (D() == B.D()) && (distortion_model == B.distortion_model);
}

// holds the camera parameters of the input camera and virtual output camera
CameraParametersPair::CameraParametersPair(const bool undistort)
    : undistort_(undistort){};

bool CameraParametersPair::setCameraParameters(
    const ros::NodeHandle& nh, const std::string& camera_namespace,
    bool updating_input_camera) {
  try {
    if (updating_input_camera) {
      input_ = std::shared_ptr<InputCameraParameters>(nh, camera_namespace);
    } else {
      output_ = std::shared_ptr<OutputCameraParameters>(nh, camera_namespace);
    }
    return true;
  } catch (std::runtime_error e) {
    ROS_ERROR(e.what());
    return false;
  }
}

bool CameraParametersPair::setCameraParameters(
    const sensor_msgs::CameraInfo& camera_info, bool updating_input_camera) {
  try {
    if (updating_input_camera) {
      input_ = std::shared_ptr<InputCameraParameters>(camera_info);
    } else {
      output_ = std::shared_ptr<OutputCameraParameters>(camera_info);
    }
    return true;
  } catch (std::runtime_error e) {
    ROS_ERROR(e.what());
    return false;
  }
}

bool CameraParametersPair::setInputCameraParameters(
    const cv::Size& resolution, const Eigen::Matrix<double, 4, 4>& T,
    const Eigen::Matrix<double, 3, 4>& K, const std::Vector<double>& D,
    const bool radtan_distortion) {
  try {
    input_ = std::shared_ptr<InputCameraParameters>(resolution, T, K, D,
                                                    radtan_distortion);
    return true;
  } catch (std::runtime_error e) {
    ROS_ERROR(e.what());
    return false;
  }
}

bool CameraParametersPair::setOutputCameraParameters(
    const cv::Size& resolution, const Eigen::Matrix<double, 4, 4>& T,
    const Eigen::Matrix<double, 3, 4>& K) {
  try {
    output_ = std::shared_ptr<OutputCameraParameters>(resolution, T, K);
    return true;
  } catch (std::runtime_error e) {
    ROS_ERROR(e.what());
    return false;
  }
}

void CameraParametersPair::generateOutputCameraInfoMessage(
    sensor_msgs::CameraInfo* camera_info) const {
  if (!valid()) {
    throw std::runtime_error(
        "Attempted to get output camera_info before a valid input and output "
        "has been set");
  } else {
    camera_info->height = output_.resolution().height;
    camera_info->width = output_.resolution().width;

    Eigen::Map<Eigen::Matrix<double, 3, 3, Eigen::RowMajor>>(
        camera_info->K.data()) = output_.K();

    Eigen::Map<Eigen::Matrix<double, 3, 3, Eigen::RowMajor>>(
        camera_info->R.data()) = output_.R();

    Eigen::Map<Eigen::Matrix<double, 3, 4, Eigen::RowMajor>>(
        camera_info->P.data()) = output_.P();

    if (undistort_) {
      for (double& d : camera_info->D) {
        d = 0;
      }
    } else {
      camera_info->D = input_.D();
    }
    if (input_.usingRadtanDistortion()) {
      camera_info->distortion_model = "radtan"
    } else {
      camera_info->distortion_model = "equidistant";
    }
  }
}

const bool& CameraParametersPair::undistort() const {return undistort_};

const Eigen::Matrix<double, 3, 4>& CameraParametersPair::P_input() {
  if (input_ == nullptr) {
    throw std::runtime_error(
        "Attempted to get projection matrix of input before a valid input "
        "had "
        "been set");
  } else {
    return input_.P();
  }
}

const Eigen::Matrix<double, 3, 4>& CameraParametersPair::P_output() {
  if (input_ == nullptr) {
    throw std::runtime_error(
        "Attempted to get projection matrix of output before a valid output "
        "had been set");
  } else {
    return input_.P();
  }
}

const std::shared_ptr<InputCameraParameters>& CameraParametersPair::getInput(){
    return input};
const std::shared_ptr<OutputCameraParameters>& CameraParametersPair::getOutput()
    const {return output};

bool CameraParametersPair::valid() const {
  return (input_ != nullptr) && (output_ != nullptr);
}

bool CameraParametersPair::valid(const bool check_input_camera) const {
  if (check_input_camera) {
    return input_ != nullptr;
  } else {
    return output_ != nullptr;
  }
}

bool operator==(const CameraParametersPair& B) const {
  return getInput() == B.getInput() && (getOutput() == getOutput());
}

bool StereoCameraParameters::setInputCameraParameters(
    const ros::NodeHandle& nh, const std::string& camera_namespace,
    bool updating_left_camera) {
  try {
    if (updating_left_camera) {
      left_.setInputCameraParameters(nh, camera_namespace);
    } else {
      right_.setInputCameraParameters(nh, camera_namespace);
    }
    return true;
  } catch (std::runtime_error e) {
    ROS_ERROR(e.what());
    return false;
  }
}

bool StereoCameraParameters::setInputCameraParameters(
    const sensor_msgs::CameraInfo& camera_info, bool updating_left_camera) {
  try {
    if (updating_left_camera) {
      left_.setInputCameraParameters(camera_info);
    } else {
      right_.setInputCameraParameters(camera_info);
    }
    return true;
  } catch (std::runtime_error e) {
    ROS_ERROR(e.what());
    return false;
  }
}

bool StereoCameraParameters::setInputCameraParameters(
    const cv::Size& resolution, const Eigen::Matrix<double, 4, 4>& T,
    const Eigen::Matrix<double, 3, 4>& P, const std::Vector<double>& D,
    const bool radtan_distortion, const bool updating_left_camera) {
  try {
    if (updating_left_camera) {
      left_.setInputCameraParameters(resolution, T, P, D, radtan_distortion,
                                     updating_left_camera);
    } else {
      right_.setInputCameraParameters(resolution, T, P, D, radtan_distortion,
                                      updating_left_camera);
    }
    return true;
  } catch (std::runtime_error e) {
    ROS_ERROR(e.what());
    return false;
  }
}

void StereoCameraParameters::generateOutputCameraInfoMessage(
    const bool get_left_camera_info,
    sensor_msgs::CameraInfo* camera_info) const {
  if (get_left_camera_info) {
    left_->generateCameraInfoMessage(camera_info);
  } else {
    right_->generateCameraInfoMessage(camera_info);
  }
}

bool StereoCameraParameters::valid() const {
  return input_.valid() && output_.valid();
}
bool StereoCameraParameters::valid(const bool check_left,
                                   const bool check_input) const {
  if (check_left) {
    return left_.valid(check_input);
  } else {
    return right_.valid(check_input);
  }
}
