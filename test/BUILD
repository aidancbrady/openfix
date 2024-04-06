load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "openfix-test",
    srcs = glob(["*.h", "*.cpp"]),
    data = [":FIXDictionary.xml"],
    deps = [
        "//src:openfix",
        "@googletest//:gtest_main"
    ],
)