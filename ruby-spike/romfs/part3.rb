# part3.rb -- entry point for M2 part 3 (the game's own scripts, headless).
#
# 3DS: loaded from romfs by the app; the player's archive is read from the SD
#      card next to the app.
# PC:  mruby.exe romfs/part3.rb   (run from ruby-spike/)
#
# Loads rgss_headless.rb, ttm_patches.rb, then harness.rb, each at top level.

if Object.const_defined?(:Spike)
  $ttm_romfs = "romfs:/"
  $ttm_archive = "sdmc:/3ds/ttm3ds_ruby/To the Moon.rgssad"
else
  $ttm_romfs = "romfs/"
  $ttm_archive = "C:/Users/davao/OneDrive/Desktop/3ds project/work/game/To the Moon.rgssad"
end

%w[rgss_headless.rb ttm_patches.rb harness.rb].each do |f|
  src = TTM.read_file($ttm_romfs + f)
  raise "missing #{$ttm_romfs}#{f}" unless src
  TTM.load_script(src, f)
end
