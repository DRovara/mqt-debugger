#include "backend/dd/DDSimDebug.hpp"
#include "frontend/cli/CliFrontEnd.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main() {
  std::ifstream file("../../app/code/test"
                     ".qasm");
  if (!file.is_open()) {
    std::cerr << "Could not open file\n";
    file.close();
    return 1;
  }
  const std::string code((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

  DDSimulationState state;
  createDDSimulationState(&state);

  file.close();

  CliFrontEnd cli;
  cli.run(code.c_str(), &state.interface);

  destroyDDSimulationState(&state);

  return 0;
}
