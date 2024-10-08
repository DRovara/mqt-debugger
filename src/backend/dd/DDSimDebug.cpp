/**
 * @file DDSimDebug.cpp
 * @brief Implementation of DDSimDebug.hpp
 */

#include "backend/dd/DDSimDebug.hpp"

#include "Definitions.hpp"
#include "backend/dd/DDSimDiagnostics.hpp"
#include "backend/debug.h"
#include "backend/diagnostics.h"
#include "circuit_optimizer/CircuitOptimizer.hpp"
#include "common.h"
#include "common/Span.hpp"
#include "common/parsing/AssertionParsing.hpp"
#include "common/parsing/CodePreprocessing.hpp"
#include "common/parsing/Utils.hpp"
#include "dd/DDDefinitions.hpp"
#include "dd/Operations.hpp"
#include "dd/Package.hpp"
#include "ir/operations/ClassicControlledOperation.hpp"
#include "ir/operations/OpType.hpp"

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <iterator>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

/**
 * @brief Cast a `SimulationState` pointer to a `DDSimulationState` pointer.
 *
 * This was defined as a function to avoid linting issues due to the frequent
 * `reinterpret_cast`s.
 * @param state The `SimulationState` pointer to cast.
 * @return The `DDSimulationState` pointer.
 */
DDSimulationState* toDDSimulationState(SimulationState* state) {
  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
  return reinterpret_cast<DDSimulationState*>(state);
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
}

/**
 * @brief Generate a random number between 0 and 1.
 *
 * This number is used for measurements.
 * @return A random number between 0 and 1.
 */
double generateRandomNumber() {
  std::random_device
      rd; // Will be used to obtain a seed for the random number engine
  std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
  std::uniform_real_distribution<> dis(0.0, 1.0);

  return dis(gen);
}

#pragma clang diagnostic push
Result createDDSimulationState(DDSimulationState* self) {
  self->interface.init = ddsimInit;

  self->interface.loadCode = ddsimLoadCode;
  self->interface.stepForward = ddsimStepForward;
  self->interface.stepBackward = ddsimStepBackward;
  self->interface.stepOverForward = ddsimStepOverForward;
  self->interface.stepOverBackward = ddsimStepOverBackward;
  self->interface.stepOutForward = ddsimStepOutForward;
  self->interface.stepOutBackward = ddsimStepOutBackward;
  self->interface.runAll = ddsimRunAll;
  self->interface.runSimulation = ddsimRunSimulation;
  self->interface.runSimulationBackward = ddsimRunSimulationBackward;
  self->interface.resetSimulation = ddsimResetSimulation;
  self->interface.pauseSimulation = ddsimPauseSimulation;
  self->interface.canStepForward = ddsimCanStepForward;
  self->interface.canStepBackward = ddsimCanStepBackward;
  self->interface.isFinished = ddsimIsFinished;
  self->interface.didAssertionFail = ddsimDidAssertionFail;
  self->interface.wasBreakpointHit = ddsimWasBreakpointHit;

  self->interface.getCurrentInstruction = ddsimGetCurrentInstruction;
  self->interface.getInstructionCount = ddsimGetInstructionCount;
  self->interface.getInstructionPosition = ddsimGetInstructionPosition;
  self->interface.getNumQubits = ddsimGetNumQubits;
  self->interface.getAmplitudeIndex = ddsimGetAmplitudeIndex;
  self->interface.getAmplitudeBitstring = ddsimGetAmplitudeBitstring;
  self->interface.getClassicalVariable = ddsimGetClassicalVariable;
  self->interface.getNumClassicalVariables = ddsimGetNumClassicalVariables;
  self->interface.getClassicalVariableName = ddsimGetClassicalVariableName;
  self->interface.getStateVectorFull = ddsimGetStateVectorFull;
  self->interface.getStateVectorSub = ddsimGetStateVectorSub;
  self->interface.getDiagnostics = ddsimGetDiagnostics;
  self->interface.setBreakpoint = ddsimSetBreakpoint;
  self->interface.clearBreakpoints = ddsimClearBreakpoints;
  self->interface.getStackDepth = ddsimGetStackDepth;
  self->interface.getStackTrace = ddsimGetStackTrace;

  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
  return self->interface.init(reinterpret_cast<SimulationState*>(self));
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
}
#pragma clang diagnostic pop

/**
 * @brief Handles all actions that need to be performed when resetting the
 * simulation state.
 *
 * This occurs when the reset method is called or when new code is loaded.
 * @param ddsim The `DDSimulationState` to reset.
 */
void resetSimulationState(DDSimulationState* ddsim) {
  if (ddsim->simulationState.p != nullptr) {
    ddsim->dd->decRef(ddsim->simulationState);
  }
  ddsim->simulationState = ddsim->dd->makeZeroState(ddsim->qc->getNqubits());
  ddsim->dd->incRef(ddsim->simulationState);
  ddsim->paused = false;
}

Result ddsimInit(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);

  ddsim->simulationState.p = nullptr;
  ddsim->qc = std::make_unique<qc::QuantumComputation>();
  ddsim->dd = std::make_unique<dd::Package<>>(1);
  ddsim->iterator = ddsim->qc->begin();
  ddsim->currentInstruction = 0;
  ddsim->previousInstructionStack.clear();
  ddsim->callReturnStack.clear();
  ddsim->callSubstitutions.clear();
  ddsim->restoreCallReturnStack.clear();
  ddsim->breakpoints.clear();
  ddsim->lastFailedAssertion = -1ULL;
  ddsim->lastMetBreakpoint = -1ULL;

  destroyDDDiagnostics(&ddsim->diagnostics);
  createDDDiagnostics(&ddsim->diagnostics, ddsim);

  resetSimulationState(ddsim);

  ddsim->ready = false;

  return OK;
}

Result ddsimLoadCode(SimulationState* self, const char* code) {
  auto* ddsim = toDDSimulationState(self);
  ddsim->currentInstruction = 0;
  ddsim->previousInstructionStack.clear();
  ddsim->callReturnStack.clear();
  ddsim->callSubstitutions.clear();
  ddsim->restoreCallReturnStack.clear();
  ddsim->code = code;
  ddsim->variables.clear();
  ddsim->variableNames.clear();

  try {
    std::stringstream ss{preprocessAssertionCode(code, ddsim)};
    ddsim->qc->import(ss, qc::Format::OpenQASM3);
    qc::CircuitOptimizer::flattenOperations(*ddsim->qc, true);
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return ERROR;
  }

  ddsim->iterator = ddsim->qc->begin();
  ddsim->dd->resize(ddsim->qc->getNqubits());
  ddsim->lastFailedAssertion = -1ULL;
  ddsim->lastMetBreakpoint = -1ULL;

  resetSimulationState(ddsim);

  ddsim->ready = true;

  return OK;
}

Result ddsimStepOverForward(SimulationState* self) {
  if (!self->canStepForward(self)) {
    return ERROR;
  }
  auto* ddsim = toDDSimulationState(self);
  if (ddsim->instructionTypes[ddsim->currentInstruction] != CALL) {
    return self->stepForward(self);
  }

  Result res = OK;
  const auto currentInstruction = ddsim->currentInstruction;
  bool done = false;
  while ((res == OK) && !done) {
    if (ddsim->paused) {
      ddsim->paused = false;
      return OK;
    }
    if (ddsim->instructionTypes[ddsim->currentInstruction] == RETURN &&
        ddsim->callReturnStack.back() == currentInstruction) {
      done = true;
    }
    res = self->stepForward(self);
    if (self->didAssertionFail(self) || self->wasBreakpointHit(self)) {
      break;
    }
  }
  return res;
}

Result ddsimStepOverBackward(SimulationState* self) {
  if (!self->canStepBackward(self)) {
    return ERROR;
  }
  auto* ddsim = toDDSimulationState(self);
  const auto prev = ddsim->previousInstructionStack.back();
  if (ddsim->instructionTypes[prev] != RETURN) {
    return self->stepBackward(self);
  }

  Result res = OK;
  const auto stackSize = ddsim->callReturnStack.size();
  while (res == OK) {
    if (ddsim->paused) {
      ddsim->paused = false;
      return OK;
    }
    res = self->stepBackward(self);
    if (ddsim->instructionTypes[ddsim->currentInstruction] == CALL &&
        ddsim->callReturnStack.size() == stackSize) {
      break;
    }
    if (self->wasBreakpointHit(self)) {
      break;
    }
  }
  return res;
}

Result ddsimStepOutForward(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  if (ddsim->callReturnStack.empty()) {
    return self->runSimulation(self);
  }

  const auto size = ddsim->callReturnStack.size();
  Result res = OK;
  while ((res = self->stepForward(self)) == OK) {
    if (self->didAssertionFail(self) || self->wasBreakpointHit(self)) {
      break;
    }
    if (ddsim->paused) {
      ddsim->paused = false;
      return OK;
    }
    if (ddsim->callReturnStack.size() == size - 1) {
      break;
    }
  }
  return res;
}

Result ddsimStepOutBackward(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  if (ddsim->callReturnStack.empty()) {
    return self->runSimulationBackward(self);
  }

  const auto size = ddsim->callReturnStack.size();
  Result res = OK;
  while ((res = self->stepBackward(self)) == OK) {
    if (self->wasBreakpointHit(self)) {
      break;
    }
    if (ddsim->paused) {
      ddsim->paused = false;
      return OK;
    }
    if (ddsim->callReturnStack.size() == size - 1) {
      break;
    }
  }
  return res;
}

Result ddsimStepForward(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  if (!self->canStepForward(self)) {
    return ERROR;
  }
  ddsim->lastMetBreakpoint = -1ULL;
  const auto currentInstruction = ddsim->currentInstruction;
  dddiagnosticsOnStepForward(&ddsim->diagnostics, currentInstruction);
  ddsim->currentInstruction = ddsim->successorInstructions[currentInstruction];

  if (ddsim->currentInstruction == 0) {
    ddsim->currentInstruction = ddsim->callReturnStack.back() + 1;
    ddsim->restoreCallReturnStack.emplace_back(ddsim->currentInstruction,
                                               ddsim->callReturnStack.back());
    ddsim->callReturnStack.pop_back();
  }

  if (ddsim->breakpoints.find(ddsim->currentInstruction) !=
      ddsim->breakpoints.end()) {
    ddsim->lastMetBreakpoint = ddsim->currentInstruction;
  }

  if (ddsim->instructionTypes[currentInstruction] == CALL) {
    ddsim->callReturnStack.push_back(currentInstruction);
  }
  ddsim->previousInstructionStack.emplace_back(currentInstruction);

  // The exact action we take depends on the type of the next instruction:
  // - ASSERTION: check the assertion and step back if it fails.
  // - Non-SIMULATE: just step to the next instruction.
  // - SIMULATE: run the corresponding operation on the DD backend.
  if (ddsim->instructionTypes[currentInstruction] == ASSERTION) {
    auto& assertion = ddsim->assertionInstructions[currentInstruction];
    try {
      const auto failed = !checkAssertion(ddsim, assertion);
      if (failed && ddsim->lastFailedAssertion != currentInstruction) {
        ddsim->lastFailedAssertion = currentInstruction;
        self->stepBackward(self);
      }
      return OK;
    } catch (const std::exception& e) {
      std::cerr << e.what() << "\n";
      return ERROR;
    }
  }

  ddsim->lastFailedAssertion = -1ULL;
  if (ddsim->instructionTypes[currentInstruction] != SIMULATE) {
    return OK;
  }

  qc::MatrixDD currDD;
  if ((*ddsim->iterator)->getType() == qc::Measure) {
    // Perform a measurement of the desired qubits, based on the amplitudes of
    // the current state.
    auto qubitsToMeasure = (*ddsim->iterator)->getTargets();
    auto classicalBits =
        dynamic_cast<qc::NonUnitaryOperation*>(ddsim->iterator->get())
            ->getClassics();
    for (size_t i = 0; i < qubitsToMeasure.size(); i++) {
      auto qubit = qubitsToMeasure[i];
      auto classicalBit = classicalBits[i];

      auto [pZero, pOne] = ddsim->dd->determineMeasurementProbabilities(
          ddsim->simulationState, static_cast<dd::Qubit>(qubit), true);
      auto rnd = generateRandomNumber();
      auto result = rnd < pZero;
      ddsim->dd->performCollapsingMeasurement(ddsim->simulationState,
                                              static_cast<dd::Qubit>(qubit),
                                              result ? pZero : pOne, result);
      auto name = getClassicalBitName(ddsim, classicalBit);
      if (ddsim->variables.find(name) != ddsim->variables.end()) {
        VariableValue value;
        value.boolValue = !result;
        ddsim->variables[name].value = value;
      } else {
        ddsim->variableNames.push_back(std::make_unique<std::string>(name));
        const Variable newVariable{ddsim->variableNames.back()->data(),
                                   VariableType::VarBool,
                                   {!result}};
        ddsim->variables.insert({name, newVariable});
      }
    }

    ddsim->iterator++;
    ddsim->previousInstructionStack
        .clear(); // after measurements, we can no longer step back.
    ddsim->restoreCallReturnStack.clear();
    return OK;
  }
  if ((*ddsim->iterator)->getType() == qc::Reset) {
    // Perform the desired qubits. This will first perform a measurement.
    auto qubitsToMeasure = (*ddsim->iterator)->getTargets();
    ddsim->iterator++;
    ddsim->previousInstructionStack.clear();
    ddsim->restoreCallReturnStack.clear();

    for (const auto qubit : qubitsToMeasure) {
      auto [pZero, pOne] = ddsim->dd->determineMeasurementProbabilities(
          ddsim->simulationState, static_cast<dd::Qubit>(qubit), true);
      auto rnd = generateRandomNumber();
      auto result = rnd < pZero;
      ddsim->dd->performCollapsingMeasurement(ddsim->simulationState,
                                              static_cast<dd::Qubit>(qubit),
                                              result ? pZero : pOne, result);
      if (!result) {
        const auto x = qc::StandardOperation(qubit, qc::X);
        auto tmp = ddsim->dd->multiply(dd::getDD(&x, *ddsim->dd),
                                       ddsim->simulationState);
        ddsim->dd->incRef(tmp);
        ddsim->dd->decRef(ddsim->simulationState);
        ddsim->simulationState = tmp;
      }
    }
    return OK;
  }
  if ((*ddsim->iterator)->getType() == qc::Barrier) {
    // Do not do anything.
    ddsim->iterator++;
    return OK;
  }
  if ((*ddsim->iterator)->isClassicControlledOperation()) {
    // For classic-controlled operations, we need to read the values of the
    // classical register first.
    const auto* op =
        dynamic_cast<qc::ClassicControlledOperation*>((*ddsim->iterator).get());
    const auto& controls = op->getControlRegister();
    const auto& exp = op->getExpectedValue();
    size_t registerValue = 0;
    for (size_t i = 0; i < controls.second; i++) {
      const auto name = getClassicalBitName(ddsim, controls.first + i);
      const auto& value = ddsim->variables[name].value.boolValue;
      registerValue |= (value ? 1ULL : 0ULL) << i;
    }
    if (registerValue == exp) {
      currDD = dd::getDD(ddsim->iterator->get(), *ddsim->dd);
    } else {
      currDD = ddsim->dd->makeIdent();
    }
  } else {
    // For all other operations, we just take the next gate to apply.
    currDD = dd::getDD(ddsim->iterator->get(),
                       *ddsim->dd); // retrieve the "new" current operation
  }

  auto temp = ddsim->dd->multiply(currDD, ddsim->simulationState);
  ddsim->dd->incRef(temp);
  ddsim->dd->decRef(ddsim->simulationState);
  ddsim->simulationState = temp;
  ddsim->dd->garbageCollect();

  ddsim->iterator++;
  return OK;
}

Result ddsimStepBackward(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  if (!self->canStepBackward(self)) {
    return ERROR;
  }

  ddsim->lastMetBreakpoint = -1ULL;
  if (!ddsim->restoreCallReturnStack.empty() &&
      ddsim->currentInstruction == ddsim->restoreCallReturnStack.back().first) {
    ddsim->callReturnStack.push_back(
        ddsim->restoreCallReturnStack.back().second);
    ddsim->restoreCallReturnStack.pop_back();
  }

  ddsim->currentInstruction = ddsim->previousInstructionStack.back();
  ddsim->previousInstructionStack.pop_back();

  if (!ddsim->callReturnStack.empty() &&
      ddsim->currentInstruction == ddsim->callReturnStack.back()) {
    ddsim->callReturnStack.pop_back();
  }

  // When going backwards, we still run the instruction that hits the breakpoint
  // because we want to stop *before* it.
  if (ddsim->breakpoints.find(ddsim->currentInstruction) !=
      ddsim->breakpoints.end()) {
    ddsim->lastMetBreakpoint = ddsim->currentInstruction;
  }

  if (ddsim->lastFailedAssertion != ddsim->currentInstruction) {
    ddsim->lastFailedAssertion = -1ULL;
  }

  if (ddsim->instructionTypes[ddsim->currentInstruction] != SIMULATE) {
    return OK;
  }

  ddsim->iterator--;
  qc::MatrixDD currDD;

  if ((*ddsim->iterator)->getType() == qc::Barrier) {
    return OK;
  }
  if ((*ddsim->iterator)->isClassicControlledOperation()) {
    const auto* op =
        dynamic_cast<qc::ClassicControlledOperation*>((*ddsim->iterator).get());
    const auto& controls = op->getControlRegister();
    const auto& exp = op->getExpectedValue();
    size_t registerValue = 0;
    for (size_t i = 0; i < controls.second; i++) {
      const auto name = getClassicalBitName(ddsim, controls.first + i);
      const auto& value = ddsim->variables[name].value.boolValue;
      registerValue |= (value ? 1ULL : 0ULL) << i;
    }
    if (registerValue == exp) {
      currDD = dd::getInverseDD(ddsim->iterator->get(), *ddsim->dd);
    } else {
      currDD = ddsim->dd->makeIdent();
    }
  } else {
    currDD = dd::getInverseDD(
        ddsim->iterator->get(),
        *ddsim->dd); // get the inverse of the current operation
  }

  auto temp = ddsim->dd->multiply(currDD, ddsim->simulationState);
  ddsim->dd->incRef(temp);
  ddsim->dd->decRef(ddsim->simulationState);
  ddsim->simulationState = temp;
  ddsim->dd->garbageCollect();

  return OK;
}

Result ddsimRunAll(SimulationState* self, size_t* failedAssertions) {
  size_t errorCount = 0;
  while (!self->isFinished(self)) {
    const Result result = self->runSimulation(self);
    if (result != OK) {
      return result;
    }
    if (self->didAssertionFail(self)) {
      errorCount++;
    }
  }
  *failedAssertions = errorCount;
  return OK;
}

Result ddsimRunSimulation(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  if (!self->canStepForward(self)) {
    return ERROR;
  }
  while (!self->isFinished(self)) {
    if (ddsim->paused) {
      ddsim->paused = false;
      return OK;
    }
    const Result res = self->stepForward(self);
    if (res != OK) {
      return res;
    }
    if (self->didAssertionFail(self) || self->wasBreakpointHit(self)) {
      break;
    }
  }
  return OK;
}

Result ddsimRunSimulationBackward(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  if (!self->canStepBackward(self)) {
    return ERROR;
  }
  while (self->canStepBackward(self)) {
    if (ddsim->paused) {
      ddsim->paused = false;
      return OK;
    }
    const Result res = self->stepBackward(self);
    if (res != OK) {
      return res;
    }
    if (self->didAssertionFail(self) || self->wasBreakpointHit(self)) {
      break;
    }
  }
  return OK;
}

Result ddsimResetSimulation(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  ddsim->currentInstruction = 0;
  ddsim->previousInstructionStack.clear();
  ddsim->callReturnStack.clear();
  ddsim->restoreCallReturnStack.clear();

  ddsim->iterator = ddsim->qc->begin();
  ddsim->lastFailedAssertion = -1ULL;
  ddsim->lastMetBreakpoint = -1ULL;

  resetSimulationState(ddsim);
  return OK;
}

Result ddsimPauseSimulation(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  ddsim->paused = true;
  return OK;
}

bool ddsimCanStepForward(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  return ddsim->ready &&
         ddsim->currentInstruction < ddsim->instructionTypes.size();
}

bool ddsimCanStepBackward(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  return ddsim->ready && !ddsim->previousInstructionStack.empty();
}

bool ddsimIsFinished(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  return ddsim->currentInstruction == ddsim->instructionTypes.size();
}

bool ddsimDidAssertionFail(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  return ddsim->lastFailedAssertion == ddsim->currentInstruction;
}

bool ddsimWasBreakpointHit(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  return ddsim->lastMetBreakpoint == ddsim->currentInstruction;
}

size_t ddsimGetCurrentInstruction(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  return ddsim->currentInstruction;
}

size_t ddsimGetInstructionCount(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  return ddsim->instructionTypes.size();
}

Result ddsimGetInstructionPosition(SimulationState* self, size_t instruction,
                                   size_t* start, size_t* end) {
  auto* ddsim = toDDSimulationState(self);
  if (instruction >= ddsim->instructionStarts.size()) {
    return ERROR;
  }
  size_t startIndex = ddsim->instructionStarts[instruction];
  size_t endIndex = ddsim->instructionEnds[instruction];

  while (ddsim->processedCode[startIndex] == ' ' ||
         ddsim->processedCode[startIndex] == '\n' ||
         ddsim->processedCode[startIndex] == '\r' ||
         ddsim->processedCode[startIndex] == '\t') {
    startIndex++;
  }
  while (ddsim->processedCode[endIndex] == ' ' ||
         ddsim->processedCode[endIndex] == '\n' ||
         ddsim->processedCode[endIndex] == '\r' ||
         ddsim->processedCode[endIndex] == '\t') {
    endIndex++;
  }
  *start = startIndex;
  *end = endIndex;
  return OK;
}

size_t ddsimGetNumQubits(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  return ddsim->qc->getNqubits();
}

Result ddsimGetAmplitudeIndex(SimulationState* self, size_t index,
                              Complex* output) {
  auto* ddsim = toDDSimulationState(self);
  auto result = ddsim->simulationState.getValueByIndex(index);
  output->real = result.real();
  output->imaginary = result.imag();
  return OK;
}

Result ddsimGetAmplitudeBitstring(SimulationState* self, const char* bitstring,
                                  Complex* output) {
  auto* ddsim = toDDSimulationState(self);
  auto path = std::string(bitstring);
  std::reverse(path.begin(), path.end());
  auto result =
      ddsim->simulationState.getValueByPath(ddsim->qc->getNqubits(), path);
  output->real = result.real();
  output->imaginary = result.imag();
  return OK;
}

Result ddsimGetClassicalVariable(SimulationState* self, const char* name,
                                 Variable* output) {
  auto* ddsim = toDDSimulationState(self);
  if (ddsim->variables.find(name) != ddsim->variables.end()) {
    *output = ddsim->variables[name];
    return OK;
  }
  return ERROR;
}
size_t ddsimGetNumClassicalVariables(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  return ddsim->variables.size();
}
Result ddsimGetClassicalVariableName(SimulationState* self,
                                     size_t variableIndex, char* output) {
  auto* ddsim = toDDSimulationState(self);

  if (variableIndex >= ddsim->variables.size()) {
    return ERROR;
  }

  const auto name = getClassicalBitName(ddsim, variableIndex);
  name.copy(output, name.length());
  return OK;
}

Result ddsimGetStateVectorFull(SimulationState* self, Statevector* output) {
  const Span<Complex> amplitudes(output->amplitudes, output->numStates);
  for (size_t i = 0; i < output->numStates; i++) {
    self->getAmplitudeIndex(self, i, &amplitudes[i]);
  }
  return OK;
}

/**
 * @brief Convert a given vector-of-vectors matrix to an Eigen3 matrix.
 * @param matrix The vector-of-vectors matrix to convert.
 * @return A new Eigen3 matrix.
 */
Eigen::MatrixXcd // NOLINT
toEigenMatrix(const std::vector<std::vector<Complex>>& matrix) {
  Eigen::MatrixXcd mat(static_cast<int64_t>(matrix.size()),  // NOLINT
                       static_cast<int64_t>(matrix.size())); // NOLINT
  for (size_t i = 0; i < matrix.size(); i++) {
    for (size_t j = 0; j < matrix.size(); j++) {
      mat(static_cast<int64_t>(i), static_cast<int64_t>(j)) = {
          matrix[i][j].real, matrix[i][j].imaginary};
    }
  }
  return mat;
}

Result ddsimGetStateVectorSub(SimulationState* self, size_t subStateSize,
                              const size_t* qubits, Statevector* output) {
  auto* ddsim = toDDSimulationState(self);
  Statevector fullState;
  fullState.numQubits = ddsim->qc->getNqubits();
  fullState.numStates = 1 << fullState.numQubits;
  std::vector<Complex> amplitudes(fullState.numStates);
  const Span<Complex> outAmplitudes(output->amplitudes, output->numStates);
  const Span<const size_t> qubitsSpan(qubits, subStateSize);

  std::vector<size_t> targetQubits;
  for (size_t i = 0; i < subStateSize; i++) {
    targetQubits.push_back(qubitsSpan[i]);
  }

  fullState.amplitudes = amplitudes.data();

  self->getStateVectorFull(self, &fullState);

  if (!isSubStateVectorLegal(fullState, targetQubits)) {
    return ERROR;
  }

  std::vector<size_t> otherQubits;
  for (size_t i = 0; i < fullState.numQubits; i++) {
    if (std::find(targetQubits.begin(), targetQubits.end(), i) ==
        targetQubits.end()) {
      otherQubits.push_back(i);
    }
  }

  const auto traced = getPartialTraceFromStateVector(fullState, otherQubits);

  // Create Eigen3 Matrix
  const auto mat = toEigenMatrix(traced);

  const Eigen::ComplexEigenSolver<Eigen::MatrixXcd> solver(mat); // NOLINT

  const auto& vectors = solver.eigenvectors();
  const auto& values = solver.eigenvalues();
  const auto epsilon = 0.000001;
  int index = -1;
  for (int i = 0; i < values.size(); i++) {
    if (values[i].imag() < -epsilon || values[i].imag() > epsilon) {
      continue;
    }
    if (values[i].real() - 1 < -epsilon || values[i].real() - 1 > epsilon) {
      continue;
    }
    index = i;
  }

  if (index == -1) {
    return ERROR;
  }

  for (size_t i = 0; i < traced.size(); i++) {
    outAmplitudes[i] = {vectors(static_cast<int>(i), index).real(),
                        vectors(static_cast<int>(i), index).imag()};
  }

  return OK;
}

Diagnostics* ddsimGetDiagnostics(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  return &ddsim->diagnostics.interface;
}

Result ddsimSetBreakpoint(SimulationState* self, size_t desiredPosition,
                          size_t* targetInstruction) {
  auto* ddsim = toDDSimulationState(self);
  for (auto i = 0ULL; i < ddsim->instructionTypes.size(); i++) {
    const size_t start = ddsim->instructionStarts[i];
    const size_t end = ddsim->instructionEnds[i];
    if (desiredPosition >= start && desiredPosition <= end) {
      if (ddsim->functionDefinitions.find(i) !=
          ddsim->functionDefinitions.end()) {
        // Breakpoint may be located in a sub-gate of the gate definition.
        for (auto j = i + 1; j < ddsim->instructionTypes.size(); j++) {
          const size_t startSub = ddsim->instructionStarts[j];
          const size_t endSub = ddsim->instructionEnds[j];
          if (startSub > desiredPosition) {
            break;
          }
          if (endSub >= desiredPosition) {
            *targetInstruction = j;
            ddsim->breakpoints.insert(j);
            return OK;
          }
          if (ddsim->instructionTypes[j] == RETURN) {
            break;
          }
        }
        *targetInstruction = i;
        ddsim->breakpoints.insert(i);
        return OK;
      }
      *targetInstruction = i;
      ddsim->breakpoints.insert(i);
      return OK;
    }
  }
  return ERROR;
}

Result ddsimClearBreakpoints(SimulationState* self) {
  auto* ddsim = toDDSimulationState(self);
  ddsim->breakpoints.clear();
  return OK;
}

Result ddsimGetStackDepth(SimulationState* self, size_t* depth) {
  auto* ddsim = toDDSimulationState(self);
  if (!ddsim->ready) {
    return ERROR;
  }
  *depth = ddsim->callReturnStack.size() + 1;
  return OK;
}

Result ddsimGetStackTrace(SimulationState* self, size_t maxDepth,
                          size_t* output) {
  auto* ddsim = toDDSimulationState(self);
  if (!ddsim->ready || maxDepth == 0) {
    return ERROR;
  }
  size_t depth = 0;
  self->getStackDepth(self, &depth);
  const Span<size_t> stackFrames(output, maxDepth);
  stackFrames[0] = self->getCurrentInstruction(self);
  for (auto i = 1ULL; i < maxDepth; i++) {
    if (i >= depth) {
      stackFrames[i] = -1ULL;
      continue;
    }
    stackFrames[i] = ddsim->callReturnStack[depth - i - 1];
  }
  return OK;
}

Result destroyDDSimulationState(DDSimulationState* self) {
  self->ready = false;
  destroyDDDiagnostics(&self->diagnostics);
  return OK;
}

//-----------------------------------------------------------------------------------------

std::vector<std::string> getTargetVariables(DDSimulationState* ddsim,
                                            size_t instruction) {
  std::vector<std::string> result;
  size_t parentFunction = -1ULL;
  size_t i = instruction;
  while (true) {
    if (ddsim->functionDefinitions.find(i) !=
        ddsim->functionDefinitions.end()) {
      parentFunction = i;
      break;
    }
    if (ddsim->instructionTypes[i] == RETURN) {
      break;
    }
    if (i == 0) {
      break;
    }
    i--;
  }

  const auto parameters = parentFunction != -1ULL
                              ? ddsim->targetQubits[parentFunction]
                              : std::vector<std::string>{};
  for (const auto& target : ddsim->targetQubits[instruction]) {
    if (std::find(parameters.begin(), parameters.end(), target) !=
        parameters.end()) {
      result.push_back(target);
      continue;
    }
    const auto foundRegister =
        std::find_if(ddsim->qubitRegisters.begin(), ddsim->qubitRegisters.end(),
                     [target](const QubitRegisterDefinition& reg) {
                       return reg.name == target;
                     });
    if (foundRegister != ddsim->qubitRegisters.end()) {
      for (size_t j = 0; j < foundRegister->size; j++) {
        result.push_back(target + "[" + std::to_string(j) + "]");
      }
    } else {
      result.push_back(target);
    }
  }
  return result;
}

size_t variableToQubit(DDSimulationState* ddsim, const std::string& variable) {
  auto declaration = replaceString(variable, " ", "");
  declaration = replaceString(declaration, "\t", "");
  std::string var;
  size_t idx = 0;
  if (declaration.find('[') != std::string::npos) {
    auto parts = splitString(declaration, '[');
    var = parts[0];
    idx = std::stoul(parts[1].substr(0, parts[1].size() - 1));
  } else {
    var = declaration;
  }

  for (size_t i = ddsim->callReturnStack.size() - 1; i != -1ULL; i--) {
    const auto call = ddsim->callReturnStack[i];
    if (ddsim->callSubstitutions[call].find(var) ==
        ddsim->callSubstitutions[call].end()) {
      continue;
    }
    var = ddsim->callSubstitutions[call][var];
    if (var.find('[') != std::string::npos) {
      auto parts = splitString(var, '[');
      var = parts[0];
      idx = std::stoul(parts[1].substr(0, parts[1].size() - 1));
    }
  }

  for (auto& reg : ddsim->qubitRegisters) {
    if (reg.name == var) {
      if (idx >= reg.size) {
        throw std::runtime_error("Index out of bounds");
      }
      return reg.index + idx;
    }
  }
  throw std::runtime_error("Unknown variable name " + var);
}

std::pair<size_t, size_t> variableToQubitAt(DDSimulationState* ddsim,
                                            const std::string& variable,
                                            size_t instruction) {
  size_t sweep = instruction;
  size_t functionDef = -1ULL;
  while (sweep < ddsim->instructionTypes.size()) {
    if (std::find(ddsim->functionDefinitions.begin(),
                  ddsim->functionDefinitions.end(),
                  sweep) != ddsim->functionDefinitions.end()) {
      functionDef = sweep;
      break;
    }
    if (ddsim->instructionTypes[sweep] == RETURN) {
      break;
    }
    sweep--;
  }

  if (functionDef == -1ULL) {
    // In the global scope, we can just use the register's index.
    return {variableToQubit(ddsim, variable), functionDef};
  }

  // In a gate-local scope, we have to define qubit indices relative to the
  // gate.
  const auto& targets = ddsim->targetQubits[functionDef];

  const auto found = std::find(targets.begin(), targets.end(), variable);
  if (found == targets.end()) {
    throw std::runtime_error("Unknown variable name " + variable);
  }

  return {static_cast<size_t>(std::distance(targets.begin(), found)),
          functionDef};
}

/**
 * @brief Compute the magnitude of a complex number.
 * @param c The complex number.
 * @return The computed magnitude.
 */
double complexMagnitude(Complex& c) {
  return std::sqrt(c.real * c.real + c.imaginary * c.imaginary);
}

/**
 * @brief Multiply two complex numbers.
 * @param c1 The first complex number.
 * @param c2 The second complex number.
 * @return The product of the two complex numbers.
 */
Complex complexMultiplication(const Complex& c1, const Complex& c2) {
  const double real = c1.real * c2.real - c1.imaginary * c2.imaginary;
  const double imaginary = c1.real * c2.imaginary + c1.imaginary * c2.real;
  return {real, imaginary};
}

/**
 * @brief Compute the complex conjugate of a complex number.
 * @param c The complex number.
 * @return The complex conjugate of the input complex number.
 */
Complex complexConjugate(const Complex& c) { return {c.real, -c.imaginary}; }

/**
 * @brief Add two complex numbers.
 * @param c1 The first complex number.
 * @param c2 The second complex number.
 * @return The sum of the two complex numbers.
 */
Complex complexAddition(const Complex& c1, const Complex& c2) {
  const double real = c1.real + c2.real;
  const double imaginary = c1.imaginary + c2.imaginary;
  return {real, imaginary};
}

/**
 * @brief Compute the dot product of two state vectors.
 * @param sv1 The first state vector.
 * @param sv2 The second state vector.
 * @return The computed dot product.
 */
double dotProduct(const Statevector& sv1, const Statevector& sv2) {
  double resultReal = 0;
  double resultImag = 0;

  const Span<Complex> amplitudes1(sv1.amplitudes, sv1.numStates);
  const Span<Complex> amplitudes2(sv2.amplitudes, sv2.numStates);

  for (size_t i = 0; i < sv1.numStates; i++) {
    resultReal += amplitudes1[i].real * amplitudes2[i].real -
                  amplitudes1[i].imaginary * amplitudes2[i].imaginary;
    resultImag += amplitudes1[i].real * amplitudes2[i].imaginary +
                  amplitudes1[i].imaginary * amplitudes2[i].real;
  }
  Complex result{resultReal, resultImag};
  return complexMagnitude(result);
}

/**
 * @brief For a given value, extract the bits at the given indices of its binary
 * representation.
 * @param indices The indices of the bits to extract.
 * @param value The value to extract the bits from.
 * @return The extracted bits.
 */
std::vector<bool> extractBits(const std::vector<size_t>& indices,
                              size_t value) {
  std::vector<bool> result(indices.size());
  for (size_t i = 0; i < indices.size(); i++) {
    result[i] = (((value >> indices[i]) & 1) == 1);
  }
  return result;
}

/**
 * @brief Split a given number into two numbers partitioned by the bits at the
 * given indices.
 *
 * Starts by extracting the bits at the given indices of its binary
 * representation. Then, the extracted and non-extracted parts are converted
 * into two new numbers.
 * @param number The number to split.
 * @param n The number of bits in the binary representation.
 * @param bits The indices of the bits to extract.
 * @return The two new numbers, partitioned by the extracted bits.
 */
std::pair<size_t, size_t> splitBitString(size_t number, size_t n,
                                         const std::vector<size_t>& bits) {
  size_t lenFirst = 0;
  size_t lenSecond = 0;

  size_t first = 0;
  size_t second = 0;

  for (size_t index = 0; index < n; index++) {
    if (std::find(bits.begin(), bits.end(), index) != bits.end()) {
      first |= (number & 1) << lenFirst;
      lenFirst++;
    } else {
      second |= (number & 1) << lenSecond;
      lenSecond++;
    }
    number >>= 1;
  }
  return {first, second};
}

/**
 * @brief Compute the partial trace of a given matrix.
 * @param matrix The matrix to compute the partial trace of.
 * @param indicesToTraceOut The indices of the qubits to trace out.
 * @param nQubits The total number of qubits.
 * @return The computed partial trace.
 */
std::vector<std::vector<Complex>>
getPartialTrace(const std::vector<std::vector<Complex>>& matrix,
                const std::vector<size_t>& indicesToTraceOut, size_t nQubits) {
  const auto traceSize = 1ULL << (nQubits - indicesToTraceOut.size());
  std::vector<std::vector<Complex>> traceMatrix(
      traceSize, std::vector<Complex>(traceSize, {0, 0}));
  for (size_t i = 0; i < matrix.size(); i++) {
    for (size_t j = 0; j < matrix.size(); j++) {
      const auto split1 = splitBitString(i, nQubits, indicesToTraceOut);
      const auto split2 = splitBitString(j, nQubits, indicesToTraceOut);
      if (split1.first != split2.first) {
        continue;
      }
      traceMatrix[split1.second][split2.second] = complexAddition(
          traceMatrix[split1.second][split2.second], matrix[i][j]);
    }
  }
  return traceMatrix;
}

/**
 * @brief Compute the entropy of a given matrix.
 * @param matrix The matrix to compute the entropy of.
 * @return The computed entropy.
 */
double getEntropy(const std::vector<std::vector<Complex>>& matrix) {
  const auto mat = toEigenMatrix(matrix);

  const Eigen::ComplexEigenSolver<Eigen::MatrixXcd> solver(mat);
  const auto& eigenvalues = solver.eigenvalues();
  double entropy = 0;
  for (const auto val : eigenvalues) {
    const auto value =
        (val.real() > -0.00001 && val.real() < 0) ? 0 : val.real();
    if (value < 0) {
      throw std::runtime_error("Negative eigenvalue");
    }
    if (value != 0) {
      entropy -= val.real() * log2(val.real());
    }
  }
  return entropy;
}

/**
 * @brief Compute the shared information of a given 4x4 density matrix.
 *
 * The density matrix is assumed to represent a two-qubit system.\n
 * The shared information is computed by adding up the entropy values of the two
 * new matrices created by tracing out each of the qubits and subtracting the
 * entropy of the original matrix.
 * @param matrix The density matrix to compute the shared information of.
 * @return The computed shared information.
 */
double getSharedInformation(const std::vector<std::vector<Complex>>& matrix) {
  const auto p0 = getPartialTrace(matrix, {1}, 2);
  const auto p1 = getPartialTrace(matrix, {0}, 2);
  return getEntropy(p0) + getEntropy(p1) - getEntropy(matrix);
}

/**
 * @brief Check if two qubits are entangled in a given density matrix.
 *
 * This is done by tracing out all other qubits from a density matrix and then
 * checking whether the shared information is greater than 0.
 * @param densityMatrix The density matrix to check for entanglement.
 * @param qubit1 The first qubit to check.
 * @param qubit2 The second qubit to check.
 * @return True if the qubits are entangled, false otherwise.
 */
bool areQubitsEntangled(std::vector<std::vector<Complex>>& densityMatrix,
                        size_t qubit1, size_t qubit2) {
  const auto numQubits = static_cast<size_t>(std::log2(densityMatrix.size()));
  if (numQubits == 2) {
    return getSharedInformation(densityMatrix) > 0;
  }
  std::vector<size_t> toTraceOut;
  for (size_t i = 0; i < numQubits; i++) {
    if (i != qubit1 && i != qubit2) {
      toTraceOut.push_back(i);
    }
  }
  const auto partialTrace =
      getPartialTrace(densityMatrix, toTraceOut, numQubits);
  return getSharedInformation(partialTrace) > 0;
}

std::vector<std::vector<Complex>>
getPartialTraceFromStateVector(const Statevector& sv,
                               const std::vector<size_t>& traceOut) {
  const auto traceSize = 1ULL << (sv.numQubits - traceOut.size());
  const Span<Complex> amplitudes(sv.amplitudes, sv.numStates);
  std::vector<std::vector<Complex>> traceMatrix(
      traceSize, std::vector<Complex>(traceSize, {0, 0}));
  for (size_t i = 0; i < sv.numStates; i++) {
    for (size_t j = 0; j < sv.numStates; j++) {
      const auto split1 = splitBitString(i, sv.numQubits, traceOut);
      const auto split2 = splitBitString(j, sv.numQubits, traceOut);
      if (split1.first != split2.first) {
        continue;
      }
      const auto product = complexMultiplication(amplitudes[i], amplitudes[j]);
      const auto row = split1.second;
      const auto col = split2.second;
      traceMatrix[row][col] = complexAddition(traceMatrix[row][col], product);
    }
  }
  return traceMatrix;
}

/**
 * @brief Compute the trace of the square of a given matrix.
 *
 * This is done so that we don't have to compute the entire square of the
 * matrix.
 * @param matrix The matrix to compute the trace of the square of.
 * @return The computed trace of the square.
 */
Complex getTraceOfSquare(const std::vector<std::vector<Complex>>& matrix) {
  Complex runningSum{0, 0};
  for (size_t i = 0; i < matrix.size(); i++) {
    for (size_t k = 0; k < matrix.size(); k++) {
      runningSum = complexAddition(
          runningSum, complexMultiplication(matrix[i][k], matrix[k][i]));
    }
  }
  return runningSum;
}

/**
 * @brief Check if the partial trace of a given state vector is pure.
 *
 * This is true if and only if the trace of the square of the traced out density
 * matrix is equal to 1.
 * @param sv The state vector to check.
 * @param traceOut The indices of the qubits to trace out.
 * @return True if the partial trace is pure, false otherwise.
 */
bool partialTraceIsPure(const Statevector& sv,
                        const std::vector<size_t>& traceOut) {
  const auto traceMatrix = getPartialTraceFromStateVector(sv, traceOut);
  const auto trace = getTraceOfSquare(traceMatrix);
  const double epsilon = 0.00000001;
  return trace.imaginary < epsilon && trace.imaginary > -epsilon &&
         (trace.real - 1) < epsilon && (trace.real - 1) > -epsilon;
}

bool isSubStateVectorLegal(const Statevector& full,
                           std::vector<size_t>& targetQubits) {
  const auto numQubits = full.numQubits;
  std::vector<size_t> ignored;
  for (size_t i = 0; i < numQubits; i++) {
    if (std::find(targetQubits.begin(), targetQubits.end(), i) ==
        targetQubits.end()) {
      ignored.push_back(i);
    }
  }
  return partialTraceIsPure(full, ignored);
}

/**
 * Checks the given entanglement assertion on the given state.
 * @param ddsim The simulation state.
 * @param assertion The entanglement assertion to check.
 * @return True if the assertion is satisfied, false otherwise.
 */
bool checkAssertionEntangled(
    DDSimulationState* ddsim,
    std::unique_ptr<EntanglementAssertion>& assertion) {
  Statevector sv;
  sv.numQubits = ddsim->interface.getNumQubits(&ddsim->interface);
  sv.numStates = 1 << sv.numQubits;
  std::vector<Complex> amplitudes(sv.numStates);
  sv.amplitudes = amplitudes.data();
  ddsim->interface.getStateVectorFull(&ddsim->interface, &sv);

  std::vector<size_t> qubits;
  for (const auto& variable : assertion->getTargetQubits()) {
    qubits.push_back(variableToQubit(ddsim, variable));
  }

  std::vector<std::vector<Complex>> densityMatrix(
      sv.numStates, std::vector<Complex>(sv.numStates, {0, 0}));
  for (size_t i = 0; i < sv.numStates; i++) {
    for (size_t j = 0; j < sv.numStates; j++) {
      densityMatrix[i][j] =
          complexMultiplication(amplitudes[i], complexConjugate(amplitudes[j]));
    }
  }

  for (const auto i : qubits) {
    for (const auto j : qubits) {
      if (i == j) {
        continue;
      }
      if (!areQubitsEntangled(densityMatrix, i, j)) {
        return false;
      }
    }
  }
  return true;
}

/**
 * Checks the given superposition assertion on the given state.
 * @param ddsim The simulation state.
 * @param assertion The superposition assertion to check.
 * @return True if the assertion is satisfied, false otherwise.
 */
bool checkAssertionSuperposition(
    DDSimulationState* ddsim,
    std::unique_ptr<SuperpositionAssertion>& assertion) {
  std::vector<size_t> qubits;

  for (const auto& variable : assertion->getTargetQubits()) {
    qubits.push_back(variableToQubit(ddsim, variable));
  }

  Complex result;
  bool firstFound = false;
  std::vector<bool> bitstring;
  for (size_t i = 0;
       i < 1ULL << ddsim->interface.getNumQubits(&ddsim->interface); i++) {
    ddsim->interface.getAmplitudeIndex(&ddsim->interface, i, &result);
    if (complexMagnitude(result) > 0.00000001) {
      if (!firstFound) {
        firstFound = true;
        bitstring = extractBits(qubits, i);
      } else {
        const std::vector<bool> compareBitstring = extractBits(qubits, i);
        if (bitstring != compareBitstring) {
          return true;
        }
      }
    }
  }

  return false;
}

/**
 * Checks the given statevector-equality assertion on the given state.
 * @param ddsim The simulation state.
 * @param assertion The equality assertion to check.
 * @return True if the assertion is satisfied, false otherwise.
 */
bool checkAssertionEqualityStatevector(
    DDSimulationState* ddsim,
    std::unique_ptr<StatevectorEqualityAssertion>& assertion) {
  std::vector<size_t> qubits;
  for (const auto& variable : assertion->getTargetQubits()) {
    qubits.push_back(variableToQubit(ddsim, variable));
  }

  Statevector sv;
  sv.numQubits = qubits.size();
  sv.numStates = 1 << sv.numQubits;
  std::vector<Complex> amplitudes(sv.numStates);
  sv.amplitudes = amplitudes.data();

  if (ddsim->interface.getStateVectorSub(&ddsim->interface, sv.numQubits,
                                         qubits.data(), &sv) == ERROR) {
    throw std::runtime_error(
        "Equality assertion on entangled sub-state is not allowed.");
  }

  const double similarityThreshold = assertion->getSimilarityThreshold();

  const double similarity = dotProduct(sv, assertion->getTargetStatevector());

  return similarity >= similarityThreshold;
}

/**
 * Checks the given circuit-equality assertion on the given state.
 * @param ddsim The simulation state.
 * @param assertion The equality assertion to check.
 * @return True if the assertion is satisfied, false otherwise.
 */
bool checkAssertionEqualityCircuit(
    DDSimulationState* ddsim,
    std::unique_ptr<CircuitEqualityAssertion>& assertion) {
  std::vector<size_t> qubits;
  for (const auto& variable : assertion->getTargetQubits()) {
    qubits.push_back(variableToQubit(ddsim, variable));
  }

  DDSimulationState secondSimulation;
  createDDSimulationState(&secondSimulation);
  secondSimulation.interface.loadCode(&secondSimulation.interface,
                                      assertion->getCircuitCode().c_str());
  if (!secondSimulation.assertionInstructions.empty()) {
    destroyDDSimulationState(&secondSimulation);
    throw std::runtime_error(
        "Circuit equality assertions cannot contain nested assertions");
  }
  secondSimulation.interface.runSimulation(&secondSimulation.interface);

  Statevector sv2;
  sv2.numQubits =
      secondSimulation.interface.getNumQubits(&secondSimulation.interface);
  sv2.numStates = 1 << sv2.numQubits;
  std::vector<Complex> amplitudes2(sv2.numStates);
  sv2.amplitudes = amplitudes2.data();
  secondSimulation.interface.getStateVectorFull(&secondSimulation.interface,
                                                &sv2);
  destroyDDSimulationState(&secondSimulation);

  Statevector sv;
  sv.numQubits = qubits.size();
  sv.numStates = 1 << sv.numQubits;
  std::vector<Complex> amplitudes(sv.numStates);
  sv.amplitudes = amplitudes.data();
  if (ddsim->interface.getStateVectorSub(&ddsim->interface, sv.numQubits,
                                         qubits.data(), &sv) == ERROR) {
    throw std::runtime_error(
        "Equality assertion on entangled sub-state is not allowed.");
  }

  const double similarityThreshold = assertion->getSimilarityThreshold();

  const double similarity = dotProduct(sv, sv2);

  return similarity >= similarityThreshold;
}

bool checkAssertion(DDSimulationState* ddsim,
                    std::unique_ptr<Assertion>& assertion) {
  if (assertion->getType() == AssertionType::Entanglement) {
    std::unique_ptr<EntanglementAssertion> entanglementAssertion(
        dynamic_cast<EntanglementAssertion*>(assertion.release()));
    auto result = checkAssertionEntangled(ddsim, entanglementAssertion);
    assertion = std::move(entanglementAssertion);
    return result;
  }
  if (assertion->getType() == AssertionType::Superposition) {
    std::unique_ptr<SuperpositionAssertion> superpositionAssertion(
        dynamic_cast<SuperpositionAssertion*>(assertion.release()));
    auto result = checkAssertionSuperposition(ddsim, superpositionAssertion);
    assertion = std::move(superpositionAssertion);
    return result;
  }
  if (assertion->getType() == AssertionType::StatevectorEquality) {
    std::unique_ptr<StatevectorEqualityAssertion> svEqualityAssertion(
        dynamic_cast<StatevectorEqualityAssertion*>(assertion.release()));
    auto result = checkAssertionEqualityStatevector(ddsim, svEqualityAssertion);
    assertion = std::move(svEqualityAssertion);
    return result;
  }
  if (assertion->getType() == AssertionType::CircuitEquality) {
    std::unique_ptr<CircuitEqualityAssertion> circuitEqualityAssertion(
        dynamic_cast<CircuitEqualityAssertion*>(assertion.release()));
    auto result =
        checkAssertionEqualityCircuit(ddsim, circuitEqualityAssertion);
    assertion = std::move(circuitEqualityAssertion);
    return result;
  }
  throw std::runtime_error("Unknown assertion type");
}

/**
 * @brief For an instruction that has a child block, extract the valid code from
 * the block's body.
 *
 * Valid code is code that can be passed to the simulation backend. Assertions
 * are removed.
 * @param parent The parent instruction.
 * @param allInstructions All instructions in the program.
 * @return The extracted valid code.
 */
std::string validCodeFromChildren(const Instruction& parent,
                                  std::vector<Instruction>& allInstructions) {
  std::string code = parent.code;
  if (!parent.block.valid) {
    return code;
  }
  code += " { ";
  for (auto child : parent.childInstructions) {
    const auto& childInstruction = allInstructions[child];
    if (childInstruction.assertion != nullptr) {
      continue;
    }
    code += validCodeFromChildren(childInstruction, allInstructions);
  }
  code += " } ";
  return code;
}

std::string preprocessAssertionCode(const char* code,
                                    DDSimulationState* ddsim) {

  auto instructions = preprocessCode(code, ddsim->processedCode);
  std::vector<std::string> correctLines;
  ddsim->instructionTypes.clear();
  ddsim->functionDefinitions.clear();
  ddsim->instructionStarts.clear();
  ddsim->instructionEnds.clear();
  ddsim->callSubstitutions.clear();
  ddsim->classicalRegisters.clear();
  ddsim->qubitRegisters.clear();
  ddsim->successorInstructions.clear();
  ddsim->dataDependencies.clear();
  ddsim->functionCallers.clear();
  ddsim->targetQubits.clear();

  for (auto& instruction : instructions) {
    ddsim->targetQubits.push_back(instruction.targets);
    ddsim->successorInstructions.insert(
        {instruction.lineNumber, instruction.successorIndex});
    ddsim->instructionStarts.push_back(instruction.originalCodeStartPosition);
    ddsim->instructionEnds.push_back(instruction.originalCodeEndPosition);
    ddsim->dataDependencies.insert({instruction.lineNumber, {}});
    for (const auto& dependency : instruction.dataDependencies) {
      ddsim->dataDependencies[instruction.lineNumber].emplace_back(
          dependency.first, dependency.second);
    }
    if (instruction.isFunctionCall) {
      const size_t successorInFunction = instruction.successorIndex;
      const size_t functionIndex = successorInFunction - 1;
      if (ddsim->functionCallers.find(functionIndex) ==
          ddsim->functionCallers.end()) {
        ddsim->functionCallers.insert({functionIndex, {}});
      }
      ddsim->functionCallers[functionIndex].insert(instruction.lineNumber);
    }

    // what exactly we do with each instruction depends on its type:
    // - RETURN instructions are not simulated.
    // - RETURN and ASSERTION instructions are not added to the final code.
    // - Custom gate definitions are also not simulated.
    // - Function calls are simulated but will be replaced by their flattened
    // instructions later.
    // - For register declarations, we store the register name.
    // - Instructions inside function definitions are not added to the final
    // code because they were already added when the function definition was
    // first encountered.
    if (instruction.code == "RETURN") {
      ddsim->instructionTypes.push_back(RETURN);
    } else if (instruction.assertion != nullptr) {
      ddsim->instructionTypes.push_back(ASSERTION);
      ddsim->assertionInstructions.insert(
          {instruction.lineNumber, std::move(instruction.assertion)});
    } else if (instruction.isFunctionDefinition) {
      if (!instruction.inFunctionDefinition) {
        correctLines.push_back(
            validCodeFromChildren(instruction, instructions));
      }
      ddsim->functionDefinitions.insert(instruction.lineNumber);
      ddsim->instructionTypes.push_back(NOP);
    } else if (instruction.isFunctionCall) {
      if (!instruction.inFunctionDefinition) {
        correctLines.push_back(instruction.code);
      }
      ddsim->callSubstitutions.insert(
          {instruction.lineNumber, instruction.callSubstitution});
      ddsim->instructionTypes.push_back(CALL);
    } else if (instruction.code.find("OPENQASM 2.0") != std::string::npos ||
               instruction.code.find("OPENQASM 3.0") != std::string::npos ||
               instruction.code.find("include") != std::string::npos) {
      if (!instruction.inFunctionDefinition) {
        correctLines.push_back(instruction.code);
      }
      ddsim->instructionTypes.push_back(NOP);
    } else if (instruction.code.find("qreg") != std::string::npos) {
      auto declaration = replaceString(instruction.code, "qreg", "");
      declaration = replaceString(declaration, " ", "");
      declaration = replaceString(declaration, "\n", "");
      declaration = replaceString(declaration, "\t", "");
      declaration = replaceString(declaration, ";", "");
      auto parts = splitString(declaration, '[');
      auto name = parts[0];
      const size_t size = std::stoul(parts[1].substr(0, parts[1].size() - 1));

      const size_t index = ddsim->qubitRegisters.empty()
                               ? 0
                               : ddsim->qubitRegisters.back().index +
                                     ddsim->qubitRegisters.back().size;
      const QubitRegisterDefinition reg{name, index, size};
      ddsim->qubitRegisters.push_back(reg);

      if (!instruction.inFunctionDefinition) {
        correctLines.push_back(instruction.code);
      }
      ddsim->instructionTypes.push_back(NOP);
    } else if (instruction.code.find("creg") != std::string::npos) {
      auto declaration = replaceString(instruction.code, "creg", "");
      declaration = replaceString(declaration, " ", "");
      declaration = replaceString(declaration, "\t", "");
      declaration = replaceString(declaration, ";", "");
      auto parts = splitString(declaration, '[');
      auto name = parts[0];
      const size_t size = std::stoul(parts[1].substr(0, parts[1].size() - 1));

      const size_t index = ddsim->classicalRegisters.empty()
                               ? 0
                               : ddsim->classicalRegisters.back().index +
                                     ddsim->classicalRegisters.back().size;
      const ClassicalRegisterDefinition reg{removeWhitespace(name), index,
                                            size};
      ddsim->classicalRegisters.push_back(reg);
      for (auto i = 0ULL; i < size; i++) {
        const auto variableName =
            removeWhitespace(name) + "[" + std::to_string(i) + "]";
        ddsim->variableNames.push_back(
            std::make_unique<std::string>(variableName));
        const Variable newVariable{ddsim->variableNames.back()->data(),
                                   VariableType::VarBool,
                                   {false}};
        ddsim->variables.insert({newVariable.name, newVariable});
      }

      if (!instruction.inFunctionDefinition) {
        correctLines.push_back(instruction.code);
      }
      ddsim->instructionTypes.push_back(NOP);
    } else {
      if (!instruction.inFunctionDefinition) {
        correctLines.push_back(
            validCodeFromChildren(instruction, instructions));
      }
      ddsim->instructionTypes.push_back(SIMULATE);
    }
  }

  // Transform all the correct lines into a single code string.
  const auto result = std::accumulate(
      correctLines.begin(), correctLines.end(), std::string(),
      [](const std::string& a, const std::string& b) { return a + b; });

  return result;
}

std::string getClassicalBitName(DDSimulationState* ddsim, size_t index) {
  for (auto& reg : ddsim->classicalRegisters) {
    if (index >= reg.index && index < reg.index + reg.size) {
      return reg.name + "[" + std::to_string(index - reg.index) + "]";
    }
  }
  return "UNKNOWN";
}
