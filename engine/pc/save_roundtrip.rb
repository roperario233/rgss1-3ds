# save_roundtrip.rb -- the game's own save file, written by the game's own code
# and read back the way Scene_Load reads it.
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\save_roundtrip.rb
#
# Runs the game (PC, software renderer) far enough to be in a map, then walks the
# save file the game writes with its own code -- the autosave (an event calls
# $ttm.s(), 096_TtM Util.rb, into $savePath + "save4.rxdata"), or, for a run too
# short to reach one, one asked for at the end -- one Marshal stream at a time:
# every stream has to describe exactly the bytes it came from (load, dump again,
# compare), the stream offsets have to line up, and nothing may be left over. On
# the PC the game's files live in TTMFiles (rgss/compat.rb), which goes away when
# mruby exits, so this is the only place the real save can be checked without a
# console.
#
# The streams are the twelve of 083_Scene_Save.rb write_save_data plus UMS's $ams.
# (pc/marshal_roundtrip.rb covers the format itself, against CRuby's bytes.)

TTM.load_script(TTM.read_file("pc/game_dir.rb"), "pc/game_dir.rb")
$ttm_game_dir = ttm_check_game_dir
$ttm_archive = ttm_check_archive($ttm_game_dir)
ROMFS = "romfs/"
SAVE = "save4.rxdata"

# The game's files are real files now (rgss/compat.rb's TTMFiles), under
# work/pc_data/ -- which is also what makes this save readable by the real Ruby
# afterwards (work/probe_save_read.rb). A clean start means "the game saved" here
# really is this run's autosave.
$ttm_data_dir = "../work/pc_data/"
%w[save/save4.rxdata save/save3.rxdata save/save2.rxdata save/save1.rxdata
   save/music.txt save/window.txt].each { |f| TTM.file_delete($ttm_data_dir + f) }

FRAMES = ($ttm_max_frames || 900)
$ttm_frame = 0
$ttm_keys_source = proc { $ttm_frame > 0 && $ttm_frame % 40 == 0 ? [13] : [] }
$ttm_frame_hook = proc { $ttm_frame += 1; $scene = nil if $ttm_frame >= FRAMES }

%w[rgss/compat.rb rgss/rgss.rb rgss/ttm_patches.rb boot.rb].each do |f|
  src = TTM.read_file(ROMFS + f)
  raise "missing #{ROMFS}#{f}" unless src
  TTM.load_script(src, f)
end

# the game's own save if it made one during the run (the autosave: an event
# calls $ttm.s() -- 096_TtM Util.rb), otherwise ask it for one
path = ($savePath || "save/") + SAVE
if TTMFiles.exist?(path)
  ttm_log("save: the game wrote its own save during the run")
else
  ttm_log("save: no autosave in #{FRAMES} frames -- calling the game's save")
  $ttm.s
end
data = begin TTMFiles.read(path) rescue nil end
raise "no save at #{path}" unless data
ttm_log(format("save: %s, %d bytes", path, data.bytesize))

PARTS = %w[characters frame_count system switches variables self_switches screen
           actors party troop map player ams]

# byte-based, like the file: String#[]/#size count characters here
# (MRB_UTF8_STRING), and a save's bytes are not text
def hex(bytes, from, len)
  bytes.to_s.byteslice(from, len).to_s.unpack("H*")[0]
end

def first_diff(a, b)
  x = a.unpack("C*")
  y = b.unpack("C*")
  n = x.size < y.size ? x.size : y.size
  i = 0
  i += 1 while i < n && x[i] == y[i]
  i
end

io = MemIO.new(path, data, true)
ok = 0
stopped = false
PARTS.each do |name|
  start = io.pos
  begin
    obj = Marshal.load(io)
  rescue => e
    ttm_log(format("  %-14s at %7d: load failed: %s", name, start, e.message))
    stopped = true
    break
  end
  stream = data.byteslice(start, io.pos - start).to_s
  again = Marshal.dump(obj)
  same = again == stream
  ok += 1 if same
  ttm_log(format("  %-14s %6d bytes at %7d  %-22s %s", name, stream.bytesize, start,
                 obj.class.to_s, same ? "dumps identically" : "DIFFERS"))
  next if same
  i = first_diff(stream, again)
  lo = i > 8 ? i - 8 : 0
  ttm_log(format("        first difference at byte %d of %d (redumped: %d)",
                 i, stream.bytesize, again.bytesize))
  ttm_log(format("        in  %s", hex(stream, lo, 24)))
  ttm_log(format("        out %s", hex(again, lo, 24)))
  ttm_log(format("        in  tail %s", hex(stream, stream.bytesize - 32, 32)))
  ttm_log(format("        out tail %s", hex(again, again.bytesize - 32, 32)))
end
if stopped
  ttm_log(format("save: %d/%d streams re-dump identically, stopped early",
                 ok, PARTS.size))
else
  ttm_log(format("save: %d/%d streams re-dump identically, %d bytes left over",
                 ok, PARTS.size, data.bytesize - io.pos))
end

# ---- the save and load screens, with this save on the card ---------------------
# The first console run crashed here and no PC run had: Window_SaveFile prints
# each slot's date with Time#strftime, which mruby lacks, and only a slot that
# has a file gets that far. So: the slot window on its own, one frame of each
# screen (the frame hook ends a scene, FRAMES is already passed), then the load
# the way choosing the slot does it.
screens = []
begin
  w = Window_SaveFile.new(3, path)
  stamp = w.instance_variable_get(:@time_stamp)
  w.dispose
  screens << (stamp.year >= 2020)
  ttm_log(format("screens: slot 4 window built, dated %s%s",
                 stamp.strftime("%Y/%m/%d %H:%M"), screens[-1] ? "" : " -- NOT a real date"))
rescue => e
  screens << false
  ttm_log("screens: slot 4 window FAILED: #{e.class}: #{e.message}")
end
[Scene_Save, Scene_Load].each do |klass|
  begin
    $scene = klass.new
    $scene.main
    screens << true
    ttm_log("screens: #{klass} ran")
  rescue => e
    screens << false
    ttm_log("screens: #{klass} FAILED: #{e.class}: #{e.message}")
  end
end
begin
  before = [$game_map.map_id, $game_player.x, $game_player.y]
  $scene = Scene_Load.new
  $scene.on_decision(path)
  after = [$game_map.map_id, $game_player.x, $game_player.y]
  screens << ($scene.is_a?(Scene_Map) && after == before)
  ttm_log(format("screens: loaded slot 4 -> %s, map %d at %d,%d (saved: map %d at %d,%d)",
                 $scene.class, *after, *before))
rescue => e
  screens << false
  ttm_log("screens: load FAILED: #{e.class}: #{e.message}")
end
ttm_log(format("screens: %d/%d", screens.count(true), screens.size))
