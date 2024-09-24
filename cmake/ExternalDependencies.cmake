include(FetchContent)
set(FETCH_PACKAGES "")

if(BUILD_MQT_DEBUGGER_BINDINGS)
  if(NOT SKBUILD)
    # Manually detect the installed pybind11 package.
    execute_process(
      COMMAND "${Python_EXECUTABLE}" -m pybind11 --cmakedir
      OUTPUT_STRIP_TRAILING_WHITESPACE
      OUTPUT_VARIABLE pybind11_DIR)

    # Add the detected directory to the CMake prefix path.
    list(APPEND CMAKE_PREFIX_PATH "${pybind11_DIR}")
  endif()

  # add pybind11 library
  find_package(pybind11 2.13 CONFIG REQUIRED)
endif()

# ---------------------------------------------------------------------------------Fetch MQT Core
# cmake-format: off
set(MQT_CORE_VERSION 2.6.0
        CACHE STRING "MQT Core version")
set(MQT_CORE_REV "28dc65e0ea04bba1fb70676a290b96709d675d07"
        CACHE STRING "MQT Core identifier (tag, branch or commit hash)")
set(MQT_CORE_REPO_OWNER "cda-tum"
        CACHE STRING "MQT Core repository owner (change when using a fork)")
# cmake-format: on
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
  # Fetch MQT Core
  FetchContent_Declare(
    mqt-core
    GIT_REPOSITORY https://github.com/${MQT_CORE_REPO_OWNER}/mqt-core.git
    GIT_TAG ${MQT_CORE_REV})
  list(APPEND FETCH_PACKAGES mqt-core)
else()
  find_package(mqt-core ${MQT_CORE_VERSION} QUIET)
  if(NOT mqt-core_FOUND)
    FetchContent_Declare(
      mqt-core
      GIT_REPOSITORY https://github.com/${MQT_CORE_REPO_OWNER}/mqt-core.git
      GIT_TAG ${MQT_CORE_REV})
    list(APPEND FETCH_PACKAGES mqt-core)
  endif()
endif()

# ---------------------------------------------------------------------------------Fetch Eigen3
# cmake-format: off
set(EIGEN_VERSION 3.4.0
        CACHE STRING "Eigen3 version")
# cmake-format: on
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
  # Fetch Eigen3
  FetchContent_Declare(
    Eigen3
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG ${EIGEN_VERSION}
    GIT_SHALLOW TRUE)
  list(APPEND FETCH_PACKAGES Eigen3)
  set(EIGEN_BUILD_TESTING
      OFF
      CACHE BOOL "Disable testing for Eigen")
  set(BUILD_TESTING
      OFF
      CACHE BOOL "Disable general testing")
  set(EIGEN_BUILD_DOC
      OFF
      CACHE BOOL "Disable documentation build for Eigen")
else()
  find_package(Eigen3 ${EIGEN3_VERSION} REQUIRED NO_MODULE)
  if(NOT Eigen3_FOUND)
    FetchContent_Declare(
      Eigen3
      GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
      GIT_TAG ${EIGEN3_VERSION}
      GIT_SHALLOW TRUE)
    list(APPEND FETCH_PACKAGES Eigen3)
    set(EIGEN_BUILD_TESTING
        OFF
        CACHE BOOL "Disable testing for Eigen")
    set(BUILD_TESTING
        OFF
        CACHE BOOL "Disable general testing")
    set(EIGEN_BUILD_DOC
        OFF
        CACHE BOOL "Disable documentation build for Eigen")
  endif()
endif()

if(BUILD_MQT_DEBUGGER_TESTS)
  set(gtest_force_shared_crt
      ON
      CACHE BOOL "" FORCE)
  set(GTEST_VERSION
      1.14.0
      CACHE STRING "Google Test version")
  set(GTEST_URL https://github.com/google/googletest/archive/refs/tags/v${GTEST_VERSION}.tar.gz)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    FetchContent_Declare(googletest URL ${GTEST_URL} FIND_PACKAGE_ARGS ${GTEST_VERSION} NAMES GTest)
    list(APPEND FETCH_PACKAGES googletest)
  else()
    find_package(googletest ${GTEST_VERSION} QUIET NAMES GTest)
    if(NOT googletest_FOUND)
      FetchContent_Declare(googletest URL ${GTEST_URL})
      list(APPEND FETCH_PACKAGES googletest)
    endif()
  endif()
endif()

if(BUILD_MQT_DEBUGGER_BINDINGS)
  # add pybind11_json library
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    FetchContent_Declare(
      pybind11_json
      GIT_REPOSITORY https://github.com/pybind/pybind11_json
      FIND_PACKAGE_ARGS)
    list(APPEND FETCH_PACKAGES pybind11_json)
  else()
    find_package(pybind11_json QUIET)
    if(NOT pybind11_json_FOUND)
      FetchContent_Declare(pybind11_json GIT_REPOSITORY https://github.com/pybind/pybind11_json)
      list(APPEND FETCH_PACKAGES pybind11_json)
    endif()
  endif()
endif()

# Make all declared dependencies available.
FetchContent_MakeAvailable(${FETCH_PACKAGES})
