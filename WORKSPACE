load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

git_repository(
    name = "crow",
    remote = "https://github.com/CrowCpp/Crow.git",
    branch = "v1.0",
    build_file_content =
"""
cc_library(
    name = "crow",
    hdrs = glob(["include/**/*.h", "include/**/*.hpp"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)
"""
)
