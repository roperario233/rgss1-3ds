# main.rb -- the 3DS app's Ruby entry point (source/main.c runs it).
# $ttm_game_dir and $ttm_archive are set by the app; TTM.log, TTM.keys and TTM.touch too.

$ttm_keys_source = proc { TTMControls.keys_for(TTM.keys) }
$ttm_touch_source = proc { TTM.touch }

%w[rgss/compat.rb rgss/rgss.rb rgss/ttm_patches.rb boot.rb].each do |f|
  src = TTM.read_file("romfs:/" + f)
  raise "missing romfs:/#{f}" unless src
  TTM.load_script(src, f)
end
