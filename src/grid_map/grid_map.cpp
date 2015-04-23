#include <vigir_terrain_classifier/grid_map/grid_map.h>

namespace vigir_terrain_classifier
{
GridMap::GridMap(const std::string& frame_id, double resolution, double min_expansion_size)
{
  grid_map.reset(new nav_msgs::OccupancyGrid());

  grid_map->header.frame_id = vigir_footstep_planning::strip_const(frame_id, '/');
  grid_map->header.seq = 0;

  grid_map->info.resolution = resolution;
  grid_map->info.width  = 0;
  grid_map->info.height = 0;
  grid_map->info.origin.position.x = 0.0;
  grid_map->info.origin.position.y = 0.0;
  grid_map->info.origin.position.z = 0.0;

  grid_map->data.clear();

  // expansion size should be a multiple of resolution to prevent shift of data
  this->min_expansion_size = pceil(min_expansion_size, resolution);

  clear();
}

GridMap::GridMap(const nav_msgs::OccupancyGrid& map, double min_expansion_size)
{
  grid_map.reset(new nav_msgs::OccupancyGrid());

  // expansion size should be a multiple of resolution to prevent shift of data
  this->min_expansion_size = pceil(min_expansion_size, grid_map->info.resolution);

  clear();

  fromMsg(map);
}

GridMap::~GridMap()
{
}

void GridMap::clear()
{
  grid_map->data.clear();
  grid_map->header.seq = 0;

  min.x = min.y = min.z = std::numeric_limits<geometry_msgs::Vector3::_x_type>::max();
  max.x = max.y = max.z = std::numeric_limits<geometry_msgs::Vector3::_x_type>::min();
}

bool GridMap::empty() const
{
  return grid_map->data.empty();
}

int8_t& GridMap::at(int idx)
{
  return grid_map->data.at(idx);
}

void GridMap::fromMsg(const nav_msgs::OccupancyGrid& map)
{
  *grid_map = map;
  min.x = grid_map->info.origin.position.x;
  min.y = grid_map->info.origin.position.y;
  min.z = grid_map->info.origin.position.z;
}

void GridMap::toMsg(nav_msgs::OccupancyGrid& map) const
{
  map = *grid_map;
}

const nav_msgs::OccupancyGrid::Ptr& GridMap::map() const
{
  return grid_map;
}

nav_msgs::OccupancyGrid::Ptr& GridMap::map()
{
  return grid_map;
}

const geometry_msgs::Vector3& GridMap::getMin() const
{
  return min;
}

const geometry_msgs::Vector3& GridMap::getMax() const
{
  return max;
}

void GridMap::resize(const pcl::PointCloud<pcl::PointXYZ>::ConstPtr& cloud)
{
  if (cloud->empty())
    return;

  geometry_msgs::Vector3 update_min;
  geometry_msgs::Vector3 update_max;
  getPointCloudBoundary(cloud, update_min, update_max);

  resize(update_min, update_max);
}

void GridMap::resize(const geometry_msgs::Vector3& min, const geometry_msgs::Vector3& max)
{
  // check if resize is needed (enlargement only)
  if (min.x >= this->min.x && min.y >= this->min.y && max.x <= this->max.x && max.y <= this->max.y)
    return;

  // init map
  double res = grid_map->info.resolution;
  if (grid_map->data.empty())
  {
    this->min.x = pfloor(min.x, res);
    this->min.y = pfloor(min.y, res);
    this->max.x = pceil(max.x, res);
    this->max.y = pceil(max.y, res);
  }
  else // update boundary in x and y, ensure new width and height are multiple of resolution
  {
    if (this->min.x > min.x)
      this->min.x -= std::max(pceil(this->min.x-min.x, res), min_expansion_size);
    if (this->min.y > min.y)
      this->min.y -= std::max(pceil(this->min.y-min.y, res), min_expansion_size);
    if (this->max.x < max.x)
      this->max.x += std::max(pceil(max.x-this->max.x, res), min_expansion_size);
    if (this->max.y < max.y)
      this->max.y += std::max(pceil(max.y-this->max.y, res), min_expansion_size);
  }

  // save pointer to old map (double buffering)
  nav_msgs::OccupancyGrid::Ptr old_grid_map = grid_map;

  // setting up new grid map
  grid_map.reset(new nav_msgs::OccupancyGrid());

  // init header
  grid_map->header = old_grid_map->header;

  // init info
  grid_map->info.resolution = old_grid_map->info.resolution;
  // add +1 to size as we use round for mapping world to map
  grid_map->info.width  = static_cast<unsigned int>(ceil((this->max.x-this->min.x) / res)) + 1;
  grid_map->info.height = static_cast<unsigned int>(ceil((this->max.y-this->min.y) / res)) + 1;
  grid_map->info.origin.position.x = this->min.x;
  grid_map->info.origin.position.y = this->min.y;
  grid_map->info.origin.position.z = old_grid_map->info.origin.position.z;

  // reorganize data
  grid_map->data.resize(grid_map->info.width * grid_map->info.height, std::numeric_limits<int8_t>::min());

  int old_idx = 0;
  int new_idx = 0;
  getGridMapIndex(*grid_map, old_grid_map->info.origin.position.x, old_grid_map->info.origin.position.y, new_idx);

  // copy row-wise data
  for (unsigned int row = 0; row < old_grid_map->info.height; row++)
  {
    memcpy(&(grid_map->data[new_idx]), &(old_grid_map->data[old_idx]), old_grid_map->info.width);

    old_idx += old_grid_map->info.width;
    new_idx += grid_map->info.width;
  }
}

bool GridMap::getGridMapCoords(const nav_msgs::OccupancyGrid& map, double x, double y, int& map_x, int& map_y)
{
  map_x = round((x-map.info.origin.position.x) / map.info.resolution);
  map_y = round((y-map.info.origin.position.y) / map.info.resolution);

  if (map_x < 0 || map.info.width  <= static_cast<unsigned int>(map_x) ||
      map_y < 0 || map.info.height <= static_cast<unsigned int>(map_y))
  {
    ROS_ERROR("getGridMapCoords(x,y) failed");
    ROS_ERROR("Map Size: %d x %d", map.info.width, map.info.height);
    ROS_ERROR("x: (%f - %f) / %f = %d", x, map.info.origin.position.x, map.info.resolution, map_x);
    ROS_ERROR("y: (%f - %f) / %f = %d", y, map.info.origin.position.y, map.info.resolution, map_y);
    return false;
  }

  return true;
}

bool GridMap::getGridMapCoords(const nav_msgs::OccupancyGrid& map, int idx, int& map_x, int& map_y)
{
  map_x = idx % map.info.width;
  map_y = idx / map.info.width;

  if (map_x < 0 || map.info.width  <= static_cast<unsigned int>(map_x) ||
      map_y < 0 || map.info.height <= static_cast<unsigned int>(map_y))
  {
    ROS_ERROR("getGridMapCoords(idx) failed");
    return false;
  }

  return true;
}

bool GridMap::getGridMapIndex(const nav_msgs::OccupancyGrid& map, double x, double y, int& idx)
{
  int map_x, map_y;

  if (!getGridMapCoords(map, x, y, map_x, map_y))
    return false;
  else
    return getGridMapIndex(map, map_x, map_y, idx);
}

bool GridMap::getGridMapIndex(const nav_msgs::OccupancyGrid& map, int map_x, int map_y, int& idx)
{
  if (map_x < 0 || map.info.width  <= static_cast<unsigned int>(map_x) ||
      map_y < 0 || map.info.height <= static_cast<unsigned int>(map_y))
  {
    ROS_ERROR("getGridMapIndex(int) failed");
    return false;
  }

  idx = map_x + map_y * map.info.width;
  return true;
}

bool GridMap::getGridMapWorldCoords(const nav_msgs::OccupancyGrid& map, int map_x, int map_y, double& x, double& y)
{
  x = static_cast<double>(map_x) * map.info.resolution + map.info.origin.position.x;
  y = static_cast<double>(map_y) * map.info.resolution + map.info.origin.position.y;
  return true;
}

bool GridMap::getGridMapWorldCoords(const nav_msgs::OccupancyGrid& map, int idx, double& x, double& y)
{
  int map_x, map_y;

  if (!getGridMapCoords(map, idx, map_x, map_y))
    return false;
  else
    return getGridMapWorldCoords(map, map_x, map_y, x, y);
}

void GridMap::getPointCloudBoundary(const pcl::PointCloud<pcl::PointXYZ>::ConstPtr& cloud, geometry_msgs::Vector3& min, geometry_msgs::Vector3& max) const
{
  min.x = min.y = min.z = std::numeric_limits<geometry_msgs::Vector3::_x_type>::max();
  max.x = max.y = max.z = std::numeric_limits<geometry_msgs::Vector3::_x_type>::min();

  for (pcl::PointCloud<pcl::PointXYZ>::const_iterator itr = cloud->begin(); itr != cloud->end(); itr++)
  {
    const pcl::PointXYZ& p = *itr;
    min.x = std::min(min.x, static_cast<double>(p.x));
    min.y = std::min(min.y, static_cast<double>(p.y));
    min.z = std::min(min.z, static_cast<double>(p.z));
    max.x = std::max(max.x, static_cast<double>(p.x));
    max.y = std::max(max.y, static_cast<double>(p.y));
    max.z = std::max(max.z, static_cast<double>(p.z));
  }
}
}