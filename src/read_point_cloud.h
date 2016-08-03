#include <assert.h>
#include <indigo.h>
#include <math.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <pcl/common/time.h>
#include <pcl/common/transforms.h>
#include <pcl/io/pcd_io.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include "ros/ros.h"
#include "nav_msgs/Odometry.h"
#include "sensor_msgs/PointCloud2.h"
#include "std_msgs/Header.h"
#include "tf/LinearMath/Matrix3x3.h"
#include "tf/LinearMath/Quaternion.h"
#include <ctime>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>
#include <sensor_msgs/Image.h>
#include <image_transport/image_transport.h>

std::pair<OccamDevice *, OccamDeviceList *> initialize();
void disposeOccamAPI(std::pair<OccamDevice *, OccamDeviceList *> occamAPI);
void getStitchedAndPointCloud(OccamDevice *device,
                              pcl::PointCloud<pcl::PointXYZRGBA>::Ptr pc,
                              cv::Mat *&cvImage);
void saveImage(OccamImage *image, std::string fileName);
void saveImage(cv::Mat *image, std::string fileName);
void **captureRgbAndDisparity(OccamDevice *device);
