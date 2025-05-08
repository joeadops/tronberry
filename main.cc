#define WEBP_EXTERN extern
#include <iostream>
#include <string>
#include <thread>
#include <fstream>
#include <sstream>
#include <chrono>
#include <webp/demux.h>
#include <webp/decode.h>
#include <webp/mux.h>
#include "httplib.h"
#include "led-matrix.h"
#include "graphics.h" // for rgb_matrix::DrawText, Color, Font
#include <vector>
#include <algorithm>  // for std::shuffle
#include <random>     // for std::mt19937
#include <cstdlib> // for rand()
#include "startup.h"
#include <cmath>
#include <ctime>


inline uint8_t ApplyGamma(uint8_t color, float gamma = 2.2f) {
    return static_cast<uint8_t>(pow(color / 255.0f, gamma) * 255.0f);
}


using namespace rgb_matrix;


inline void ProcessPixel(uint8_t in_r, uint8_t in_g, uint8_t in_b, uint8_t alpha,
                         uint8_t& out_r, uint8_t& out_g, uint8_t& out_b,
                         float gamma = 2.2f, float min_floor = 0.07f,
                         float brightness_scale = 1.0f) {
    if ((in_r + in_g + in_b < 20) || alpha < 20) {
        out_r = out_g = out_b = 0;
        return;
    }

    float a = alpha / 255.0f;
    float rf = pow((in_r * a) / 255.0f, gamma);
    float gf = pow((in_g * a) / 255.0f, gamma);
    float bf = pow((in_b * a) / 255.0f, gamma);

    float maxc = std::max({ rf, gf, bf });
    float scale = (maxc < min_floor) ? (min_floor / maxc) : 1.0f;

    rf = std::min(1.0f, rf * scale * brightness_scale);
    gf = std::min(1.0f, gf * scale * brightness_scale);
    bf = std::min(1.0f, bf * scale * brightness_scale);

    out_r = static_cast<uint8_t>(rf * 255.0f);
    out_g = static_cast<uint8_t>(gf * 255.0f);
    out_b = static_cast<uint8_t>(bf * 255.0f);
}

using namespace std::chrono_literals;


enum class TransitionStyle {
  OrbitDots,
  Pulse,
};

static int transition_index = 0;

void ShowStartupSplash(rgb_matrix::RGBMatrix* matrix, rgb_matrix::FrameCanvas* canvas) {
  WebPData webp_data;
  webp_data.bytes = STARTUP_WEBP;
  webp_data.size = STARTUP_WEBP_LEN;

  WebPAnimDecoderOptions dec_options;
  WebPAnimDecoderOptionsInit(&dec_options);
  WebPAnimDecoder* decoder = WebPAnimDecoderNew(&webp_data, &dec_options);
  if (!decoder) {
    std::cerr << "❌ Failed to create decoder for splash\n";
    return;
  }

  WebPAnimInfo anim_info;
  if (!WebPAnimDecoderGetInfo(decoder, &anim_info)) {
    std::cerr << "❌ Failed to get splash animation info\n";
    WebPAnimDecoderDelete(decoder);
    return;
  }

  uint8_t* frame;
  int timestamp, last_timestamp = 0;

  while (WebPAnimDecoderHasMoreFrames(decoder)) {
    if (!WebPAnimDecoderGetNext(decoder, &frame, &timestamp)) break;

    for (uint32_t y = 0; y < anim_info.canvas_height && y < (uint32_t)canvas->height(); ++y) {
      for (uint32_t x = 0; x < anim_info.canvas_width && x < (uint32_t)canvas->width(); ++x) {
        int idx = (y * anim_info.canvas_width + x) * 4;
        
        uint8_t alpha = frame[idx + 3];
        float alpha_f = alpha / 255.0f;

        uint8_t r = ApplyGamma(static_cast<uint8_t>(frame[idx] * alpha_f));
        uint8_t g = ApplyGamma(static_cast<uint8_t>(frame[idx + 1] * alpha_f));
        uint8_t b = ApplyGamma(static_cast<uint8_t>(frame[idx + 2] * alpha_f));

        canvas->SetPixel(x, y, r, g, b);

      }
    }

    canvas = matrix->SwapOnVSync(canvas);
    int delay = timestamp - last_timestamp;
    std::this_thread::sleep_for(std::chrono::milliseconds(delay > 10 ? delay : 10));
    last_timestamp = timestamp;
  }

  WebPAnimDecoderDelete(decoder);
}

void HSVtoRGB(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b) {
  float c = v * s;
  float x = c * (1 - fabs(fmod(h / 60.0, 2) - 1));
  float m = v - c;
  float r1, g1, b1;

  if (h < 60)      { r1 = c; g1 = x; b1 = 0; }
  else if (h < 120){ r1 = x; g1 = c; b1 = 0; }
  else if (h < 180){ r1 = 0; g1 = c; b1 = x; }
  else if (h < 240){ r1 = 0; g1 = x; b1 = c; }
  else if (h < 300){ r1 = x; g1 = 0; b1 = c; }
  else             { r1 = c; g1 = 0; b1 = x; }

  r = static_cast<uint8_t>((r1 + m) * 255);
  g = static_cast<uint8_t>((g1 + m) * 255);
  b = static_cast<uint8_t>((b1 + m) * 255);
}

void TransitionOrbitDots(rgb_matrix::RGBMatrix* matrix, rgb_matrix::FrameCanvas* canvas) {
  const int centerX = canvas->width() / 2;
  const int centerY = canvas->height() / 2;
  const int radius = std::min(centerX, centerY) - 1;
  const int dot_count = 4;
  const int cycles = 3;
  const int frames_per_cycle = 72;

  std::vector<float> angles(dot_count);   // current angle for each dot
  std::vector<float> delays(dot_count);   // offset timing for staggered takeoff

  for (int i = 0; i < dot_count; ++i) {
    angles[i] = (float)i / dot_count * 2.0f * M_PI;
    delays[i] = (float)i * 0.2f;  // staggered start
  }

  float base_hue = static_cast<float>(std::rand() % 360);

  for (int cycle = 0; cycle < cycles; ++cycle) {
    for (int frame = 0; frame < frames_per_cycle; ++frame) {
      float progress = (float)frame / frames_per_cycle;
      float easing = std::cos(progress * M_PI);  // slows them near middle
      float base_speed = (1.0f - easing) * 0.15f + 0.015f;

      canvas->Clear();

      for (int i = 0; i < dot_count; ++i) {
        float angle_offset = (progress - delays[i]);
        if (angle_offset < 0) angle_offset = 0;

        float speed = base_speed * (1.0f + (float)i * 0.05f);
        angles[i] += speed;

        float hue = fmod(base_hue + frame * 2 + i * 30, 360.0f);
        uint8_t r, g, b;
        HSVtoRGB(hue, 1.0f, 1.0f, r, g, b);

        int x = static_cast<int>(centerX + std::cos(angles[i]) * radius);
        int y = static_cast<int>(centerY + std::sin(angles[i]) * radius);

        // Draw a 3x3 "dot" centered on (x, y)
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            int px = x + dx;
            int py = y + dy;
            if (px >= 0 && px < canvas->width() && py >= 0 && py < canvas->height()) {
              float falloff = 1.0f - 0.25f * (abs(dx) + abs(dy));  // simple brightness gradient
              uint8_t rr = static_cast<uint8_t>(r * falloff);
              uint8_t gg = static_cast<uint8_t>(g * falloff);
              uint8_t bb = static_cast<uint8_t>(b * falloff);
              canvas->SetPixel(px, py, rr, gg, bb);
            }
          }
        }
      }

      canvas = matrix->SwapOnVSync(canvas);
      std::this_thread::sleep_for(std::chrono::milliseconds(22));  // ~45fps
    }
  }
}

void TransitionPulse(rgb_matrix::RGBMatrix* matrix, rgb_matrix::FrameCanvas* canvas, int, int, int) {
  const int centerX = canvas->width() / 2;
  const int centerY = canvas->height() / 2;
  const int max_radius = std::max(centerX, centerY);
  const int fade_width = 6;
  const int pulse_count = 4;
  const int frames_per_pulse = 24;
  const int total_frames = pulse_count * frames_per_pulse;

  std::srand(std::time(nullptr));
  float base_hue = std::rand() % 360;

  for (int frame = 0; frame < total_frames; ++frame) {
    float pulse_progress = (float)(frame % frames_per_pulse) / (frames_per_pulse - 1);
    float eased = std::sin(pulse_progress * M_PI);
    int radius = static_cast<int>(eased * max_radius);

    for (int y = 0; y < canvas->height(); ++y) {
      for (int x = 0; x < canvas->width(); ++x) {
        int dx = x - centerX;
        int dy = y - centerY;
        int dist = std::sqrt(dx * dx + dy * dy);

        if (dist <= radius && dist >= radius - fade_width) {
          float alpha = 1.0f - (float)(radius - dist) / fade_width;

          // Use distance or angle to influence hue
          float angle = std::atan2(dy, dx);  // -π to π
          float hue_offset = (angle + M_PI) / (2 * M_PI); // 0 to 1
          float hue = fmod(base_hue + hue_offset * 60, 360.0f); // subtle gradient

          uint8_t rr, gg, bb;
          HSVtoRGB(hue, 1.0f, alpha, rr, gg, bb);
          canvas->SetPixel(x, y, rr, gg, bb);
        } else {
          canvas->SetPixel(x, y, 0, 0, 0);
        }
      }
    }

    canvas = matrix->SwapOnVSync(canvas);
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
}

void RunTransition(rgb_matrix::RGBMatrix* matrix, rgb_matrix::FrameCanvas* canvas) {
  int style = transition_index % 2;
  transition_index++;
  std::cout << "Transition index = " << transition_index << " | style = " << style << std::endl;
  std::cout << "Transition enum: " << static_cast<int>(static_cast<TransitionStyle>(style)) << std::endl;

  switch (static_cast<TransitionStyle>(style)) {
    case TransitionStyle::OrbitDots:
      std::cout << "<< Entering OrbitDots transition\n";
      TransitionOrbitDots(matrix, canvas);
      std::cout << "<< Exiting OrbitDots transition\n";
      break;
    case TransitionStyle::Pulse:
      std::cout << ">> Entering Pulse transition\n";
      TransitionPulse(matrix, canvas, 64, 64, 64);
      std::cout << "<< Exiting Pulse transition\n";
      break;
    }
  }


void RunFetchLoop(rgb_matrix::RGBMatrix* matrix, const std::string& host, const std::string& path) {

  WebPAnimDecoder* decoder = nullptr;
  (void)decoder;
  std::vector<uint8_t> prev_frame, current_frame;

  rgb_matrix::FrameCanvas* canvas = matrix->CreateFrameCanvas();

  httplib::Client client(host.c_str());
  if (!client.is_valid()) {
    std::cerr << "Invalid client for: " << host << std::endl;
    return;
  }

  while (true) {
    int brightness = 100;  // Default brightness
    int dwell_secs = 10;   // Default dwell time in seconds

    std::string last_hash;
    // RunTransition(matrix, canvas);
    auto res = client.Get(path.c_str());
    if (!res || res->status != 200) {
      std::cerr << "Failed to fetch from: " << host << path << std::endl;
      std::this_thread::sleep_for(1s);
      continue;
    }

    // Extract tronbyt-brightness and tronbyt-dwell-secs headers
    std::string brightness_header = res->get_header_value("tronbyt-brightness");
    std::string dwell_header = res->get_header_value("tronbyt-dwell-secs");

    if (!brightness_header.empty()) {
      
    std::istringstream brightness_stream(brightness_header);
    if (!(brightness_stream >> brightness)) {
      std::cerr << "Invalid brightness header: " << brightness_header << std::endl;
    } else {
      if (brightness < 1) brightness = 1;
  if (brightness > 50) brightness = 50;
      matrix->SetBrightness(brightness);
    }
    
    }

    if (!dwell_header.empty()) {
      
      std::istringstream dwell_stream(dwell_header);
      if (!(dwell_stream >> dwell_secs)) {
        std::cerr << "Invalid dwell header: " << dwell_header << std::endl;
      } else {
      if (dwell_secs < 1) dwell_secs = 1;
      }
    
    }

    std::string current_hash = std::to_string(std::hash<std::string>{}(res->body));
    RunTransition(matrix, canvas);
    std::cout << "✅ Transition complete, preparing to decode WebP\n";
    if (current_hash == last_hash) {
      std::this_thread::sleep_for(500ms);
      continue;
    }
    last_hash = current_hash;
    if (!res || res->status != 200) {
      std::cerr << "Failed to fetch from: " << host << path << std::endl;
      std::this_thread::sleep_for(1s);
      continue;
    }

WebPDemuxer* demux = nullptr;
WebPAnimDecoder* decoder = nullptr;
int last_timestamp = 0;
int delay = 0;
auto start_time = std::chrono::steady_clock::now();

// Load WebP data
WebPData webp_data;
webp_data.bytes = reinterpret_cast<const uint8_t*>(res->body.c_str());
webp_data.size = res->body.size();

demux = WebPDemux(&webp_data);
if (!demux) {
  std::cerr << "❌ demux creation failed — skipping decode.\n";
  goto cleanup;
}

WebPAnimInfo anim_info;
anim_info.frame_count = WebPDemuxGetI(demux, WEBP_FF_FRAME_COUNT);
anim_info.loop_count = WebPDemuxGetI(demux, WEBP_FF_LOOP_COUNT);
anim_info.canvas_width = WebPDemuxGetI(demux, WEBP_FF_CANVAS_WIDTH);
anim_info.canvas_height = WebPDemuxGetI(demux, WEBP_FF_CANVAS_HEIGHT);

if (anim_info.frame_count == 1) {
  int width = 0, height = 0;
  uint8_t* rgb = WebPDecodeRGB(webp_data.bytes, webp_data.size, &width, &height);
  if (!rgb) {
    std::cerr << "❌ Failed to decode static WebP image\n";
    goto cleanup;
  }

  for (uint32_t y = 0; y < anim_info.canvas_height && y < (uint32_t)canvas->height(); ++y) {
    for (uint32_t x = 0; x < anim_info.canvas_width && x < (uint32_t)canvas->width(); ++x) {
      int idx = (y * width + x) * 3;
      canvas->SetPixel(x, y, rgb[idx], rgb[idx + 1], rgb[idx + 2]);
    }
  }

  canvas = matrix->SwapOnVSync(canvas);
  free(rgb);
  std::this_thread::sleep_for(std::chrono::seconds(dwell_secs));
  goto cleanup;
}


// Animated WebP
WebPAnimDecoderOptions dec_options;
WebPAnimDecoderOptionsInit(&dec_options);
decoder = WebPAnimDecoderNew(&webp_data, &dec_options);
if (!decoder) {
  std::cerr << "❌ Failed to create WebPAnimDecoder\n";
  goto cleanup;
}

if (!WebPAnimDecoderGetInfo(decoder, &anim_info)) {
  std::cerr << "❌ Failed to get animation info from decoder\n";
  goto cleanup;
}

uint8_t* frame;
int timestamp;
last_timestamp = 0;



while (true) {
  WebPAnimDecoderReset(decoder);  // Restart animation from beginning
  last_timestamp = 0;

  while (WebPAnimDecoderHasMoreFrames(decoder)) {
    if (!WebPAnimDecoderGetNext(decoder, &frame, &timestamp)) {
      std::cerr << "⚠️ Failed to get next frame\n";
      break;
    }

    for (uint32_t y = 0; y < anim_info.canvas_height && y < (uint32_t)canvas->height(); ++y) {
      for (uint32_t x = 0; x < anim_info.canvas_width && x < (uint32_t)canvas->width(); ++x) {
        int idx = (y * anim_info.canvas_width + x) * 4;
        
        uint8_t alpha = frame[idx + 3];
        float alpha_f = alpha / 255.0f;

        uint8_t r = ApplyGamma(static_cast<uint8_t>(frame[idx] * alpha_f));
        uint8_t g = ApplyGamma(static_cast<uint8_t>(frame[idx + 1] * alpha_f));
        uint8_t b = ApplyGamma(static_cast<uint8_t>(frame[idx + 2] * alpha_f));

        canvas->SetPixel(x, y, r, g, b);

      }
    }

    canvas = matrix->SwapOnVSync(canvas);
    delay = timestamp - last_timestamp;
    if (delay < 10) delay = 10;
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    last_timestamp = timestamp;
  }

  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
  if (elapsed >= dwell_secs) {
    break;
  }
}

cleanup:
  if (decoder != nullptr) {
    WebPAnimDecoderDelete(decoder);
    decoder = nullptr;
  }
  if (demux != nullptr) {
    WebPDemuxDelete(demux);
    demux = nullptr;
  }
  continue;
  }
}
int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: tronberry <URL>" << std::endl;
    return 1;
  }

  RGBMatrix::Options options;
  RuntimeOptions runtime_opt;
  options.hardware_mapping = "adafruit-hat";
  options.rows = 32;
  options.cols = 64;
  options.chain_length = 1;
  options.parallel = 1;
  options.show_refresh_rate = false;

  rgb_matrix::RGBMatrix* matrix = rgb_matrix::CreateMatrixFromFlags(&argc, &argv, &options, &runtime_opt);
  FrameCanvas* canvas = matrix->CreateFrameCanvas();
  if (matrix == nullptr) {
    std::cerr << "Failed to initialize matrix" << std::endl;
    return 1;
  }

  std::ifstream config("tronberry.conf");
  std::string full_url;
  if (!config.is_open()) {
    std::cerr << "Could not open tronberry.conf" << std::endl;
    return 1;
  }
  std::string line;
  while (std::getline(config, line)) {
    if (line.rfind("URL=", 0) == 0) {
      full_url = line.substr(4);
      break;
    }
  }
  if (full_url.empty()) {
    std::cerr << "No URL= entry found in config" << std::endl;
    return 1;
  }
  auto pos = full_url.find("/", full_url.find("//") + 2);
  if (pos == std::string::npos) {
    std::cerr << "Invalid URL: " << full_url << std::endl;
    return 1;
  }
  std::string host = full_url.substr(0, pos);
  std::string path = full_url.substr(pos);
  ShowStartupSplash(matrix, canvas);
  RunFetchLoop(matrix, host, path);
  return 0;
}
