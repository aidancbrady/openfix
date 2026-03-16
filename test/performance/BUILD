load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_library(
    name = "benchmark-framework",
    hdrs = ["BenchmarkFramework.h"],
    strip_include_prefix = "/test/performance",
    visibility = ["//test/performance:__subpackages__"],
)

cc_binary(
    name = "openfix-perf",
    srcs = glob(["*.h", "*.cpp"]),
    data = ["//test:fix-dictionary"],
    deps = [
        "//src:openfix",
        "//test:test-harness",
    ],
)
