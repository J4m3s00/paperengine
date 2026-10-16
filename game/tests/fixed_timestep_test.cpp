#include <doctest/doctest.h>

#include "base/fixed_timestep.h"

using Paper::FixedTimestep;

namespace {
int drain(FixedTimestep& stepper) {
  int n = 0;
  while (stepper.consumeStep())
    ++n;
  return n;
}
} // namespace

TEST_CASE("no step before a full step has elapsed") {
  FixedTimestep stepper(0.01);
  stepper.advance(0.009);
  CHECK(drain(stepper) == 0);
  CHECK(stepper.alpha() == doctest::Approx(0.9));
}

TEST_CASE("steps accumulate across frames and leave a remainder") {
  FixedTimestep stepper(0.01);
  stepper.advance(0.025);
  CHECK(drain(stepper) == 2);
  CHECK(stepper.tick() == 2);
  CHECK(stepper.alpha() == doctest::Approx(0.5));
  stepper.advance(0.005);
  CHECK(drain(stepper) == 1);
  CHECK(stepper.alpha() == doctest::Approx(0.0).epsilon(1e-9));
}

TEST_CASE("a long stall is clamped to maxSteps") {
  FixedTimestep stepper(0.01, 4);
  stepper.advance(5.0);
  CHECK(drain(stepper) == 4);
  CHECK(stepper.alpha() < 1.0);
}

TEST_CASE("negative elapsed time is ignored") {
  FixedTimestep stepper(0.01);
  stepper.advance(-1.0);
  CHECK(drain(stepper) == 0);
}
