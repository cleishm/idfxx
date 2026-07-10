// SPDX-License-Identifier: Apache-2.0

// Samples one analog pin at 20 kHz and logs min/max/mean once per second.

#include <idfxx/adc>
#include <idfxx/log>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>

using namespace std::chrono_literals;
using namespace frequency_literals;

static constexpr idfxx::log::logger logger{"example"};

// ADC1-capable pin carrying the signal to measure (change as needed).
static constexpr auto signal_pin = idfxx::gpio_3;

extern "C" void app_main() {
    idfxx::adc::sampler sampler({.pins = {signal_pin}, .sample_rate = 20_kHz});
    logger.info(
        "sampling GPIO {} at {} Hz ({}calibrated)",
        signal_pin.num(),
        sampler.sample_rate().count(),
        sampler.calibrated() ? "" : "un"
    );

    sampler.start();

    std::array<idfxx::adc::sampler::sample, 256> buf;
    int min = std::numeric_limits<int>::max();
    int max = std::numeric_limits<int>::min();
    long long sum = 0;
    size_t count = 0;
    auto window_start = std::chrono::steady_clock::now();

    while (true) {
        size_t n = sampler.read(buf);
        for (size_t i = 0; i < n; ++i) {
            int raw = buf[i].raw;
            min = std::min(min, raw);
            max = std::max(max, raw);
            sum += raw;
        }
        count += n;

        auto now = std::chrono::steady_clock::now();
        if (now - window_start < 1s || count == 0) {
            continue;
        }
        int mean = static_cast<int>(sum / static_cast<long long>(count));
        if (sampler.calibrated()) {
            logger.info(
                "raw min {} max {} mean {} | voltage min {} max {} mean {} | {} samples, {} overruns",
                min,
                max,
                mean,
                sampler.to_voltage(min),
                sampler.to_voltage(max),
                sampler.to_voltage(mean),
                count,
                sampler.overruns()
            );
        } else {
            logger.info(
                "raw min {} max {} mean {} | {} samples, {} overruns", min, max, mean, count, sampler.overruns()
            );
        }
        min = std::numeric_limits<int>::max();
        max = std::numeric_limits<int>::min();
        sum = 0;
        count = 0;
        window_start = now;
    }
}
