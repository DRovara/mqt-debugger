/**
 * @file DDSimDiagnostics.hpp
 * @brief Contains the implementation of the `Diagnostics` interface for the DD
 * simulator.
 */
#pragma once

#include "backend/diagnostics.h"
#include "common.h"
#include "common/parsing/AssertionParsing.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <vector>

struct DDSimulationState;

/**
 * @brief The DD-simulator implementation of the `Diagnostics` interface.
 */
struct DDDiagnostics {
  /**
   * @brief The `Diagnostics` interface.
   */
  Diagnostics interface;

  /**
   * @brief The simulation state.
   */
  DDSimulationState* simulationState;
  /**
   * @brief The qubits that have been marked as zero controls.
   */
  std::map<size_t, std::set<size_t>> zeroControls;
  /**
   * @brief The qubits that have been marked as non-zero controls.
   */
  std::map<size_t, std::set<size_t>> nonZeroControls;

  /**
   * @brief The actual qubits that each instruction has targeted.
   */
  std::map<size_t, std::set<std::vector<size_t>>> actualQubits;
};

/**
 * @brief Get the number of qubits in the system.
 * @param self The diagnostics instance to query.
 * @return The number of qubits.
 */
size_t dddiagnosticsGetNumQubits(Diagnostics* self);

/**
 * @brief Get the number of instructions in the code.
 * @param self The diagnostics instance to query.
 * @return The number of instructions.
 */
size_t dddiagnosticsGetInstructionCount(Diagnostics* self);

/**
 * @brief Initializes the diagnostics interface.
 * @param self The instance to initialize.
 * @return The result of the operation.
 */
Result dddiagnosticsInit(Diagnostics* self);

/**
 * @brief Extract all data dependencies for a given instruction.
 *
 * This method expects a continuous memory block of booleans with size equal to
 * the number of instructions. Each element represents an instruction and will
 * be set to true if the instruction is a data dependency.\n\n
 *
 * If the instruction is inside a custom gate definition, the data dependencies
 * will by default not go outside of the custom gate, unless a new call
 * instruction is found. By setting `includeCallers` to true, all possible
 * callers of the custom gate will also be included and further dependencies
 * outside the custom gate will be taken from there.\n\n
 *
 * The line itself will also be counted as a dependency. Gate and register
 * declarations will not.\n\n
 *
 * This method can be performed without running the program, as it is a static
 * analysis method.\n\n
 *
 * @param self The diagnostics instance to query.
 * @param instruction The instruction to extract the data dependencies for.
 * @param includeCallers True if the data dependencies should include all
 * possible callers of the containing custom gate.
 * @param instructions An array of booleans that will be set to true for each
 * instruction that is a data dependency.
 * @return The result of the operation.
 */
Result dddiagnosticsGetDataDependencies(Diagnostics* self, size_t instruction,
                                        bool includeCallers,
                                        bool* instructions);

/**
 * @brief Extract all qubits that interact with a given qubit up to a specific
 * instruction.
 *
 * This method expects a continuous memory block of booleans with size equal to
 * the number of qubits. Each element represents a qubit and will be set to true
 * if the qubit interacts with the given qubit.\n\n
 *
 * If the target instruction is inside a custom gate definition, the
 * interactions will only be searched inside the custom gate, unless a new call
 * instruction is found.\n\n
 *
 * The qubit itself will also be counted as an interaction.\n\n
 *
 * This method can be performed without running the program, as it is a static
 * analysis method.\n\n
 *
 * @param self The diagnostics instance to query.
 * @param beforeInstruction The instruction to extract the interactions up to
 * (excluding).
 * @param qubit The qubit to extract the interactions for.
 * @param qubitsAreInteracting An array of booleans that will be set to true for
 * each qubit that interacts with the given qubit.
 * @return The result of the operation.
 */
Result dddiagnosticsGetInteractions(Diagnostics* self, size_t beforeInstruction,
                                    size_t qubit, bool* qubitsAreInteracting);

/**
 * @brief Extract all controlled gates that have been marked as only having
 * controls with value zero.
 *
 * This method expects a continuous memory block of booleans with size equal to
 * the number of instructions. Each element represents an instruction and will
 * be set to true if the instruction is a controlled gate with only zero
 * controls.\n\n
 *
 * This method can only be performed at runtime, as it is a dynamic analysis
 * method.\n\n
 *
 * @param self The diagnostics instance to query.
 * @param instructions An array of booleans that will be set to true for each
 * instruction that is a controlled gate with only zero controls.
 * @return The result of the operation.
 */
Result dddiagnosticsGetZeroControlInstructions(Diagnostics* self,
                                               bool* instructions);

/**
 * @brief Extract a list of potential error causes encountered during execution.
 *
 * Up to `count` error causes will be returned.
 *
 * This method should be run after the program has been executed and reached an
 * assertion error.
 * @param self The diagnostics instance to query.
 * @param output An array of error causes to be filled. It is expected to have
 * space for at least `count` elements.
 * @param count The maximum number of error causes to return.
 * @return The number of error causes found.
 */
size_t dddiagnosticsPotentialErrorCauses(Diagnostics* self, ErrorCause* output,
                                         size_t count);

/**
 * @brief Creates a new `DDDiagnostics` instance.
 *
 * This method expects an allocated memory block for the `DDDiagnostics`
 * instance.
 * @param self The instance to create.
 * @param state The simulation state to connect this diagnostics instance to.
 * @return The result of the operation.
 */
Result createDDDiagnostics(DDDiagnostics* self, DDSimulationState* state);

/**
 * @brief Destroys a `DDDiagnostics` instance.
 * @param self The instance to destroy.
 * @return The result of the operation.
 */
Result destroyDDDiagnostics([[maybe_unused]] DDDiagnostics* self);

/**
 * @brief Called, whenever simulation steps forward to update the diagnostics.
 * @param diagnostics The diagnostics instance to update.
 * @param instruction The instruction that was executed.
 */
void dddiagnosticsOnStepForward(DDDiagnostics* diagnostics, size_t instruction);

/**
 * @brief Tries to find potential errors caused by missing interactions at
 * runtime.
 *
 * Used by the `potentialErrorCauses` method.
 * @param diagnostics The diagnostics instance to query.
 * @param state The simulation state.
 * @param instruction The instruction for which to investigate interactions.
 * @param assertion The assertion that failed.
 * @param output An array of error causes to be filled.
 * @param count The maximum number of error causes to return.
 * @return The number of error causes found.
 */
size_t tryFindMissingInteraction(DDDiagnostics* diagnostics,
                                 DDSimulationState* state, size_t instruction,
                                 const std::unique_ptr<Assertion>& assertion,
                                 ErrorCause* output, size_t count);

/**
 * @brief Tries to find potential errors caused by controls that are always zero
 * at runtime.
 *
 * Used by the `potentialErrorCauses` method.
 * @param diagnostics The diagnostics instance to query.
 * @param instruction The instruction up to which to investigate the program.
 * @param output An array of error causes to be filled.
 * @param count The maximum number of error causes to return.
 * @return The number of error causes found.
 */
size_t tryFindZeroControls(DDDiagnostics* diagnostics, size_t instruction,
                           ErrorCause* output, size_t count);
