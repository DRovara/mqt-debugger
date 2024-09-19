#include "common/Span.hpp"
#include "common/parsing/AssertionParsing.hpp"
#include "common/parsing/CodePreprocessing.hpp"
#include "common/parsing/ParsingError.hpp"

#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <string>

class ParsingTest : public testing::Test {
  void SetUp() override {}
};

TEST_F(ParsingTest, EqualityAssertion) {
  // With statevector
  const auto a1 = parseAssertion("assert-eq 0.5, q[0]", "1, 0");
  ASSERT_EQ(a1->getType(), AssertionType::StatevectorEquality);
  ASSERT_EQ(a1->getTargetQubits().size(), 1);
  ASSERT_EQ(a1->getTargetQubits()[0], "q[0]");
  const auto* sv = dynamic_cast<StatevectorEqualityAssertion*>(a1.get());
  ASSERT_EQ(sv->getSimilarityThreshold(), 0.5);
  ASSERT_EQ(sv->getTargetStatevector().numQubits, 1);
  ASSERT_EQ(sv->getTargetStatevector().numStates, 2);
  ASSERT_EQ(sv->getTargetStatevector().amplitudes->real, 1);

  // With circuit
  const auto a2 = parseAssertion("assert-eq 0.5, q[0]", "qreg q[1]; h q[0];");
  ASSERT_EQ(a2->getType(), AssertionType::CircuitEquality);
  ASSERT_EQ(a2->getTargetQubits().size(), 1);
  ASSERT_EQ(a2->getTargetQubits()[0], "q[0]");
  const auto* c = dynamic_cast<CircuitEqualityAssertion*>(a2.get());
  ASSERT_EQ(c->getSimilarityThreshold(), 0.5);
  ASSERT_EQ(c->getCircuitCode(), "qreg q[1]; h q[0];");
}

TEST_F(ParsingTest, EntanglementAssertion) {
  const auto a = parseAssertion("assert-ent q[0], q[1]", "");
  ASSERT_EQ(a->getType(), AssertionType::Entanglement);
  ASSERT_EQ(a->getTargetQubits().size(), 2);
  ASSERT_EQ(a->getTargetQubits()[0], "q[0]");
  ASSERT_EQ(a->getTargetQubits()[1], "q[1]");
}

TEST_F(ParsingTest, SuperpositionAssertion) {
  const auto a = parseAssertion("assert-sup q[0], q[1]", "");
  ASSERT_EQ(a->getType(), AssertionType::Superposition);
  ASSERT_EQ(a->getTargetQubits().size(), 2);
  ASSERT_EQ(a->getTargetQubits()[0], "q[0]");
  ASSERT_EQ(a->getTargetQubits()[1], "q[1]");
}

TEST_F(ParsingTest, ErrorStatevectorEqualityAssertion) {
  ASSERT_THROW(parseAssertion("assert-eq 1.5, q[0]", "1, 0")->validate(),
               ParsingError);
  ASSERT_THROW(parseAssertion("assert-eq 0.5, q[0]", "1, 0, 0")->validate(),
               ParsingError);
  ASSERT_THROW(parseAssertion("assert-eq 0.5, q[0]", "1, 0, 0, 0")->validate(),
               ParsingError);
}

TEST_F(ParsingTest, ErrorCircuitEqualityAssertion) {
  ASSERT_THROW(
      parseAssertion("assert-eq 1.5, q[0]", "qreg q[1]; h q[0];")->validate(),
      ParsingError);
}

TEST_F(ParsingTest, ErrorInvalidAssertion) {
  ASSERT_THROW(parseAssertion("assert-fake q[0]", ""), ParsingError);
}

TEST_F(ParsingTest, ComplexNumberParsing) {
  // With statevector
  const auto a1 = parseAssertion("assert-eq 0.5, q[0], q[1]",
                                 "0.5j, 0.5i, 0.5 + 0i, 0 + 0.5j");
  ASSERT_EQ(a1->getType(), AssertionType::StatevectorEquality);
  ASSERT_EQ(a1->getTargetQubits().size(), 2);
  const auto* sv = dynamic_cast<StatevectorEqualityAssertion*>(a1.get());
  const Span<Complex> amplitudes(sv->getTargetStatevector().amplitudes,
                                 sv->getTargetStatevector().numStates);
  for (size_t i = 0; i < 4; i++) {
    ASSERT_EQ(amplitudes[i].real, i != 2 ? 0.0 : 0.5);
    ASSERT_EQ(amplitudes[i].imaginary, i != 2 ? 0.5 : 0.0);
  }
}

TEST_F(ParsingTest, ComplexNumberParsingErrorBadSimilarity) {
  // Similarity > 1
  ASSERT_THROW(parseAssertion("assert-eq 2, q[0]", "1, 0")->validate(),
               ParsingError);

  // Similarity out of double range
  ASSERT_THROW(parseAssertion("assert-eq 1e500, q[0]", "1, 0")->validate(),
               ParsingError);
}

TEST_F(ParsingTest, BadGateDefinition) {
  const std::string input = "gate my_gate q0;";
  std::string output;
  ASSERT_THROW(preprocessCode(input, output), ParsingError);
}

TEST_F(ParsingTest, BadFunctionCall) {
  const std::string input =
      "gate my_gate q0, q1 { h q0; h q1 } qreg q[3]; my_gate q[0];";
  std::string output;
  ASSERT_THROW(preprocessCode(input, output), ParsingError);
  const std::string input2 =
      "gate my_gate q0, q1 { h q0; h q1 } qreg q[3]; my_gate q[0], q[1], q[2];";
  ASSERT_THROW(preprocessCode(input2, output), ParsingError);
}
