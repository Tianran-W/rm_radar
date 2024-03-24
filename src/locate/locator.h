#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/point_types.h>
#include <pcl/segmentation/extract_clusters.h>

#include <functional>
#include <opencv2/opencv.hpp>
#include <unordered_map>
#include <utility>

#include "robot/robot.h"

namespace radar {

struct PairHash {
    template <typename T1, typename T2>
    std::size_t operator()(const std::pair<T1, T2>& p) const {
        std::size_t h1 = std::hash<T1>{}(p.first);
        std::size_t h2 = std::hash<T2>{}(p.second);
        return h1 ^ h2;
    }
};

class Locator {
   public:
    Locator() = delete;

    Locator(int image_width, int image_height, const cv::Matx33f& intrinsic,
            const cv::Matx44f& lidar_to_camera,
            const cv::Matx44f& world_to_camera, float min_depth_diff = 500,
            float max_depth_diff = 4000, float tolerance = 200.f,
            float min_cluster_point_num = 10,
            float max_cluster_point_num = 5000, float max_distance = 29300);

    void update(pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_ptr) noexcept;

    void cluster() noexcept;

    void search(std::vector<Robot>& robot) noexcept;

   private:
    cv::Point3f cameraToLidar(const cv::Point3f& point) const noexcept;
    cv::Point3f lidarToWorld(const cv::Point3f& point) const noexcept;
    cv::Point3f lidarToCamera(const cv::Point3f& point) const noexcept;
    void search(Robot& robot) noexcept;
    int image_width_, image_height_;
    cv::Matx33f intrinsic_, intrinsic_inv_;
    cv::Matx44f lidar_to_camera_transform_;
    cv::Matx31f camera_to_lidar_translate_;
    cv::Matx33f camera_to_lidar_rotate_;
    cv::Matx44f camera_to_world_transform_;
    float min_depth_diff_, max_depth_diff_;
    float max_distance_;
    cv::Mat depth_image_, background_depth_image_, diff_depth_image_;
    pcl::PointCloud<pcl::PointXYZ>::Ptr diff_cloud_{nullptr};
    pcl::search::KdTree<pcl::PointXYZ>::Ptr search_tree_{nullptr};
    pcl::EuclideanClusterExtraction<pcl::PointXYZ> extraction_;
    std::vector<pcl::PointIndices> cluster_indices_;
    std::unordered_map<std::pair<int, int>, int, PairHash> pixel_index_map_;
    std::unordered_map<int, int> index_cluster_map_;
};

}  // namespace radar