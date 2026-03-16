load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "quickfix-perf",
    srcs = glob(["*.h", "*.cpp"]),
    data = ["@quickfix//:spec/FIX42.xml"],
    deps = [
        "//test/performance:benchmark-framework",
        "@quickfix",
    ],
)
