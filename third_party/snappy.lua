group("third_party")
project("snappy")
  uuid("bb143d61-3fd4-44c2-8b7e-04cc538ba2c7")
  kind("StaticLib")
  language("C++")

  files({
    "snappy/snappy-internal.h",
    "snappy/snappy-sinksource.cc",
    "snappy/snappy-sinksource.h",
    "snappy/snappy-stubs-internal.cc",
    "snappy/snappy-stubs-internal.h",
    "snappy/snappy-stubs-public.h",
    "snappy/snappy.cc",
    "snappy/snappy.h",
  })

  -- Disable AVX requirement to prevent BMI2 instructions from being baked in at compile time
  -- This allows Snappy to work on CPUs without BMI2 support (e.g., Sandy Bridge, Ivy Bridge)
  local snappy_dir = path.getabsolute("snappy")
  if not os.isfile(path.join(snappy_dir, "snappy-stubs-public.h")) then
    prebuildcommands({
      "cmake -DSNAPPY_BUILD_TESTS=OFF -DSNAPPY_BUILD_BENCHMARKS=OFF "..snappy_dir.." -B"..snappy_dir
    })
  end
