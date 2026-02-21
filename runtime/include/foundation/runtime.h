#ifndef FOUNDATION_RUNTIME_H
#define FOUNDATION_RUNTIME_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

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

typedef enum fdn_task_poll {
    FDN_TASK_PENDING = 0,
    FDN_TASK_READY = 1,
} fdn_task_poll;

typedef fdn_task_poll (*fdn_task_poll_fn)(void *frame, bool cancellation_requested);
typedef void (*fdn_task_move_result_fn)(void *frame, void *result);
typedef void (*fdn_task_drop_frame_fn)(void *frame);
typedef void (*fdn_channel_drop_value_fn)(void *value);
typedef void (*fdn_blocking_work_fn)(void *context);

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
size_t fdn_bounds_check(int32_t index, size_t length);
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
bool fdn_blocking_poll(fdn_blocking_job **job, void *context,
                       fdn_blocking_work_fn work);
size_t fdn_blocking_live_jobs(void);

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
int32_t fdn_i32_add(int32_t left, int32_t right);
int32_t fdn_i32_subtract(int32_t left, int32_t right);
int32_t fdn_i32_multiply(int32_t left, int32_t right);
int32_t fdn_i32_divide(int32_t left, int32_t right);
int32_t fdn_i32_remainder(int32_t left, int32_t right);
int32_t fdn_i32_negate(int32_t value);
uint64_t fdn_u64_add(uint64_t left, uint64_t right);
uint64_t fdn_u64_subtract(uint64_t left, uint64_t right);
uint64_t fdn_u64_multiply(uint64_t left, uint64_t right);
uint64_t fdn_u64_divide(uint64_t left, uint64_t right);
uint64_t fdn_u64_remainder(uint64_t left, uint64_t right);
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
uint64_t foundation_runtime_string_builder_open(void);
void foundation_runtime_string_builder_append(uint64_t handle, const fdn_string *value);
bool foundation_runtime_string_builder_append_code_point(uint64_t handle, uint64_t value);
fdn_string foundation_runtime_string_builder_finish(uint64_t handle);
void foundation_runtime_string_builder_close(uint64_t handle);
uint64_t foundation_runtime_string_builder_live_handles(void);
fdn_string foundation_runtime_format_i32(int32_t value);
fdn_string foundation_runtime_format_u64(uint64_t value);
uint64_t foundation_runtime_time_unix_seconds(void);
int32_t foundation_runtime_time_format_utc(uint64_t unix_seconds, fdn_string *result);
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
uint64_t foundation_runtime_fs_live_directories(void);

#ifdef __cplusplus
}
#endif

#endif
