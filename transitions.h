#pragma once

#include <functional>
#include <optional>
#include <string_view>

#include "led-matrix.h"

enum class TransitionMode {
  None,
  Alternate,  // Cycle through all transition styles in order
  OrbitDots,
  Pulse,
};

std::optional<TransitionMode> ParseTransitionMode(std::string_view name);

// Plays the next transition for the given mode on the matrix. `canvas` is
// updated to the current offscreen canvas after each SwapOnVSync. Returns
// early when `should_stop` returns true.
void RunTransition(TransitionMode mode, rgb_matrix::RGBMatrix *matrix,
                   rgb_matrix::FrameCanvas *&canvas,
                   const std::function<bool()> &should_stop);
