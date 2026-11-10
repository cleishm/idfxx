# idfxx_rtc_pcf8563

NXP PCF8563 real-time clock driver, with alarm, countdown timer and clock output.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__rtc.html)**

## Features

- Calendar time through the chip-agnostic `idfxx::rtc::clock` interface (`now()`, `set_time()`),
  as `std::chrono::sys_seconds` in UTC
- Never reports a time it can't trust: a lost time (the chip's voltage-low flag), a stopped clock,
  and corrupt or impossible register contents all count as not valid, `now()` returns
  `std::nullopt` while the time is not valid, and `set_time()` makes it valid again in every case
- Alarm on any combination of minute, hour, day of the month and weekday, with an optional interrupt
- Countdown timer from 1/4096 s to 255 minutes, set as any integer `std::chrono` duration (exact,
  never rounded) or as an explicit source and count, with level or pulse interrupts
- Clock output (CLKOUT) at 32.768 kHz, 1.024 kHz, 32 Hz or 1 Hz, or off
- Stopping the oscillator, to set the time to a precise second or to pause and resume timekeeping
- Creating the driver writes nothing to the chip
- Dual API: exception-based and result-based error handling

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- idfxx_core, idfxx_rtc, idfxx_i2c, and cleishm/quanta (installed automatically)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  idfxx_rtc_pcf8563:
    version: "^1.0.0"
```

Or add `idfxx_rtc_pcf8563` to the `REQUIRES` list in your component's `CMakeLists.txt`.

## Usage

### Exception-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/chrono>
#include <idfxx/i2c/master>
#include <idfxx/log>
#include <idfxx/rtc/pcf8563>

#include <chrono>

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace quanta::frequency_literals;

try {
    idfxx::i2c::master_bus bus(idfxx::i2c::port::i2c0, {
        .sda = idfxx::gpio_8,
        .scl = idfxx::gpio_9,
        .frequency = 400_kHz,
    });

    idfxx::rtc::pcf8563 rtc(bus);
    rtc.set_clkout(std::nullopt); // CLKOUT isn't used on this board

    // Restore the system time at boot, or seed the clock if it has lost track
    if (auto t = rtc.now()) {
        idfxx::chrono::set_system_time(*t);
    } else {
        rtc.set_time(sys_days{2026y / January / 1});
    }

    // Alarm every day at 07:30, driving INT
    rtc.set_alarm({.minute = 30min, .hour = 7h, .interrupt = true});

    // ...later
    auto events = rtc.events();
    if (events.contains(idfxx::rtc::pcf8563::event::alarm)) {
        // ...handle the alarm
    }
    rtc.clear_events(events);

    // A countdown timer every 30 seconds
    rtc.start_timer(30s);

} catch (const std::system_error& e) {
    idfxx::log::error("RTC", "Error: {}", e.what());
}
```

### Result-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is *not* enabled, the result-based API must be used:

```cpp
#include <idfxx/chrono>
#include <idfxx/log>
#include <idfxx/rtc/pcf8563>

auto rtc = idfxx::rtc::pcf8563::make(bus);
if (!rtc) {
    idfxx::log::error("RTC", "Failed to create the driver: {}", rtc.error().message());
    return;
}

auto now = rtc->try_now();
if (!now) {
    idfxx::log::error("RTC", "Failed to read the clock: {}", now.error().message());
    return;
}
if (!*now) {
    idfxx::log::warn("RTC", "The clock's time is not valid");
    return;
}
if (auto r = idfxx::chrono::try_set_system_time(**now); !r) {
    idfxx::log::error("RTC", "Failed to set the system time: {}", r.error().message());
}
```

### Setting the Time to a Precise Second

`stop()` holds the chip's sub-second divider in reset, and `set_time()` restarts the clock, which
counts from that write. The first one-second increment comes about 0.508 s after the oscillator
starts:

```cpp
rtc.stop();
// ...wait until about 0.49 s after t...
rtc.set_time(t);
```

`start()` and `stop()` on their own pause and resume the clock without setting the time. While the
clock is stopped, its time is not valid: `now()` returns `std::nullopt`.

## API Overview

### Types

- `pcf8563::config` - I2C address (default 0x51) and an optional I2C clock (at most 400 kHz)
- `pcf8563::alarm_config` - Optional `minute`, `hour`, `day` and `weekday` to match, and the
  `interrupt` enable
- `pcf8563::timer_config` - How the countdown timer drives INT
- `pcf8563::timer_duration` - Time left in the countdown (`timer_remaining()`), in ticks of 1/4096 s
- `pcf8563::timer_source` - Timer source clock (`hz_4096`, `hz_64`, `hz_1`, `hz_1_60`)
- `pcf8563::timer_interrupt` - Timer interrupt mode (`none`, `level`, `pulse`)
- `pcf8563::clkout_frequency` - CLKOUT frequency (`hz_32768`, `hz_1024`, `hz_32`, `hz_1`)

### `pcf8563`

**Creation:**
- `pcf8563(bus)`, `pcf8563(bus, config)` - Constructors (exception-based, if enabled)
- `make(bus)`, `make(bus, config)` - Create the driver (result-based)

**Time (from `idfxx::rtc::clock`):**
- `now()` / `try_now()` - Read the current time, or `std::nullopt` while it is not valid: the
  voltage-low flag is set, the clock is stopped, or the time registers don't hold a time from 2000
  to 2099
- `set_time(t)` / `try_set_time(t)` - Set the time, clear the voltage-low flag and restart a stopped
  clock

**Oscillator:**
- `start()` / `try_start()` - Resume timekeeping
- `stop()` / `try_stop()` - Stop timekeeping until `start()` or `set_time()`
- `running()` / `try_running()` - Whether the clock is running

**Alarm:**
- `set_alarm(alarm)` / `try_set_alarm(alarm)` - Configure the alarm; `set_alarm({})` turns it off
- `alarm()` / `try_alarm()` - Read the alarm settings back

**Countdown timer:**
- `start_timer(period[, cfg])` / `try_start_timer(period[, cfg])` - Start with a period, any
  `std::chrono::duration` with an integer rep (e.g. `30s`, `250ms`, `5min`)
- `start_timer(source, count[, cfg])` / `try_start_timer(source, count[, cfg])` - Start with an
  explicit source and count
- `timer_remaining()` / `try_timer_remaining()` - Time left in the current period
- `stop_timer()` / `try_stop_timer()` - Stop the timer and put it in its low-power setting

**Events:**
- `events()` / `try_events()` - Which of the alarm and timer flags (`pcf8563::event`) are set
- `clear_events(events)` / `try_clear_events(events)` - Clear only the named flags

**Clock output:**
- `set_clkout(frequency)` / `try_set_clkout(frequency)` - Set the CLKOUT frequency, or
  `std::nullopt` for off
- `clkout()` / `try_clkout()` - Read the CLKOUT setting

## Error Handling

- **Dual API Pattern**: The component provides both result-based and exception-based APIs
  - `try_*` methods return `idfxx::result<T>` (always available)
  - Methods without `try_` prefix throw `std::system_error` (requires `CONFIG_COMPILER_CXX_EXCEPTIONS`)
- **Time that is not valid**: not an error. `now()` returns `std::nullopt` while the voltage-low
  flag is set (such as on first boot or after a battery change), the clock is stopped, or the time
  registers hold corrupt BCD, an impossible date, or the century bit. Reseed the clock with
  `set_time()`, which also restarts a stopped clock
- **Error codes**:
  - `errc::invalid_arg` - An I2C clock above 400 kHz, a time outside 2000–2099, an alarm field out of
    range, a timer period the timer can't produce exactly, or a timer count of 0; nothing is written
  - `errc::not_found` - No chip acknowledged the probe when the driver was created
  - `errc::invalid_state` - From `alarm()`: an enabled alarm field read back from the chip holds an
    impossible value; set the alarm again with `set_alarm()`
  - `errc::invalid_response` - An I2C transfer failed: the chip didn't acknowledge, the transfer
    timed out on the bus (including clock stretching), or it didn't finish within the timeout. Retry
  - `errc::timeout` - The I2C bus couldn't be acquired in time. Retry. When the driver is created,
    this also covers the probe timing out on the bus (for example, SCL held low)
  - Other I2C driver errors are passed through

## Important Notes

- **Time range**: The chip holds 2000-01-01T00:00:00 to 2099-12-31T23:59:59 UTC. `set_time()`
  writes the century bit as 0 and the weekday from the date.
- **Voltage-low flag**: The chip sets it at power-on and when its supply drops too far to keep
  reliable time. Only `set_time()` clears it.
- **CLKOUT**: The clock output runs at 32.768 kHz from power-on. Boards that don't use it should call
  `set_clkout(std::nullopt)` to save current. Creating the driver doesn't change it.
- **No writes at creation**: Creating the driver only probes the chip's address. It doesn't clear
  the STOP bit, the alarm or timer flags, or change the clock output.
- **Stopped clock**: While the STOP bit is set, the time is not valid: `now()` returns
  `std::nullopt`. `set_time()` writes the time, then clears STOP, so
  the time is valid afterwards; if clearing STOP fails, the time is already written and
  `set_time()` reports the transfer error.
- **Event flags**: The alarm and timer flags stay set until cleared. `clear_events()` clears only the
  flags it is given, and no other operation clears either. Clearing the events that `events()`
  returned never wipes one that arrived after the read.
- **Alarm weekday**: The weekday field matches the chip's weekday register, which `set_time()` writes
  from the date.
- **Countdown timer**: `start_timer()` picks the fastest source that gives the period exactly (30 s
  runs at 1 Hz with a count of 30, 250 ms at 64 Hz with a count of 16; 100 ms can't be produced and
  is rejected, never rounded). Periods must use an integer rep. It disables the timer with the new
  source and count, sets the interrupt mode, then enables it. The timer repeats until
  `stop_timer()`.
- **Interrupt pin**: The chip's INT pin is an open-drain, active-low output, pulled low while an
  enabled alarm or timer interrupt is signaling, so it needs a pull-up. The driver sets the
  interrupt enables and reads the flags; it doesn't configure a GPIO or ISR for INT.
- **Thread safety**: A driver is not thread-safe. Use it from one task, or synchronize access
  externally. Using a moved-from driver object is undefined behavior.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
