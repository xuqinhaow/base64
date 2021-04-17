package(default_visibility = ["//visibility:public"])

###AVX2_CFLAGS=-mavx2 SSSE3_CFLAGS=-mssse3 SSE41_CFLAGS=-msse4.1 SSE42_CFLAGS=-msse4.2 AVX_CFLAGS=-mavx make lib/libbase64.a

cc_library(
    name = "libbase64",
    srcs = glob([
        "lib/*.a"
        ]
        ),
    hdrs = glob([
        "include/*.h",
        ]),
    copts = [],
    includes = ["include"],
    deps = [],
)

