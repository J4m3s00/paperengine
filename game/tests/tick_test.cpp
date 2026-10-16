#include <doctest/doctest.h>

#include "game/tick.h"

TEST_CASE("server tick duration matches the tick rate") {
  CHECK(Paper::kServerTickSeconds * Paper::kServerTickRate == doctest::Approx(1.0));
}
