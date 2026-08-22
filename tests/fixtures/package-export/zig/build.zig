const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const dependency = b.dependency("sample_native", .{ .target = target });
    const tests = b.addTest(.{
        .root_module = b.createModule(.{
            .root_source_file = b.path("main.zig"),
            .target = target,
            .imports = &.{.{
                .name = "sample_native",
                .module = dependency.module("sample_native"),
            }},
        }),
    });
    const run = b.addRunArtifact(tests);
    b.default_step.dependOn(&run.step);
}
