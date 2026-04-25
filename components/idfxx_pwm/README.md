# idfxx_pwm

Type-safe LEDC PWM controller for ESP32.

## Features

- **Lightweight timer identifiers** with predefined constants (timer_0 through timer_3)
- **RAII output management** with automatic PWM stop on destruction
- **Type-safe configuration** with config structs and `freq::hertz` frequency type
- **Hardware fade support** with time-based and step-based fading
- **Dual error handling** — exception-based and result-based APIs
- **High-speed mode support** on ESP32 (SOC_LEDC_SUPPORT_HS_MODE)

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_pwm:
    version: "^1.0.0"
```

Or add `idfxx_pwm` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Exception-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/pwm>

using namespace frequency_literals;
using namespace std::chrono_literals;

// Configure a timer
auto tmr = idfxx::pwm::timer_0;
tmr.configure({.frequency = 5_kHz, .resolution_bits = 13});

// Start PWM output (RAII — stops on destruction)
auto led = idfxx::pwm::start(idfxx::gpio_18, tmr, idfxx::pwm::channel::ch_0);
led.set_duty(0.5f);  // 50% duty

// Fading (install fade service first)
idfxx::pwm::output::install_fade_service();
led.fade_to(0.0f, 1s);  // fade to off over 1 second
```

### Result-based API

Always available, even without exceptions:

```cpp
#include <idfxx/pwm>

using namespace frequency_literals;

auto tmr = idfxx::pwm::timer_0;
if (auto r = tmr.try_configure(5_kHz, 13); !r) {
    // handle error
}

auto led = idfxx::pwm::try_start(idfxx::gpio_18, tmr, idfxx::pwm::channel::ch_0);
if (!led) {
    // handle error
}

led->try_set_duty(0.5f);
```

## API Overview

### Timer (value type)

| Method | Description |
|--------|-------------|
| `configure(config)` | Set frequency, duty resolution, and clock source |
| `set_frequency(freq)` | Change the PWM frequency |
| `frequency()` | Read the current frequency from hardware |
| `set_period(duration)` | Set the PWM period (converts to frequency) |
| `period()` | Get the PWM period as a `std::chrono::nanoseconds` |
| `tick_period()` | Get the duration of a single tick |
| `resolution_bits()` | Get the configured resolution (0 if not configured) |
| `ticks_max()` | Get the maximum duty value (1 << resolution) |
| `pause()` / `resume()` / `reset()` | Timer counter control |

### Output (RAII)

| Method | Description |
|--------|-------------|
| `set_duty(ratio)` | Set PWM duty as a ratio 0.0-1.0 (thread-safe) |
| `duty()` | Read current duty as a ratio 0.0-1.0 |
| `set_duty_ticks(ticks)` | Set PWM duty in ticks (0 to ticks_max()) |
| `duty_ticks()` | Read current duty in ticks |
| `set_pulse_width(duration)` | Set PWM duty as a pulse width duration |
| `pulse_width()` | Read current pulse width as `std::chrono::nanoseconds` |
| `ticks_max()` | Maximum duty ticks for the configured resolution |
| `stop(level)` | Stop output and set idle level |
| `release()` | Release ownership without stopping (PWM continues) |
| `fade_to(target, duration)` | Fade to a duty ratio (requires fade service) |
| `fade_to_duty_ticks(target, duration)` | Fade to a duty in ticks (requires fade service) |
| `fade_to_pulse_width(target, duration)` | Fade to a pulse width (requires fade service) |
| `fade_with_step(target, scale, cycles)` | Step-based fade in ticks (requires fade service) |
| `install_fade_service()` | Install global fade ISR (static) |

### Free Functions

| Function | Description |
|----------|-------------|
| `start(gpio, timer, channel)` | Start PWM output (returns `output`) |
| `try_start(gpio, timer, channel)` | Start PWM output (returns `result<output>`) |
| `stop(channel)` | Stop a channel directly (e.g. after `release()`) |
| `is_active(channel)` | Check if a channel has an active output |
| `get_timer(channel)` | Get the timer bound to an active channel |

## Error Handling

Timer and output operations return `idfxx::result<T>` or throw `std::system_error`:

- `invalid_arg` — Invalid frequency, duty resolution, GPIO, or duty value
- `invalid_state` — Timer not configured, or output moved from
- `fail` — Requested frequency cannot be achieved

## Important Notes

- **Configure before use**: Call `timer::configure()` before creating any `output` bound to that timer
- **Timer persistence**: Timers are fixed hardware — configuration persists even after the timer object goes out of scope
- **Output cleanup**: `output` destructor stops PWM and sets the GPIO low
- **Fade service**: Call `output::install_fade_service()` once before using any fade operations
- **Thread safety**: `set_duty()` and fade operations use thread-safe ESP-IDF APIs

## License

Apache-2.0. See [LICENSE](LICENSE).
