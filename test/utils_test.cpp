/**
 * @file utils_test.cpp
 * @brief Implementation of utility functions for testing.
 *
 * This file implements several utility functions that are used in the test
 * files.
 */

#include "utils/utils_test.hpp"

#include "common.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

bool complexEquality(const Complex& c, double real, double imaginary) {
  const double epsilon = 0.001;
  if (real - c.real > epsilon || c.real - real > epsilon) {
    return false;
  }
  if (imaginary - c.imaginary > epsilon || c.imaginary - imaginary > epsilon) {
    return false;
  }
  return true;
}

bool classicalEquals(const Variable& v, bool value) {
  return v.type == VarBool && v.value.boolValue == value;
}

std::string readFromCircuitsPath(const std::string& testName) {
  const std::filesystem::path localPath =
      std::filesystem::path("circuits") / (testName + std::string(".qasm"));
  std::ifstream file(localPath);
  if (!file.is_open()) {
    file = std::ifstream(std::filesystem::path("../../test/circuits") /
                         (testName + std::string(".qasm")));
    if (!file.is_open()) {
      std::cerr << "Could not open file\n";
      file.close();
      return "";
    }
  }

  const std::string code((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  file.close();
  return code;
}
