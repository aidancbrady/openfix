load("@rules_cc//cc:defs.bzl", "cc_library", "cc_binary")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "openfix",
    include_prefix = "openfix",
    hdrs = glob(["*.h"]),
    srcs = glob(["*.cpp"]),
    deps = [
        "//openfix/lib/pugixml",
        "@oneTBB//:tbb"
    ]
)

cc_binary(
    name = "openfix-exe",
    deps = [
        ":openfix"
    ]
)