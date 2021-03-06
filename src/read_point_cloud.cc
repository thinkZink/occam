#include "read_point_cloud.h"

using namespace cv;

void handleError(int returnCode) {
  if (returnCode != OCCAM_API_SUCCESS) {
    char errorMsg[30];
    occamGetErrorString((OccamError)returnCode, errorMsg, 30);
    fprintf(stderr, "Occam API Error: %d. %s\n", returnCode, errorMsg);
    abort();
  }
}

int convertToPcl(OccamPointCloud *occamPointCloud, PointCloudT::Ptr pclPointCloud) {
  int numPointsConverted = 0;
  int i;
  for (i = 0; i < 3 * occamPointCloud->point_count; i += 3) {
    PointT *point = new PointT();

    point->r = occamPointCloud->rgb[i];
    point->g = occamPointCloud->rgb[i + 1];
    point->b = occamPointCloud->rgb[i + 2];
    point->a = 255;

    point->x = occamPointCloud->xyz[i];
    point->y = occamPointCloud->xyz[i + 1];
    point->z = occamPointCloud->xyz[i + 2];

    // printf("R: %d G: %d B: %d X: %f Y: %f Z: %f\n", point->r, point->g,
    // point->b, point->x, point->y, point->z);

    double CULL_THRESHOLD = 1000;
    double inf = std::numeric_limits<double>::infinity();
    if (point->x < inf && point->y < inf && point->z < inf) {
      double sqdist =
          point->x * point->x + point->y * point->y + point->z * point->z;
      if (sqrt(sqdist) < CULL_THRESHOLD) {
        pclPointCloud->push_back(*point);
        numPointsConverted++;
      }
    }
  }

  pclPointCloud->is_dense = false;

  return numPointsConverted;
}

void savePointCloud(PointCloudT::Ptr point_cloud,
                    int counter) {
  double timestamp = pcl::getTime();
  std::stringstream ss;
  ss << "data/pointcloud" << counter << ".pcd";
  std::string name = ss.str();
  pcl::PCDWriter writer;
  if (point_cloud->size() > 0) {
    writer.write<PointT>(name, *point_cloud);
  }
}

Mat occamImageToCvMat(OccamImage *image) {
  Mat *cvImage;
  Mat colorImage;
  if (image && image->format == OCCAM_GRAY8) {
    cvImage = new Mat_<uchar>(image->height, image->width,
                                  (uchar *)image->data[0], image->step[0]);
  } else if (image && image->format == OCCAM_RGB24) {
    cvImage =
        new Mat_<Vec3b>(image->height, image->width,
                                (Vec3b *)image->data[0], image->step[0]);
    cvtColor(*cvImage, colorImage, COLOR_BGR2RGB);
  } else if (image && image->format == OCCAM_SHORT1) {
    cvImage = new Mat_<short>(image->height, image->width,
                                  (short *)image->data[0], image->step[0]);
  } else {
    printf("Image type not supported: %d\n", image->format);
    cvImage = NULL;
  }
  return colorImage;
}

void saveImage(OccamImage *image, std::string fileName) {
  Mat cvImage = occamImageToCvMat(image);
  saveImage(&cvImage, fileName);
}

void saveImage(Mat *cvImage, std::string fileName) {
  imwrite(fileName, *cvImage);
}

void capturePointCloud(OccamDevice *device,
                       PointCloudT::Ptr pclPointCloud,
                       OccamDataName PCLOUD) {
  // Capture a point cloud
  OccamDataName requestTypes[] = {PCLOUD};
  OccamDataType returnTypes[] = {OCCAM_POINT_CLOUD};
  OccamPointCloud *pointCloud;
  handleError(occamDeviceReadData(device, 1, requestTypes, returnTypes,
                                  (void **)&pointCloud, 1));

  // Print statistics
  // printf("Number of points in Occam point cloud: %d\n", pointCloud->point_count);

  // Convert to PCL point cloud
  int numConverted = convertToPcl(pointCloud, pclPointCloud);
  // printf("Number of points converted to PCL: %d\n", numConverted);

  // Clean up
  handleError(occamFreePointCloud(pointCloud));
}

const int sensor_count = 5;
Eigen::Matrix4f extrisic_transforms[sensor_count];
void initSensorExtrisics(OccamDevice *device) {
  for (int i = 0; i < sensor_count; ++i) {
    // Get sensor extrisics
    double R[9];
    handleError(occamGetDeviceValuerv(
        device, OccamParam(OCCAM_SENSOR_ROTATION0 + i), R, 9));
    double T[3];
    handleError(occamGetDeviceValuerv(
        device, OccamParam(OCCAM_SENSOR_TRANSLATION0 + i), T, 3));

    // double K[9];
    // handleError(occamGetDeviceValuerv(
    //     device, OccamParam(OCCAM_SENSOR_INTRINSICS0 + i), K, 9));
    // printf("K:\n");
    // for(int a=0; a<3; a++) {
    //   for(int b=0; b<3; b++) {
    //     printf("%f,\t", K[3*a+b]);
    //   }
    //   printf("\n");
    // }
    // printf("\n");

    // Init transform
    Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();

    // Set rotation
    transform(0, 0) = R[0];
    transform(1, 0) = R[1];
    transform(2, 0) = R[2];
    transform(0, 1) = R[3];
    transform(1, 1) = R[4];
    transform(2, 1) = R[5];
    transform(0, 2) = R[6];
    transform(1, 2) = R[7];
    transform(2, 2) = R[8];

    // Set translation
    transform(0, 3) = T[0];
    transform(1, 3) = T[1];
    transform(2, 3) = T[2];

    extrisic_transforms[i] = transform;

    // printf ("Transform for sensor %d:\n", i-1);
    // std::cout << extrisic_transforms[i] << std::endl;
  }
}

Eigen::Matrix4f transform_from_pose(geometry_msgs::Pose pose) {
  tf::Quaternion q(pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w);
  // Get a rotation matrix from the quaternion
  tf::Matrix3x3 m(q);

  // Init transform
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();

  // Set rotation
  transform (0,0) = m.getRow(0)[0];
  transform (0,1) = m.getRow(0)[1];
  transform (0,2) = m.getRow(0)[2];
  transform (1,0) = m.getRow(1)[0];
  transform (1,1) = m.getRow(1)[1];
  transform (1,2) = m.getRow(1)[2];
  transform (2,0) = m.getRow(2)[0];
  transform (2,1) = m.getRow(2)[1];
  transform (2,2) = m.getRow(2)[2];

  // Set translation
  transform (0,3) = pose.position.x;
  transform (1,3) = pose.position.y;
  transform (2,3) = pose.position.z;

  return transform;
}

Eigen::Matrix4f beam_occam_scale_transform;
Eigen::Matrix4f odom_beam_transform;
void initTransforms() {
  // scale the cloud from cm to m
  double scale = 0.01;
  Eigen::Matrix4f scale_transform = Eigen::Matrix4f::Identity();
  scale_transform (0,0) = scale;
  scale_transform (1,1) = scale;
  scale_transform (2,2) = scale;

  // use the transform from the beam robot base to the occam frame to orient the cloud
  geometry_msgs::Pose beam_occam_pose;
  // done so that z axis points up, x axis points forward, y axis points left
  beam_occam_pose.orientation.x = -0.5;
  beam_occam_pose.orientation.y = 0.5;
  beam_occam_pose.orientation.z = -0.5;
  beam_occam_pose.orientation.w = 0.5;
  // beam_occam_pose.orientation.w = 1.0;
  //beam_occam_pose.position.z = 1.658;  
  beam_occam_pose.position.z = 1.10;  
  beam_occam_pose.position.x = -0.140;  
  Eigen::Matrix4f beam_occam_transform = transform_from_pose(beam_occam_pose);
  // Eigen::Matrix4f beam_occam_transform = Eigen::Matrix4f::Identity();

  // combine the transforms
  beam_occam_scale_transform = beam_occam_transform * scale_transform;

  // if no odom recieved, assume identity transform
  odom_beam_transform = Eigen::Matrix4f::Identity();

  printf("Initialized Transforms.\n");
}

void odomCallback(const nav_msgs::Odometry::ConstPtr& msg) {
  // Update the matrix used to transform the pointcloud to the odom frame
  odom_beam_transform = transform_from_pose(msg->pose.pose);  
}

void occamCloudsToPCL(
  OccamPointCloud** pointClouds,
  PointCloudT::Ptr pclPointCloud) {
  // use latest odom data to transform the cloud with the movement of the robot
  Eigen::Matrix4f odom_occam_transform = odom_beam_transform * beam_occam_scale_transform;
  for (int i = 0; i < sensor_count; ++i) {
    // Print statistics
    // printf("Number of points in OCCAM_POINT_CLOUD%d: %d\n", i, pointClouds[i]->point_count);

    // Convert to PCL point cloud
    PointCloudT::Ptr tempCloud(new PointCloudT);
    int numConverted = convertToPcl(pointClouds[i], tempCloud);
    // printf("Number of points converted to PCL: %d\n", numConverted);

    // combine extrisic transform and then apply to the cloud
    Eigen::Matrix4f combined_transform = odom_occam_transform * extrisic_transforms[i];
    PointCloudT::Ptr transformedCloud (new PointCloudT);
    pcl::transformPointCloud (*tempCloud, *transformedCloud, combined_transform);

    // Add to large cloud
    *pclPointCloud += *transformedCloud;
  }
  // printf("Number of points in large cloud: %zu\n", pclPointCloud->size());
}

OccamPointCloud** captureAllOccamClouds(OccamDevice *device) {
  // Capture all point clouds
  OccamDataName *requestTypes = (OccamDataName *)occamAlloc(sensor_count * sizeof(OccamDataName));
  for (int i = 0; i < sensor_count; ++i) {
    requestTypes[i] = (OccamDataName)(OCCAM_POINT_CLOUD0 + i);
  }
  OccamDataType returnTypes[] = {OCCAM_POINT_CLOUD};
  OccamPointCloud** pointClouds = (OccamPointCloud**) occamAlloc(sensor_count * sizeof(OccamPointCloud*));
  handleError(occamDeviceReadData(device, sensor_count, requestTypes, 0, (void**)pointClouds, 1));
  return pointClouds;
}

void visualizePointCloud(
    PointCloudT::Ptr pclPointCloud) {
  pcl::visualization::PCLVisualizer::Ptr viewer(
      new pcl::visualization::PCLVisualizer("PCL Viewer"));

  pcl::visualization::PointCloudColorHandlerRGBField<PointT> rgb(
      pclPointCloud);
  viewer->addPointCloud<PointT>(pclPointCloud, rgb, "cloud");
  while (!viewer->wasStopped()) {
    viewer->spinOnce();
  }
}

OccamImage *captureImage(OccamDevice *device, OccamDataName requestType) {
  OccamDataName *req = (OccamDataName *)occamAlloc(sizeof(OccamDataName));
  req[0] = requestType;
  OccamImage **images = (OccamImage **)occamAlloc(sizeof(OccamImage *));
  handleError(occamDeviceReadData(device, 1, req, 0, (void **)images, 1));
  occamFree(req);
  return images[0];
}

void constructPointCloud(
    OccamDevice *device,
    PointCloudT::Ptr pclPointCloud) {
  // Capture RBG image and disparity image
  // OccamImage* rgbImage = captureImage(device, OCCAM_IMAGE2);
  // printf("RGB Image captured at time: %llu\n", (long long unsigned
  // int)rgbImage->time_ns);
  // OccamImage* disparityImage = captureImage(device, OCCAM_DISPARITY_IMAGE1);
  // printf("Disparity Image captured at time: %llu\n", (long long unsigned
  // int)disparityImage->time_ns);

  // Save the images
  // saveImage(rgbImage, "rgbImage.jpg");
  // saveImage(disparityImage, "disparityImage.jpg");

  // Get basic sensor information for the camera
  int sensor_count;
  handleError(occamGetDeviceValuei(device, OCCAM_SENSOR_COUNT, &sensor_count));
  int sensor_width;
  handleError(occamGetDeviceValuei(device, OCCAM_SENSOR_WIDTH, &sensor_width));
  int sensor_height;
  handleError(
      occamGetDeviceValuei(device, OCCAM_SENSOR_HEIGHT, &sensor_height));

  // Initialize sensor parameter variables
  double *Dp[sensor_count];
  double *Kp[sensor_count];
  double *Rp[sensor_count];
  double *Tp[sensor_count];

  // XXX Rp and Tp are the extrinsics for the occam
  // Get sensor parameters for all sensors
  for (int i = 0; i < sensor_count; ++i) {
    double D[5];
    handleError(occamGetDeviceValuerv(
        device, OccamParam(OCCAM_SENSOR_DISTORTION_COEFS0 + i), D, 5));
    Dp[i] = D;
    double K[9];
    handleError(occamGetDeviceValuerv(
        device, OccamParam(OCCAM_SENSOR_INTRINSICS0 + i), K, 9));
    Kp[i] = K;
    printf("K:\n");
    for(int a=0; a<3; a++) {
      for(int b=0; b<3; b++) {
        printf("%f,\t", K[3*a+b]);
      }
      printf("\n");
    }
    printf("\n");
    double R[9];
    handleError(occamGetDeviceValuerv(
        device, OccamParam(OCCAM_SENSOR_ROTATION0 + i), R, 9));
    Rp[i] = R;
    double T[3];
    handleError(occamGetDeviceValuerv(
        device, OccamParam(OCCAM_SENSOR_TRANSLATION0 + i), T, 3));
    Tp[i] = T;
  }

  // Initialize interface to the Occam Stereo Rectify module
  void *rectifyHandle = 0;
  occamConstructModule(OCCAM_MODULE_STEREO_RECTIFY, "prec", &rectifyHandle);
  assert(rectifyHandle);
  IOccamStereoRectify *rectifyIface = 0;
  occamGetInterface(rectifyHandle, IOCCAMSTEREORECTIFY, (void **)&rectifyIface);
  assert(rectifyIface);

  // Configure the module with the sensor information
  handleError(rectifyIface->configure(rectifyHandle, sensor_count, sensor_width,
                                      sensor_height, Dp, Kp, Rp, Tp, 0));

  OccamImage *images = (OccamImage *)captureRgbAndDisparity(device);
  // Get point cloud for the image
  int indices[] = {0, 1, 2, 3, 4};
  OccamImage *rgbImages[5];
  OccamImage *disparityImages[5];
  for (int i = 0; i < 5; i++) {
    rgbImages[i] = &images[i];
    disparityImages[i] = &images[i + 5];
  }
  OccamPointCloud **pointClouds =
      (OccamPointCloud **)occamAlloc(sizeof(OccamPointCloud *) * 5);
  handleError(rectifyIface->generateCloud(
      rectifyHandle, 1, indices, 1, rgbImages, disparityImages, pointClouds));

  /* // method signature
     virtual int generateCloud(int N,const int* indices,int transform,
                          const OccamImage* const* img0,const OccamImage* const*
     disp0,
                          OccamPointCloud** cloud1) = 0;
   */

  // Print statistics
  // for (int i = 0; i < 5; i++) {
  //   printf("Number of points in Occam point cloud: %d\n", pointClouds[i]->point_count);
  // }

  /*

  // Convert to PCL point cloud
  int numConverted = convertToPcl(pointCloud, pclPointCloud);
  printf("Number of points converted to PCL: %d\n", numConverted);
  */

  // Clean up
  for (int i = 0; i < 5; i++) {
    handleError(occamFreePointCloud(pointClouds[i]));
  }
}

void **captureStitchedAndPointCloud(OccamDevice *device) {
  OccamDataName *req = (OccamDataName *)occamAlloc(6 * sizeof(OccamDataName));
  req[0] = OCCAM_STITCHED_IMAGE0;
  req[1] = OCCAM_POINT_CLOUD0;
  req[2] = OCCAM_POINT_CLOUD1;
  req[3] = OCCAM_POINT_CLOUD2;
  req[4] = OCCAM_POINT_CLOUD3;
  req[5] = OCCAM_POINT_CLOUD4;
  OccamDataType returnTypes[] = {OCCAM_IMAGE, OCCAM_POINT_CLOUD};
  void **data = (void **)occamAlloc(sizeof(void *) * 6);
  handleError(occamDeviceReadData(device, 6, req, returnTypes, data, 1));
  return data;
}

void **captureRgbAndDisparity(OccamDevice *device) {
  int num_images = 10;
  OccamDataName *req =
      (OccamDataName *)occamAlloc(num_images * sizeof(OccamDataName));
  req[0] = OCCAM_IMAGE0;
  req[1] = OCCAM_IMAGE1;
  req[2] = OCCAM_IMAGE2;
  req[3] = OCCAM_IMAGE3;
  req[4] = OCCAM_IMAGE4;
  req[5] = OCCAM_DISPARITY_IMAGE0;
  req[6] = OCCAM_DISPARITY_IMAGE1;
  req[7] = OCCAM_DISPARITY_IMAGE2;
  req[8] = OCCAM_DISPARITY_IMAGE3;
  req[9] = OCCAM_DISPARITY_IMAGE4;
  OccamDataType returnTypes[] = {OCCAM_IMAGE};
  // void** data = occamAlloc(sizeof(OccamImage*) + sizeof(OccamPointCloud*));
  void **data = (void **)occamAlloc(sizeof(void *) * num_images);
  handleError(
      occamDeviceReadData(device, num_images, req, returnTypes, data, 1));
  return data;
}

std::pair<OccamDevice *, OccamDeviceList *> initializeOccamAPI() {

  // Initialize Occam SDK
  handleError(occamInitialize());

  // Find all connected Occam devices
  OccamDeviceList *deviceList;
  handleError(occamEnumerateDeviceList(2000, &deviceList));
  int i;
  for (i = 0; i < deviceList->entry_count; ++i) {
    printf("%d. Device identifier: %s\n", i, deviceList->entries[i].cid);
  }

  // Connect to first device
  OccamDevice *device;
  char *cid = deviceList->entries[0].cid;
  handleError(occamOpenDevice(cid, &device));
  printf("Opened device: %p\n", device);
  return std::make_pair(device, deviceList);
}

void disposeOccamAPI(std::pair<OccamDevice *, OccamDeviceList *> occamAPI) {
  // Clean up
  handleError(occamCloseDevice(occamAPI.first));
  handleError(occamFreeDeviceList(occamAPI.second));
  handleError(occamShutdown());
}

Mat getStitchedAndPointCloud(OccamDevice *device,
                              PointCloudT::Ptr pclPointCloud) {
  void **data = captureStitchedAndPointCloud(device);
  OccamImage *image = (OccamImage *)data[0];
  Mat i = occamImageToCvMat(image);
  handleError(occamFreeImage(image));

  // use latest odom data to transform the cloud with the movement of the robot
  Eigen::Matrix4f odom_occam_transform = odom_beam_transform * beam_occam_scale_transform;
  for (int i = 0; i < sensor_count; ++i) {
    OccamPointCloud *occamCloud = (OccamPointCloud *)data[i+1];
    // Print statistics
    // printf("Number of points in OCCAM_POINT_CLOUD%d: %d\n", i, occamCloud->point_count);

    // Convert to PCL point cloud
    PointCloudT::Ptr tempCloud(new PointCloudT);
    int numConverted = convertToPcl(occamCloud, tempCloud);
    // printf("Number of points converted to PCL: %d\n", numConverted);

    // Downsample the pointcloud
    // pcl::VoxelGrid<PointT> vgf;
    // vgf.setInputCloud (tempCloud);
    // float leaf_size = 0.01f;
    // vgf.setLeafSize (leaf_size, leaf_size, leaf_size);
    // vgf.filter (*tempCloud);

    // combine extrisic transform and then apply to the cloud
    Eigen::Matrix4f combined_transform = odom_occam_transform * extrisic_transforms[i];
    PointCloudT::Ptr transformedCloud (new PointCloudT);
    pcl::transformPointCloud (*tempCloud, *transformedCloud, combined_transform);
    
    // Add to large cloud
    *pclPointCloud += *transformedCloud;
  }
  for (int i = 0; i < sensor_count; ++i) {
    handleError(occamFreePointCloud((OccamPointCloud *)data[i+1]));
  }
  // printf("Number of points in large cloud: %zu\n", pclPointCloud->size());
  return i;
}

int main(int argc, char **argv) {

  // Init ROS node
  ros::init(argc, argv, "beam_occam");
  ros::NodeHandle n;
  printf("Initialized ROS node.\n");

  // Subscribe to odometry data
  ros::Subscriber odom_sub = n.subscribe("/beam/odom", 1, odomCallback);
  // PointCloud2 publisher
  ros::Publisher pc2_pub = n.advertise<sensor_msgs::PointCloud2>("/occam/points", 1);
  ros::Publisher pc2_and_stitched_pub = n.advertise<beam_joy::PointcloudAndImage>("/occam/points_and_stitched", 1);

  // image_transport::ImageTransport it(n);
  // image_transport::Publisher pub = it.advertise("camera/image", 1);
  ros::Publisher stitched_pub = n.advertise<sensor_msgs::Image>("/occam/stitched", 1);

  std::pair<OccamDevice *, OccamDeviceList *> occamAPI = initializeOccamAPI();
  OccamDevice *device = occamAPI.first;
  OccamDeviceList *deviceList = occamAPI.second;  

  // Enable auto exposure and gain **important**
  occamSetDeviceValuei(device, OCCAM_AUTO_EXPOSURE, 1);
  occamSetDeviceValuei(device, OCCAM_AUTO_GAIN, 1);

  PointCloudT::Ptr cloud(new PointCloudT);
  PointCloudT::Ptr cloud_filtered(new PointCloudT);
  // initialize global constants
  initSensorExtrisics(device);
  initTransforms();
  
  Mat cvImage;
  int counter = 0;
  while (ros::ok()) {
    (*cloud).clear();

    ros::Time capture_time = ros::Time::now();
    cvImage = getStitchedAndPointCloud(device, cloud);

    printf("Cloud size before filtering: %zu\n", cloud->size());
    clock_t start;

    // Crop out the floor and ceiling
    Eigen::Vector4f minP, maxP;
    float inf = std::numeric_limits<float>::infinity();
    minP[0] = -inf; minP[1] = -inf; minP[2] = 0;
    maxP[0] = inf; maxP[1] = inf; maxP[2] = 1.4; 
    pcl::CropBox<PointT> cropFilter;
    cropFilter.setInputCloud (cloud);
    cropFilter.setMin(minP);
    cropFilter.setMax(maxP);
    cropFilter.filter (*cloud); 


    // start = clock();
    // Downsample the pointcloud
    pcl::VoxelGrid<PointT> vgf;
    vgf.setInputCloud (cloud);
    float leaf_size = 0.015f;
    vgf.setLeafSize (leaf_size, leaf_size, leaf_size);
    vgf.filter (*cloud);
    // cout << (( clock() - start ) / (double) CLOCKS_PER_SEC) << " ################" << endl;

    // Remove the ground using the given plane coefficients 
    float plane_dist_thresh = 0.05;
    Eigen::Vector4f gc;   
    gc[0] = 0.0;
    gc[1] = 0.0;
    gc[2] = -1.0;
    gc[3] = 0.0;
    pcl::SampleConsensusModelPlane<PointT>::Ptr dit (new pcl::SampleConsensusModelPlane<PointT> (cloud));
    std::vector<int> ground_inliers;
    dit->selectWithinDistance (gc, plane_dist_thresh, ground_inliers);
    pcl::PointIndices::Ptr ground_ptr (new pcl::PointIndices);
    ground_ptr->indices = ground_inliers;   
    pcl::ExtractIndices<PointT> extract;
    extract.setInputCloud (cloud);  
    extract.setIndices (ground_ptr);  
    extract.setNegative (true);
    extract.filter (*cloud);
        
    // Remove other planes such as walls
    //ransacRemoveMultiple (cloud, plane_dist_thresh, 100, 500); 

    // Remove outliers to make point cloud cleaner
    // start = clock();
    // int outlier_num_points = 50;
    // float outlier_std_dev = 1.0;
    // pcl::StatisticalOutlierRemoval<PointT> sor;
    // sor.setInputCloud (cloud);
    // sor.setMeanK (outlier_num_points);
    // sor.setStddevMulThresh (outlier_std_dev);
    // sor.filter (*cloud);
    // cout << (( clock() - start ) / (double) CLOCKS_PER_SEC) << " $$$$$$$$$$$$$$$$" << endl;

    // pcl::RadiusOutlierRemoval<PointT> outrem;
    // outrem.setInputCloud(cloud);
    // outrem.setRadiusSearch(0.8);
    // outrem.setMinNeighborsInRadius (2);
    // outrem.filter (*cloud);
    
    // Remove NAN from cloud
    std::vector<int> index;
    pcl::removeNaNFromPointCloud(*cloud, *cloud, index);

    printf("Cloud size after filtering: %zu\n", cloud->size());

    // Convert cvImage to ROS Image msg
    sensor_msgs::ImagePtr img_msg = cv_bridge::CvImage(std_msgs::Header(), "bgr8", cvImage).toImageMsg();
    img_msg->header.frame_id = "occam_optical_link";
    img_msg->header.stamp = capture_time;

    // Convert PCL pointcloud to ROS PointCloud2 msg
    sensor_msgs::PointCloud2 pc2;
    pcl::PCLPointCloud2 tmp_cloud;
    pcl::toPCLPointCloud2(*cloud, tmp_cloud);
    // pcl::toPCLPointCloud2(*cloud_filtered, tmp_cloud);
    pcl_conversions::fromPCL(tmp_cloud, pc2);
    pc2.header.frame_id = "odom";
    pc2.header.stamp = capture_time;

    // Init PointcloudAndImage msg
    beam_joy::PointcloudAndImage pc_img_msg;
    pc_img_msg.pc = pc2;
    pc_img_msg.img = *img_msg;

    // Publish all msgs
    // stitched_pub.publish(img_msg);
    // pc2_pub.publish(pc2);
    pc2_and_stitched_pub.publish(pc_img_msg);

    ros::spinOnce();

    ++counter;
  }
  disposeOccamAPI(occamAPI);

  return 0;
}
