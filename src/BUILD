load("@rules_cc//cc:defs.bzl", "cc_library", "cc_binary")
load("@hedron_compile_commands//:refresh_compile_commands.bzl", "refresh_compile_commands")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "openfix",
    include_prefix = "openfix",
    hdrs = glob(["*.h"]),
    srcs = glob(["*.cpp"]),
    deps = [
        "//lib:openfix-lib",
        "@pugixml//:pugixml",
        "@crowcpp//:crow",
        "@boringssl//:ssl",
    ]
)

refresh_compile_commands(
    name = "refresh_compile_commands",
    targets = {
        ":openfix": "",
    },
)
