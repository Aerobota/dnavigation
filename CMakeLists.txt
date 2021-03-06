cmake_minimum_required(VERSION 2.8.3)
project(dnavigation)

## Find catkin macros and libraries
find_package(catkin REQUIRED COMPONENTS 
 rospy roscpp  roslib diagnostic_msgs message_generation actionlib geometry_msgs move_base_msgs nav_msgs std_msgs tf urg_node sensor_msgs joy cv_bridge image_transport sound_play)
find_package(PCL 1.3 REQUIRED COMPONENTS common io system )

## add demu
include_directories(/usr/local/include ${catkin_INCLUDE_DIRS} ${PCL_INCLUDE_DIRS} )
link_directories(/usr/local/lib ${PCL_LIBRARY_DIRS})
add_definitions(${PCL_DEFINITIONS})

catkin_package(
  INCLUDE_DIRS
  LIBRARIES dnavigation
  CATKIN_DEPENDS roscpp rospy std_msgs  roslib geometry_msgs 
  DEPENDS system_lib urg_node sensor_msgs roscpp 
)

###########
## Build ##
###########

add_executable(dnavigation src/dnavigation.cpp)
target_link_libraries(dnavigation ${catkin_LIBRARIES} ${PCL_COMMON_LIBRARIES} ${PCL_IO_LIBRARIES})
add_executable(follow src/follow.cpp)
target_link_libraries(follow ${catkin_LIBRARIES} ${PCL_COMMON_LIBRARIES} ${PCL_IO_LIBRARIES})

#############
## Install ##
#############

## Mark executable scripts (Python etc.) for installation
#install(PROGRAMS
#  scripts/move
#  DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
#)

## Mark executables and/or libraries for installation
install(TARGETS dnavigation
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

install(TARGETS follow
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

## Mark all other useful stuff for installation
install(DIRECTORY launch
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
)
