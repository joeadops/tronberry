#define CPPHTTPLIB_OPENSSL_SUPPORT

#include <webp/decode.h>
#include <webp/demux.h>

#include <chrono>
#include <iostream>
#include <queue>
#include <string>
#include <thread>

#include "httplib.h"
#include "led-matrix.h"
#include "startup.h"

using namespace rgb_matrix;

static std::atomic<bool> running(true);
static void InterruptHandler(int) { running = false; }

static void DrawFrame(FrameCanvas *canvas, const uint8_t *frame_data, int width,
                      int height) {
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int index = (y * width + x) * 4;  // RGBA
      canvas->SetPixel(x, y, frame_data[index], frame_data[index + 1],
                       frame_data[index + 2]);
    }
  }
}

static void DisplayImage(RGBMatrix *matrix, FrameCanvas *&canvas,
                         const uint8_t *image_data, int width, int height,
                         int brightness, int dwell_secs) {
  DrawFrame(canvas, image_data, width, height);

  canvas = matrix->SwapOnVSync(canvas);
  std::this_thread::sleep_for(std::chrono::seconds(dwell_secs));
}

static void DisplayAnimation(RGBMatrix *matrix, FrameCanvas *&canvas,
                             WebPAnimDecoder *anim_decoder, int width,
                             int height, int brightness, int dwell_secs) {
  auto start_time = std::chrono::steady_clock::now();
  uint8_t *frame_data;
  int timestamp;
  int prev_timestamp = 0;

  while (std::chrono::steady_clock::now() - start_time <
         std::chrono::seconds(dwell_secs)) {
    if (!WebPAnimDecoderGetNext(anim_decoder, &frame_data, &timestamp)) {
      WebPAnimDecoderReset(anim_decoder);
      prev_timestamp = 0;
      continue;
    }

    DrawFrame(canvas, frame_data, width, height);

    canvas = matrix->SwapOnVSync(canvas);

    int delay_ms = timestamp - prev_timestamp;
    if (delay_ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    prev_timestamp = timestamp;
  }
}

static int usage(const char *progname, const char *msg = NULL) {
  if (msg) {
    std::cerr << msg << std::endl;
  }
  std::cerr << "Fetch images over HTTP and display on RGB-Matrix" << std::endl;
  std::cerr << "usage: " << progname << " <URL>" << std::endl;

  std::cerr << "\nGeneral LED matrix options:" << std::endl;
  PrintMatrixFlags(stderr);
  return 1;
}

int main(int argc, char *argv[]) {
  RGBMatrix::Options matrix_options;
  matrix_options.rows = 32;
  matrix_options.cols = 64;
  matrix_options.chain_length = 1;
  matrix_options.parallel = 1;
  matrix_options.brightness = INITIAL_BRIGHTNESS;
  matrix_options.hardware_mapping = "regular";

  RuntimeOptions runtime_options;
  runtime_options.gpio_slowdown = 2;
  runtime_options.drop_privileges = true;

  if (!ParseOptionsFromFlags(&argc, &argv, &matrix_options, &runtime_options)) {
    return usage(argv[0]);
  }

  if (argc != 2) {
    usage(argv[0], "Invalid number of arguments");
    return 1;
  }

  std::string url = argv[1];
  size_t scheme_end = url.find("://");
  if (scheme_end == std::string::npos) {
    std::cerr << "Invalid URL: Missing scheme (http:// or https://)"
              << std::endl;
    return 1;
  }
  size_t path_start = url.find(
      '/', scheme_end + 3);  // Find the start of the path after the scheme
  httplib::Client client(url.substr(
      0, path_start != std::string::npos ? path_start
                                         : url.length()));  // Extract base URL
  std::string path = path_start != std::string::npos
                         ? url.substr(path_start)
                         : "/";  // Extract path or default to "/"

  RGBMatrix *matrix = CreateMatrixFromOptions(matrix_options, runtime_options);
  if (!matrix) {
    std::cerr << "Failed to initialize RGB matrix" << std::endl;
    return 1;
  }

  FrameCanvas *offscreen_canvas = matrix->CreateFrameCanvas();

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  std::mutex queue_mutex;
  std::condition_variable queue_not_full;
  std::condition_variable queue_not_empty;
  struct ResponseData {
    std::string data;
    int brightness;
    int dwell_secs;
  };
  std::queue<ResponseData> response_queue;
  const size_t max_queue_size = 1;

  // Display the startup image
  ResponseData startup_response;
  startup_response.data = std::string(
      reinterpret_cast<const char *>(STARTUP_WEBP), STARTUP_WEBP_LEN);
  startup_response.brightness = INITIAL_BRIGHTNESS;
  startup_response.dwell_secs = INITIAL_DWELL_SECS;
  response_queue.push(std::move(startup_response));

  // Thread to fetch the next image
  std::thread fetch_thread([&]() {
    int retry_count = 0;
    while (running) {
      // std::cout << "Fetching the next image from URL: " << url << std::endl;
      auto res = client.Get(path.c_str());
      if (!res || res->status != 200) {
        std::cerr << "Failed to fetch image from URL: " << url << std::endl;
        int wait_time = std::min(
            1 << retry_count,
            60);  // Exponential backoff with max wait time of 60 seconds
        wait_time =
            std::max(wait_time, 1);  // Ensure at least 1 second wait time
        std::this_thread::sleep_for(std::chrono::seconds(wait_time));
        retry_count++;
        continue;
      }
      retry_count = 0;  // Reset retry_count on success

      ResponseData response;
      response.data = std::move(res->body);
      try {
        response.brightness =
            std::stoi(res->get_header_value("Tronbyt-Brightness", "0"));
        response.dwell_secs =
            std::stoi(res->get_header_value("Tronbyt-Dwell-Secs", "0"));
      } catch (const std::invalid_argument &e) {
        std::cerr << "Invalid header value: " << e.what() << std::endl;
        response.brightness = 0;
        response.dwell_secs = 0;
      } catch (const std::out_of_range &e) {
        std::cerr << "Header value out of range: " << e.what() << std::endl;
        response.brightness = 0;
        response.dwell_secs = 0;
      }

      {
        std::unique_lock<std::mutex> lock(queue_mutex);
        queue_not_full.wait(lock, [&]() {
          return response_queue.size() < max_queue_size || !running;
        });
        if (!running) {
          break;
        }
        response_queue.push(std::move(response));
      }
      queue_not_empty.notify_one();
    }
  });

  while (running) {
    ResponseData response;
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      queue_not_empty.wait(lock, [&]() { return !response_queue.empty(); });

      response = std::move(response_queue.front());
      response_queue.pop();
    }
    queue_not_full.notify_one();

    static int previous_brightness = -1;
    if (response.brightness != previous_brightness) {
      matrix->SetBrightness(response.brightness);
      previous_brightness = response.brightness;
    }

    WebPData webp_data = {
        reinterpret_cast<const uint8_t *>(response.data.data()),
        response.data.size()};
    WebPAnimDecoderOptions anim_options;
    WebPAnimDecoderOptionsInit(&anim_options);
    WebPAnimDecoder *anim_decoder =
        WebPAnimDecoderNew(&webp_data, &anim_options);

    if (anim_decoder) {
      WebPAnimInfo anim_info;
      WebPAnimDecoderGetInfo(anim_decoder, &anim_info);

      if (anim_info.frame_count > 1) {
        DisplayAnimation(matrix, offscreen_canvas, anim_decoder,
                         anim_info.canvas_width, anim_info.canvas_height,
                         response.brightness, response.dwell_secs);
      } else {
        uint8_t *frame_data;
        int timestamp;
        WebPAnimDecoderGetNext(anim_decoder, &frame_data, &timestamp);
        DisplayImage(matrix, offscreen_canvas, frame_data,
                     anim_info.canvas_width, anim_info.canvas_height,
                     response.brightness, response.dwell_secs);
      }

      WebPAnimDecoderDelete(anim_decoder);
    } else {
      int width, height;
      uint8_t *image_data =
          WebPDecodeRGBA(webp_data.bytes, webp_data.size, &width, &height);
      if (image_data) {
        DisplayImage(matrix, offscreen_canvas, image_data, width, height,
                     response.brightness, response.dwell_secs);
        WebPFree(image_data);
      } else {
        std::cerr << "Failed to decode WebP image" << std::endl;
      }
    }
  }

  std::cout << "Shutting down..." << std::endl;
  fetch_thread.join();

  delete matrix;
  return 0;
}