load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_library(
    name = "benchmark-framework",
    hdrs = [
        "BenchmarkFixtures.h",
        "BenchmarkFramework.h",
    ],
    strip_include_prefix = "/test/performance",
    visibility = ["//test/performance:__subpackages__"],
)

cc_binary(
    name = "openfix-perf",
    srcs = glob(["*.h", "*.cpp"], exclude = ["ParseProfileMain.cpp"]),
    data = ["//test:fix-dictionary"],
    deps = [
        "//src:openfix",
        "//test:test-harness",
    ],
)

cc_binary(
    name = "parse-profile",
    srcs = [
        "BenchmarkFixtures.h",
        "ParseProfileMain.cpp",
    ],
    data = ["//test:fix-dictionary"],
    deps = [
        ":benchmark-framework",
        "//src:openfix",
        "//test:test-harness",
    ],
)
