#include "foundation/runtime.h"
#include "task_internal.h"

#include <stdint.h>
#include <string.h>

enum {
    FDN_CHANNEL_WAIT_SEND = 1,
    FDN_CHANNEL_WAIT_RECEIVE = 2,
};

typedef struct fdn_channel_message {
    struct fdn_channel_message *next;
    unsigned char value[];
} fdn_channel_message;

struct fdn_channel {
    size_t value_size;
    size_t capacity;
    size_t buffered;
    size_t senders;
    size_t receivers;
    fdn_channel_drop_value_fn drop_value;
    fdn_channel_message *message_head;
    fdn_channel_message *message_tail;
    fdn_task *send_head;
    fdn_task *send_tail;
    fdn_task *receive_head;
    fdn_task *receive_tail;
};

static size_t fdn_channel_count;

static void fdn_channel_copy(const fdn_channel *channel, void *target,
                             const void *source) {
    if (channel->value_size == 0) {
        return;
    }
    if (target == NULL || source == NULL) {
        fdn_panic_cstr("channel value is null");
    }
    memcpy(target, source, channel->value_size);
}

static void fdn_channel_wait_push(fdn_task **head, fdn_task **tail, fdn_task *task) {
    if (task->wait_next != NULL) {
        fdn_panic_cstr("task is already in a channel wait queue");
    }
    if (*tail == NULL) {
        *head = task;
    } else {
        (*tail)->wait_next = task;
    }
    *tail = task;
}

static fdn_task *fdn_channel_wait_pop(fdn_task **head, fdn_task **tail) {
    fdn_task *task = *head;
    if (task == NULL) {
        return NULL;
    }
    *head = task->wait_next;
    if (*head == NULL) {
        *tail = NULL;
    }
    task->wait_next = NULL;
    return task;
}

static bool fdn_channel_wait_remove(fdn_task **head, fdn_task **tail,
                                    fdn_task *target) {
    fdn_task *previous = NULL;
    fdn_task *current = *head;
    while (current != NULL) {
        if (current == target) {
            if (previous == NULL) {
                *head = current->wait_next;
            } else {
                previous->wait_next = current->wait_next;
            }
            if (*tail == current) {
                *tail = previous;
            }
            current->wait_next = NULL;
            return true;
        }
        previous = current;
        current = current->wait_next;
    }
    return false;
}

static void fdn_channel_cancel_wait(fdn_task *task, void *context) {
    fdn_channel *channel = context;
    bool removed;
    if (task->wait_kind == FDN_CHANNEL_WAIT_SEND) {
        removed = fdn_channel_wait_remove(&channel->send_head, &channel->send_tail, task);
    } else if (task->wait_kind == FDN_CHANNEL_WAIT_RECEIVE) {
        removed =
            fdn_channel_wait_remove(&channel->receive_head, &channel->receive_tail, task);
    } else {
        fdn_panic_cstr("invalid channel wait kind");
    }
    if (!removed) {
        fdn_panic_cstr("channel waiter is missing");
    }
    fdn_task_wake(task, FDN_CHANNEL_CANCELLED);
}

static void fdn_channel_wake_all(fdn_task **head, fdn_task **tail,
                                 fdn_channel_status status) {
    fdn_task *task;
    while ((task = fdn_channel_wait_pop(head, tail)) != NULL) {
        fdn_task_wake(task, (unsigned int)status);
    }
}

static void fdn_channel_drop_message(fdn_channel *channel,
                                     fdn_channel_message *message) {
    if (channel->drop_value != NULL) {
        channel->drop_value(message->value);
    }
    fdn_dealloc(message);
}

static void fdn_channel_drop_messages(fdn_channel *channel) {
    fdn_channel_message *message = channel->message_head;
    while (message != NULL) {
        fdn_channel_message *next = message->next;
        fdn_channel_drop_message(channel, message);
        message = next;
    }
    channel->message_head = NULL;
    channel->message_tail = NULL;
    channel->buffered = 0;
}

static void fdn_channel_release_if_unused(fdn_channel *channel) {
    if (channel->senders != 0 || channel->receivers != 0) {
        return;
    }
    if (channel->message_head != NULL || channel->send_head != NULL ||
        channel->receive_head != NULL) {
        fdn_panic_cstr("unused channel still owns state");
    }
    --fdn_channel_count;
    fdn_dealloc(channel);
}

static void fdn_channel_buffer_push(fdn_channel *channel, const void *value) {
    fdn_channel_message *message;
    if (channel->value_size > SIZE_MAX - sizeof(*message)) {
        fdn_panic_cstr("channel value size overflow");
    }
    message = fdn_alloc(sizeof(*message) + channel->value_size);
    message->next = NULL;
    fdn_channel_copy(channel, message->value, value);
    if (channel->message_tail == NULL) {
        channel->message_head = message;
    } else {
        channel->message_tail->next = message;
    }
    channel->message_tail = message;
    ++channel->buffered;
}

static void fdn_channel_fill_buffer_from_sender(fdn_channel *channel) {
    fdn_task *sender;
    if (channel->buffered >= channel->capacity || channel->receivers == 0) {
        return;
    }
    sender = fdn_channel_wait_pop(&channel->send_head, &channel->send_tail);
    if (sender == NULL) {
        return;
    }
    fdn_channel_buffer_push(channel, sender->wait_value);
    fdn_task_wake(sender, FDN_CHANNEL_READY);
}

void fdn_channel_open(size_t value_size, size_t capacity,
                      fdn_channel_drop_value_fn drop_value, fdn_channel **sender,
                      fdn_channel **receiver) {
    fdn_channel *channel;
    if (sender == NULL || receiver == NULL || sender == receiver) {
        fdn_panic_cstr("invalid channel endpoints");
    }
    channel = fdn_alloc(sizeof(*channel));
    channel->value_size = value_size;
    channel->capacity = capacity;
    channel->buffered = 0;
    channel->senders = 1;
    channel->receivers = 1;
    channel->drop_value = drop_value;
    channel->message_head = NULL;
    channel->message_tail = NULL;
    channel->send_head = NULL;
    channel->send_tail = NULL;
    channel->receive_head = NULL;
    channel->receive_tail = NULL;
    ++fdn_channel_count;
    *sender = channel;
    *receiver = channel;
}

fdn_channel *fdn_channel_clone_sender(fdn_channel *sender) {
    if (sender == NULL || sender->senders == 0 || sender->senders == SIZE_MAX) {
        fdn_panic_cstr("invalid channel sender clone");
    }
    ++sender->senders;
    return sender;
}

fdn_channel *fdn_channel_clone_receiver(fdn_channel *receiver) {
    if (receiver == NULL || receiver->receivers == 0 || receiver->receivers == SIZE_MAX) {
        fdn_panic_cstr("invalid channel receiver clone");
    }
    ++receiver->receivers;
    return receiver;
}

void fdn_channel_drop_sender(fdn_channel **sender) {
    fdn_channel *channel;
    if (sender == NULL || *sender == NULL) {
        return;
    }
    channel = *sender;
    *sender = NULL;
    if (channel->senders == 0) {
        fdn_panic_cstr("channel sender count underflow");
    }
    --channel->senders;
    if (channel->senders == 0) {
        fdn_channel_wake_all(&channel->receive_head, &channel->receive_tail,
                             FDN_CHANNEL_CLOSED);
    }
    fdn_channel_release_if_unused(channel);
}

void fdn_channel_drop_receiver(fdn_channel **receiver) {
    fdn_channel *channel;
    if (receiver == NULL || *receiver == NULL) {
        return;
    }
    channel = *receiver;
    *receiver = NULL;
    if (channel->receivers == 0) {
        fdn_panic_cstr("channel receiver count underflow");
    }
    --channel->receivers;
    if (channel->receivers == 0) {
        fdn_channel_drop_messages(channel);
        fdn_channel_wake_all(&channel->send_head, &channel->send_tail,
                             FDN_CHANNEL_CLOSED);
    }
    fdn_channel_release_if_unused(channel);
}

fdn_channel_status fdn_channel_poll_send(fdn_channel *sender, void *value) {
    fdn_task *current;
    fdn_task *receiver;
    unsigned int status;
    if (sender == NULL || sender->senders == 0) {
        fdn_panic_cstr("invalid channel sender");
    }
    current = fdn_task_current_get();
    if (current == NULL) {
        fdn_panic_cstr("channel send requires an active task");
    }
    if (fdn_task_take_wake(sender, FDN_CHANNEL_WAIT_SEND, &status)) {
        return (fdn_channel_status)status;
    }
    if (current->cancellation_requested) {
        return FDN_CHANNEL_CANCELLED;
    }
    if (sender->receivers == 0) {
        return FDN_CHANNEL_CLOSED;
    }
    receiver = fdn_channel_wait_pop(&sender->receive_head, &sender->receive_tail);
    if (receiver != NULL) {
        fdn_channel_copy(sender, receiver->wait_value, value);
        fdn_task_wake(receiver, FDN_CHANNEL_READY);
        return FDN_CHANNEL_READY;
    }
    if (sender->buffered < sender->capacity) {
        fdn_channel_buffer_push(sender, value);
        return FDN_CHANNEL_READY;
    }
    fdn_task_park_current(sender, value, FDN_CHANNEL_WAIT_SEND, fdn_channel_cancel_wait);
    fdn_channel_wait_push(&sender->send_head, &sender->send_tail, current);
    return FDN_CHANNEL_PENDING;
}

fdn_channel_status fdn_channel_poll_receive(fdn_channel *receiver, void *value) {
    fdn_task *current;
    fdn_channel_message *message;
    fdn_task *sender;
    unsigned int status;
    if (receiver == NULL || receiver->receivers == 0) {
        fdn_panic_cstr("invalid channel receiver");
    }
    current = fdn_task_current_get();
    if (current == NULL) {
        fdn_panic_cstr("channel receive requires an active task");
    }
    if (fdn_task_take_wake(receiver, FDN_CHANNEL_WAIT_RECEIVE, &status)) {
        return (fdn_channel_status)status;
    }
    if (current->cancellation_requested) {
        return FDN_CHANNEL_CANCELLED;
    }
    message = receiver->message_head;
    if (message != NULL) {
        receiver->message_head = message->next;
        if (receiver->message_head == NULL) {
            receiver->message_tail = NULL;
        }
        --receiver->buffered;
        fdn_channel_copy(receiver, value, message->value);
        fdn_dealloc(message);
        fdn_channel_fill_buffer_from_sender(receiver);
        return FDN_CHANNEL_READY;
    }
    sender = fdn_channel_wait_pop(&receiver->send_head, &receiver->send_tail);
    if (sender != NULL) {
        fdn_channel_copy(receiver, value, sender->wait_value);
        fdn_task_wake(sender, FDN_CHANNEL_READY);
        return FDN_CHANNEL_READY;
    }
    if (receiver->senders == 0) {
        return FDN_CHANNEL_CLOSED;
    }
    fdn_task_park_current(receiver, value, FDN_CHANNEL_WAIT_RECEIVE,
                          fdn_channel_cancel_wait);
    fdn_channel_wait_push(&receiver->receive_head, &receiver->receive_tail, current);
    return FDN_CHANNEL_PENDING;
}

size_t fdn_channel_live_count(void) { return fdn_channel_count; }
