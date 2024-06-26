#include "ddsimDebug.hpp"
#include "debug.h"
#include "common.h"

#include <iostream>
#include <algorithm>
#include <cctype>
#include <string>


Result createDDSimulationState(DDSimulationState* self) {
    self->interface.init = ddsimInit;

    self->interface.loadCode = ddsimLoadCode;
    self->interface.stepForward = ddsimStepForward;
    self->interface.stepBackward = ddsimStepBackward;
    self->interface.runSimulation = ddsimRunSimulation;
    self->interface.resetSimulation = ddsimResetSimulation;
    self->interface.canStepForward = ddsimCanStepForward;
    self->interface.canStepBackward = ddsimCanStepBackward;
    self->interface.isFinished = ddsimIsFinished;

    self->interface.getCurrentLine = ddsimGetCurrentLine;
    self->interface.getAmplitudeIndex = ddsimGetAmplitudeIndex;
    self->interface.getAmplitudeBitstring = ddsimGetAmplitudeBitstring;
    self->interface.getClassicalVariable = ddsimGetClassicalVariable;
    self->interface.getStateVectorFull = ddsimGetStateVectorFull;
    self->interface.getStateVectorSub = ddsimGetStateVectorSub;

    return self->interface.init(reinterpret_cast<SimulationState*>(self));
}

void resetSimulationState(DDSimulationState* ddsim) {
    if (ddsim->simulationState.p != nullptr) {
        ddsim->dd->decRef(ddsim->simulationState);
    }
    ddsim->simulationState = ddsim->dd->makeZeroState(ddsim->qc->getNqubits());
    ddsim->dd->incRef(ddsim->simulationState);
}

Result ddsimInit(SimulationState* self) {
    auto ddsim = reinterpret_cast<DDSimulationState*>(self);

    ddsim->qc = std::make_unique<qc::QuantumComputation>();
    ddsim->dd = std::make_unique<dd::Package<>>(1);
    ddsim->iterator = ddsim->qc->begin();
    ddsim->currentLine = 0;
    ddsim->assertionFailed = false;

    resetSimulationState(ddsim);

    return OK;
}

Result ddsimLoadCode(SimulationState* self, const char* code) {
    auto ddsim = reinterpret_cast<DDSimulationState*>(self);
    ddsim->currentLine = 0;
    std::stringstream ss{preprocessAssertionCode(code, ddsim)};
    ddsim->qc->import(ss, qc::Format::OpenQASM3);

    ddsim->iterator = ddsim->qc->begin();
    ddsim->dd->resize(ddsim->qc->getNqubits());
    ddsim->assertionFailed = false;

    resetSimulationState(ddsim);

    return OK;
}

Result ddsimStepForward(SimulationState* self) {
    auto ddsim = reinterpret_cast<DDSimulationState*>(self);
    if(!self->canStepForward(self))
        return ERROR;
    ddsim->currentLine++;

    if(ddsim->instructionTypes[ddsim->currentLine - 1] == ASSERTION) {
        auto& assertion = ddsim->assertionInstructions[ddsim->currentLine - 1];
        ddsim->assertionFailed = !checkAssertion(ddsim, assertion);
        return OK;
    }

    ddsim->assertionFailed = false;
    if(ddsim->instructionTypes[ddsim->currentLine - 1] != SIMULATE) {
        return OK;
    }

    qc::MatrixDD currDD{};
    if ((*ddsim->iterator)->isClassicControlledOperation()) {
        // TODO this is for later
    } else {
        currDD = dd::getDD(ddsim->iterator->get(), *ddsim->dd); // retrieve the "new" current operation
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
    auto ddsim = reinterpret_cast<DDSimulationState*>(self);
    if(!self->canStepBackward(self))
        return ERROR;
    ddsim->currentLine--;

    if(ddsim->instructionTypes[ddsim->currentLine] == SIMULATE) {

        ddsim->iterator--;
        qc::MatrixDD currDD{};
        if ((*ddsim->iterator)->isClassicControlledOperation()) {
            // TODO this is for later
        } else {
            currDD = dd::getInverseDD(ddsim->iterator->get(),
                                      *ddsim->dd); // get the inverse of the current operation
        }

        auto temp = ddsim->dd->multiply(currDD, ddsim->simulationState);
        ddsim->dd->incRef(temp);
        ddsim->dd->decRef(ddsim->simulationState);
        ddsim->simulationState = temp;
        ddsim->dd->garbageCollect();
    }

    if(ddsim->currentLine > 0 && ddsim->instructionTypes[ddsim->currentLine - 1] == ASSERTION) {
        auto& assertion = ddsim->assertionInstructions[ddsim->currentLine - 1];
        ddsim->assertionFailed = !checkAssertion(ddsim, assertion);
    }
    else {
        ddsim->assertionFailed = false;
    }

    return OK;
}

Result ddsimRunSimulation(SimulationState* self) {
    auto ddsim = reinterpret_cast<DDSimulationState*>(self);
    while(!self->isFinished(self) && !ddsim->assertionFailed)
        self->stepForward(self);
    return OK;
}

Result ddsimResetSimulation(SimulationState* self) {
    auto ddsim = reinterpret_cast<DDSimulationState*>(self);
    ddsim->currentLine = 0;

    ddsim->iterator = ddsim->qc->begin();
    ddsim->assertionFailed = false;

    resetSimulationState(ddsim);
    return OK;
}

bool ddsimCanStepForward(SimulationState* self) {
    auto ddsim = reinterpret_cast<DDSimulationState*>(self);
    return ddsim->currentLine < ddsim->instructionTypes.size();
}

bool ddsimCanStepBackward(SimulationState* self) {
    auto ddsim = reinterpret_cast<DDSimulationState*>(self);
    return ddsim->currentLine > 0;
}

bool ddsimIsFinished(SimulationState* self) {
    auto ddsim = reinterpret_cast<DDSimulationState*>(self);
    return ddsim->currentLine == ddsim->instructionTypes.size();
}

size_t ddsimGetCurrentLine(SimulationState* self) {
    auto ddsim = reinterpret_cast<DDSimulationState*>(self);
    return ddsim->currentLine;
}

Result ddsimGetAmplitudeIndex(SimulationState* self, size_t qubit, Complex* output) {
    auto ddsim = reinterpret_cast<DDSimulationState*>(self);
    auto result = ddsim->simulationState.getValueByIndex(qubit);
    output->real = result.real();
    output->imaginary = result.imag();
    return OK;
}

Result ddsimGetAmplitudeBitstring(SimulationState* self, const char* bitstring, Complex* output) {
    auto ddsim = reinterpret_cast<DDSimulationState*>(self);
    auto path = std::string(bitstring);
    std::reverse(path.begin(), path.end());
    auto result = ddsim->simulationState.getValueByPath(ddsim->qc->getNqubits(), path);
    output->real = result.real();
    output->imaginary = result.imag();
    return OK;
}

Result ddsimGetClassicalVariable(SimulationState* self, const char* name, Variable* output) {
    std::cout << "Getting classical variable " << name << " into " << output << " for " << self << std::endl;
    return ERROR; // TODO this is for later
}

Result ddsimGetStateVectorFull(SimulationState* self, Statevector* output) {
    for(size_t i = 0; i < output->numStates; i++) {
        Complex c;
        self->getAmplitudeIndex(self, i, &c);
        output->amplitudes[i] = c;
    }
    return OK;
}

Result ddsimGetStateVectorSub(SimulationState* self, size_t subStateSize, const size_t* qubits, Statevector* output) {
    auto ddsim = reinterpret_cast<DDSimulationState*>(self);
    Statevector fullState;
    fullState.numQubits = ddsim->qc->getNqubits();
    fullState.numStates = 1 << fullState.numQubits;
    fullState.amplitudes = new Complex[fullState.numStates];
    self->getStateVectorFull(self, &fullState);

    for(size_t i = 0; i < output->numStates; i++) {
        output->amplitudes[i].real = 0;
        output->amplitudes[i].imaginary = 0;
    }

    for(size_t i = 0; i < fullState.numStates; i++) {
        size_t outputIndex = 0;
        for(size_t j = 0; j < subStateSize; j++) {
            outputIndex |= ((i >> qubits[j]) & 1) << j;
        }
        output->amplitudes[outputIndex].real += fullState.amplitudes[i].real;
        output->amplitudes[outputIndex].imaginary += fullState.amplitudes[i].imaginary;
    }

    free(fullState.amplitudes);
    return OK;
}


Result destroyDDSimulationState([[maybe_unused]] DDSimulationState* self) {
    return OK;
}

//-----------------------------------------------------------------------------------------

std::string trim(const std::string& str) {
    auto start = std::find_if_not(str.begin(), str.end(), ::isspace);
    auto end = std::find_if_not(str.rbegin(), str.rend(), ::isspace).base();
    return (start < end) ? std::string(start, end) : std::string();
}

bool startsWith(const std::string& str, const std::string& prefix) {
    return str.compare(0, prefix.size(), prefix) == 0;
}

std::string replaceAll(std::string str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
    }
    return str;
}

std::vector<std::string> split(std::string& text, char delimiter) {
    std::vector<std::string> result;
    std::istringstream iss(text);
    for(std::string s; std::getline(iss, s, delimiter); )
        result.push_back(s);
    return result;
}

size_t variableToQubit(DDSimulationState* ddsim, std::string& variable) {
    auto declaration = replaceAll(variable, " ", "");
    declaration = replaceAll(declaration, "\t", "");
    auto parts = split(declaration, '[');
    auto var = parts[0];
    size_t idx = std::stoul(parts[1].substr(0, parts[1].size() - 1));

    for(auto& reg : ddsim->qubitRegisters) {
        if(reg.name == var) {
            if(idx >= reg.size)
                throw std::runtime_error("Index out of bounds");
            return reg.index + idx;
        }
    }
    throw std::runtime_error("Unknown variable name " + var);
}

double complexMagnitude(Complex& c) {
    return std::sqrt(c.real * c.real + c.imaginary * c.imaginary);
}

bool areQubitsEntangled(Statevector* sv) {
    double epsilon = 0.0001;
    bool canBe00 = complexMagnitude(sv->amplitudes[0]) > epsilon;
    bool canBe01 = complexMagnitude(sv->amplitudes[1]) > epsilon;
    bool canBe10 = complexMagnitude(sv->amplitudes[2]) > epsilon;
    bool canBe11 = complexMagnitude(sv->amplitudes[3]) > epsilon;

    return (canBe00 && canBe11 && !(canBe01 && canBe10)) ||
        (canBe01 && canBe10 && !(canBe00 && canBe11));
}

bool checkAssertionEntangled(DDSimulationState* ddsim, std::string& assertion) {
    std::string expression = replaceAll(assertion, "assert-ent ", "");
    expression = replaceAll(expression, " ", "");
    expression = replaceAll(expression, "\t", "");
    std::vector<std::string> variables = split(expression, ',');
    std::vector<size_t> qubits;
    for(auto variable : variables) {
        qubits.push_back(variableToQubit(ddsim, variable));
    }

    Statevector sv;
    sv.numQubits = 2;
    sv.numStates = 4;
    sv.amplitudes = new Complex[4];
    size_t qubitsStep[] = {0, 0};

    bool result = true;
    for(size_t i = 0; i < qubits.size() && result; i++) {
        for(size_t j = 0; j < qubits.size(); j++) {
            if(i == j)
                continue;
            qubitsStep[0] = qubits[i];
            qubitsStep[1] = qubits[j];
            ddsim->interface.getStateVectorSub(&ddsim->interface, 2, qubitsStep, &sv);
            if(!areQubitsEntangled(&sv)) {
                result = false;
                break;
            }
        }
    }

    free(sv.amplitudes);
    return result;
}

bool checkAssertionSuperposition(DDSimulationState* ddsim, std::string& assertion) {
    std::string expression = replaceAll(assertion, "assert-sup ", "");
    expression = replaceAll(expression, " ", "");
    expression = replaceAll(expression, "\t", "");
    std::vector<std::string> variables = split(expression, ',');
    std::vector<size_t> qubits;
    for(auto variable : variables) {
        qubits.push_back(variableToQubit(ddsim, variable));
    }

    Statevector sv;
    sv.numQubits = qubits.size();
    sv.numStates = 1 << sv.numQubits;
    sv.amplitudes = new Complex[sv.numStates];

    ddsim->interface.getStateVectorSub(&ddsim->interface, sv.numQubits, qubits.data(), &sv);

    int found = 0;
    double epsilon = 0.00000001;
    for(size_t i = 0; i < sv.numStates && found < 2; i++) {
        if(complexMagnitude(sv.amplitudes[i]) > epsilon) {
            found++;
        }
    }

    free(sv.amplitudes);
    return found > 1;
}

bool checkAssertion(DDSimulationState* ddsim, std::string& assertion) {
    assertion = trim(assertion);
    if(startsWith(assertion, "assert-ent ")) {
        return checkAssertionEntangled(ddsim, assertion);
    }
    else if(startsWith(assertion, "assert-sup ")) {
        return checkAssertionSuperposition(ddsim, assertion);
    }
    else if(startsWith(assertion, "assert ")) {
        return assertion.size() == 3; // TODO
    }
    else {
        return false;
    }
}

std::string preprocessAssertionCode(const char* code, DDSimulationState* ddsim) {
    std::vector<std::string> lines;
    std::string token;
    std::istringstream tokenStream(code);
    while (std::getline(tokenStream, token, ';')) {
        lines.push_back(token);
    }

    std::vector<std::string> correctLines;
    size_t i = 0;
    for (const auto& line : lines) {
        if (line.find("qreg") != std::string::npos) {
            auto declaration = replaceAll(line, "qreg", "");
            declaration = replaceAll(declaration, " ", "");
            declaration = replaceAll(declaration, "\t", "");
            declaration = replaceAll(declaration, ";", "");
            auto parts = split(declaration, '[');
            auto name = parts[0];
            size_t size = std::stoul(parts[1].substr(0, parts[1].size() - 1));

            size_t index = ddsim->qubitRegisters.empty() ? 0 : ddsim->qubitRegisters.back().index + ddsim->qubitRegisters.back().size;
            QubitRegisterDefinition reg{name, index, size};
            ddsim->qubitRegisters.push_back(reg);

            correctLines.push_back(line);
            ddsim->instructionTypes.push_back(NOP);
        }
        else if (line.find("creg") != std::string::npos) {
            correctLines.push_back(line);
            ddsim->instructionTypes.push_back(NOP);
        }
        else if (line.find("assert") != std::string::npos) {
            ddsim->instructionTypes.push_back(ASSERTION);
            ddsim->assertionInstructions.insert({i, line});
        }
        else {
            correctLines.push_back(line);
            ddsim->instructionTypes.push_back(SIMULATE);
        }
        i++;
    }

    return std::accumulate(correctLines.begin(), correctLines.end(), std::string(),
                           [](const std::string& a, const std::string& b) {
                               return a + b + ";";
                           });
}