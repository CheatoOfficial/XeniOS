include("third_party/zstd/contrib/premake/zstd.lua")

group("third_party")
project("zstd")
  uuid("df336aac-f0c8-11ed-a05b-0242ac120003")
  -- https://gitlab.kitware.com/cmake/cmake/-/issues/25744
  filter({"toolset:not msc"})
    defines({
      "ZSTD_DISABLE_ASM",
    })
  filter({"toolset:msc"})
    defines({
      -- Override STATIC_BMI2=1 assumption on Windows
      -- MSVC incorrectly assumes AVX2 implies BMI2, but Sandy Bridge has AVX2 without BMI2
      "STATIC_BMI2=0",
      "DYNAMIC_BMI2=1",
    })
  filter({})
  project_zstd("zstd/lib/")
