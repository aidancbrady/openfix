load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
  name = "com_google_googletest",
  urls = ["https://github.com/google/googletest/archive/5ab508a01f9eb089207ee87fd547d290da39d015.zip"],
  strip_prefix = "googletest-5ab508a01f9eb089207ee87fd547d290da39d015",
)

git_repository(
    name = "oneTBB",
    branch = "master",
    remote = "https://github.com/oneapi-src/oneTBB/",
)

http_archive(
  name = "pugixml",
  urls = ["https://github.com/zeux/pugixml/releases/download/v1.8.1/pugixml-1.8.1.tar.gz"],
  strip_prefix = "pugixml-1.8",
  build_file_content = """
cc_library(
  name = "pugixml",
  include_prefix = "pugixml",
  strip_include_prefix = "src/",
  hdrs = glob(["src/*.hpp"]),
  srcs = glob(["src/*.cpp"]),
  visibility = ["//visibility:public"]
)
  """
)