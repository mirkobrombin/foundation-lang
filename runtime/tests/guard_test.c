#include "foundation/runtime.h"

int main(void) {
    const size_t allocations_before = fdn_total_allocations();
    const size_t deallocations_before = fdn_total_deallocations();
    const fdn_string empty = fdn_string_static("", 0);
    const fdn_string invalid_utf8 = fdn_string_static("\xc3\x28", 2);
    const fdn_string admin = fdn_string_static("admin", 5);
    const fdn_string guest = fdn_string_static("guest", 5);
    const fdn_string owner = fdn_string_static("owner", 5);
    const fdn_string first_id = fdn_string_static("1", 1);
    const fdn_string second_id = fdn_string_static("2", 1);
    uint64_t roles = UINT64_C(42);
    uint64_t relationships = UINT64_C(42);
    uint64_t selected_roles = UINT64_C(42);
    uint64_t count = 0;
    fdn_string selected = fdn_string_static("", 0);

    if (foundation_runtime_guard_roles_open(0, &roles) == 0 || roles != 0 ||
        foundation_runtime_guard_roles_open(UINT64_C(65537), &roles) == 0 || roles != 0 ||
        foundation_runtime_guard_roles_open(2, NULL) == 0 ||
        foundation_runtime_guard_roles_open(2, &roles) != 0 || roles == 0) {
        return 1;
    }
    if (foundation_runtime_guard_roles_add(roles, &empty) != 2 ||
        foundation_runtime_guard_roles_add(roles, &invalid_utf8) != 2 ||
        foundation_runtime_guard_roles_add(roles, &admin) != 0 ||
        foundation_runtime_guard_roles_add(roles, &admin) != 1 ||
        foundation_runtime_guard_roles_add(roles, &guest) != 0 ||
        foundation_runtime_guard_roles_add(roles, &owner) != 3) {
        return 2;
    }
    if (!foundation_runtime_guard_roles_has(roles, &admin) ||
        foundation_runtime_guard_roles_has(roles, &owner) ||
        foundation_runtime_guard_roles_count(roles, &count) != 0 || count != 2) {
        return 3;
    }
    if (foundation_runtime_guard_roles_at(roles, 0, &selected) != 0 ||
        !fdn_string_equal(selected, admin)) {
        return 4;
    }
    fdn_string_drop(&selected);
    if (foundation_runtime_guard_roles_at(roles, 1, &selected) != 0 ||
        !fdn_string_equal(selected, guest)) {
        return 5;
    }
    fdn_string_drop(&selected);
    if (foundation_runtime_guard_roles_at(roles, 2, &selected) != 2) {
        return 6;
    }
    foundation_runtime_guard_roles_close(&roles);
    if (roles != 0 || foundation_runtime_guard_roles_count(roles, &count) != 1) {
        return 7;
    }

    if (foundation_runtime_guard_relationships_open(0, &relationships) == 0 ||
        relationships != 0 ||
        foundation_runtime_guard_relationships_open(UINT64_C(65537), &relationships) == 0 ||
        relationships != 0 || foundation_runtime_guard_relationships_open(2, NULL) == 0 ||
        foundation_runtime_guard_relationships_open(2, &relationships) != 0 ||
        relationships == 0 ||
        foundation_runtime_guard_relationships_add(relationships, &empty, &owner) != 2 ||
        foundation_runtime_guard_relationships_add(relationships, &invalid_utf8, &owner) != 2 ||
        foundation_runtime_guard_relationships_add(relationships, &first_id, &empty) != 2 ||
        foundation_runtime_guard_relationships_add(relationships, &first_id, &invalid_utf8) != 2 ||
        foundation_runtime_guard_relationships_add(relationships, &first_id, &owner) != 0 ||
        foundation_runtime_guard_relationships_add(relationships, &first_id, &owner) != 1 ||
        foundation_runtime_guard_relationships_add(relationships, &first_id, &admin) != 0 ||
        foundation_runtime_guard_relationships_add(relationships, &second_id, &guest) != 3) {
        return 8;
    }
    if (foundation_runtime_guard_relationships_roles(relationships, &first_id,
                                                     &selected_roles) != 0 ||
        selected_roles == 0 ||
        foundation_runtime_guard_roles_count(selected_roles, &count) != 0 || count != 2 ||
        foundation_runtime_guard_roles_at(selected_roles, 0, &selected) != 0 ||
        !fdn_string_equal(selected, owner)) {
        return 9;
    }
    fdn_string_drop(&selected);
    if (foundation_runtime_guard_roles_at(selected_roles, 1, &selected) != 0 ||
        !fdn_string_equal(selected, admin)) {
        return 10;
    }
    fdn_string_drop(&selected);
    foundation_runtime_guard_roles_close(&selected_roles);
    if (foundation_runtime_guard_relationships_roles(relationships, &second_id,
                                                     &selected_roles) != 0 ||
        foundation_runtime_guard_roles_add(selected_roles, &admin) != 0 ||
        foundation_runtime_guard_roles_add(selected_roles, &guest) != 0 ||
        foundation_runtime_guard_roles_add(selected_roles, &owner) != 0 ||
        foundation_runtime_guard_roles_count(selected_roles, &count) != 0 || count != 3) {
        return 11;
    }
    foundation_runtime_guard_roles_close(&selected_roles);
    selected_roles = UINT64_C(42);
    if (foundation_runtime_guard_relationships_roles(relationships, &empty,
                                                     &selected_roles) != 1 ||
        selected_roles != 0 ||
        foundation_runtime_guard_relationships_roles(relationships, &first_id, NULL) != 1) {
        return 12;
    }
    foundation_runtime_guard_relationships_close(&relationships);
    if (relationships != 0 ||
        foundation_runtime_guard_relationships_roles(relationships, &first_id,
                                                     &selected_roles) != 1) {
        return 13;
    }
    if (fdn_total_allocations() - allocations_before !=
            fdn_total_deallocations() - deallocations_before ||
        fdn_live_allocations() != 0) {
        return 14;
    }
    return 0;
}
