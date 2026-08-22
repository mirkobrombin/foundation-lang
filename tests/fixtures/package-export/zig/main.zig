const sample = @import("sample_native");
const std = @import("std");

fn increment(value: i32) callconv(.c) i32 {
    return value + 1;
}

test "calls Foundation archive" {
    try std.testing.expectEqual(@as(i32, 42), sample.foundationIncrement(41));
    try std.testing.expectEqual(@as(i32, 21), sample.foundationInvoke(increment, 20));
}
