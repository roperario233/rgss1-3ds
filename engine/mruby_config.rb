# mruby_config.rb -- mruby 4.0.0 build for the To the Moon 3DS engine.
#
# Two builds from one config:
#   host  -- mrbc + mruby for Windows (Strawberry's MinGW gcc). Used to compile
#            gem sources for the cross build, to run spike.rb on the PC before it
#            goes to the console, and to syntax-check the game's own scripts.
#   3ds   -- libmruby.a for the 3DS (devkitARM, ARM11 MPCore, hard float).
#
# Both builds get the SAME gem set, so a script that passes on the PC is testing
# the same language surface the console runs.
#
# Run through engine/build_mruby.ps1 (separate build dir from the M2 spike).

DKP = (ENV['DEVKITPRO'] || 'C:/Users/davao/devkitpro/root/opt/devkitpro').tr('\\', '/')

# What RGSS scripts need, by gem. No IO/Dir/Socket/Sleep: the engine provides its
# own file access (RGSSAD reader), so the console build carries no OS layer.
SPIKE_GEMS = %w[
  mruby-compar-ext mruby-enum-ext mruby-string-ext mruby-numeric-ext
  mruby-array-ext mruby-hash-ext mruby-range-ext mruby-proc-ext
  mruby-symbol-ext mruby-object-ext mruby-objectspace mruby-set mruby-fiber
  mruby-enumerator mruby-enum-lazy mruby-enum-chain mruby-toplevel-ext
  mruby-kernel-ext mruby-class-ext mruby-catch
  mruby-pack mruby-sprintf mruby-time mruby-struct mruby-data mruby-random
  mruby-math mruby-bigint
  mruby-metaprog mruby-method mruby-eval mruby-binding mruby-proc-binding
  mruby-compiler mruby-error mruby-os-memsize
]

# RGSS text is UTF-8 (the game sets $KCODE = 'u'); make String#size, #[] and
# friends count characters, not bytes.
# MRB_STR_LENGTH_MAX=0: no cap on string length (some of the game's images are
# over the default 1 MB, and the archive hands them over as strings)
SPIKE_DEFINES = %w[MRB_UTF8_STRING MRB_HIGH_PROFILE MRB_STR_LENGTH_MAX=0]

# Gems of our own (engine/gems, mirrored to a space-free path by
# build_mruby.ps1). mruby-onig-regexp-3ds supplies Regexp, which the game's
# message system needs; mruby-ttm-core the archive reader, Marshal.load, zlib
# and timing that running the real scripts needs; mruby-ttm-audio the Audio module.
TTM_GEMS = (ENV['TTM_GEMS'] || 'C:/Users/davao/devkitpro/src/ttm-engine-gems').tr('\\', '/')
LOCAL_GEMS = %w[mruby-onig-regexp-3ds mruby-ttm-core mruby-ttm-rgss mruby-ttm-gfx mruby-ttm-audio]

MRuby::Build.new do |conf|
  conf.toolchain :gcc
  # MRB_USE_DEBUG_HOOK: the line profiler in mruby-ttm-core (PC only -- it
  # changes mrb_state's layout, so the 3DS build stays without it)
  conf.cc.defines += SPIKE_DEFINES + %w[MRB_USE_DEBUG_HOOK]
  SPIKE_GEMS.each { |g| conf.gem core: g }
  LOCAL_GEMS.each { |g| conf.gem "#{TTM_GEMS}/#{g}" }
  conf.gem core: 'mruby-bin-mrbc'
  conf.gem core: 'mruby-bin-mruby'
end

MRuby::CrossBuild.new('3ds') do |conf|
  conf.toolchain :gcc

  arch = %w[-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft]

  conf.cc do |cc|
    cc.command = "#{DKP}/devkitARM/bin/arm-none-eabi-gcc"
    cc.flags = arch + %w[-O2 -g -mword-relocations -ffunction-sections -fdata-sections]
    cc.defines += SPIKE_DEFINES + %w[__3DS__]
    cc.include_paths << "#{DKP}/libctru/include"
  end

  conf.cxx do |cxx|
    cxx.command = "#{DKP}/devkitARM/bin/arm-none-eabi-g++"
    cxx.flags = conf.cc.flags.dup + %w[-fno-rtti -fno-exceptions]
    cxx.defines = conf.cc.defines.dup
    cxx.include_paths = conf.cc.include_paths.dup
  end

  conf.archiver.command = "#{DKP}/devkitARM/bin/arm-none-eabi-ar"
  conf.linker.command = "#{DKP}/devkitARM/bin/arm-none-eabi-gcc"

  conf.bins = []                    # library only; the 3DS app links it
  SPIKE_GEMS.each { |g| conf.gem core: g }
  LOCAL_GEMS.each { |g| conf.gem "#{TTM_GEMS}/#{g}" }
end
