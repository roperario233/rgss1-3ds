# mruby-ttm-gfx -- RGSS1's graphics classes in C for the To the Moon 3DS
# engine: Bitmap (images, text), Viewport, Sprite, Window, Graphics.
# Rendering goes through a small backend: software on the PC (frames can be
# saved as PNG), citro3d on the 3DS. See src/gfx.h.

MRuby::Gem::Specification.new('mruby-ttm-gfx') do |spec|
  spec.license = 'MIT; third_party/stb: public domain or MIT'
  spec.authors = 'ttm3ds port project'
  spec.summary = 'RGSS1 graphics: Bitmap, Viewport, Sprite, Window, Graphics'
  spec.add_dependency 'mruby-ttm-rgss'                 # Rect, Color, Tone (ttm_values.h)
  spec.cc.include_paths << "#{dir}/third_party/stb"
  # src/png.c inflates PNGs with zlib (linked by mruby-ttm-core)
  if build.kind_of?(MRuby::CrossBuild)
    portlibs = "#{(ENV['DEVKITPRO'] || 'C:/Users/davao/devkitpro/root/opt/devkitpro').tr('\\', '/')}/portlibs/3ds"
    spec.cc.include_paths << "#{portlibs}/include"
  end
end
