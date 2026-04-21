project_root = "../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-vfs")
  uuid("395c8abd-4dc9-46ed-af7a-c2b9b68a3a98")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "zstd",
    "zarchive"
  })

  recursive_platform_files()
  removefiles({
    "vfs_dump.cc",
    "xex_metadata_main.cc",
  })

if enableMiscSubprojects then
  project("xenia-vfs-dump")
    uuid("2EF270C7-41A8-4D0E-ACC5-59693A9CCE32")
    kind("ConsoleApp")
    language("C++")
    links({
      "fmt",
      "xenia-base",
      "xenia-vfs",
    })

    files({
      "vfs_dump.cc",
      project_root.."/src/xenia/base/console_app_main_"..platform_suffix..".cc",
    })
    resincludedirs({
      project_root,
    })

  project("xenia-xex-metadata")
    uuid("A1B2C3D4-E5F6-7890-ABCD-EF1234567890")
    kind("ConsoleApp")
    language("C++")
    links({
      "fmt",
      "xenia-base",
      "xenia-vfs",
    })

    files({
      "xex_metadata_main.cc",
      project_root.."/src/xenia/base/console_app_main_"..platform_suffix..".cc",
    })
    resincludedirs({
      project_root,
    })
end

if enableTests then
  include("testing")
end
