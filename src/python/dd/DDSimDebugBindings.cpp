#include "python/dd/DDSimDebugBindings.hpp"

#include "backend/dd/DDSimDebug.hpp"
#include "backend/debug.h"
#include "pybind11/pybind11.h"

void bindBackend(pybind11::module& m) {

  m.def("create_ddsim_simulation_state", []() {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto* state = new DDSimulationState();
    createDDSimulationState(state);
    return &state->interface;
  });

  m.def("destroy_ddsim_simulation_state", [](SimulationState* state) {
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
    destroyDDSimulationState(reinterpret_cast<DDSimulationState*>(state));
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
  });
}
