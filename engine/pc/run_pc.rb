# run_pc.rb -- run To the Moon on the PC with the engine's software renderer,
# saving screenshots, to check the graphics code without a console.
#
#   (from engine/)  C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\run_pc.rb
#
# Input is scripted: "confirm" is pressed for one frame every CONFIRM_EVERY
# frames, so messages advance. Frames listed in SHOTS are saved to pc/shots/.
# The game stops after MAX_FRAMES frames.

$ttm_game_dir = "C:/Users/davao/OneDrive/Desktop/3ds project/work/game/"
$ttm_archive = $ttm_game_dir + "To the Moon.rgssad"
ROMFS = "romfs/"
OUT = "pc/shots"

# The game's own writable files are real files now (rgss/compat.rb's TTMFiles),
# under work/pc_data/. A run starts clean, so its result never depends on the
# last run's save.
$ttm_data_dir = "../work/pc_data/"
%w[save/save4.rxdata save/save3.rxdata save/save2.rxdata save/save1.rxdata
   save/music.txt save/window.txt].each { |f| TTM.file_delete($ttm_data_dir + f) }

MAX_FRAMES = ($ttm_max_frames || 900)
CONFIRM_EVERY = ($ttm_confirm_every || 40)
SHOTS = ($ttm_shots || [1, 20, 40, 60, 90, 120, 160, 200, 250, 300, 400, 500, 600, 700, 800, 900])

# the sounds (python tools\pack_audio.py <the game's zip>); the PC's audio
# backend is silent but decodes, on a clock this runner advances a frame at a time
$ttm_audio_pack = "../work/audio/audio.ttma"

$ttm_frame = 0
$ttm_keys_source = proc do
  keys = $ttm_frame > 0 && $ttm_frame % CONFIRM_EVERY == 0 ? [13] : []
  $ttm_extra_keys ? keys + $ttm_extra_keys : keys    # pc/bottom_menu_check.rb
end
$ttm_frame_hook = proc do
  $ttm_frame += 1
  Audio.ttm_advance(1000 / Graphics.frame_rate)
  $ttm_extra_frame_hook.call if $ttm_extra_frame_hook    # pc/mapload_profile.rb
  if SHOTS.include?($ttm_frame)
    Graphics.ttm_screenshot(format("%s/frame%04d.png", OUT, $ttm_frame))
    tex = Graphics.ttm_tex_stats
    ttm_log(format("frame %4d: map %03d, %d drawables, render %.1f ms, " +
                   "textures: %d bitmaps %d KB, %d not made, %d collections", $ttm_frame,
                   $game_map ? $game_map.map_id : 0, Graphics.ttm_drawables,
                   Graphics.ttm_render_us / 1000.0, *tex))
    a = Audio.ttm_state
    st = Audio.ttm_stats
    ttm_log(format("            audio: bgm %s, bgs %s, me %s, %d voices; SE %d decoded, %d from cache, %d missing",
                   *a[0, 3].collect { |s| s[:playing] ? s[:name] : "-" }, a[3].size, *st[0, 3]))
  end
  $scene = nil if $ttm_frame >= MAX_FRAMES
end

%w[rgss/compat.rb rgss/rgss.rb rgss/ttm_patches.rb boot.rb].each do |f|
  src = TTM.read_file(ROMFS + f)
  raise "missing #{ROMFS}#{f}" unless src
  TTM.load_script(src, f)
end

# The game's own save, if this run made one (the autosave: an event calls
# $ttm.s(), 096_TtM Util.rb). Read back the way Scene_Load reads it -- twelve
# Marshal streams plus $ams -- and note Scene_Load's own order. pc/save_roundtrip.rb
# walks the real save stream by stream; this is the runner's short version. On the
# PC the game's files live in TTMFiles (rgss/compat.rb), which goes away at exit.
save_path = ($savePath || "save/") + "save4.rxdata"
save = begin TTMFiles.read(save_path) rescue nil end
if save.nil?
  ttm_log("save: nothing at #{save_path} (this run did not reach the autosave)")
else
  parts = %w[characters frame_count system switches variables self_switches screen
             actors party troop map player ams]
  io = MemIO.new(save_path, save, true)
  objs = parts.collect { Marshal.load(io) }
  ttm_log(format("save: %s read back: %d bytes, %d streams, %d bytes left over",
                 save_path, save.bytesize, objs.size, save.bytesize - io.pos))
  ttm_log(format("save: %d frames of play time, map %d, $ams %s",
                 objs[1], objs[10].map_id, objs[12].class))
end
