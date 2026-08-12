#ifndef FOUNDATION_RUNTIME_H
#define FOUNDATION_RUNTIME_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <float.h>

#ifdef __cplusplus
static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24,
              "Foundation f32 requires IEEE 754 binary32");
static_assert(sizeof(double) == 8 && FLT_RADIX == 2 && DBL_MANT_DIG == 53,
              "Foundation f64 requires IEEE 754 binary64");
#else
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24,
               "Foundation f32 requires IEEE 754 binary32");
_Static_assert(sizeof(double) == 8 && FLT_RADIX == 2 && DBL_MANT_DIG == 53,
               "Foundation f64 requires IEEE 754 binary64");
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fdn_frame {
    struct fdn_frame *previous;
    const char *package_name;
    const char *function_name;
    const char *source_file;
    uint32_t line;
    uint32_t column;
    uint8_t native_boundary;
} fdn_frame;

typedef struct fdn_string {
    const char *data;
    size_t length;
    uint8_t owned;
} fdn_string;

typedef struct fdn_task fdn_task;
typedef struct fdn_channel fdn_channel;
typedef struct fdn_blocking_job fdn_blocking_job;
typedef struct fdn_reactor_operation fdn_reactor_operation;

typedef enum fdn_task_poll {
    FDN_TASK_PENDING = 0,
    FDN_TASK_READY = 1,
} fdn_task_poll;

typedef fdn_task_poll (*fdn_task_poll_fn)(void *frame, bool cancellation_requested);
typedef void (*fdn_task_move_result_fn)(void *frame, void *result);
typedef void (*fdn_task_drop_frame_fn)(void *frame);
typedef void (*fdn_channel_drop_value_fn)(void *value);
typedef void (*fdn_blocking_work_fn)(void *context);
typedef void (*fdn_reactor_start_fn)(void *context,
                                     fdn_reactor_operation *operation);
typedef void (*fdn_reactor_cancel_fn)(void *context);

typedef enum fdn_channel_status {
    FDN_CHANNEL_PENDING = 0,
    FDN_CHANNEL_READY = 1,
    FDN_CHANNEL_CLOSED = 2,
    FDN_CHANNEL_CANCELLED = 3,
    FDN_CHANNEL_TIMEOUT = 4,
} fdn_channel_status;

typedef enum fdn_channel_select_kind {
    FDN_CHANNEL_SELECT_SEND = 1,
    FDN_CHANNEL_SELECT_RECEIVE = 2,
} fdn_channel_select_kind;

typedef struct fdn_channel_select_case {
    fdn_channel *channel;
    void *value;
    fdn_channel_select_kind kind;
} fdn_channel_select_case;

static inline fdn_string fdn_string_static(const char *data, size_t length) {
    fdn_string value = {data, length, 0};
    return value;
}

void fdn_frame_enter(fdn_frame *frame, const char *package_name, const char *function_name,
                     const char *source_file, uint32_t line, uint32_t column);
void fdn_frame_enter_native(fdn_frame *frame, const char *function_name, const char *source_file,
                            uint32_t line, uint32_t column);
void fdn_frame_leave(fdn_frame *frame);
static inline void fdn_frame_location(fdn_frame *frame, uint32_t line, uint32_t column) {
    frame->line = line;
    frame->column = column;
}

fdn_string fdn_string_move(fdn_string *value);
void fdn_string_drop(fdn_string *value);
fdn_string fdn_string_concat(fdn_string left, fdn_string right);
int fdn_string_equal(fdn_string left, fdn_string right);
void fdn_println(fdn_string value);
void fdn_abi_string_concat(fdn_string *result, const fdn_string *left,
                           const fdn_string *right);
int fdn_abi_string_equal(const fdn_string *left, const fdn_string *right);
void fdn_abi_println(const fdn_string *value);
void fdn_abi_panic(const fdn_string *message);
size_t fdn_bounds_check(size_t index, size_t length);
void *fdn_alloc(size_t size);
void fdn_dealloc(void *value);
size_t fdn_total_allocations(void);
size_t fdn_total_deallocations(void);
size_t fdn_live_allocations(void);
fdn_task *fdn_task_spawn(void *frame, fdn_task_poll_fn poll,
                         fdn_task_move_result_fn move_result,
                         fdn_task_drop_frame_fn drop_frame);
bool fdn_task_poll_wait(fdn_task **task, void *result);
void fdn_task_wait(fdn_task **task, void *result);
void fdn_task_drop(fdn_task **task);
bool fdn_task_cancellation_requested(const fdn_task *task);
bool fdn_task_cancellation_enter(bool requested);
void fdn_task_cancellation_leave(bool previous);
bool fdn_task_cancellation_current(void);
size_t fdn_task_live_count(void);
uint64_t foundation_runtime_supervisor_open(void);
void foundation_runtime_supervisor_adopt(uint64_t handle, fdn_task *task);
void foundation_runtime_supervisor_wait(uint64_t handle);
void foundation_runtime_supervisor_cancel(uint64_t handle);
void foundation_runtime_supervisor_release(uint64_t handle);
uint64_t foundation_runtime_supervisor_live_count(void);
uint64_t foundation_runtime_pool_open(uint64_t workers);
void foundation_runtime_pool_submit(uint64_t handle, fdn_task *task);
void foundation_runtime_pool_wait(uint64_t handle);
void foundation_runtime_pool_cancel(uint64_t handle);
void foundation_runtime_pool_release(uint64_t handle);
uint64_t foundation_runtime_pool_live_count(void);
bool fdn_utf8_valid(const char *value, size_t length);
bool fdn_blocking_poll(fdn_blocking_job **job, void *context,
                       fdn_blocking_work_fn work);
size_t fdn_blocking_live_jobs(void);
bool fdn_reactor_poll(fdn_reactor_operation **operation, void *context,
                      fdn_reactor_start_fn start, fdn_reactor_cancel_fn cancel,
                      int32_t *status);
void fdn_reactor_complete(fdn_reactor_operation *operation, int32_t status);
size_t fdn_reactor_live_operations(void);

void fdn_channel_open(size_t value_size, size_t capacity,
                      fdn_channel_drop_value_fn drop_value, fdn_channel **sender,
                      fdn_channel **receiver);
fdn_channel *fdn_channel_clone_sender(fdn_channel *sender);
fdn_channel *fdn_channel_clone_receiver(fdn_channel *receiver);
void fdn_channel_drop_sender(fdn_channel **sender);
void fdn_channel_drop_receiver(fdn_channel **receiver);
fdn_channel_status fdn_channel_poll_send(fdn_channel *sender, void *value);
fdn_channel_status fdn_channel_poll_receive(fdn_channel *receiver, void *value);
fdn_channel_status fdn_channel_poll_select(
    void *context, const fdn_channel_select_case *cases, size_t count,
    uint64_t deadline_nanoseconds, size_t *selected);
uint64_t fdn_monotonic_nanoseconds(void);
void fdn_retry_wait(uint32_t retry_index);
size_t fdn_channel_live_count(void);
#ifdef __cplusplus
[[noreturn]] void fdn_panic(fdn_string message);
[[noreturn]] void fdn_panic_cstr(const char *message);
[[noreturn]] void fdn_invalid_enum_tag(void);
#else
_Noreturn void fdn_panic(fdn_string message);
_Noreturn void fdn_panic_cstr(const char *message);
_Noreturn void fdn_invalid_enum_tag(void);
#endif
#define FDN_DECLARE_SIGNED_ARITHMETIC(TYPE, NAME) \
    TYPE fdn_##NAME##_add(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_subtract(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_multiply(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_divide(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_remainder(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_negate(TYPE value)

#define FDN_DECLARE_UNSIGNED_ARITHMETIC(TYPE, NAME) \
    TYPE fdn_##NAME##_add(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_subtract(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_multiply(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_divide(TYPE left, TYPE right); \
    TYPE fdn_##NAME##_remainder(TYPE left, TYPE right)

FDN_DECLARE_SIGNED_ARITHMETIC(int8_t, i8);
FDN_DECLARE_SIGNED_ARITHMETIC(int16_t, i16);
FDN_DECLARE_SIGNED_ARITHMETIC(int32_t, i32);
FDN_DECLARE_SIGNED_ARITHMETIC(int64_t, i64);
FDN_DECLARE_SIGNED_ARITHMETIC(intptr_t, isize);
FDN_DECLARE_UNSIGNED_ARITHMETIC(uint8_t, u8);
FDN_DECLARE_UNSIGNED_ARITHMETIC(uint16_t, u16);
FDN_DECLARE_UNSIGNED_ARITHMETIC(uint32_t, u32);
FDN_DECLARE_UNSIGNED_ARITHMETIC(uint64_t, u64);
FDN_DECLARE_UNSIGNED_ARITHMETIC(size_t, usize);

#undef FDN_DECLARE_SIGNED_ARITHMETIC
#undef FDN_DECLARE_UNSIGNED_ARITHMETIC
int32_t foundation_runtime_env_read(const fdn_string *name, fdn_string *value);
fdn_string foundation_runtime_string_copy(const fdn_string *value);
uint64_t foundation_runtime_string_byte_length(const fdn_string *value);
bool foundation_runtime_string_contains(const fdn_string *value, const fdn_string *part);
bool foundation_runtime_string_starts_with(const fdn_string *value, const fdn_string *prefix);
bool foundation_runtime_string_ends_with(const fdn_string *value, const fdn_string *suffix);
int32_t foundation_runtime_string_slice(const fdn_string *value, uint64_t start, uint64_t end,
                                        fdn_string *result);
int32_t foundation_runtime_string_byte_at(const fdn_string *value, uint64_t index,
                                          uint64_t *result);
bool foundation_runtime_string_find(const fdn_string *value, const fdn_string *part,
                                    uint64_t *result);
int32_t foundation_runtime_string_compare(const fdn_string *left, const fdn_string *right);
uint64_t foundation_runtime_string_hash_fnv1a(const fdn_string *value);
uint64_t foundation_runtime_string_builder_open(void);
void foundation_runtime_string_builder_append(uint64_t handle, const fdn_string *value);
bool foundation_runtime_string_builder_append_code_point(uint64_t handle, uint64_t value);
fdn_string foundation_runtime_string_builder_finish(uint64_t handle);
void foundation_runtime_string_builder_close(uint64_t handle);
uint64_t foundation_runtime_string_builder_live_handles(void);
int32_t foundation_runtime_guard_roles_open(uint64_t limit, uint64_t *result);
int32_t foundation_runtime_guard_roles_add(uint64_t handle, const fdn_string *role);
bool foundation_runtime_guard_roles_has(uint64_t handle, const fdn_string *role);
int32_t foundation_runtime_guard_roles_count(uint64_t handle, uint64_t *result);
int32_t foundation_runtime_guard_roles_at(uint64_t handle, uint64_t index,
                                          fdn_string *result);
void foundation_runtime_guard_roles_close(uint64_t *handle);
int32_t foundation_runtime_guard_relationships_open(uint64_t limit, uint64_t *result);
int32_t foundation_runtime_guard_relationships_add(uint64_t handle,
                                                   const fdn_string *identity,
                                                   const fdn_string *role);
int32_t foundation_runtime_guard_relationships_roles(uint64_t handle,
                                                     const fdn_string *identity,
                                                     uint64_t *result);
void foundation_runtime_guard_relationships_close(uint64_t *handle);
fdn_string foundation_runtime_format_bool(bool value);
fdn_string foundation_runtime_format_i8(int8_t value);
fdn_string foundation_runtime_format_i16(int16_t value);
fdn_string foundation_runtime_format_i32(int32_t value);
fdn_string foundation_runtime_format_i64(int64_t value);
fdn_string foundation_runtime_format_isize(intptr_t value);
fdn_string foundation_runtime_format_u8(uint8_t value);
fdn_string foundation_runtime_format_u16(uint16_t value);
fdn_string foundation_runtime_format_u32(uint32_t value);
fdn_string foundation_runtime_format_u64(uint64_t value);
fdn_string foundation_runtime_format_usize(size_t value);
fdn_string foundation_runtime_format_f32(float value);
fdn_string foundation_runtime_format_f64(double value);
int32_t foundation_runtime_parse_f32(const fdn_string *value, float *result);
int32_t foundation_runtime_parse_f64(const fdn_string *value, double *result);
uint64_t foundation_runtime_bytes_from_text(const fdn_string *value);
int32_t foundation_runtime_bytes_copy(uint64_t handle, uint64_t *result);
int32_t foundation_runtime_bytes_length(uint64_t handle, uint64_t *result);
int32_t foundation_runtime_bytes_at(uint64_t handle, uint64_t index, uint64_t *result);
int32_t foundation_runtime_bytes_to_text(uint64_t handle, fdn_string *result);
int32_t foundation_runtime_bytes_slice(uint64_t handle, uint64_t start, uint64_t end,
                                       uint64_t *result);
void foundation_runtime_bytes_close(uint64_t *handle);
int32_t foundation_runtime_bytes_builder_open(uint64_t limit, uint64_t *result);
int32_t foundation_runtime_bytes_builder_length(uint64_t handle, uint64_t *result);
int32_t foundation_runtime_bytes_builder_append_byte(uint64_t handle, uint64_t value);
int32_t foundation_runtime_bytes_builder_append(uint64_t handle, uint64_t value);
int32_t foundation_runtime_bytes_builder_finish(uint64_t *handle, uint64_t *result);
void foundation_runtime_bytes_builder_close(uint64_t *handle);
int32_t foundation_runtime_base64url_encode(uint64_t handle, fdn_string *result);
int32_t foundation_runtime_base64url_decode(const fdn_string *value, uint64_t *result);
int32_t foundation_runtime_base64_encode(uint64_t handle, fdn_string *result);
int32_t foundation_runtime_base64_decode(const fdn_string *value, uint64_t *result);
int32_t foundation_runtime_hmac_sha256(uint64_t key_handle, uint64_t value_handle,
                                       uint64_t *result);
bool foundation_runtime_bytes_constant_time_equal(uint64_t left_handle,
                                                  uint64_t right_handle);
int32_t foundation_runtime_aes256_gcm_encrypt(uint64_t key_handle,
                                              uint64_t value_handle,
                                              const fdn_string *associated_data,
                                              uint64_t *result);
int32_t foundation_runtime_aes256_gcm_decrypt(uint64_t key_handle,
                                              uint64_t value_handle,
                                              const fdn_string *associated_data,
                                              uint64_t *result);
uint64_t foundation_runtime_secret_memory_open(void);
uint64_t foundation_runtime_secret_memory_retain(uint64_t handle);
int32_t foundation_runtime_secret_memory_set(uint64_t handle,
                                             const fdn_string *key,
                                             uint64_t value_handle);
int32_t foundation_runtime_secret_memory_get(uint64_t handle,
                                             const fdn_string *key,
                                             uint64_t *result);
int32_t foundation_runtime_secret_memory_delete(uint64_t handle,
                                                const fdn_string *key);
void foundation_runtime_secret_memory_close(uint64_t *handle);
uint64_t foundation_runtime_time_unix_seconds(void);
uint64_t foundation_runtime_time_monotonic_nanoseconds(void);
int32_t foundation_runtime_time_format_utc(uint64_t unix_seconds, fdn_string *result);
int32_t foundation_runtime_time_parse_duration(const fdn_string *value, int64_t *result);
bool foundation_runtime_resiliency_finite(double value);
int32_t foundation_runtime_resiliency_retry_delay(
    int64_t initial_nanoseconds, int64_t max_nanoseconds, double factor,
    double jitter, uint64_t attempt, int64_t *result);
uint64_t foundation_runtime_bulkhead_open(uint64_t max_concurrent,
                                          uint64_t max_queue);
void foundation_runtime_bulkhead_retain(uint64_t handle);
void foundation_runtime_bulkhead_release(uint64_t handle);
void foundation_runtime_bulkhead_acquire(uint64_t handle,
                                         fdn_reactor_operation *operation);
void foundation_runtime_bulkhead_cancel(fdn_reactor_operation *operation);
void foundation_runtime_bulkhead_permit_release(uint64_t handle);
uint64_t foundation_runtime_bulkhead_live_handles(void);
uint64_t foundation_runtime_bulkhead_live_waiters(void);
void foundation_runtime_uuid_v4(uint64_t *high, uint64_t *low);
void foundation_runtime_uuid_v7(uint64_t *high, uint64_t *low);
uint64_t foundation_runtime_cancellation_open(void);
uint64_t foundation_runtime_cancellation_retain(uint64_t handle);
void foundation_runtime_cancellation_request(uint64_t handle);
bool foundation_runtime_cancellation_requested(uint64_t handle);
void foundation_runtime_cancellation_release(uint64_t handle);
uint64_t foundation_runtime_cancellation_live_states(void);
int32_t foundation_runtime_fs_open_lines(const fdn_string *path, uint64_t *handle);
int32_t foundation_runtime_fs_next_line(uint64_t handle, fdn_string *line);
int32_t foundation_runtime_fs_next_line_limited(uint64_t handle, uint64_t max_length,
                                                fdn_string *line);
int32_t foundation_runtime_fs_close(uint64_t handle);
int32_t foundation_runtime_fs_size(const fdn_string *path, uint64_t *size);
uint64_t foundation_runtime_fs_live_handles(void);
int32_t foundation_runtime_fs_open_directory(const fdn_string *path, uint64_t *handle);
int32_t foundation_runtime_fs_next_directory(uint64_t handle, fdn_string *name);
int32_t foundation_runtime_fs_close_directory(uint64_t handle);
int32_t foundation_runtime_fs_is_directory(const fdn_string *path, bool *result);
int32_t foundation_runtime_fs_modified(const fdn_string *path, uint64_t *unix_seconds);
int32_t foundation_runtime_fs_read_text_limited(const fdn_string *path, uint64_t max_length,
                                                fdn_string *result);
int32_t foundation_runtime_fs_read_private_text_limited(const fdn_string *path,
                                                        uint64_t max_length,
                                                        fdn_string *result);
int32_t foundation_runtime_fs_create_private_directory(const fdn_string *path);
int32_t foundation_runtime_fs_write_private_text_atomic(const fdn_string *path,
                                                        const fdn_string *value,
                                                        uint64_t max_length);
int32_t foundation_runtime_fs_delete_private_file(const fdn_string *path);
uint64_t foundation_runtime_fs_live_directories(void);
int32_t foundation_runtime_fs_tree_open(const fdn_string *path,
                                        uint64_t max_entries,
                                        uint64_t max_path_length,
                                        uint64_t *handle);
int32_t foundation_runtime_fs_tree_next(uint64_t handle, fdn_string *path,
                                        uint32_t *kind, bool *executable,
                                        uint64_t *size);
int32_t foundation_runtime_fs_tree_read(uint64_t handle,
                                        const fdn_string *relative_path,
                                        uint64_t max_length,
                                        uint64_t *bytes_handle);
int32_t foundation_runtime_fs_tree_close(uint64_t *handle);
int32_t foundation_runtime_fs_root_open(const fdn_string *path,
                                        uint64_t *handle);
int32_t foundation_runtime_fs_root_create_directory(
    uint64_t handle, const fdn_string *relative_path);
int32_t foundation_runtime_fs_root_write_file(
    uint64_t handle, const fdn_string *relative_path, uint64_t bytes_handle,
    uint32_t permissions);
int32_t foundation_runtime_fs_root_close(uint64_t *handle);
int32_t foundation_runtime_net_resolve(const fdn_string *host, uint64_t port,
                                       uint64_t *addresses);
void foundation_runtime_net_addresses_close(uint64_t addresses);
int32_t foundation_runtime_net_listen(const fdn_string *address, uint64_t port,
                                      uint64_t backlog, uint64_t *listener,
                                      uint64_t *bound_port);
void foundation_runtime_net_listener_close(uint64_t listener);
void foundation_runtime_net_accept_start(uint64_t listener, uint64_t *connection,
                                         fdn_reactor_operation *operation);
void foundation_runtime_net_accept_cancel(fdn_reactor_operation *operation);
void foundation_runtime_net_connect_start(uint64_t addresses, uint64_t *connection,
                                          fdn_reactor_operation *operation);
void foundation_runtime_net_connect_until_start(
    uint64_t addresses, uint64_t deadline, uint64_t *connection,
    fdn_reactor_operation *operation);
void foundation_runtime_net_connect_cancel(fdn_reactor_operation *operation);
int32_t foundation_runtime_net_peer_address(uint64_t connection,
                                            fdn_string *address);
int32_t foundation_runtime_net_split(uint64_t connection, uint64_t *reader,
                                     uint64_t *writer);
void foundation_runtime_net_connection_close(uint64_t connection);
void foundation_runtime_net_reader_close(uint64_t reader);
void foundation_runtime_net_writer_close(uint64_t writer);
void foundation_runtime_net_read_line_start(uint64_t reader, uint64_t limit,
                                            fdn_string *line,
                                            fdn_reactor_operation *operation);
void foundation_runtime_net_read_line_until_start(
    uint64_t reader, uint64_t limit, uint64_t deadline, fdn_string *line,
    fdn_reactor_operation *operation);
void foundation_runtime_net_read_line_cancel(fdn_reactor_operation *operation);
void foundation_runtime_net_read_exact_start(uint64_t reader, uint64_t length,
                                             fdn_string *text,
                                             fdn_reactor_operation *operation);
void foundation_runtime_net_read_exact_until_start(
    uint64_t reader, uint64_t length, uint64_t deadline, fdn_string *text,
    fdn_reactor_operation *operation);
void foundation_runtime_net_read_exact_cancel(fdn_reactor_operation *operation);
void foundation_runtime_net_read_exact_bytes_start(
    uint64_t reader, uint64_t length, uint64_t *bytes,
    fdn_reactor_operation *operation);
void foundation_runtime_net_read_exact_bytes_until_start(
    uint64_t reader, uint64_t length, uint64_t deadline, uint64_t *bytes,
    fdn_reactor_operation *operation);
void foundation_runtime_net_read_some_bytes_start(
    uint64_t reader, uint64_t limit, uint64_t *bytes,
    fdn_reactor_operation *operation);
void foundation_runtime_net_read_some_bytes_until_start(
    uint64_t reader, uint64_t limit, uint64_t deadline, uint64_t *bytes,
    fdn_reactor_operation *operation);
void foundation_runtime_net_read_some_bytes_cancel(
    fdn_reactor_operation *operation);
void foundation_runtime_net_write_all_start(uint64_t writer, const fdn_string *text,
                                            fdn_reactor_operation *operation);
void foundation_runtime_net_write_all_until_start(
    uint64_t writer, const fdn_string *text, uint64_t deadline,
    fdn_reactor_operation *operation);
void foundation_runtime_net_write_all_cancel(fdn_reactor_operation *operation);
void foundation_runtime_net_write_all_bytes_start(
    uint64_t writer, uint64_t bytes, fdn_reactor_operation *operation);
void foundation_runtime_net_write_all_bytes_until_start(
    uint64_t writer, uint64_t bytes, uint64_t deadline,
    fdn_reactor_operation *operation);
uint64_t foundation_runtime_net_live_addresses(void);
uint64_t foundation_runtime_net_live_listeners(void);
uint64_t foundation_runtime_net_live_connections(void);
uint64_t foundation_runtime_net_live_requests(void);
uint64_t foundation_runtime_net_live_services(void);
int32_t foundation_runtime_plugin_open(const fdn_string *path, uint64_t *handle,
                                       fdn_string *name, fdn_string *detail);
int32_t foundation_runtime_plugin_start(uint64_t handle, fdn_string *detail);
int32_t foundation_runtime_plugin_stop(uint64_t handle, fdn_string *detail);
int32_t foundation_runtime_plugin_close(uint64_t *handle, fdn_string *detail);
uint64_t foundation_runtime_plugin_live_handles(void);
int32_t foundation_runtime_plugin_sandbox_open(const fdn_string *path,
                                               uint64_t *handle,
                                               fdn_string *detail);
int32_t foundation_runtime_plugin_sandbox_add_argument(uint64_t handle,
                                                       const fdn_string *argument,
                                                       fdn_string *detail);
int32_t foundation_runtime_plugin_sandbox_start(uint64_t handle,
                                                uint64_t timeout_nanoseconds,
                                                fdn_string *ready,
                                                fdn_string *detail);
int32_t foundation_runtime_plugin_sandbox_stop(uint64_t handle,
                                               uint64_t timeout_nanoseconds,
                                               int32_t *exit_code,
                                               fdn_string *detail);
void foundation_runtime_plugin_sandbox_abort(uint64_t handle);
void foundation_runtime_plugin_sandbox_close(uint64_t *handle);
uint64_t foundation_runtime_plugin_sandbox_live_handles(void);
uint64_t foundation_runtime_plugin_sandbox_live_processes(void);

#ifdef __cplusplus
}
#endif

#endif
