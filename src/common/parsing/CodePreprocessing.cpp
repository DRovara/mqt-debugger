/**
 * @file CodePreprocessing.cpp
 * @brief Implementation of the code preprocessing functionality.
 */

#include "common/parsing/CodePreprocessing.hpp"

#include "common/parsing/AssertionParsing.hpp"
#include "common/parsing/ParsingError.hpp"
#include "common/parsing/Utils.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

Instruction::Instruction(size_t inputLineNumber, std::string inputCode,
                         std::unique_ptr<Assertion>& inputAssertion,
                         std::vector<std::string> inputTargets, size_t startPos,
                         size_t endPos, size_t successor, bool isFuncCall,
                         std::string function, bool inFuncDef, bool isFuncDef,
                         Block inputBlock)
    : lineNumber(inputLineNumber), code(std::move(inputCode)),
      assertion(std::move(inputAssertion)), targets(std::move(inputTargets)),
      originalCodeStartPosition(startPos), originalCodeEndPosition(endPos),
      successorIndex(successor), isFunctionCall(isFuncCall),
      calledFunction(std::move(function)), inFunctionDefinition(inFuncDef),
      isFunctionDefinition(isFuncDef), block(std::move(inputBlock)) {}

/**
 * @brief Sweep a given code string for blocks and replace them with a unique
 * identifier.
 * @param code The code to sweep.
 * @param blocks A map to store the blocks and their respective identifiers in.
 * @return The code with the blocks replaced by their identifiers.
 */
std::string sweepBlocks(const std::string& code,
                        std::map<std::string, std::string>& blocks) {
  std::string result = code;
  size_t start = 0;
  int level = 0;
  size_t pos = 0;
  while (pos < result.size()) {
    auto c = result[pos];
    if (c == '{') {
      if (level == 0) {
        start = pos;
      }
      level++;
    } else if (c == '}') {
      level--;
      if (level == 0) {
        const std::string block = result.substr(start + 1, pos - 1 - start);
        const std::string blockName =
            "$__block" + std::to_string(blocks.size()) + "$;";
        blocks[blockName] = block;
        result.replace(start, pos + 1 - start, blockName);
        pos = start;
      }
    }
    pos++;
  }
  return result;
}

/**
 * @brief Sweep a given code string for comments and replace them with
 * whitespace.
 * @param code The code to sweep.
 * @return The code with the comments replaced by whitespace.
 */
std::string removeComments(const std::string& code) {
  std::string result = code;
  for (size_t pos = 0; pos < result.size(); pos++) {
    auto nextComment = result.find("//", pos);
    if (nextComment == std::string::npos) {
      break;
    }
    auto commentEnd = result.find('\n', nextComment);
    if (commentEnd == std::string::npos) {
      commentEnd = result.size();
    }
    const std::string spaces(commentEnd - nextComment, ' ');
    result.replace(nextComment, commentEnd - nextComment, spaces);
  }
  return result;
}

/**
 * @brief Check if a given line is a function definition.
 *
 * This is done by checking if it starts with `gate `.
 * @param line The line to check.
 * @return True if the line is a function definition, false otherwise.
 */
bool isFunctionDefinition(const std::string& line) {
  return startsWith(trim(line), "gate ");
}

/**
 * @brief Check if a given line is a classic controlled gate.
 *
 * This is done by checking if it starts with `if` and contains parentheses.
 * @param line The line to check.
 * @return True if the line is a classic controlled gate, false otherwise.
 */
bool isClassicControlledGate(const std::string& line) {
  return startsWith(trim(line), "if") &&
         (line.find('(') != std::string::npos) &&
         (line.find(')') != std::string::npos);
}

/**
 * @brief Check if a given line is a variable declaration.
 *
 * This is done by checking if it contains `creg ` or `qreg `.
 * @param line The line to check.
 * @return True if the line is a variable declaration, false otherwise.
 */
bool isVariableDeclaration(const std::string& line) {
  return startsWith(trim(line), "creg ") || startsWith(trim(line), "qreg ");
}

/**
 * @brief Parse a function definition from a given signature.
 * @param signature The signature to parse.
 * @return The parsed function definition.
 */
FunctionDefinition parseFunctionDefinition(const std::string& signature) {
  auto parts = splitString(
      replaceString(replaceString(signature, "\n", " "), "\t", " "), ' ');
  std::string name;
  size_t index = 0;
  for (auto& part : parts) {
    index++;
    if (part != "gate" && !part.empty()) {
      name = part;
      break;
    }
  }

  std::string parameterParts;
  for (size_t i = index; i < parts.size(); i++) {
    parameterParts += parts[i];
  }
  auto parameters = splitString(removeWhitespace(parameterParts), ',');

  return {name, parameters};
}

/**
 * @brief Parse the parameters or arguments from a given instruction.
 * @param instruction The instruction to parse.
 * @return A vector containing the parsed parameters.
 */
std::vector<std::string> parseParameters(const std::string& instruction) {
  if (isFunctionDefinition(instruction)) {
    const auto fd = parseFunctionDefinition(instruction);
    return fd.parameters;
  }

  if (instruction.find("->") != std::string::npos) {
    // We only add the quantum variable to the measurement's targets.
    return parseParameters(splitString(instruction, '-')[0]);
  }

  if (isClassicControlledGate(instruction)) {
    const auto end = instruction.find(')');

    return parseParameters(
        instruction.substr(end + 1, instruction.length() - end - 1));
  }

  auto parts = splitString(
      replaceString(
          replaceString(replaceString(instruction, ";", " "), "\n", " "), "\t",
          " "),
      ' ');
  size_t index = 0;
  size_t openBrackets = 0;
  for (auto& part : parts) {
    index++;
    openBrackets +=
        static_cast<size_t>(std::count(part.begin(), part.end(), '('));
    openBrackets -=
        static_cast<size_t>(std::count(part.begin(), part.end(), ')'));
    if (!part.empty() && openBrackets == 0) {
      break;
    }
  }

  std::string parameterParts;
  for (size_t i = index; i < parts.size(); i++) {
    if (parts[i].empty()) {
      continue;
    }
    parameterParts += parts[i];
  }
  auto parameters = splitString(removeWhitespace(parameterParts), ',');
  if (parameters.size() == 1 && parameters[0].empty()) {
    return {};
  }
  return parameters;
}

/**
 * @brief Sweep a given code string for function names.
 * @param code The code to sweep.
 * @return A vector containing the function names.
 */
std::vector<std::string> sweepFunctionNames(const std::string& code) {
  std::vector<std::string> result;
  const std::vector<char> delimiters{';', '}'};
  const auto instructions = splitString(code, delimiters);
  for (const auto& instruction : instructions) {
    if (isFunctionDefinition(instruction)) {
      const auto f = parseFunctionDefinition(instruction);
      result.push_back(f.name);
    }
  }
  return result;
}

/**
 * @brief Unfold the targets of assertions that otherwise target full registers.
 *
 * E.g. `assert-ent q` can be unfolded into `assert-ent q[0], q[1], q[2],
 * ...`.\n `shadowedRegisters` contains all registers that are shadowed by
 * function-local variables and should therefore not be unfolded.
 * @param assertion The assertion to unfold.
 * @param definedRegisters The defined registers in the code.
 * @param shadowedRegisters The shadowed registers in the code.
 */
void unfoldAssertionTargetRegisters(
    Assertion& assertion, const std::map<std::string, size_t>& definedRegisters,
    const std::vector<std::string>& shadowedRegisters) {
  bool found = false;
  std::vector<std::string> targets;
  for (const auto& target : assertion.getTargetQubits()) {
    if (std::find(shadowedRegisters.begin(), shadowedRegisters.end(), target) !=
        shadowedRegisters.end()) {
      targets.push_back(target);
      continue;
    }
    if (definedRegisters.find(target) != definedRegisters.end()) {
      for (size_t i = 0; i < definedRegisters.at(target); i++) {
        targets.push_back(target + "[" + std::to_string(i) + "]");
      }
      found = true;
    } else {
      targets.push_back(target);
    }
  }

  if (found) {
    assertion.setTargetQubits(targets);
  }
}

std::vector<Instruction> preprocessCode(const std::string& code,
                                        std::string& processedCode) {
  std::map<std::string, size_t> definedRegisters;
  return preprocessCode(code, 0, 0, {}, definedRegisters, {}, processedCode);
}

std::vector<Instruction>
preprocessCode(const std::string& code, size_t startIndex,
               size_t initialCodeOffset,
               const std::vector<std::string>& allFunctionNames,
               std::map<std::string, size_t>& definedRegisters,
               const std::vector<std::string>& shadowedRegisters,
               std::string& processedCode) {

  std::map<std::string, std::string> blocks;
  std::map<std::string, size_t> functionFirstLine;
  std::map<std::string, FunctionDefinition> functionDefinitions;
  std::map<size_t, std::vector<std::string>> variableUsages;

  processedCode = removeComments(code);
  const std::string blocksRemoved = sweepBlocks(processedCode, blocks);
  std::vector<std::string> functionNames = sweepFunctionNames(processedCode);
  for (const auto& name : allFunctionNames) {
    functionNames.push_back(name);
  }

  std::vector<Instruction> instructions;

  size_t pos = 0;
  size_t i = startIndex;
  size_t blocksOffset = initialCodeOffset;

  while (pos != std::string::npos) {
    const size_t end = blocksRemoved.find(';', pos);
    if (end == std::string::npos) {
      break;
    }

    std::string line = blocksRemoved.substr(pos, end - pos + 1);
    auto trimmedLine = trim(line);
    auto tokens = splitString(trimmedLine, ' ');
    auto isAssert = isAssertion(line);
    auto blockPos = line.find("$__block");

    const size_t trueStart = pos + blocksOffset;

    Block block{false, ""};
    if (blockPos != std::string::npos) {
      const auto endPos = line.find('$', blockPos + 1) + 1;
      const auto blockName = line.substr(blockPos, endPos - blockPos + 1);
      const auto blockContent = blocks[blockName];

      // in the actual code, the current instruction is longer, because we
      // replaced the block with its name. Also, we add +2 because the block
      // also had `{` and `}`, which is not included in `blockContent`.
      blocksOffset += blockContent.size() + 2 - blockName.size();
      block.code = blockContent;
      block.valid = true;
      line.replace(blockPos, endPos - blockPos + 1, "");
    }

    const auto targets = parseParameters(line);

    const size_t trueEnd = end + blocksOffset;

    if (isVariableDeclaration(line)) {
      const auto declaration = removeWhitespace(
          replaceString(replaceString(trimmedLine, "creg", ""), "qreg", ""));
      const auto parts = splitString(declaration, {'[', ']'});
      const auto& name = parts[0];
      const auto size = std::stoi(parts[1]);
      definedRegisters.insert({name, size});
    }

    if (isFunctionDefinition(line)) {
      if (!block.valid) {
        throw ParsingError("Gate definitions require a body block");
      }
      const auto f = parseFunctionDefinition(line);
      functionDefinitions.insert({f.name, f});
      i++;
      std::string processedSubCode;
      auto subInstructions = preprocessCode(
          block.code, i, code.find('{', trueStart) + 1, functionNames,
          definedRegisters, f.parameters, processedSubCode);
      for (auto& instr : subInstructions) {
        instr.inFunctionDefinition = true;
      }
      functionFirstLine.insert({f.name, subInstructions[0].lineNumber});
      // successor 0 means "pop call stack"
      i += subInstructions.size();

      std::unique_ptr<Assertion> a(nullptr);
      instructions.emplace_back(i - subInstructions.size() - 1, line, a,
                                targets, trueStart, trueEnd, i + 1, false, "",
                                false, true, block);
      for (auto& instr : subInstructions) {
        instructions.back().childInstructions.push_back(instr.lineNumber);
      }
      instructions.insert(instructions.end(),
                          std::make_move_iterator(subInstructions.begin()),
                          std::make_move_iterator(subInstructions.end()));

      const auto closingBrace = code.find(
          '}', instructions[instructions.size() - 1].originalCodeEndPosition);
      const Block noBlock{false, ""};
      instructions.emplace_back(i, "RETURN", a, targets, closingBrace,
                                closingBrace, 0, false, "", true, false,
                                noBlock);
      i++;
      pos = end + 1;

      continue;
    }

    bool isFunctionCall = false;
    std::string calledFunction;
    if (!tokens.empty() && std::find(functionNames.begin(), functionNames.end(),
                                     tokens[0]) != functionNames.end()) {
      isFunctionCall = true;
      calledFunction = tokens[0];
    }

    if (isAssert) {
      auto a = parseAssertion(line, block.code);
      unfoldAssertionTargetRegisters(*a, definedRegisters, shadowedRegisters);
      a->validate();
      instructions.emplace_back(i, line, a, targets, trueStart, trueEnd, i + 1,
                                isFunctionCall, calledFunction, false, false,
                                block);
    } else {
      std::unique_ptr<Assertion> a(nullptr);
      instructions.emplace_back(i, line, a, targets, trueStart, trueEnd, i + 1,
                                isFunctionCall, calledFunction, false, false,
                                block);

      variableUsages.insert({i, parseParameters(line)});
    }

    i++;
    pos = end + 1;
  }

  for (auto& instr : instructions) {
    auto vars = parseParameters(instr.code);
    size_t idx = instr.lineNumber - 1;
    while (instr.lineNumber != 0 && !vars.empty() &&
           (instr.lineNumber < instructions.size() ||
            idx > instr.lineNumber - instructions.size())) {
      size_t foundIndex = 0;
      for (const auto& var : variableUsages[idx]) {
        const auto found =
            std::find_if(vars.begin(), vars.end(), [&var](const auto& v) {
              return variablesEqual(v, var);
            });
        if (found != vars.end()) {
          const auto newEnd = std::remove(vars.begin(), vars.end(), var);
          vars.erase(newEnd, vars.end());
          instr.dataDependencies.emplace_back(idx, foundIndex);
        }
        foundIndex++;
      }
      if (idx - 1 == instr.lineNumber - instructions.size() || idx == 0) {
        break;
      }
      idx--;
    }
    if (instr.isFunctionCall) {
      instr.successorIndex = functionFirstLine[instr.calledFunction];
      if (functionDefinitions.find(instr.calledFunction) ==
          functionDefinitions.end()) {
        continue;
      }
      instr.callSubstitution.clear();
      const auto func = functionDefinitions[instr.calledFunction];
      const auto arguments = parseParameters(instr.code);
      if (func.parameters.size() != arguments.size()) {
        throw ParsingError(
            "Custom gate call uses incorrect number of arguments.");
      }
      for (size_t j = 0; j < func.parameters.size(); j++) {
        instr.callSubstitution.insert({func.parameters[j], arguments[j]});
      }
    }
  }

  return instructions;
}
