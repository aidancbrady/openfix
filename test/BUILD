load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "openfix-test",
    srcs = glob(["*.h", "*.cpp"]),
    data = [":FIXDictionary.xml"],
    deps = [
        "//openfix/src:openfix",
        "@com_google_googletest//:gtest_main"
    ],
)