#include "foundation/runtime.h"

#include <stdint.h>
#include <stdlib.h>

static void require(bool condition) {
    if (!condition) {
        abort();
    }
}

int main(void) {
    const size_t baseline = fdn_live_allocations();
    const fdn_string host = fdn_string_static("127.0.0.1", 9);
    const fdn_string invalid = fdn_string_static("", 0);
    const fdn_string invalid_utf8 = fdn_string_static("\xc3\x28", 2);
    const fdn_string loopback = fdn_string_static("127.0.0.1", 9);
    uint64_t addresses = 0;
    uint64_t listener = 0;
    uint64_t listener_controller = 0;
    uint64_t second_listener = 0;
    uint64_t bound_port = 0;
    uint64_t second_port = 0;
    uint64_t receiver = 0;
    uint64_t sender = 0;
    uint64_t receiver_port = 0;
    uint64_t sender_port = 0;
    uint64_t payload = 0;
    uint64_t received = 0;
    uint64_t received_port = 0;
    uint64_t local_addresses = 0;
    uint64_t local_count = 0;
    fdn_string peer = fdn_string_static("unmodified", 10);
    fdn_string received_address = fdn_string_static("", 0);
    fdn_string received_text = fdn_string_static("", 0);
    fdn_string local_address = fdn_string_static("", 0);
    const fdn_string payload_text = fdn_string_static("datagram", 8);
    const fdn_string multicast = fdn_string_static("224.0.0.251", 11);

    require(foundation_runtime_net_peer_address(0, &peer) == 4);
    require(peer.length == 0);

    require(foundation_runtime_net_resolve(&invalid, UINT64_C(80), &addresses) == 1);
    require(addresses == 0);
    require(foundation_runtime_net_resolve(&invalid_utf8, UINT64_C(80), &addresses) == 1);
    require(addresses == 0);
    require(foundation_runtime_net_resolve(&host, UINT64_C(0), &addresses) == 1);
    require(addresses == 0);
    require(foundation_runtime_net_resolve(&host, UINT64_C(65536), &addresses) == 1);
    require(addresses == 0);
    require(foundation_runtime_net_resolve(&host, UINT64_C(80), &addresses) == 0);
    require(addresses != 0);
    require(foundation_runtime_net_live_addresses() == 1);
    foundation_runtime_net_addresses_close(addresses);
    require(foundation_runtime_net_live_addresses() == 0);
    require(foundation_runtime_net_listen(&invalid_utf8, UINT64_C(0),
                                          UINT64_C(16), &listener,
                                          &bound_port) == 1);
    require(listener == 0);
    require(bound_port == 0);
    require(foundation_runtime_net_listen(&loopback, UINT64_C(65536),
                                          UINT64_C(16), &listener,
                                          &bound_port) == 1);
    require(foundation_runtime_net_listen(&loopback, UINT64_C(0), UINT64_C(0),
                                          &listener, &bound_port) == 1);
    require(foundation_runtime_net_listen(&loopback, UINT64_C(0), UINT64_C(16),
                                          &listener, &bound_port) == 0);
    require(listener != 0);
    require(bound_port != 0);
    require(foundation_runtime_net_live_listeners() == 1);
    require(foundation_runtime_net_listener_control(listener, &listener_controller) == 0);
    require(listener_controller == listener);
    foundation_runtime_net_listener_controller_release(listener_controller);
    listener_controller = 0;
    require(foundation_runtime_net_listener_control(listener, &listener_controller) == 0);
    require(foundation_runtime_net_listen(&loopback, bound_port, UINT64_C(16),
                                          &second_listener, &second_port) == 10);
    require(second_listener == 0);
    require(second_port == 0);
    foundation_runtime_net_listener_controller_close(listener_controller);
    require(foundation_runtime_net_listener_control(listener, &listener_controller) == 4);
    require(listener_controller == 0);
    require(foundation_runtime_net_live_listeners() == 1);
    foundation_runtime_net_listener_close(listener);
    require(foundation_runtime_net_live_listeners() == 0);

    require(foundation_runtime_net_datagram_open(
                &loopback, UINT64_C(0), false, &receiver,
                &receiver_port) == 0);
    require(receiver != 0);
    require(receiver_port != 0);
    require(foundation_runtime_net_datagram_open(
                &loopback, UINT64_C(0), false, &sender, &sender_port) == 0);
    require(sender != 0);
    require(sender_port != 0);
    require(foundation_runtime_net_live_datagrams() == 2);
    payload = foundation_runtime_bytes_from_text(&payload_text);
    require(payload != 0);
    require(foundation_runtime_net_datagram_send(
                sender, &loopback, receiver_port, payload) == 0);
    foundation_runtime_bytes_close(&payload);
    require(foundation_runtime_net_datagram_receive(
                receiver, UINT64_C(32),
                foundation_runtime_time_monotonic_nanoseconds() +
                    UINT64_C(1000000000),
                &received, &received_address, &received_port) == 0);
    require(received != 0);
    require(received_port == sender_port);
    require(fdn_string_equal(received_address, loopback));
    require(foundation_runtime_bytes_to_text(received, &received_text) == 0);
    require(fdn_string_equal(received_text, payload_text));
    foundation_runtime_bytes_close(&received);
    fdn_string_drop(&received_address);
    fdn_string_drop(&received_text);
    require(foundation_runtime_net_datagram_receive(
                receiver, UINT64_C(32),
                foundation_runtime_time_monotonic_nanoseconds(),
                &received, &received_address, &received_port) == 11);
    require(foundation_runtime_net_datagram_join_ipv4(sender, &loopback) == 1);
    require(foundation_runtime_net_datagram_join_ipv4(sender, &multicast) == 0);
    foundation_runtime_net_datagram_close(&sender);
    foundation_runtime_net_datagram_close(&receiver);
    require(foundation_runtime_net_live_datagrams() == 0);

    require(foundation_runtime_net_local_addresses_open(
                true, &local_addresses, &local_count) == 0);
    require(local_addresses != 0);
    require(local_count != 0);
    require(foundation_runtime_net_live_local_addresses() == 1);
    require(foundation_runtime_net_local_address_at(
                local_addresses, UINT64_C(0), &local_address) == 0);
    require(local_address.length != 0);
    fdn_string_drop(&local_address);
    foundation_runtime_net_local_addresses_close(&local_addresses);
    require(foundation_runtime_net_live_local_addresses() == 0);
    require(foundation_runtime_net_live_connections() == 0);
    require(foundation_runtime_net_live_requests() == 0);
    require(foundation_runtime_net_live_services() == 0);
    require(fdn_live_allocations() == baseline);
    return 0;
}
