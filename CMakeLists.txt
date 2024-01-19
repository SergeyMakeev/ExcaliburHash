cmake_minimum_required(VERSION 2.8.12)

# set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

# if (MSVC)
#  set(CMAKE_VS_GLOBALS "EnableASAN=true")
# endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(PROJ_NAME ExcaliburHashTest)
project(${PROJ_NAME})

set (CMAKE_CXX_STANDARD 17)

set(TEST_SOURCES 
  ExcaliburHashTest01.cpp
  ExcaliburHashTest02.cpp
  ExcaliburHashTest03.cpp
  ExcaliburHashTest04.cpp
)

set (TEST_EXE_NAME ${PROJ_NAME})
add_executable(${TEST_EXE_NAME} ${TEST_SOURCES})

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory("${PROJECT_SOURCE_DIR}/extern/googletest" "extern/googletest")
target_link_libraries(${TEST_EXE_NAME} gtest_main)

if(MSVC)
  target_compile_options(${TEST_EXE_NAME} PRIVATE /W4 /WX)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)

#  target_compile_options(${TEST_EXE_NAME} PRIVATE "$<$<CONFIG:Release>:/Zi>")
#  target_link_options(${TEST_EXE_NAME} PRIVATE "$<$<CONFIG:Release>:/DEBUG>")
#  target_link_options(${TEST_EXE_NAME} PRIVATE "$<$<CONFIG:Release>:/OPT:REF>")
#  target_link_options(${TEST_EXE_NAME} PRIVATE "$<$<CONFIG:Release>:/OPT:ICF>")

else()
  target_compile_options(${TEST_EXE_NAME} PRIVATE -Wall -Wextra -pedantic -Werror)
endif()

# add sm_hash_map
add_subdirectory("${PROJECT_SOURCE_DIR}/ExcaliburHash")
target_link_libraries(${TEST_EXE_NAME} ExcaliburHash)
