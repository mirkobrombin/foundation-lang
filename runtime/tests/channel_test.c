#include "foundation/runtime.h"

#include <assert.h>
#include <stdint.h>

typedef struct send_frame {
    fdn_channel *sender;
    int32_t value;
    fdn_channel_status status;
    int *polls;
} send_frame;

typedef struct receive_frame {
    fdn_channel *receiver;
    int32_t value;
    fdn_channel_status status;
    int *polls;
    bool *cancelled_seen;
} receive_frame;

typedef struct parent_frame {
    fdn_channel *receiver;
    fdn_task *child;
    fdn_channel_status status;
    int *child_polls;
    bool *cancelled_seen;
} parent_frame;

typedef struct yield_frame {
    bool yielded;
} yield_frame;

typedef struct close_frame {
    fdn_channel *endpoint;
    bool sender;
} close_frame;

typedef struct select_frame {
    fdn_channel *channels[2];
    fdn_channel_select_kind kinds[2];
    int32_t values[2];
    size_t count;
    size_t selected;
    uint64_t deadline;
    fdn_channel_status status;
    int context;
    int *polls;
    bool *cancelled_seen;
} select_frame;

typedef struct select_result {
    int32_t value;
    size_t selected;
    fdn_channel_status status;
} select_result;

static int dropped_values;

static void count_drop(void *raw) {
    int32_t *value = raw;
    dropped_values += *value;
}

typedef struct receive_result {
    int32_t value;
    fdn_channel_status status;
} receive_result;

static fdn_task_poll poll_send(void *raw, bool cancellation_requested) {
    send_frame *frame = raw;
    (void)cancellation_requested;
    ++*frame->polls;
    frame->status = fdn_channel_poll_send(frame->sender, &frame->value);
    return frame->status == FDN_CHANNEL_PENDING ? FDN_TASK_PENDING : FDN_TASK_READY;
}

static void move_send_result(void *raw, void *output) {
    send_frame *frame = raw;
    *(fdn_channel_status *)output = frame->status;
}

static void drop_send_frame(void *raw) {
    send_frame *frame = raw;
    fdn_channel_drop_sender(&frame->sender);
    fdn_dealloc(frame);
}

static fdn_task *spawn_send(fdn_channel *sender, int32_t value, int *polls) {
    send_frame *frame = fdn_alloc(sizeof(*frame));
    frame->sender = sender;
    frame->value = value;
    frame->status = FDN_CHANNEL_PENDING;
    frame->polls = polls;
    return fdn_task_spawn(frame, poll_send, move_send_result, drop_send_frame);
}

static fdn_task_poll poll_receive(void *raw, bool cancellation_requested) {
    receive_frame *frame = raw;
    (void)cancellation_requested;
    ++*frame->polls;
    frame->status = fdn_channel_poll_receive(frame->receiver, &frame->value);
    if (frame->status == FDN_CHANNEL_CANCELLED && frame->cancelled_seen != NULL) {
        *frame->cancelled_seen = true;
    }
    return frame->status == FDN_CHANNEL_PENDING ? FDN_TASK_PENDING : FDN_TASK_READY;
}

static void move_receive_result(void *raw, void *output) {
    receive_frame *frame = raw;
    receive_result *result = output;
    result->value = frame->value;
    result->status = frame->status;
}

static void drop_receive_frame(void *raw) {
    receive_frame *frame = raw;
    fdn_channel_drop_receiver(&frame->receiver);
    fdn_dealloc(frame);
}

static fdn_task *spawn_receive(fdn_channel *receiver, int *polls,
                               bool *cancelled_seen) {
    receive_frame *frame = fdn_alloc(sizeof(*frame));
    frame->receiver = receiver;
    frame->value = 0;
    frame->status = FDN_CHANNEL_PENDING;
    frame->polls = polls;
    frame->cancelled_seen = cancelled_seen;
    return fdn_task_spawn(frame, poll_receive, move_receive_result,
                          drop_receive_frame);
}

static fdn_task_poll poll_parent(void *raw, bool cancellation_requested) {
    parent_frame *frame = raw;
    receive_result result;
    (void)cancellation_requested;
    if (frame->child == NULL) {
        frame->child =
            spawn_receive(frame->receiver, frame->child_polls, frame->cancelled_seen);
        frame->receiver = NULL;
    }
    if (!fdn_task_poll_wait(&frame->child, &result)) {
        return FDN_TASK_PENDING;
    }
    frame->status = result.status;
    return FDN_TASK_READY;
}

static void move_parent_result(void *raw, void *output) {
    parent_frame *frame = raw;
    *(fdn_channel_status *)output = frame->status;
}

static void drop_parent_frame(void *raw) {
    parent_frame *frame = raw;
    fdn_task_drop(&frame->child);
    fdn_channel_drop_receiver(&frame->receiver);
    fdn_dealloc(frame);
}

static fdn_task *spawn_parent(fdn_channel *receiver, int *child_polls,
                              bool *cancelled_seen) {
    parent_frame *frame = fdn_alloc(sizeof(*frame));
    frame->receiver = receiver;
    frame->child = NULL;
    frame->status = FDN_CHANNEL_PENDING;
    frame->child_polls = child_polls;
    frame->cancelled_seen = cancelled_seen;
    return fdn_task_spawn(frame, poll_parent, move_parent_result, drop_parent_frame);
}

static fdn_task_poll poll_yield(void *raw, bool cancellation_requested) {
    yield_frame *frame = raw;
    if (!frame->yielded && !cancellation_requested) {
        frame->yielded = true;
        return FDN_TASK_PENDING;
    }
    return FDN_TASK_READY;
}

static void move_yield_result(void *raw, void *output) {
    (void)raw;
    (void)output;
}

static void drop_yield_frame(void *raw) { fdn_dealloc(raw); }

static fdn_task *spawn_yield(void) {
    yield_frame *frame = fdn_alloc(sizeof(*frame));
    frame->yielded = false;
    return fdn_task_spawn(frame, poll_yield, move_yield_result, drop_yield_frame);
}

static fdn_task_poll poll_close(void *raw, bool cancellation_requested) {
    close_frame *frame = raw;
    (void)cancellation_requested;
    if (frame->sender) {
        fdn_channel_drop_sender(&frame->endpoint);
    } else {
        fdn_channel_drop_receiver(&frame->endpoint);
    }
    return FDN_TASK_READY;
}

static void move_close_result(void *raw, void *output) {
    (void)raw;
    (void)output;
}

static void drop_close_frame(void *raw) {
    close_frame *frame = raw;
    if (frame->sender) {
        fdn_channel_drop_sender(&frame->endpoint);
    } else {
        fdn_channel_drop_receiver(&frame->endpoint);
    }
    fdn_dealloc(frame);
}

static fdn_task *spawn_close(fdn_channel *endpoint, bool sender) {
    close_frame *frame = fdn_alloc(sizeof(*frame));
    frame->endpoint = endpoint;
    frame->sender = sender;
    return fdn_task_spawn(frame, poll_close, move_close_result, drop_close_frame);
}

static fdn_task_poll poll_select(void *raw, bool cancellation_requested) {
    select_frame *frame = raw;
    fdn_channel_select_case cases[2];
    (void)cancellation_requested;
    ++*frame->polls;
    for (size_t index = 0; index < frame->count; ++index) {
        cases[index].channel = frame->channels[index];
        cases[index].value = &frame->values[index];
        cases[index].kind = frame->kinds[index];
    }
    frame->status = fdn_channel_poll_select(
        &frame->context, cases, frame->count, frame->deadline, &frame->selected);
    if (frame->status == FDN_CHANNEL_CANCELLED && frame->cancelled_seen != NULL) {
        *frame->cancelled_seen = true;
    }
    return frame->status == FDN_CHANNEL_PENDING ? FDN_TASK_PENDING : FDN_TASK_READY;
}

static void move_select_result(void *raw, void *output) {
    select_frame *frame = raw;
    select_result *result = output;
    result->value = frame->selected < frame->count ? frame->values[frame->selected] : 0;
    result->selected = frame->selected;
    result->status = frame->status;
}

static void drop_select_frame(void *raw) {
    select_frame *frame = raw;
    for (size_t index = 0; index < frame->count; ++index) {
        if (frame->kinds[index] == FDN_CHANNEL_SELECT_SEND) {
            fdn_channel_drop_sender(&frame->channels[index]);
        } else {
            fdn_channel_drop_receiver(&frame->channels[index]);
        }
    }
    fdn_dealloc(frame);
}

static fdn_task *spawn_select(fdn_channel *first, fdn_channel_select_kind first_kind,
                              int32_t first_value, fdn_channel *second,
                              fdn_channel_select_kind second_kind, int32_t second_value,
                              uint64_t deadline, int *polls, bool *cancelled_seen) {
    select_frame *frame = fdn_alloc(sizeof(*frame));
    frame->channels[0] = first;
    frame->channels[1] = second;
    frame->kinds[0] = first_kind;
    frame->kinds[1] = second_kind;
    frame->values[0] = first_value;
    frame->values[1] = second_value;
    frame->count = second == NULL ? 1 : 2;
    frame->selected = SIZE_MAX;
    frame->deadline = deadline;
    frame->status = FDN_CHANNEL_PENDING;
    frame->context = 0;
    frame->polls = polls;
    frame->cancelled_seen = cancelled_seen;
    return fdn_task_spawn(frame, poll_select, move_select_result, drop_select_frame);
}

static void test_rendezvous_receive_first(void) {
    fdn_channel *sender;
    fdn_channel *receiver;
    fdn_task *send_task;
    fdn_task *receive_task;
    fdn_channel_status send_status;
    receive_result received;
    int send_polls = 0;
    int receive_polls = 0;

    fdn_channel_open(sizeof(int32_t), 0, NULL, &sender, &receiver);
    receive_task = spawn_receive(receiver, &receive_polls, NULL);
    receiver = NULL;
    send_task = spawn_send(sender, 42, &send_polls);
    sender = NULL;

    fdn_task_wait(&receive_task, &received);
    fdn_task_wait(&send_task, &send_status);
    assert(received.status == FDN_CHANNEL_READY);
    assert(received.value == 42);
    assert(send_status == FDN_CHANNEL_READY);
    assert(receive_polls == 2);
    assert(send_polls == 1);
}

static void test_rendezvous_send_first(void) {
    fdn_channel *sender;
    fdn_channel *receiver;
    fdn_task *send_task;
    fdn_task *receive_task;
    fdn_channel_status send_status;
    receive_result received;
    int send_polls = 0;
    int receive_polls = 0;

    fdn_channel_open(sizeof(int32_t), 0, NULL, &sender, &receiver);
    send_task = spawn_send(sender, 21, &send_polls);
    sender = NULL;
    receive_task = spawn_receive(receiver, &receive_polls, NULL);
    receiver = NULL;

    fdn_task_wait(&send_task, &send_status);
    fdn_task_wait(&receive_task, &received);
    assert(send_status == FDN_CHANNEL_READY);
    assert(received.status == FDN_CHANNEL_READY);
    assert(received.value == 21);
    assert(send_polls == 2);
    assert(receive_polls == 1);
}

static void test_void_rendezvous(void) {
    fdn_channel *sender;
    fdn_channel *receiver;
    fdn_task *send_task;
    fdn_task *receive_task;
    fdn_channel_status send_status;
    receive_result received;
    int polls = 0;

    fdn_channel_open(0, 0, NULL, &sender, &receiver);
    receive_task = spawn_receive(receiver, &polls, NULL);
    receiver = NULL;
    send_task = spawn_send(sender, 0, &polls);
    sender = NULL;
    fdn_task_wait(&receive_task, &received);
    fdn_task_wait(&send_task, &send_status);
    assert(received.status == FDN_CHANNEL_READY);
    assert(send_status == FDN_CHANNEL_READY);
}

static void test_buffer_and_close(void) {
    fdn_channel *sender;
    fdn_channel *receiver;
    fdn_task *send_task;
    fdn_task *receive_task;
    fdn_channel_status send_status;
    receive_result received;
    int send_polls = 0;
    int receive_polls = 0;

    fdn_channel_open(sizeof(int32_t), 1, NULL, &sender, &receiver);
    send_task = spawn_send(sender, 7, &send_polls);
    sender = NULL;
    fdn_task_wait(&send_task, &send_status);
    assert(send_status == FDN_CHANNEL_READY);
    assert(send_polls == 1);

    receive_task = spawn_receive(receiver, &receive_polls, NULL);
    receiver = NULL;
    fdn_task_wait(&receive_task, &received);
    assert(received.status == FDN_CHANNEL_READY);
    assert(received.value == 7);
    assert(receive_polls == 1);
}

static void test_buffer_fifo_and_clones(void) {
    fdn_channel *sender;
    fdn_channel *receiver;
    fdn_task *first_send;
    fdn_task *second_send;
    fdn_task *first_receive;
    fdn_task *second_receive;
    fdn_channel_status send_status;
    receive_result first;
    receive_result second;
    int send_polls = 0;
    int receive_polls = 0;

    fdn_channel_open(sizeof(int32_t), 2, NULL, &sender, &receiver);
    first_send = spawn_send(fdn_channel_clone_sender(sender), 10, &send_polls);
    second_send = spawn_send(sender, 20, &send_polls);
    sender = NULL;
    fdn_task_wait(&first_send, &send_status);
    assert(send_status == FDN_CHANNEL_READY);
    fdn_task_wait(&second_send, &send_status);
    assert(send_status == FDN_CHANNEL_READY);

    first_receive =
        spawn_receive(fdn_channel_clone_receiver(receiver), &receive_polls, NULL);
    second_receive = spawn_receive(receiver, &receive_polls, NULL);
    receiver = NULL;
    fdn_task_wait(&first_receive, &first);
    fdn_task_wait(&second_receive, &second);
    assert(first.status == FDN_CHANNEL_READY);
    assert(first.value == 10);
    assert(second.status == FDN_CHANNEL_READY);
    assert(second.value == 20);
    assert(send_polls == 2);
    assert(receive_polls == 2);
}

static void test_buffer_drop(void) {
    fdn_channel *sender;
    fdn_channel *receiver;
    fdn_task *first_send;
    fdn_task *second_send;
    fdn_channel_status send_status;
    int polls = 0;

    dropped_values = 0;
    fdn_channel_open(sizeof(int32_t), 2, count_drop, &sender, &receiver);
    first_send = spawn_send(fdn_channel_clone_sender(sender), 3, &polls);
    second_send = spawn_send(sender, 4, &polls);
    sender = NULL;
    fdn_task_wait(&first_send, &send_status);
    assert(send_status == FDN_CHANNEL_READY);
    fdn_task_wait(&second_send, &send_status);
    assert(send_status == FDN_CHANNEL_READY);
    fdn_channel_drop_receiver(&receiver);
    assert(dropped_values == 7);
}

static void test_closed_endpoints(void) {
    fdn_channel *sender;
    fdn_channel *receiver;
    fdn_task *task;
    fdn_channel_status send_status;
    receive_result received;
    int polls = 0;

    fdn_channel_open(sizeof(int32_t), 0, NULL, &sender, &receiver);
    fdn_channel_drop_sender(&sender);
    task = spawn_receive(receiver, &polls, NULL);
    receiver = NULL;
    fdn_task_wait(&task, &received);
    assert(received.status == FDN_CHANNEL_CLOSED);
    assert(polls == 1);

    polls = 0;
    fdn_channel_open(sizeof(int32_t), 0, NULL, &sender, &receiver);
    fdn_channel_drop_receiver(&receiver);
    task = spawn_send(sender, 9, &polls);
    sender = NULL;
    fdn_task_wait(&task, &send_status);
    assert(send_status == FDN_CHANNEL_CLOSED);
    assert(polls == 1);
}

static void test_cancel_before_receive(void) {
    fdn_channel *sender;
    fdn_channel *receiver;
    fdn_task *task;
    int polls = 0;

    fdn_channel_open(sizeof(int32_t), 0, NULL, &sender, &receiver);
    task = spawn_receive(receiver, &polls, NULL);
    receiver = NULL;
    fdn_task_drop(&task);
    fdn_channel_drop_sender(&sender);
    assert(polls == 1);
}

static void test_close_wakes_parked_operations(void) {
    fdn_channel *sender;
    fdn_channel *receiver;
    fdn_task *operation;
    fdn_task *closer;
    fdn_channel_status send_status;
    receive_result received;
    int polls = 0;

    fdn_channel_open(sizeof(int32_t), 0, NULL, &sender, &receiver);
    operation = spawn_receive(receiver, &polls, NULL);
    receiver = NULL;
    closer = spawn_close(sender, true);
    sender = NULL;
    fdn_task_wait(&operation, &received);
    fdn_task_wait(&closer, NULL);
    assert(received.status == FDN_CHANNEL_CLOSED);
    assert(polls == 2);

    polls = 0;
    fdn_channel_open(sizeof(int32_t), 0, NULL, &sender, &receiver);
    operation = spawn_send(sender, 11, &polls);
    sender = NULL;
    closer = spawn_close(receiver, false);
    receiver = NULL;
    fdn_task_wait(&operation, &send_status);
    fdn_task_wait(&closer, NULL);
    assert(send_status == FDN_CHANNEL_CLOSED);
    assert(polls == 2);
}

static void test_cancel_parked_receive(void) {
    fdn_channel *sender;
    fdn_channel *receiver;
    fdn_task *parent;
    fdn_task *yielding;
    bool cancelled_seen = false;
    int child_polls = 0;

    fdn_channel_open(sizeof(int32_t), 0, NULL, &sender, &receiver);
    parent = spawn_parent(receiver, &child_polls, &cancelled_seen);
    receiver = NULL;
    yielding = spawn_yield();
    fdn_task_wait(&yielding, NULL);
    assert(child_polls == 1);

    fdn_task_drop(&parent);
    fdn_channel_drop_sender(&sender);
    assert(cancelled_seen);
    assert(child_polls == 2);
}

static void test_select_source_order(void) {
    fdn_channel *first_sender;
    fdn_channel *first_receiver;
    fdn_channel *second_sender;
    fdn_channel *second_receiver;
    fdn_task *first_send;
    fdn_task *second_send;
    fdn_task *selection;
    fdn_channel_status sent;
    select_result result;
    int polls = 0;

    fdn_channel_open(sizeof(int32_t), 1, NULL, &first_sender, &first_receiver);
    fdn_channel_open(sizeof(int32_t), 1, NULL, &second_sender, &second_receiver);
    first_send = spawn_send(first_sender, 11, &polls);
    first_sender = NULL;
    second_send = spawn_send(second_sender, 22, &polls);
    second_sender = NULL;
    fdn_task_wait(&first_send, &sent);
    assert(sent == FDN_CHANNEL_READY);
    fdn_task_wait(&second_send, &sent);
    assert(sent == FDN_CHANNEL_READY);

    polls = 0;
    selection = spawn_select(first_receiver, FDN_CHANNEL_SELECT_RECEIVE, 0,
                             second_receiver, FDN_CHANNEL_SELECT_RECEIVE, 0,
                             UINT64_MAX, &polls, NULL);
    first_receiver = NULL;
    second_receiver = NULL;
    fdn_task_wait(&selection, &result);
    assert(result.status == FDN_CHANNEL_READY);
    assert(result.selected == 0);
    assert(result.value == 11);
    assert(polls == 1);
}

static void test_select_rendezvous(void) {
    fdn_channel *sender;
    fdn_channel *receiver;
    fdn_task *receiving;
    fdn_task *sending;
    select_result received;
    select_result sent;
    int receive_polls = 0;
    int send_polls = 0;

    fdn_channel_open(sizeof(int32_t), 0, NULL, &sender, &receiver);
    receiving = spawn_select(receiver, FDN_CHANNEL_SELECT_RECEIVE, 0, NULL,
                             FDN_CHANNEL_SELECT_RECEIVE, 0, UINT64_MAX,
                             &receive_polls, NULL);
    receiver = NULL;
    sending = spawn_select(sender, FDN_CHANNEL_SELECT_SEND, 42, NULL,
                           FDN_CHANNEL_SELECT_SEND, 0, UINT64_MAX,
                           &send_polls, NULL);
    sender = NULL;
    fdn_task_wait(&receiving, &received);
    fdn_task_wait(&sending, &sent);
    assert(received.status == FDN_CHANNEL_READY);
    assert(received.value == 42);
    assert(sent.status == FDN_CHANNEL_READY);
    assert(receive_polls == 2);
    assert(send_polls == 1);
}

static void test_select_timeout_close_and_cancel(void) {
    fdn_channel *sender;
    fdn_channel *receiver;
    fdn_task *selection;
    fdn_task *yielding;
    select_result result;
    bool cancelled_seen = false;
    int polls = 0;

    fdn_channel_open(sizeof(int32_t), 0, NULL, &sender, &receiver);
    selection = spawn_select(receiver, FDN_CHANNEL_SELECT_RECEIVE, 0, NULL,
                             FDN_CHANNEL_SELECT_RECEIVE, 0,
                             fdn_monotonic_nanoseconds() + UINT64_C(1000000),
                             &polls, NULL);
    receiver = NULL;
    fdn_task_wait(&selection, &result);
    assert(result.status == FDN_CHANNEL_TIMEOUT);
    assert(polls == 2);
    fdn_channel_drop_sender(&sender);

    polls = 0;
    fdn_channel_open(sizeof(int32_t), 0, NULL, &sender, &receiver);
    fdn_channel_drop_sender(&sender);
    selection = spawn_select(receiver, FDN_CHANNEL_SELECT_RECEIVE, 0, NULL,
                             FDN_CHANNEL_SELECT_RECEIVE, 0, UINT64_MAX,
                             &polls, NULL);
    receiver = NULL;
    fdn_task_wait(&selection, &result);
    assert(result.status == FDN_CHANNEL_CLOSED);
    assert(result.selected == 0);
    assert(polls == 1);

    polls = 0;
    fdn_channel_open(sizeof(int32_t), 0, NULL, &sender, &receiver);
    selection = spawn_select(receiver, FDN_CHANNEL_SELECT_RECEIVE, 0, NULL,
                             FDN_CHANNEL_SELECT_RECEIVE, 0, UINT64_MAX,
                             &polls, &cancelled_seen);
    receiver = NULL;
    yielding = spawn_yield();
    fdn_task_wait(&yielding, NULL);
    assert(polls == 1);
    fdn_task_drop(&selection);
    assert(cancelled_seen);
    assert(polls == 2);
    fdn_channel_drop_sender(&sender);
}

int main(void) {
    test_rendezvous_receive_first();
    test_rendezvous_send_first();
    test_void_rendezvous();
    test_buffer_and_close();
    test_buffer_fifo_and_clones();
    test_buffer_drop();
    test_closed_endpoints();
    test_cancel_before_receive();
    test_close_wakes_parked_operations();
    test_cancel_parked_receive();
    test_select_source_order();
    test_select_rendezvous();
    test_select_timeout_close_and_cancel();
    assert(fdn_channel_live_count() == 0);
    assert(fdn_task_live_count() == 0);
    assert(fdn_live_allocations() == 0);
    return 0;
}
