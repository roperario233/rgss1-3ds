# mruby-ttm-core -- engine core for the To the Moon 3DS port.
#
# TTM module (timing, allocation counter, file read, zlib inflate, the .rgssad
# archive reader) and Marshal.load. Builds for the 3DS and the PC test build.
# Needs zlib: Strawberry's on the PC, devkitPro's 3ds-zlib (portlibs) on the 3DS.
# TTM.obj_allocs needs the gc.c counter that build_mruby.ps1 patches in.

MRuby::Gem::Specification.new('mruby-ttm-core') do |spec|
  spec.license = 'MIT'
  spec.authors = 'ttm3ds port project'
  spec.summary = 'RGSS engine core: archive, Marshal, zlib, timing'
  spec.add_dependency 'mruby-compiler', core: 'mruby-compiler'   # TTM.load_script

  if build.kind_of?(MRuby::CrossBuild)
    portlibs = "#{(ENV['DEVKITPRO'] || 'C:/Users/davao/devkitpro/root/opt/devkitpro').tr('\\', '/')}/portlibs/3ds"
    spec.cc.include_paths << "#{portlibs}/include"
    spec.linker.library_paths << "#{portlibs}/lib"
  end
  spec.linker.libraries << 'z'
end
