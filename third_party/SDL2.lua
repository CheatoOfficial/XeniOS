--
-- SDL2 dispatch:
--   Windows / macOS / iOS: build from source as a static library under
--     third_party/, matching how DirectX works on Windows.
--   Linux: build against the system libsdl2-dev via sdl2-config.
--

-- iOS detection: on iOS we build SDL2 from source as a static library.
local _ios =
    -- Resolve .ios_target relative to the main premake script directory so this
    -- works regardless of premake include script directory changes.
    os.isfile((_MAIN_SCRIPT_DIR and path.join(_MAIN_SCRIPT_DIR, ".ios_target")) or ".ios_target")
    or os.getenv("XE_TARGET_IOS") == "1"
    or (os.target and os.target() == "ios")
    or os.istarget("ios")
    or (_OPTIONS and _OPTIONS["os"] == "ios")
if _ios then
  print("SDL2.lua: iOS target detected, building from source")
  include("SDL2-static-apple.lua")
  local third_party_path = os.getcwd()
  function sdl2_include()
    includedirs({
      path.getrelative(".", third_party_path) .. "/SDL2/include",
    })
  end
  return
end

local sdl2_sys_includedirs = {}
local sdl2_sys_libdirs = {}
local third_party_path = os.getcwd()

if os.istarget("windows") then
  -- Build ourselves from third_party/SDL2.
  include("SDL2-static.lua")
elseif os.istarget("macosx") then
  -- Build ourselves from third_party/SDL2, same as Windows. Xenia only uses
  -- SDL for audio (CoreAudio) and joystick (IOKit + MFi) on macOS.
  print("SDL2.lua: macOS target detected, building from source")
  include("SDL2-static-apple.lua")
else
  -- Linux: use system libsdl2-dev via sdl2-config.
  local sdl2_config = os.getenv("SDL2_CONFIG") or "sdl2-config"
  local result, code, what = os.outputof(sdl2_config .. " --cflags")
  if result then
    print("SDL2: sdl2-config --cflags returned: " .. result)
    for inc in string.gmatch(result, "-I([%S]+)") do
      table.insert(sdl2_sys_includedirs, inc)
      print("SDL2: Added include dir: " .. inc)
    end
    if #sdl2_sys_includedirs == 0 then
      print("SDL2: Warning - no include directories found in sdl2-config output")
    end
  else
    error("Failed to run 'sdl2-config'. Are libsdl2 development files installed?")
  end
  local libs, libs_code, libs_what = os.outputof(sdl2_config .. " --libs")
  if libs then
    for libdir in string.gmatch(libs, "-L([%S]+)") do
      table.insert(sdl2_sys_libdirs, libdir)
    end
  else
    error("Failed to run 'sdl2-config --libs'. Are libsdl2 development files installed?")
  end
end

--
-- Call this function in project scope to include the SDL2 headers.
--
function sdl2_include()
  filter("platforms:Windows-* or Mac-*")
    includedirs({
      path.getrelative(".", third_party_path) .. "/SDL2/include",
    })
  filter("platforms:Linux-*")
    includedirs(sdl2_sys_includedirs)
    libdirs(sdl2_sys_libdirs)
  filter({})

  -- For Linux, also add SDL2 includes outside of filter for cmake compatibility
  -- The premake-cmake module doesn't properly handle filtered include directories
  if os.istarget("linux") then
    includedirs(sdl2_sys_includedirs)
    libdirs(sdl2_sys_libdirs)
    -- Also add common fallback path for cases where sdl2-config doesn't return -I flags
    if #sdl2_sys_includedirs == 0 then
      includedirs({"/usr/include/SDL2"})
      print("SDL2: Using fallback include path /usr/include/SDL2")
    else
      print("SDL2: Added includes for Linux cmake: " .. table.concat(sdl2_sys_includedirs, ", "))
    end
  end
end
