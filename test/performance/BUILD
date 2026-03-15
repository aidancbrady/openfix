load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "openfix-perf",
    srcs = glob(["*.h", "*.cpp"]),
    data = ["//test:fix-dictionary"],
    deps = [
        "//src:openfix",
        "//test:test-harness",
    ],
)
