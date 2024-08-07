#include "python/InterfaceBindings.hpp"
#include "python/dd/DDSimDebugBindings.hpp"

#include <pybind11/pybind11.h>

PYBIND11_MODULE(pydebug, m) {
  bindDiagnostics(m);
  bindFramework(m);
  bindBackend(m);
}
