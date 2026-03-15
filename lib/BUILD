load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "openfix-lib",
    include_prefix = "openfix",
    hdrs = glob(["*.h"]),
    srcs = glob(["*.cpp"]),
    deps = [
        "@concurrentqueue//:concurrentqueue",
        "@unordered_dense//:unordered_dense",
        "@spdlog//:spdlog"
    ]
)
