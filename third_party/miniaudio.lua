group("third_party")
project("miniaudio")
  uuid("d8f3a2b1-c4e5-4f6a-8b9c-0d1e2f3a4b5c")
  if os.istarget("android") then
    -- ndk-build only supports StaticLib and SharedLib.
    kind("StaticLib")
  else
    kind("Utility")
  end
  language("C")
  files({
    "miniaudio/miniaudio.h",
  })
  warnings("Off")
