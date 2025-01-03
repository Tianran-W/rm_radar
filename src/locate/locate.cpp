/**
 * @file locate.cpp
 * @author zmsbruce (zmsbruce@163.com)
 * @brief The file implements the `Locator` class, which performs robot
 * localization by processing point cloud data. It integrates depth images,
 * performs clustering, and searches for robots within the analyzed data using
 * sensor fusion techniques.
 * @date 2024-03-27
 *
 * @copyright (c) 2024 HITCRT
 * All rights reserved.
 *
 */

#include <algorithm>
#include <cmath>
#include <execution>
#include <iostream>
#include <map>
#include <opencv2/opencv.hpp>
#include <ranges>

#include "locator.h"

namespace radar {

/**
 * @brief Converts a point from the Lidar coordinate system to the world
 * coordinate system.
 *
 * This function takes a point in the Lidar coordinate system and applies the
 * appropriate transformations to convert it to the world coordinate system.
 *
 * @param point The point in the Lidar coordinate system to be converted.
 * @return The corresponding point in the world coordinate system.
 */
cv::Point3f Locator::lidarToWorld(const cv::Point3f& point) const noexcept {
    cv::Matx41f lidar_coor{point.x, point.y, point.z, 1.0f};
    cv::Matx41f world_coor =
        camera_to_world_transform_ * lidar_to_camera_transform_ * lidar_coor;
    return cv::Point3f(world_coor(0), world_coor(1), world_coor(2));
}

/**
 * @brief Converts a point from the camera coordinate system to the Lidar
 * coordinate system.
 *
 * This function takes a point in the camera coordinate system and applies the
 * necessary transformations to convert it to the Lidar coordinate system.
 *
 * @param point The point in the camera coordinate system to be converted.
 * @return The corresponding point in the Lidar coordinate system.
 */
cv::Point3f Locator::cameraToLidar(const cv::Point3f& point) const noexcept {
    cv::Matx31f camera_coor{point.x / zoom_factor_, point.y / zoom_factor_,
                            1.0f};
    cv::Matx31f lidar_coor =
        camera_to_lidar_rotate_ *
        (intrinsic_inv_ * point.z * camera_coor + camera_to_lidar_translate_);
    return cv::Point3f(lidar_coor(0), lidar_coor(1), lidar_coor(2));
}

/**
 * @brief Converts a point from the Lidar coordinate system to the camera
 * coordinate system.
 *
 * This function takes a point in the Lidar coordinate system and applies the
 * necessary transformations to convert it to the camera coordinate system.
 *
 * @param point The point in the Lidar coordinate system to be converted.
 * @return The corresponding point in the camera coordinate system.
 */
cv::Point3f Locator::lidarToCamera(const cv::Point3f& point) const noexcept {
    cv::Matx41f lidar_coor{point.x, point.y, point.z, 1.0f};
    cv::Matx31f camera_coor =
        intrinsic_ *
        (lidar_to_camera_transform_ * lidar_coor).get_minor<3, 1>(0, 0);
    return cv::Point3f(camera_coor(0) * zoom_factor_ / camera_coor(2),
                       camera_coor(1) * zoom_factor_ / camera_coor(2),
                       camera_coor(2));
}

/**
 * @brief Constructs a Locator object with the specified parameters.
 *
 * This constructor initializes a Locator object with the given parameters,
 * including image dimensions, camera intrinsic matrix, Lidar-to-camera
 * transformation matrix, world-to-camera transformation matrix, zoom factor,
 * scale factor, queue size, depth difference thresholds, cluster tolerance,
 * minimum and maximum cluster sizes, and maximum distance.
 *
 * @param image_width The width of the image.
 * @param image_height The height of the image.
 * @param intrinsic The camera intrinsic matrix.
 * @param lidar_to_camera The transformation matrix from Lidar to camera
 * coordinates.
 * @param world_to_camera The transformation matrix from world to camera
 * coordinates.
 * @param zoom_factor The zoom factor to apply to the image, which is used to
 * shrink the size of depth image in order to accelerate speed of processing.
 * @param queue_size The size of the queue of depth images.
 * @param min_depth_diff The minimum depth difference for point cloud
 * differencing.
 * @param max_depth_diff The maximum depth difference for point cloud
 * differencing.
 * @param cluster_tolerance The cluster tolerance for point cloud clustering.
 * @param min_cluster_size The minimum cluster size for point cloud clustering.
 * @param max_cluster_size The maximum cluster size for point cloud clustering.
 * @param max_distance The maximum distance threshold of x-coordinate for point
 * cloud processing.
 */
Locator::Locator(int image_width, int image_height,
                 const cv::Matx33f& intrinsic,
                 const cv::Matx44f& lidar_to_camera,
                 const cv::Matx44f& world_to_camera, float zoom_factor,
                 size_t queue_size, float min_depth_diff, float max_depth_diff,
                 float cluster_tolerance, int min_cluster_size,
                 int max_cluster_size, float max_distance)
    : image_width_{image_width},
      image_height_{image_height},
      zoom_factor_{zoom_factor},
      image_width_zoomed_{static_cast<int>(image_width * zoom_factor)},
      image_height_zoomed_{static_cast<int>(image_height * zoom_factor)},
      queue_size_{queue_size},
      intrinsic_{intrinsic},
      lidar_to_camera_transform_{lidar_to_camera},
      min_depth_diff_{min_depth_diff},
      max_depth_diff_{max_depth_diff},
      max_distance_{max_distance},
      kdtree_{new pcl::search::KdTree<pcl::PointXYZ>()},
      cloud_foreground_{new pcl::PointCloud<pcl::PointXYZ>()} {
    intrinsic_inv_ = intrinsic.inv();
    cv::Matx44f camera_to_lidar = lidar_to_camera.inv();
    camera_to_lidar_rotate_ = camera_to_lidar.get_minor<3, 3>(0, 0);
    camera_to_lidar_translate_ = camera_to_lidar.get_minor<3, 1>(0, 3);
    camera_to_world_transform_ = world_to_camera.inv();
    depth_image_.create(image_height_zoomed_, image_width_zoomed_, CV_32F);
    background_depth_image_.create(image_height_zoomed_, image_width_zoomed_,
                                   CV_32F);
    diff_depth_image_.create(image_height_zoomed_, image_width_zoomed_, CV_32F);

    cluster_extractor_.setClusterTolerance(cluster_tolerance);
    cluster_extractor_.setMinClusterSize(min_cluster_size);
    cluster_extractor_.setMaxClusterSize(max_cluster_size);
    cluster_extractor_.setSearchMethod(kdtree_);
}

/**
 * @brief Updates the Locator with a new point cloud.
 *
 * This method updates the Locator with a new point cloud. It processes the
 * input point cloud to generate depth images, perform differencing with the
 * background depth image, and updates the depth image of difference between the
 * background image and current images stored in a queue.
 *
 * @param cloud The input point cloud.
 */
void Locator::update(
    const pcl::PointCloud<pcl::PointXYZ>::Ptr& cloud) noexcept {
    depth_image_.setTo(0);
    diff_depth_image_.setTo(0);

    if (!cloud) {
        std::cerr << "cloud is null." << std::endl;
        return;
    }

    if (cloud->empty()) {
        std::cerr << "cloud is empty." << std::endl;
        return;
    }

    std::for_each(
        std::execution::par_unseq, cloud->begin(), cloud->end(),
        [this](const pcl::PointXYZ& point) {
            if (iszero(point.x) && iszero(point.y) && iszero(point.z)) {
                return;
            }
            if (point.x > max_distance_) {
                return;
            }
            auto uvd = lidarToCamera(cv::Point3f(point.x, point.y, point.z));
            float &u = uvd.x, &v = uvd.y, &d = uvd.z;
            if (u < 0 || u > image_width_zoomed_ || v < 0 ||
                v > image_height_zoomed_) {
                return;
            }
            float& background_depth = background_depth_image_.at<float>(v, u);
            if (d > background_depth) {
                background_depth = d;
            }
            depth_image_.at<float>(v, u) = d;
        });

    depth_images_.push_back(depth_image_.clone());
    if (depth_images_.size() > queue_size_) {
        depth_images_.pop_front();
    }

    std::for_each(
        std::execution::par_unseq, depth_images_.begin(), depth_images_.end(),
        [this](const cv::Mat& image) {
            for (int i = 0; i < image.rows; ++i) {
                const float* depth_row = image.ptr<float>(i);
                const float* background_row =
                    background_depth_image_.ptr<float>(i);
                float* diff_row = diff_depth_image_.ptr<float>(i);
                for (int j = 0; j < image.cols; ++j) {
                    const float value = depth_row[j];
                    if (iszero(value)) {
                        continue;
                    }
                    float diff = background_row[j] - value;
                    if (diff >= min_depth_diff_ && diff <= max_depth_diff_) {
                        diff_row[j] = value;
                    }
                }
            }
        });
}

/**
 * @brief Clusters points based on depth values from a differential depth image.
 *
 * This method iterates over the differential depth image stored in
 * `diff_depth_image_` and collects non-zero points into a vector. Each point
 * contains the column (x), the row (y), and the depth value (z) from the depth
 * image. These points are then clustered using the DBSCAN algorithm and then
 * stored in an image.
 */
void Locator::cluster() noexcept {
    point_index_map_.clear();
    index_cluster_map_.clear();
    clusters_.clear();
    cloud_foreground_->clear();

    for (int i = 0; i < diff_depth_image_.rows; ++i) {
        const float* image_row = diff_depth_image_.ptr<float>(i);
        for (int j = 0; j < diff_depth_image_.cols; ++j) {
            float value = image_row[j];
            if (iszero(value)) {
                continue;
            }
            cv::Point3f lidar_point = cameraToLidar(cv::Point3f(j, i, value));
            cloud_foreground_->emplace_back(lidar_point.x, lidar_point.y,
                                            lidar_point.z);
            point_index_map_.emplace(cv::Point2i(j, i),
                                     cloud_foreground_->size() - 1);
        }
    }

    if (cloud_foreground_->empty()) {
        return;
    }
    kdtree_->setInputCloud(cloud_foreground_);
    cluster_extractor_.setInputCloud(cloud_foreground_);
    cluster_extractor_.extract(clusters_);

    for (size_t i = 0; i < clusters_.size(); ++i) {
        for (int index : clusters_[i].indices) {
            index_cluster_map_.emplace(index, i);
        }
    }
}

/**
 * @brief Searches for the robot within the specified region of interest.
 *
 * This method searches for the robot within the specified region of interest
 * (ROI) by analyzing the differencing depth image. It identifies candidate
 * points within the ROI, associates them with clusters, and determines the
 * location of the robot based on the largest cluster of points.
 *
 * @param robot The robot object.
 */
void Locator::search(Robot& robot) const noexcept {
    if (!robot.rect().has_value()) {
        return;
    }

    std::map<int, std::vector<cv::Point3f>> candidates;

    auto rect{zoom(robot.rect().value())};
    for (int v = rect.y; v < rect.y + rect.height; ++v) {
        const float* image_row = diff_depth_image_.ptr<float>(v);
        for (int u = rect.x; u < rect.x + rect.width; ++u) {
            float depth = image_row[u];
            if (iszero(depth)) {
                continue;
            }
            int index = point_index_map_.at(cv::Point2i(u, v));
            int cluster_id = index_cluster_map_.contains(index)
                                 ? index_cluster_map_.at(index)
                                 : -1;
            candidates[cluster_id].emplace_back(
                cameraToLidar(cv::Point3f(u, v, depth)));
        }
    }

    if (candidates.empty()) {
        return;
    }
    auto& points =
        std::ranges::max_element(candidates, [](auto&& pair_a, auto&& pair_b) {
            return pair_a.second.size() < pair_b.second.size();
        })->second;
    auto location = std::accumulate(points.begin(), points.end(),
                                    cv::Point3f(0.0f, 0.0f, 0.0f)) /
                    static_cast<float>(points.size());
    robot.setLocation(lidarToWorld(location));
}

/**
 * @brief Searches for robots within the specified vector of robots.
 *
 * This method searches for robots within the specified vector of Robot objects
 * by invoking the `search` method for each individual robot in parallel. It
 * updates the location of each robot based on the analysis of the differencing
 * depth image.
 *
 * @param robots The vector of Robot objects to search for.
 */
void Locator::search(std::vector<Robot>& robots) const noexcept {
    std::for_each(std::execution::par_unseq, robots.begin(), robots.end(),
                  [this](Robot& robot) { search(robot); });
}

/**
 * @brief Applies zoom transformations to the input rectangle.
 *
 * @param rect The input rectangle to be transformed.
 * @return The transformed rectangle after applying zoom transform.
 *
 * @note The `zoom` factor is used to shrink the size of depth image in order to
 * accelerate processing.
 */
cv::Rect Locator::zoom(const cv::Rect& rect) const noexcept {
    const cv::Rect image_rect(0, 0, image_width_zoomed_, image_height_zoomed_);
    auto center_x = rect.x * zoom_factor_ + rect.width * zoom_factor_ * 0.5f;
    auto center_y = rect.y * zoom_factor_ + rect.height * zoom_factor_ * 0.5f;

    int ret_width = rect.width * zoom_factor_;
    int ret_height = rect.height * zoom_factor_;
    int ret_x = center_x - ret_width * 0.5f;
    int ret_y = center_y - ret_height * 0.5f;

    cv::Rect ret(ret_x, ret_y, ret_width, ret_height);
    ret &= image_rect;
    return ret;
}

}  // namespace radar