#pragma once

#include "AssertionParsing.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

struct Block {
  bool valid;
  std::string code;
};

struct Instruction {
  size_t lineNumber;
  std::string code;
  std::unique_ptr<Assertion> assertion;
  std::set<std::string> targets;

  size_t originalCodeStartPosition;
  size_t originalCodeEndPosition;

  size_t successorIndex;

  bool isFunctionCall;
  std::string calledFunction;
  bool inFunctionDefinition;
  bool isFunctionDefition;

  std::map<std::string, std::string> callSubstitution;

  std::vector<size_t> dataDependencies;

  Block block;
  std::vector<size_t> childInstructions;
  Instruction(size_t inputLineNumber, std::string inputCode,
              std::unique_ptr<Assertion>& inputAssertion,
              std::set<std::string> inputTargets, size_t startPos,
              size_t endPos, size_t successor, bool isFuncCall,
              std::string function, bool inFuncDef, bool isFuncDef,
              Block inputBlock);
};

struct FunctionDefinition {
  std::string name;
  std::vector<std::string> parameters;
};

std::vector<Instruction> preprocessCode(const std::string& code,
                                        std::string& processedCode);
std::vector<Instruction> preprocessCode(
    const std::string& code, size_t startIndex, size_t initialCodeOffset,
    const std::vector<std::string>& functionNames, std::string& processedCode);
