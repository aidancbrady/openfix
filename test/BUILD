load("@rules_cc//cc:defs.bzl", "cc_test", "cc_library")

filegroup(
    name = "fix-dictionary",
    srcs = ["FIXDictionary.xml"],
    visibility = ["//test/performance:__pkg__"],
)

cc_library(
    name = "test-harness",
    hdrs = ["SessionTestHarness.h"],
    strip_include_prefix = "/test",
    deps = [
        "//src:openfix",
        "@googletest//:gtest",
    ],
    visibility = ["//test/performance:__pkg__"],
)

cc_test(
    name = "openfix-test",
    srcs = glob(["*.h", "*.cpp"], allow_empty=True),
    data = glob([
        "FIXDictionary.xml",
        "tls/*",
    ]),
    deps = [
        "//src:openfix",
        "@googletest//:gtest_main"
    ],
)
