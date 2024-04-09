load("@rules_cc//cc:defs.bzl", "cc_library", "cc_binary")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "openfix",
    include_prefix = "openfix",
    hdrs = glob(["*.h", "lib/*.h"]),
    srcs = glob(["*.cpp", "lib/*.cpp"]),
    deps = [
        "//lib:openfix-lib",
        "@pugixml//:pugixml",
        "@crow//:crow"
    ]
)
