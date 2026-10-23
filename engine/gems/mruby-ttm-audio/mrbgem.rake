# mruby-ttm-audio -- RGSS1's Audio module in C for the To the Moon 3DS engine:
# BGM, BGS, ME and SE from the audio pack (tools/pack_audio.py) or the game's
# Audio/ folder, decoded with stb_vorbis. Played through NDSP on its own thread
# on the 3DS; on the PC a silent backend that still decodes. See src/audio.h.

MRuby::Gem::Specification.new('mruby-ttm-audio') do |spec|
  spec.license = 'MIT; third_party/stb: public domain or MIT'
  spec.authors = 'ttm3ds port project'
  spec.summary = 'RGSS1 Audio: bgm/bgs/me/se'
  spec.cc.include_paths << "#{dir}/third_party/stb"
end
