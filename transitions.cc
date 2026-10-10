#include "transitions.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <thread>

using rgb_matrix::FrameCanvas;
using rgb_matrix::RGBMatrix;

namespace {

std::mt19937 &Rng() {
  static std::mt19937 rng{std::random_device{}()};
  return rng;
}

float RandomHue() {
  return std::uniform_real_distribution<float>(0.0f, 360.0f)(Rng());
}

void HSVtoRGB(float h, float s, float v, uint8_t &r, uint8_t &g, uint8_t &b) {
  float c = v * s;
  float x = c * (1 - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1));
  float m = v - c;
  float r1, g1, b1;

  if (h < 60) {
    r1 = c; g1 = x; b1 = 0;
  } else if (h < 120) {
    r1 = x; g1 = c; b1 = 0;
  } else if (h < 180) {
    r1 = 0; g1 = c; b1 = x;
  } else if (h < 240) {
    r1 = 0; g1 = x; b1 = c;
  } else if (h < 300) {
    r1 = x; g1 = 0; b1 = c;
  } else {
    r1 = c; g1 = 0; b1 = x;
  }

  r = static_cast<uint8_t>((r1 + m) * 255);
  g = static_cast<uint8_t>((g1 + m) * 255);
  b = static_cast<uint8_t>((b1 + m) * 255);
}

// Four rainbow dots orbiting the center, easing in and out each cycle.
void TransitionOrbitDots(RGBMatrix *matrix, FrameCanvas *&canvas,
                         const std::function<bool()> &should_stop) {
  const int width = canvas->width();
  const int height = canvas->height();
  const int center_x = width / 2;
  const int center_y = height / 2;
  const int radius = std::min(center_x, center_y) - 1;
  constexpr int kDotCount = 4;
  constexpr int kCycles = 3;
  constexpr int kFramesPerCycle = 72;

  float angles[kDotCount];
  for (int i = 0; i < kDotCount; ++i) {
    angles[i] = static_cast<float>(i) / kDotCount * 2.0f * M_PI;
  }

  const float base_hue = RandomHue();

  for (int cycle = 0; cycle < kCycles; ++cycle) {
    for (int frame = 0; frame < kFramesPerCycle; ++frame) {
      if (should_stop()) {
        return;
      }
      float progress = static_cast<float>(frame) / kFramesPerCycle;
      float easing = std::cos(progress * M_PI);  // slows them near middle
      float base_speed = (1.0f - easing) * 0.15f + 0.015f;

      canvas->Clear();

      for (int i = 0; i < kDotCount; ++i) {
        angles[i] += base_speed * (1.0f + i * 0.05f);

        float hue = std::fmod(base_hue + frame * 2 + i * 30, 360.0f);
        uint8_t r, g, b;
        HSVtoRGB(hue, 1.0f, 1.0f, r, g, b);

        int x = static_cast<int>(center_x + std::cos(angles[i]) * radius);
        int y = static_cast<int>(center_y + std::sin(angles[i]) * radius);

        // 3x3 dot with a simple brightness falloff from the center
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            int px = x + dx;
            int py = y + dy;
            if (px < 0 || px >= width || py < 0 || py >= height) {
              continue;
            }
            float falloff = 1.0f - 0.25f * (std::abs(dx) + std::abs(dy));
            canvas->SetPixel(px, py, static_cast<uint8_t>(r * falloff),
                             static_cast<uint8_t>(g * falloff),
                             static_cast<uint8_t>(b * falloff));
          }
        }
      }

      canvas = matrix->SwapOnVSync(canvas);
      std::this_thread::sleep_for(std::chrono::milliseconds(22));  // ~45fps
    }
  }
}

// Expanding and contracting rainbow rings from the center.
void TransitionPulse(RGBMatrix *matrix, FrameCanvas *&canvas,
                     const std::function<bool()> &should_stop) {
  const int width = canvas->width();
  const int height = canvas->height();
  const int center_x = width / 2;
  const int center_y = height / 2;
  const int max_radius = std::max(center_x, center_y);
  constexpr int kFadeWidth = 6;
  constexpr int kPulseCount = 4;
  constexpr int kFramesPerPulse = 24;
  constexpr int kTotalFrames = kPulseCount * kFramesPerPulse;

  const float base_hue = RandomHue();

  for (int frame = 0; frame < kTotalFrames; ++frame) {
    if (should_stop()) {
      return;
    }
    float pulse_progress = static_cast<float>(frame % kFramesPerPulse) /
                           (kFramesPerPulse - 1);
    int radius = static_cast<int>(std::sin(pulse_progress * M_PI) * max_radius);

    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        int dx = x - center_x;
        int dy = y - center_y;
        int dist = static_cast<int>(std::sqrt(dx * dx + dy * dy));

        if (dist <= radius && dist >= radius - kFadeWidth) {
          float alpha = 1.0f - static_cast<float>(radius - dist) / kFadeWidth;
          // Angle around the center gives a subtle hue gradient
          float hue_offset = (std::atan2(dy, dx) + M_PI) / (2 * M_PI);
          float hue = std::fmod(base_hue + hue_offset * 60, 360.0f);

          uint8_t r, g, b;
          HSVtoRGB(hue, 1.0f, alpha, r, g, b);
          canvas->SetPixel(x, y, r, g, b);
        } else {
          canvas->SetPixel(x, y, 0, 0, 0);
        }
      }
    }

    canvas = matrix->SwapOnVSync(canvas);
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
}

}  // namespace

std::optional<TransitionMode> ParseTransitionMode(std::string_view name) {
  if (name == "none") return TransitionMode::None;
  if (name == "alternate") return TransitionMode::Alternate;
  if (name == "orbit") return TransitionMode::OrbitDots;
  if (name == "pulse") return TransitionMode::Pulse;
  return std::nullopt;
}

void RunTransition(TransitionMode mode, RGBMatrix *matrix,
                   FrameCanvas *&canvas,
                   const std::function<bool()> &should_stop) {
  static int alternate_index = 0;
  if (mode == TransitionMode::Alternate) {
    mode = (alternate_index++ % 2 == 0) ? TransitionMode::OrbitDots
                                        : TransitionMode::Pulse;
  }

  switch (mode) {
    case TransitionMode::OrbitDots:
      TransitionOrbitDots(matrix, canvas, should_stop);
      break;
    case TransitionMode::Pulse:
      TransitionPulse(matrix, canvas, should_stop);
      break;
    case TransitionMode::None:
    case TransitionMode::Alternate:
      break;
  }
}
