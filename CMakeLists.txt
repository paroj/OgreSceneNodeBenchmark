cmake_minimum_required (VERSION 2.8)
project(BenchmarkOgre)

## [discover_ogre]
# specify which version you need
find_package(OGRE REQUIRED CONFIG)
find_package(SDL2 REQUIRED)

# the search paths
include_directories(${OGRE_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS}  include/)
link_directories(${OGRE_LIBRARY_DIRS})

add_definitions(-std=c++11)

# copy essential config files next to our binary where OGRE autodiscovers them
file(COPY ${OGRE_CONFIG_DIR}/plugins.cfg DESTINATION ${CMAKE_BINARY_DIR})

#file(COPY ${OGRE_CONFIG_DIR}/resources.cfg DESTINATION ${CMAKE_BINARY_DIR})
#file(APPEND ${CMAKE_BINARY_DIR}/resources.cfg  "[General]\nFileSystem=.\n")
## [discover_ogre]

add_executable(BenchmarkOgre main.cpp OgreApplicationContext.cpp OgreSGTechniqueResolverListener.cpp)
target_link_libraries(BenchmarkOgre ${OGRE_LIBRARIES} ${SDL2_LIBRARIES})