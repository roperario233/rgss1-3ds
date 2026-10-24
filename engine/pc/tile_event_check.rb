# tile_event_check.rb -- events whose graphic is a tile (romfs/rgss/rgss.rb,
# RPG::Cache.tile): books, vases, flowers, the furniture the game moves.
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\tile_event_check.rb
#
# Until console run 10 RPG::Cache.tile returned a blank bitmap, so all 488
# event pages on 69 maps that show a tile were invisible. For every distinct
# (tileset, tile id) any event shows, the cached bitmap must be that 32x32
# cell of the tileset, pixel for pixel. Run twice: as normal, then with 256 px
# texture slices and a 4 MB RAM budget for image pixels (the tilesets are shed
# and decoded again in between). Saves map 1's tile events to
# pc/shots/tile_events_map001.png, for the eye.

TTM.load_script(TTM.read_file("pc/game_dir.rb"), "pc/game_dir.rb")
$ttm_game_dir = ttm_check_game_dir
%w[rgss/compat.rb rgss/rgss.rb].each do |f|
  TTM.load_script(TTM.read_file("romfs/" + f), f)
end
TTM.archive_open(ttm_check_archive($ttm_game_dir))

$ttm_passed = 0
$ttm_failed = []
def check(name, cond, extra = nil)
  if cond
    $ttm_passed += 1
    __ttm_print format("  ok    %s%s\n", name, extra ? "  (#{extra})" : "")
  else
    $ttm_failed << name
    __ttm_print format("  FAIL  %s%s\n", name, extra ? "  (#{extra})" : "")
  end
end

# a fully transparent pixel is the same whatever colour it carries (blt into a
# cleared bitmap leaves those at 0,0,0,0; the tileset keeps its own)
def same(a, b)
  return a.alpha == 0 if b.alpha == 0
  a.red == b.red && a.green == b.green && a.blue == b.blue && a.alpha == b.alpha
end

# every (tileset name, tile id) an event page shows, and map 1's events
tilesets = load_data("Data/Tilesets.rxdata")
used = {}
pages = 0
maps = 0
map1 = []
infos = load_data("Data/MapInfos.rxdata")
infos.keys.sort.each do |id|
  map = begin load_data(format("Data/Map%03d.rxdata", id)) rescue nil end
  next unless map
  name = tilesets[map.tileset_id].tileset_name
  found = false
  map.events.each_value do |ev|
    ev.pages.each do |pg|
      t = pg.graphic.tile_id
      next unless t >= 384
      pages += 1
      found = true
      used[[name, t]] = true
      map1 << [ev.x, ev.y, t] if id == 1 && pg.equal?(ev.pages[0])
    end
  end
  maps += 1 if found
end
check("event pages showing a tile", pages == 488, "#{pages} pages on #{maps} maps, #{used.size} distinct tiles")

def compare_all(used, label)
  bad = []
  used.each_key do |(name, t)|
    tile = RPG::Cache.tile(name, t, 0)
    src = RPG::Cache.tileset(name)
    id = t - 384
    sx = id % 8 * 32
    sy = id / 8 * 32
    diff = 0
    for y in 0...32
      for x in 0...32
        diff += 1 unless same(tile.get_pixel(x, y), src.get_pixel(sx + x, sy + y))
      end
    end
    bad << "#{name}:#{t} (#{diff} px)" if diff > 0
  end
  check("#{label}: every tile identical to its tileset cell", bad.empty?,
        bad.empty? ? "#{used.size} tiles, #{used.size * 1024} pixels" : bad.first(3).join(", "))
end

__ttm_print "as normal\n"
compare_all(used, "as normal")
name, t = used.keys.first
a = RPG::Cache.tile(name, t, 0)
check("cached: the same bitmap the second time", a.equal?(RPG::Cache.tile(name, t, 0)))
a.dispose
b = RPG::Cache.tile(name, t, 0)
check("rebuilt after a dispose", !b.disposed? && !a.equal?(b))
# (a few events point at an empty cell on purpose: invisible markers)
visible = lambda { |bm, x0, y0| (0...32).any? { |y| (0...32).any? { |x| bm.get_pixel(x0 + x, y0 + y).alpha > 0 } } }
want = used.keys.count { |(n, i)| visible.call(RPG::Cache.tileset(n), (i - 384) % 8 * 32, (i - 384) / 8 * 32) }
got = used.keys.count { |(n, i)| visible.call(RPG::Cache.tile(n, i, 0), 0, 0) }
check("not blank (the run 10 bug)", got == want && want > 0,
      "#{got} tiles with visible pixels, as in the tileset; #{used.size - want} cells are empty there too")

__ttm_print "256 px slices, 4 MB of image pixels\n"
RPG::Cache.clear
$ttm_max_tex_dim = 256
$ttm_ram_budget_kb = 4096
compare_all(used, "sliced and shed")
$ttm_max_tex_dim = nil
$ttm_ram_budget_kb = nil

# map 1 with its tile events, placed as Sprite_Character places them (ox 16,
# oy 32, z its row + 32), scrolled so they are on screen
RPG::Cache.clear
map = load_data("Data/Map001.rxdata")
ts = tilesets[map.tileset_id]
viewport = Viewport.new(0, 0, 640, 480)
tilemap = Tilemap.new(viewport)
tilemap.tileset = RPG::Cache.tileset(ts.tileset_name)
(0..6).each { |i| tilemap.autotiles[i] = RPG::Cache.autotile(ts.autotile_names[i]) }
tilemap.map_data = map.data
tilemap.priorities = ts.priorities
ox = [[map1.collect { |e| e[0] }.min * 32 - 320, 0].max, map.width * 32 - 640].min
oy = [[map1.collect { |e| e[1] }.min * 32 - 240, 0].max, map.height * 32 - 480].min
tilemap.ox = ox
tilemap.oy = oy
sprites = map1.collect do |(x, y, t)|
  s = Sprite.new(viewport)
  s.bitmap = RPG::Cache.tile(ts.tileset_name, t, 0)
  s.ox = 16
  s.oy = 32
  s.x = x * 32 + 16 - ox
  s.y = y * 32 + 32 - oy
  s.z = s.y + 32
  s
end
tilemap.update
Graphics.update
Graphics.ttm_screenshot("pc/shots/tile_events_map001.png")
check("map 1: tile events drawn", sprites.size > 0, "#{sprites.size} events at #{map1.inspect}, pc/shots/tile_events_map001.png")
sprites.each(&:dispose)
tilemap.dispose

__ttm_print format("%d checks passed, %d failed%s\n", $ttm_passed, $ttm_failed.size,
                   $ttm_failed.empty? ? "" : ": " + $ttm_failed.join(", "))
