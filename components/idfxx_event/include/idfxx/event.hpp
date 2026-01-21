// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/event>
 * @file event.hpp
 * @brief Event loop library.
 *
 * @defgroup idfxx_event Event Loop Component
 * @brief Type-safe event loop management.
 *
 * Provides event loops for posting and subscribing to events with
 * type-safe event bases and IDs.
 *
 * Depends on @ref idfxx_core for error handling.
 * @{
 */

#include <idfxx/chrono>
#include <idfxx/cpu>
#include <idfxx/error>

#include <esp_event.h>
#include <functional>
#include <memory>
#include <type_traits>

/**
 * @brief Defines an event base.
 *
 * Creates an event_base with the given name and enum type. The event base name string is automatically derived from the
 * variable name.
 *
 * @param name The variable name for the event base.
 * @param id_enum The enum class for event IDs within this base.
 *
 * @code
 * // Define event IDs
 * enum class app_event : int32_t { started, stopped };
 *
 * // Define the event base (typically in a header)
 * IDFXX_EVENT_DEFINE_BASE(app_events, app_event);
 *
 * // Use it
 * loop.post(app_events, app_event::started);
 * @endcode
 */
#define IDFXX_EVENT_DEFINE_BASE(name, id_enum)                                                                         \
    inline constexpr char _##name##_base[] = #name;                                                                    \
    inline constexpr ::idfxx::event_base<id_enum> name {                                                               \
        _##name##_base                                                                                                 \
    }

namespace idfxx {

template<typename IdEnum>
class event_base;

template<typename IdEnum>
struct event_type;

/**
 * @headerfile <idfxx/event>
 * @brief Typed event base template.
 *
 * Represents a category of related events, parameterized by an enum type
 * that defines the specific event IDs within that category. This ensures
 * type safety when registering listeners and posting events.
 *
 * @tparam IdEnum The enum type for event IDs within this base.
 *
 * @code
 * // Define a custom event category
 * enum class my_event : int32_t { started, stopped };
 * IDFXX_EVENT_DEFINE_BASE(my_events, my_event);
 *
 * // Use with ESP-IDF event categories
 * inline constexpr event_base<wifi_event_t> wifi{WIFI_EVENT};
 * @endcode
 */
template<typename IdEnum>
class event_base {
public:
    /** @brief The enum type for event IDs. */
    using id_type = IdEnum;

    /**
     * @brief Constructs from an ESP-IDF event base pointer.
     *
     * @param base The event base pointer (must have static storage duration).
     *
     * @warning For custom event bases, use the IDFXX_EVENT_DEFINE_BASE macro
     * instead of this constructor. The macro creates the underlying ESP-IDF
     * event base with proper static storage duration.
     *
     * Only use this constructor directly when wrapping existing ESP-IDF system event
     * bases (e.g., `WIFI_EVENT`, `IP_EVENT`):
     *
     * @code
     * // Wrap ESP-IDF system events - use constructor directly
     * inline constexpr event_base<wifi_event_t> wifi{WIFI_EVENT};
     * inline constexpr event_base<ip_event_t> ip{IP_EVENT};
     *
     * // Define custom event bases - use the macro instead
     * enum class my_event_id : int32_t { started, stopped };
     * IDFXX_EVENT_DEFINE_BASE(my_events, my_event_id);
     * @endcode
     */
    constexpr event_base(esp_event_base_t base) noexcept
        : _base(base) {}

    /**
     * @brief Returns the underlying ESP-IDF event base.
     * @return The esp_event_base_t value.
     */
    [[nodiscard]] constexpr esp_event_base_t idf_base() const noexcept { return _base; }

    /**
     * @brief Creates an event_type combining this base with a specific ID.
     * @param id The event ID.
     * @return An event_type pairing this base with the ID.
     *
     * @code
     * auto evt = events::wifi(WIFI_EVENT_STA_START);
     * loop.try_listener_add(evt, callback);
     * @endcode
     */
    [[nodiscard]] constexpr event_type<IdEnum> operator()(IdEnum id) const;

private:
    esp_event_base_t _base;
};

/**
 * @headerfile <idfxx/event>
 * @brief Combines an event base with a specific event ID.
 *
 * @tparam IdEnum The enum type for the event ID.
 *
 * @code
 * event_type evt{events::wifi, WIFI_EVENT_STA_START};
 * loop.try_listener_add(evt, callback);
 * @endcode
 */
template<typename IdEnum>
struct event_type {
    /** @brief The event base. */
    event_base<IdEnum> base;
    /** @brief The event ID. */
    IdEnum id;

    /**
     * @brief Returns the underlying ESP-IDF event base.
     * @return The esp_event_base_t value.
     */
    [[nodiscard]] constexpr esp_event_base_t idf_base() const { return base.idf_base(); }

    /**
     * @brief Returns the event ID as an int32_t.
     * @return The event ID cast to int32_t.
     */
    [[nodiscard]] constexpr int32_t idf_id() const { return static_cast<int32_t>(id); }
};

// CTAD guide
template<typename IdEnum>
event_type(event_base<IdEnum>, IdEnum) -> event_type<IdEnum>;

// Implement operator() after event_type is defined
template<typename IdEnum>
constexpr event_type<IdEnum> event_base<IdEnum>::operator()(IdEnum id) const {
    return {*this, id};
}

/**
 * @headerfile <idfxx/event>
 * @brief Callback type for event listeners.
 *
 * Invoked when an event matching the registered base and/or ID is dispatched.
 *
 * @tparam IdEnum The event ID enum type.
 */
template<typename IdEnum>
using event_callback = std::move_only_function<void(event_base<IdEnum> base, IdEnum id, void* event_data) const>;

class user_event_loop;

/**
 * @headerfile <idfxx/event>
 * @brief Base class for event loops.
 *
 * Provides listener registration and event posting operations.
 *
 * @code
 * void register_listeners(event_loop& loop) {
 *     loop.listener_add(my_events, my_event::started,
 *         [](auto, auto id, void*) { idfxx::log::info("event", "Event received"); });
 * }
 *
 * // Use with user-created loop (with task)
 * auto loop = event_loop::make_user(32, {.name = "events"});
 * register_listeners(*loop);
 *
 * // Use with system loop
 * event_loop::create_system();
 * register_listeners(event_loop::system());
 * @endcode
 */
class event_loop {
public:
    class listener_handle;
    class unique_listener_handle;

    // =========================================================================
    // System (default) loop lifecycle
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates the system (default) event loop.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    inline static void create_system() { unwrap(try_create_system()); }

    /**
     * @brief Destroys the system (default) event loop.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    inline static void destroy_system() { unwrap(try_destroy_system()); }
#endif

    /**
     * @brief Creates the system (default) event loop.
     *
     * @return Success or an error if already created or on failure.
     */
    [[nodiscard]] static result<void> try_create_system();

    /**
     * @brief Destroys the system (default) event loop.
     *
     * @return Success or an error.
     */
    [[nodiscard]] static result<void> try_destroy_system();

    /**
     * @brief Returns a reference to the system (default) event loop.
     *
     * @return A reference to the event_loop representing the system event loop.
     * @note The system loop must have been created via create_system() first.
     */
    [[nodiscard]] static event_loop& system();

    // =========================================================================
    // User event loop creation
    // =========================================================================

    /**
     * @brief Configuration for a dedicated event dispatch task.
     */
    struct task_config {
        /** @brief Name of the task. */
        std::string_view name;
        /** @brief Stack size for the task. */
        size_t stack_size = 2048;
        /** @brief Priority for the task. */
        unsigned int priority = 5;
        /**
         * @brief Core affinity for the task.
         * @note If nullopt, the task can run on any core.
         */
        std::optional<core_id> core_affinity = std::nullopt;
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a user event loop without a dedicated task.
     *
     * Events must be dispatched manually via user_event_loop::run().
     *
     * @param queue_size Maximum number of events in the queue.
     * @return A user event loop with manual dispatch support.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    static std::unique_ptr<user_event_loop> make_user(size_t queue_size = 32);

    /**
     * @brief Creates a user event loop with a dedicated dispatch task.
     *
     * The task automatically dispatches events from the queue.
     *
     * @param task Configuration for the dedicated dispatch task.
     * @param queue_size Maximum number of events in the queue.
     * @return An event loop with automatic dispatch.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    static std::unique_ptr<event_loop> make_user(task_config task, size_t queue_size = 32);
#endif

    /**
     * @brief Creates a user event loop without a dedicated task.
     *
     * Events must be dispatched manually via user_event_loop::try_run().
     *
     * @param queue_size Maximum number of events in the queue.
     * @return A user event loop with manual dispatch support, or an error.
     */
    [[nodiscard]] static result<std::unique_ptr<user_event_loop>> try_make_user(size_t queue_size = 32);

    /**
     * @brief Creates a user event loop with a dedicated dispatch task.
     *
     * The task automatically dispatches events from the queue.
     *
     * @param task Configuration for the dedicated dispatch task.
     * @param queue_size Maximum number of events in the queue.
     * @return An event loop with automatic dispatch, or an error.
     */
    [[nodiscard]] static result<std::unique_ptr<event_loop>> try_make_user(task_config task, size_t queue_size = 32);

    // =========================================================================
    // Listener registration (base + specific id)
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Registers a typed listener for a specific event.
     *
     * @tparam IdEnum The event ID enum type.
     * @param base The event base.
     * @param id The specific event ID to listen for.
     * @param callback Function called when the event occurs. Takes ownership of the callback.
     * @return A listener handle for the registered listener.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    template<typename IdEnum>
    listener_handle
    listener_add(event_base<IdEnum> base, IdEnum id, event_callback<std::type_identity_t<IdEnum>> callback);

    /**
     * @brief Registers a typed listener for a specific event.
     *
     * @tparam IdEnum The event ID enum type.
     * @param event The event type (base + id).
     * @param callback Function called when the event occurs. Takes ownership of the callback.
     * @return A listener handle for the registered listener.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    template<typename IdEnum>
    listener_handle listener_add(event_type<IdEnum> event, event_callback<std::type_identity_t<IdEnum>> callback);

    /**
     * @brief Registers a typed listener for any event from a base.
     *
     * @tparam IdEnum The event ID enum type.
     * @param base The event base.
     * @param callback Function called when any event from this base occurs. Takes ownership of the callback.
     * @return A listener handle for the registered listener.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    template<typename IdEnum>
    listener_handle listener_add(event_base<IdEnum> base, event_callback<std::type_identity_t<IdEnum>> callback);
#endif

    /**
     * @brief Registers a typed listener for a specific event.
     *
     * @tparam IdEnum The event ID enum type.
     * @param base The event base.
     * @param id The specific event ID to listen for.
     * @param callback Function called when the event occurs. Takes ownership of the callback.
     * @return A listener handle, or an error.
     */
    template<typename IdEnum>
    [[nodiscard]] result<listener_handle>
    try_listener_add(event_base<IdEnum> base, IdEnum id, event_callback<std::type_identity_t<IdEnum>> callback);

    /**
     * @brief Registers a typed listener for a specific event.
     *
     * @tparam IdEnum The event ID enum type.
     * @param event The event type (base + id).
     * @param callback Function called when the event occurs. Takes ownership of the callback.
     * @return A listener handle, or an error.
     */
    template<typename IdEnum>
    [[nodiscard]] result<listener_handle>
    try_listener_add(event_type<IdEnum> event, event_callback<std::type_identity_t<IdEnum>> callback);

    /**
     * @brief Registers a typed listener for any event from a base.
     *
     * @tparam IdEnum The event ID enum type.
     * @param base The event base.
     * @param callback Function called when any event from this base occurs. Takes ownership of the callback.
     * @return A listener handle, or an error.
     */
    template<typename IdEnum>
    [[nodiscard]] result<listener_handle>
    try_listener_add(event_base<IdEnum> base, event_callback<std::type_identity_t<IdEnum>> callback);

    // =========================================================================
    // Listener removal
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Removes a listener by handle.
     *
     * @param handle The listener handle to remove.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    void listener_remove(listener_handle handle);
#endif

    /**
     * @brief Removes a listener by handle.
     *
     * @param handle The listener handle to remove.
     * @return Success or an error.
     */
    [[nodiscard]] result<void> try_listener_remove(listener_handle handle);

    // =========================================================================
    // Event posting
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Posts a typed event, waiting indefinitely.
     *
     * @tparam IdEnum The event ID enum type.
     * @param base The event base.
     * @param id The event ID.
     * @param data Optional event data.
     * @param size Size of the event data.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    template<typename IdEnum>
    void post(event_base<IdEnum> base, IdEnum id, const void* data = nullptr, size_t size = 0) {
        unwrap(try_post(base, id, data, size));
    }

    /**
     * @brief Posts a typed event with a timeout.
     *
     * @tparam IdEnum The event ID enum type.
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param base The event base.
     * @param id The event ID.
     * @param data Event data.
     * @param size Size of the event data.
     * @param timeout Maximum time to wait for space in the event queue.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure or timeout.
     */
    template<typename IdEnum, typename Rep, typename Period>
    void post(
        event_base<IdEnum> base,
        IdEnum id,
        const void* data,
        size_t size,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        unwrap(try_post(base, id, data, size, timeout));
    }

    /**
     * @brief Posts a typed event via event_type, waiting indefinitely.
     *
     * @tparam IdEnum The event ID enum type.
     * @param event The event type (base + id).
     * @param data Optional event data.
     * @param size Size of the event data.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    template<typename IdEnum>
    void post(event_type<IdEnum> event, const void* data = nullptr, size_t size = 0) {
        unwrap(try_post(event, data, size));
    }

    /**
     * @brief Posts a typed event via event_type with a timeout.
     *
     * @tparam IdEnum The event ID enum type.
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param event The event type (base + id).
     * @param data Event data.
     * @param size Size of the event data.
     * @param timeout Maximum time to wait for space in the event queue.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure or timeout.
     */
    template<typename IdEnum, typename Rep, typename Period>
    void
    post(event_type<IdEnum> event, const void* data, size_t size, const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_post(event, data, size, timeout));
    }
#endif

    /**
     * @brief Posts a typed event, waiting indefinitely.
     *
     * @tparam IdEnum The event ID enum type.
     * @param base The event base.
     * @param id The event ID.
     * @param data Optional event data.
     * @param size Size of the event data.
     * @return Success or an error.
     */
    template<typename IdEnum>
    [[nodiscard]] result<void>
    try_post(event_base<IdEnum> base, IdEnum id, const void* data = nullptr, size_t size = 0) {
        if (_handle == nullptr) {
            return wrap(esp_event_post(base.idf_base(), static_cast<int32_t>(id), data, size, portMAX_DELAY));
        }
        return wrap(esp_event_post_to(_handle, base.idf_base(), static_cast<int32_t>(id), data, size, portMAX_DELAY));
    }

    /**
     * @brief Posts a typed event with a timeout.
     *
     * @tparam IdEnum The event ID enum type.
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param base The event base.
     * @param id The event ID.
     * @param data Event data.
     * @param size Size of the event data.
     * @param timeout Maximum time to wait for space in the event queue.
     * @return Success or an error (e.g., timeout).
     */
    template<typename IdEnum, typename Rep, typename Period>
    [[nodiscard]] result<void> try_post(
        event_base<IdEnum> base,
        IdEnum id,
        const void* data,
        size_t size,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        auto ticks = chrono::ticks(timeout);
        if (_handle == nullptr) {
            return wrap(esp_event_post(base.idf_base(), static_cast<int32_t>(id), data, size, ticks));
        }
        return wrap(esp_event_post_to(_handle, base.idf_base(), static_cast<int32_t>(id), data, size, ticks));
    }

    /**
     * @brief Posts a typed event via event_type, waiting indefinitely.
     *
     * @tparam IdEnum The event ID enum type.
     * @param event The event type (base + id).
     * @param data Optional event data.
     * @param size Size of the event data.
     * @return Success or an error.
     */
    template<typename IdEnum>
    [[nodiscard]] result<void> try_post(event_type<IdEnum> event, const void* data = nullptr, size_t size = 0) {
        return try_post(event.base, event.id, data, size);
    }

    /**
     * @brief Posts a typed event via event_type with a timeout.
     *
     * @tparam IdEnum The event ID enum type.
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param event The event type (base + id).
     * @param data Event data.
     * @param size Size of the event data.
     * @param timeout Maximum time to wait for space in the event queue.
     * @return Success or an error (e.g., timeout).
     */
    template<typename IdEnum, typename Rep, typename Period>
    [[nodiscard]] result<void> try_post(
        event_type<IdEnum> event,
        const void* data,
        size_t size,
        const std::chrono::duration<Rep, Period>& timeout
    ) {
        return try_post(event.base, event.id, data, size, timeout);
    }

    /**
     * @brief Returns the underlying ESP-IDF event loop handle.
     * @return The esp_event_loop_handle_t, or nullptr for the system loop.
     */
    [[nodiscard]] esp_event_loop_handle_t idf_handle() const { return _handle; }

    /**
     * @brief Virtual destructor. Deletes the owned event loop handle if non-null.
     */
    virtual ~event_loop();

    event_loop(const event_loop&) = delete;
    event_loop& operator=(const event_loop&) = delete;
    event_loop(event_loop&&) = delete;
    event_loop& operator=(event_loop&&) = delete;

protected:
    /**
     * @brief Constructs an event_loop with the given handle.
     * @param handle The ESP-IDF event loop handle, or nullptr for the system loop.
     */
    explicit event_loop(esp_event_loop_handle_t handle)
        : _handle(handle) {}

private:
    static result<listener_handle> register_listener(
        esp_event_loop_handle_t loop,
        esp_event_base_t base,
        int32_t id,
        std::move_only_function<void(esp_event_base_t, int32_t, void*) const> callback
    );

    template<typename IdEnum>
    static result<listener_handle>
    register_listener(esp_event_loop_handle_t loop, esp_event_base_t base, int32_t id, event_callback<IdEnum> callback);

    static result<void> unregister_listener(
        esp_event_loop_handle_t loop,
        esp_event_handler_instance_t instance,
        esp_event_base_t base,
        int32_t id
    );

    esp_event_loop_handle_t _handle;
};

/**
 * @headerfile <idfxx/event>
 * @brief Handle to a registered event listener.
 *
 * Returned by listener registration methods. This handle does not provide
 * RAII semantics - use unique_listener_handle for automatic cleanup.
 *
 * @note This handle can be copied freely. The listener remains registered
 *       until explicitly removed via try_listener_remove().
 */
class event_loop::listener_handle {
    friend class event_loop;
    friend class unique_listener_handle;

public:
    /** @brief Default constructor creates an invalid handle. */
    listener_handle() = default;

private:
    listener_handle(
        esp_event_loop_handle_t loop,
        esp_event_handler_instance_t instance,
        esp_event_base_t base,
        int32_t id
    )
        : _loop(loop)
        , _instance(instance)
        , _base(base)
        , _id(id) {}

    esp_event_loop_handle_t _loop = nullptr;
    esp_event_handler_instance_t _instance = nullptr;
    esp_event_base_t _base = nullptr;
    int32_t _id = 0;
};

/**
 * @headerfile <idfxx/event>
 * @brief RAII handle for event listener registration.
 *
 * Automatically removes the listener when destroyed. Move-only.
 */
class event_loop::unique_listener_handle {
public:
    /**
     * @brief Constructs an empty handle.
     */
    unique_listener_handle() noexcept = default;

    /**
     * @brief Takes ownership of a listener_handle.
     * @param handle The handle to take ownership of.
     */
    explicit unique_listener_handle(listener_handle handle) noexcept
        : _handle(handle) {}

    /**
     * @brief Move constructor.
     */
    unique_listener_handle(unique_listener_handle&& other) noexcept
        : _handle(other._handle) {
        other._handle = listener_handle{};
    }

    /**
     * @brief Move assignment.
     */
    unique_listener_handle& operator=(unique_listener_handle&& other) noexcept {
        if (this != &other) {
            reset();
            _handle = other._handle;
            other._handle = listener_handle{};
        }
        return *this;
    }

    unique_listener_handle(const unique_listener_handle&) = delete;
    unique_listener_handle& operator=(const unique_listener_handle&) = delete;

    /**
     * @brief Destructor. Removes the listener if owned.
     */
    ~unique_listener_handle() { reset(); }

    /**
     * @brief Releases ownership without removing the listener.
     * @return The underlying listener_handle.
     */
    [[nodiscard]] listener_handle release() noexcept {
        auto h = _handle;
        _handle = listener_handle{};
        return h;
    }

    /**
     * @brief Removes the listener and resets to empty.
     */
    void reset() noexcept;

    /**
     * @brief Checks if this handle owns a listener.
     * @return true if a listener is owned.
     */
    [[nodiscard]] explicit operator bool() const noexcept { return _handle._instance != nullptr; }

private:
    listener_handle _handle;
};

/**
 * @headerfile <idfxx/event>
 * @brief User-created event loop with manual dispatch.
 *
 * Inherits all listener registration and event posting operations from
 * event_loop, and adds manual dispatch via run()/try_run(). Created
 * via event_loop::make_user() or event_loop::try_make_user() without
 * a task_config.
 *
 * @code
 * // Create loop without task (manual dispatch)
 * auto loop = event_loop::make_user(16);
 * loop->listener_add(my_events, my_event::started, callback);
 * loop->post(my_events, my_event::started);
 * loop->run(100ms);
 *
 * // Generic code accepts event_loop&
 * void register_handlers(event_loop& loop) {
 *     loop.listener_add(my_events, my_event::started, callback);
 * }
 * register_handlers(*loop);
 * @endcode
 */
class user_event_loop : public event_loop {
    friend class event_loop;

public:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    // =========================================================================
    // Manual dispatch (for loops without dedicated task)
    // =========================================================================

    /**
     * @brief Dispatches pending events.
     *
     * Call this to manually dispatch events from the queue.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param duration Maximum time to spend dispatching events.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure.
     */
    template<typename Rep, typename Period>
    void run(const std::chrono::duration<Rep, Period>& duration) {
        unwrap(try_run(duration));
    }
#endif

    /**
     * @brief Dispatches pending events.
     *
     * Call this to manually dispatch events from the queue.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param duration Maximum time to spend dispatching events.
     * @return Success or an error.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_run(const std::chrono::duration<Rep, Period>& duration) {
        return wrap(esp_event_loop_run(idf_handle(), chrono::ticks(duration)));
    }

    user_event_loop(const user_event_loop&) = delete;
    user_event_loop& operator=(const user_event_loop&) = delete;
    user_event_loop(user_event_loop&&) = delete;
    user_event_loop& operator=(user_event_loop&&) = delete;

private:
    explicit user_event_loop(esp_event_loop_handle_t handle)
        : event_loop(handle) {}
};

// =============================================================================
// event_loop template implementations
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
inline std::unique_ptr<user_event_loop> event_loop::make_user(size_t queue_size) {
    auto result = try_make_user(queue_size);
    return std::move(result).transform_error([](auto ec) -> std::error_code { throw std::system_error(ec); }).value();
}

inline std::unique_ptr<event_loop> event_loop::make_user(task_config task, size_t queue_size) {
    auto result = try_make_user(std::move(task), queue_size);
    return std::move(result).transform_error([](auto ec) -> std::error_code { throw std::system_error(ec); }).value();
}

template<typename IdEnum>
event_loop::listener_handle
event_loop::listener_add(event_base<IdEnum> base, IdEnum id, event_callback<std::type_identity_t<IdEnum>> callback) {
    return unwrap(try_listener_add(base, id, std::move(callback)));
}

template<typename IdEnum>
event_loop::listener_handle
event_loop::listener_add(event_base<IdEnum> base, event_callback<std::type_identity_t<IdEnum>> callback) {
    return unwrap(try_listener_add(base, std::move(callback)));
}

template<typename IdEnum>
event_loop::listener_handle
event_loop::listener_add(event_type<IdEnum> event, event_callback<std::type_identity_t<IdEnum>> callback) {
    return unwrap(try_listener_add(event, std::move(callback)));
}

inline void event_loop::listener_remove(listener_handle handle) {
    unwrap(try_listener_remove(handle));
}
#endif

template<typename IdEnum>
result<event_loop::listener_handle> event_loop::register_listener(
    esp_event_loop_handle_t loop,
    esp_event_base_t base,
    int32_t id,
    event_callback<IdEnum> callback
) {
    return register_listener(loop, base, id, [cb = std::move(callback)](esp_event_base_t b, int32_t i, void* data) {
        cb(event_base<IdEnum>{b}, static_cast<IdEnum>(i), data);
    });
}

template<typename IdEnum>
result<event_loop::listener_handle> event_loop::try_listener_add(
    event_base<IdEnum> base,
    IdEnum id,
    event_callback<std::type_identity_t<IdEnum>> callback
) {
    return register_listener<IdEnum>(_handle, base.idf_base(), static_cast<int32_t>(id), std::move(callback));
}

template<typename IdEnum>
result<event_loop::listener_handle>
event_loop::try_listener_add(event_base<IdEnum> base, event_callback<std::type_identity_t<IdEnum>> callback) {
    return register_listener<IdEnum>(_handle, base.idf_base(), ESP_EVENT_ANY_ID, std::move(callback));
}

template<typename IdEnum>
result<event_loop::listener_handle>
event_loop::try_listener_add(event_type<IdEnum> event, event_callback<std::type_identity_t<IdEnum>> callback) {
    return try_listener_add(event.base, event.id, std::move(callback));
}

/** @} */ // end of idfxx_event

} // namespace idfxx
