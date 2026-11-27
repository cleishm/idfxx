# idfxx_mdns

mDNS hostname, DNS-SD service advertisement, queries and browsing.

📚 **[Full API Documentation](https://cleishm.github.io/idfxx/group__idfxx__mdns.html)**

## Features

- Answers for a `<hostname>.local` name, and posts a typed `hostname_changed` event to the system
  event loop whenever the hostname changes (including when mDNS renames the device after a name
  conflict)
- RAII `service` objects advertise DNS-SD services with a port, a TXT record and subtypes, and stop
  advertising them when destroyed. Port, instance name, TXT items and subtypes can be changed in
  place
- Binary-safe TXT values, and key-only (boolean) TXT items
- RAII `delegated_host` objects answer for other hosts' names and addresses, and carry services of
  their own
- One-shot queries for PTR, SRV, TXT, A and AAAA records, blocking or as an `idfxx::future`,
  returning typed `answer`s with optional fields instead of null pointers
- RAII `browser` objects follow the instances of a service type as they appear, change and go away
- Local lookups of the services advertised for the device or on delegated hosts
- mDNS on additional network interfaces besides the default Wi-Fi and Ethernet ones

## Requirements

- ESP-IDF 5.5 or later
- C++23 compiler
- idfxx_core, idfxx_event, idfxx_net, idfxx_netif
- espressif/mdns (fetched by the component manager)

## Installation

### ESP-IDF Component Manager

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  cleishm/idfxx_mdns: "^1.0.0"
```

## Usage

### Exception-based API

If `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled:

```cpp
#include <idfxx/event>
#include <idfxx/log>
#include <idfxx/mdns>

using namespace std::chrono_literals;

void start_mdns() {
    try {
        // mDNS needs the system event loop and esp_netif_init() (idfxx::netif::init()).
        idfxx::mdns::init();
        idfxx::mdns::set_hostname("kitchen-display");

        static idfxx::mdns::service web({
            .instance_name = "Kitchen Display",
            .type = "_http",
            .protocol = idfxx::mdns::protocol::tcp,
            .port = 80,
            .txt = {{.key = "path", .value = "/"}, {.key = "secure"}},
        });

        for (const auto& peer : idfxx::mdns::query_ptr("_http", idfxx::mdns::protocol::tcp, 3s)) {
            idfxx::log::info("mdns", "{} at {}.local:{}", peer.instance_name.value_or("?"),
                             peer.hostname.value_or("?"), peer.port.value_or(0));
        }
    } catch (const std::system_error& e) {
        idfxx::log::error("mdns", "mDNS failed: {}", e.what());
    }
}
```

### Result-based API

Always available:

```cpp
#include <idfxx/mdns>

idfxx::result<idfxx::mdns::service> advertise() {
    if (auto r = idfxx::mdns::try_init(); !r) {
        return idfxx::error(r.error());
    }
    if (auto r = idfxx::mdns::try_set_hostname("kitchen-display"); !r) {
        return idfxx::error(r.error());
    }
    return idfxx::mdns::service::make({
        .type = "_http",
        .protocol = idfxx::mdns::protocol::tcp,
        .port = 80,
    });
}
```

### Queries in the background

```cpp
using namespace std::chrono_literals;

auto pending = idfxx::mdns::start_query({
    .service_type = "_ipp",
    .protocol = idfxx::mdns::protocol::tcp,
    .record_type = idfxx::mdns::record_type::ptr,
    .timeout = 2s,
});
// ... other work ...
for (const auto& printer : pending.wait()) {
    // printer.instance_name, printer.hostname, printer.port, printer.ipv4_addresses, ...
}
```

Dropping the future before the query finishes is fine: the query runs to its timeout and is then
freed in the background.

### Browsing

```cpp
using namespace std::chrono_literals;

idfxx::mdns::browser printers({.type = "_ipp", .protocol = idfxx::mdns::protocol::tcp},
    [](const idfxx::mdns::answer& a) {
        // Runs on the mDNS task: pass the answer to another task, don't call mDNS here.
        // a.ttl == 0s when the printer went away.
    });
```

### Delegated hosts

```cpp
idfxx::mdns::delegated_host sensor({
    .name = "garden-sensor",
    .ipv4_addresses = {idfxx::net::ipv4_addr(192, 168, 1, 77)},
});
idfxx::mdns::service sensor_http(sensor, {.type = "_http", .protocol = idfxx::mdns::protocol::tcp, .port = 80});
```

## API Overview

| Item | Description |
| ---- | ----------- |
| `init()` / `try_init()`, `deinit()` | Start and stop the mDNS service. `deinit()` removes all services, delegated hosts and browsers, after letting queries from `start_query` finish. |
| `set_hostname()`, `get_hostname()`, `set_instance_name()` | The device's hostname and its default service instance name. |
| `hostname_exists(name)` | Whether a name is the device's or a delegated host's. |
| `hostname_changed` | Event (`hostname_info`) posted to the system event loop when the hostname changes. |
| `service(cfg)` / `service(host, cfg)` / `service::make(...)` | Advertise a service for the device or on a delegated host. `type()`, `protocol()`, `instance_name()` and `port()` describe it. `set_port`, `set_instance_name`, `set_txt`, `set_txt_item`, `remove_txt_item`, `add_subtype`, `remove_subtype`, `set_subtypes` change it. |
| `service_exists(type, protocol[, instance])` / `service_exists(host, ...)` | Whether such a service is advertised. |
| `delegated_host(cfg)` / `delegated_host::make(cfg)` | Answer for another host's name and addresses. `name()` returns the name, and `set_addresses` replaces the addresses. |
| `lookup_services(self_hosted \| delegated, type, protocol[, options])` | List the services advertised locally, optionally filtered by instance name or subtype. |
| `query(cfg)` / `start_query(cfg)` | Send a query described by a `query_config` and collect the answers, blocking or as an `idfxx::future<std::vector<answer>>`. Both ask PTR queries by multicast and others by unicast. |
| `query(cfg, transmission)` | Send a blocking query that asks for unicast or multicast answers, as chosen. |
| `query_ptr`, `query_srv`, `query_txt`, `query_a`, `query_aaaa` | Typed queries for service instances, an instance's SRV or TXT record, or a host's address. `query_a` needs `CONFIG_LWIP_IPV4`, `query_aaaa` needs `CONFIG_LWIP_IPV6`. |
| `browser(cfg, callback)` / `browser::make(...)` | Follow a service type continuously (needs `CONFIG_MDNS_ENABLE_BROWSE`). |
| `register_netif`, `unregister_netif`, `post_netif_action` | Run mDNS on additional interfaces, and enable, announce or disable it on an interface. `unregister_netif` disables mDNS on the interface and waits for the mDNS task. |
| Types | `txt_item` (a TXT key and optional binary-safe value), `answer` (a found record: instance name, service type, protocol, hostname, port, TXT items, IPv4 and IPv6 addresses, subtype, interface key, address family and TTL), `query_config` (what a query asks for), `lookup_options` (filters for `lookup_services`), `hostname_info` (the `hostname_changed` event data). |

## Error Handling

| Error | Meaning |
| ----- | ------- |
| `mdns::errc::limit_reached` | A capacity is exhausted: `CONFIG_MDNS_MAX_SERVICES` services are already advertised, every one of the `CONFIG_MDNS_MAX_INTERFACES` interface slots is in use, or the mDNS task's action queue (`CONFIG_MDNS_ACTION_QUEUE_LEN`) is full. A full queue is transient, so retrying later can succeed. `unregister_netif` also fails with it when the mDNS task did not run the disable within about 1 second. |
| `idfxx::errc::invalid_state` | mDNS is not running, or a service or delegated host is used after the `deinit()` that it outlived. The device has no hostname where one is needed, also for a service on a delegated host. A service, delegated host or browser with the same identity already exists. A service without an instance name would share its type and protocol with another on its host, including a renamed service that its destructor could not remove. The hostname would be a delegated host's name. |
| `idfxx::errc::invalid_arg` | An empty or over-long name (more than 63 characters, one DNS label, for hostnames, instance names, delegated host names, service types and subtypes), or a name with a NUL character in it. A zero port or `max_results`, a timeout that is not positive (NaN included) or exceeds 2147483647 ms (`INT32_MAX`, about 24.8 days), an invalid TXT item, or the same subtype twice in one list. A query without a field that its record type needs, or with `service_type` but not `protocol` (or the reverse). |
| `idfxx::errc::not_found` | Removing a subtype that isn't there, or unregistering an interface that `register_netif` did not register (or that a disable action has unregistered). Changing a service whose delegated host was destroyed first. Changing a named service that the network has renamed: any change while it shares its type and protocol with another service, and removing or setting its subtypes in every case. (Removing a missing TXT item does nothing.) |
| `idfxx::errc::not_supported` | Delegated-host addresses for an IP version lwIP is built without, or removing a subtype from a service without an instance name. |
| `idfxx::errc::timeout` | Waiting on a query future with a timeout that expired before the query finished. |
| `idfxx::errc::fail` | `init()` could not start the service, for example because the system default event loop does not exist. |

A query that nobody answers is not an error: it returns no answers, or `std::nullopt` for the
queries that look for a single record. Running out of memory is not an error code either: as
everywhere in idfxx, `std::bad_alloc` is thrown when exceptions are enabled, and the program aborts
otherwise. The exception is the calls that can also fail because a capacity is exhausted: creating
a service, `register_netif`, and the calls that pass work to the mDNS task's action queue (setting
the hostname or default instance name, adding a delegated host or changing its addresses, starting
a query or a browser). For those, the mDNS library reports a failed allocation with the same code
as a full capacity or queue, so both arrive as `limit_reached`.

## Important Notes

- **A browser can report the device's own service without a host or port.** This happens when
  another host queries for the service type. Use `lookup_services(idfxx::mdns::self_hosted, ...)`
  to read the device's own services.
- **Callbacks run on the mDNS task, under its lock.** A browser callback must return quickly and
  must not call any `idfxx::mdns` function, including creating or destroying a browser or dropping a
  query future. Doing so deadlocks. Pass the answer to another task (through a queue, or an event)
  for further work. `hostname_changed` is delivered on the system event loop's task instead, so its
  listeners may call mDNS functions.
- **The predefined interfaces use interface slots.** mDNS runs on the default Wi-Fi station, access
  point and Ethernet interfaces (`CONFIG_MDNS_PREDEF_NETIF_STA`, `_AP`, `_ETH`), and each enabled
  one takes one of the `CONFIG_MDNS_MAX_INTERFACES` slots (default 3) whether or not it exists.
  Disable the predefined interfaces you don't use, or raise the slot count, before calling
  `register_netif`, which otherwise fails with `limit_reached`.
- **Services are limited.** At most `CONFIG_MDNS_MAX_SERVICES` services (default 10), counting those
  on delegated hosts, can be advertised at once. Creating one more fails with `limit_reached`.
- **The mDNS task's action queue can fill up.** Setting the hostname or default instance name,
  adding a delegated host or changing its addresses, and sending a query all pass work to the mDNS
  task through a queue of `CONFIG_MDNS_ACTION_QUEUE_LEN` entries (default 16). They do not wait for
  a free entry. When the queue is full they fail with `limit_reached` (so do `start_query` and
  creating a `browser`), and retrying after the task has processed the queue can succeed.
  `post_netif_action` uses the same queue but can't report a full one: the mDNS library drops the
  actions silently. Destroying a delegated host or a browser also queues an action. The destructor
  retries for about 100 ms, then logs an error and leaves the name or browse in place (a new one
  with the same name or key then fails with `invalid_state` until `deinit()`). `unregister_netif`
  waits up to about 1 second for the mDNS task to run its disable action. It fails with
  `limit_reached` only if the task has not run the action by then. Raise the queue length if bursts
  of calls fill it.
- **mDNS needs the system event loop and the network stack.** Create the system default event loop
  (`idfxx::event_loop::create_system()`) and call `esp_netif_init()` (`idfxx::netif::init()`) before
  `init()`. `hostname_changed` is posted to that loop without waiting, and is dropped if its queue
  is full.
- **The console commands pull in `console`.** The mDNS library's `CONFIG_MDNS_ENABLE_CONSOLE_CLI`
  defaults to `y`, which needs the `console` component in the build. Set it to `n` unless you use
  the `mdns_*` console commands.
- **A service without an instance name must be the only service of its type and protocol on its
  host. To advertise several, name them all.** Creating a service without an instance name while
  another of its type and protocol is advertised on the host, or any service while one without a
  name is, fails with `invalid_state`. The device and each delegated host are separate hosts. A
  service without an instance name follows the default instance name (`set_instance_name()`, or else
  the hostname).
- **Only a service with an instance name can remove a subtype.** On a service without one,
  `remove_subtype` fails with `not_supported`. `set_subtypes` fails with the same error when the new
  subtypes leave out one that the service has. Give the service an instance name to remove subtypes.
  A list of subtypes can't hold the same subtype twice. Subtypes that differ only in case are
  different.
- **Goodbye packets use the calling task's stack.** A call that removes a subtype
  (`remove_subtype`, or `set_subtypes` when it leaves out a subtype) uses about 2.2 KB of the
  calling task's stack. Destroying a `service` uses about 1.5 KB. The system event task has 2304
  bytes by default, and the event loop uses part of it. Raise
  `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE` before you make these calls from a system event
  listener.
- **Network renames can strand a service that shares its type.** mDNS renames an instance by itself
  when another host on the network already uses its name. A service that is the only one of its type
  and protocol on its host keeps working after that. The exception is `remove_subtype` and
  `set_subtypes` on a named service: they fail with `not_found` until `set_instance_name()` gives
  the service a new name. A named service that shares its type and protocol with another can no
  longer be changed or removed once the network has renamed it. Its calls fail with `not_found`, and
  its destructor can't remove it. It stays advertised until `deinit()`, or until its delegated host
  is destroyed. Until then, creating a service without an instance name of its type and protocol on
  its host fails with `invalid_state`.
- **Destroy a delegated host's services before the host.** Destroying the host removes its services
  from the network too. Changes to `service` objects left behind fail with `not_found`.
- **Objects must not outlive `deinit()`.** Services, delegated hosts and browsers that still exist
  after `deinit()` are a mistake. Destroying them afterward does nothing, and using them fails.
  Futures from `start_query` stay valid: `deinit()` waits for those queries to finish first, so it
  can block for up to the longest remaining query timeout. No other mDNS call may be in progress on
  another task when `deinit()` runs. In particular, a blocking query must have returned.
- **Some changes complete in the background.** Removing a delegated host and replacing its addresses
  return before the mDNS task has applied them. A name freed by destroying a delegated host can be
  reused at once.
- **Delegated hosts need an address.** A host with no addresses can't answer address queries, so one
  is required.
- **Services on a delegated host need the device's hostname.** The mDNS library refuses every
  service while the device has no hostname, so `service(host, cfg)` fails with `invalid_state` until
  `set_hostname()` is called.
- **`unregister_netif` waits for the mDNS task.** It disables mDNS on the interface for both IP
  versions, without goodbye packets, and waits until the mDNS task has run the disable (about 1
  second at most). It only unregisters interfaces that `register_netif` registered. For a default
  interface it fails with `not_found` and leaves the interface alone.
- **Very long query timeouts can end early.** The mDNS library keeps a query's deadline in 32-bit
  milliseconds since boot, so a query ends early if the device's uptime plus its timeout exceeds
  2^32 ms (about 49.7 days).
- **Don't wait for mDNS from an esp_timer callback.** The mDNS library ends queries from the
  esp_timer task, so a blocking query, a wait on a `start_query` future or `deinit()` called from an
  esp_timer callback never returns.
- **`start_query` always uses the default transmission**: multicast for PTR queries, unicast for
  others. Use the blocking `query(cfg, transmission)` to choose.
- `service`, `delegated_host` and `browser` are move-only. A moved-from object must not be used: any
  operation other than destruction or assignment is undefined behavior.

## License

Apache-2.0
