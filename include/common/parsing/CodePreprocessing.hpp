#pragma once

#include "AssertionParsing.hpp"

#include <map>
#include <string>

struct Block {
  bool valid;
  std::string code;
};

struct Instruction {
  size_t lineNumber;
  std::string code;
  std::unique_ptr<Assertion> assertion;

  size_t originalCodeStartPosition;
  size_t originalCodeEndPosition;

  size_t successorIndex;

  bool isFunctionCall;
  std::string calledFunction;
  bool inFunctionDefinition;

  std::map<std::string, std::string> callSubstitution;

  std::vector<size_t> dataDependencies;

  Block block;
  std::vector<size_t> childInstructions;
  Instruction(size_t lineNumber, std::string code,
              std::unique_ptr<Assertion>& assertion,
              size_t originalCodeStartPosition, size_t originalCodeEndPosition,
              size_t successorIndex, bool isFunctionCall,
              std::string calledFunction, bool inFunctionDefinition,
              Block block);
};

struct FunctionDefinition {
  std::string name;
  std::vector<std::string> parameters;
};

std::vector<Instruction> preprocessCode(const std::string& code);
std::vector<Instruction>
preprocessCode(const std::string& code, size_t startIndex,
               size_t initialCodeOffset,
               const std::vector<std::string>& functionNames);
