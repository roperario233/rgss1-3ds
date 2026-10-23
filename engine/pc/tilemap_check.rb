# tilemap_check.rb -- Tilemap (gems/mruby-ttm-gfx/src/tilemap.c) against the
# game's own maps and tilesets.
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\tilemap_check.rb
#
# Each map is set up the way Spriteset_Map does it, viewed around its middle at
# an ox/oy that is not a multiple of 32 and with 256 px slices (so tileset rows cross slice edges),
# drawn once, and the frame compared with the source images directly:
#   - plain tiles: every visible cell holding one priority-0 tileset tile and
#     nothing above it must show that tile's 32x32 pixels, exactly
#   - autotiles: cells with pattern 0 (the sheet's centre tile, at 32,64) or 47
#     (its top-left tile) -- the two patterns whose meaning does not depend on
#     the quarter table -- must show those pixels
#   - z: a priority-1 tile must cover a sprite one z below
#     (y*32 - oy) + 32 + 32p, and lose to one at exactly that z (the tie goes
#     to the sprite, made later), which is how characters pass behind trees

$ttm_game_dir = "C:/Users/davao/OneDrive/Desktop/3ds project/work/game/"
%w[rgss/compat.rb rgss/rgss.rb].each do |f|
  TTM.load_script(TTM.read_file("romfs/" + f), f)
end
TTM.archive_open($ttm_game_dir + "To the Moon.rgssad")
$ttm_max_tex_dim = 256

# the car scene, the lighthouse cliff, a house (Tileset, with autotiles), and
# the two maps with the most plain autotile cells of patterns 0 and 47
MAPS = [38, 8, 1, 41, 45]
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

# a tile's pixel as it lands on the black frame: opaque, or blended over black
def over_black(c)
  a = c.alpha
  [c.red * a / 255, c.green * a / 255, c.blue * a / 255]
end

def same_rgb(c, rgb, slack = 1)
  (c.red - rgb[0]).abs <= slack && (c.green - rgb[1]).abs <= slack && (c.blue - rgb[2]).abs <= slack
end

# compare the 32x32 block of src at (sx,sy) with the frame at screen (x,y),
# pixels on screen only; returns [compared, differing]
def compare_block(shot, src, sx, sy, x, y)
  n = 0
  bad = 0
  for j in 0...32
    py = y + j
    next if py < 0 || py >= 480
    for i in 0...32
      px = x + i
      next if px < 0 || px >= 640
      n += 1
      bad += 1 unless same_rgb(shot.get_pixel(px, py), over_black(src.get_pixel(sx + i, sy + j)))
    end
  end
  [n, bad]
end

tilesets = load_data("Data/Tilesets.rxdata")

MAPS.each do |map_id|
  map = load_data(format("Data/Map%03d.rxdata", map_id))
  ts = tilesets[map.tileset_id]
  __ttm_print format("map %03d (%dx%d), tileset %s:\n", map_id, map.width, map.height, ts.tileset_name)
  viewport = Viewport.new(0, 0, 640, 480)
  tilemap = Tilemap.new(viewport)
  tilemap.tileset = Bitmap.new("Graphics/Tilesets/" + ts.tileset_name)
  autotiles = []
  for i in 0..6
    name = ts.autotile_names[i]
    autotiles[i] = name.to_s.empty? ? nil : Bitmap.new("Graphics/Autotiles/" + name)
    tilemap.autotiles[i] = autotiles[i]
  end
  tilemap.map_data = map.data
  tilemap.priorities = ts.priorities
  ox = (map.width * 32 - 640) / 2
  oy = (map.height * 32 - 480) / 2
  ox = ox < 0 ? 5 : ox / 32 * 32 + 5
  oy = oy < 0 ? 11 : oy / 32 * 32 + 11
  tilemap.ox = ox
  tilemap.oy = oy
  Graphics.update
  shot = Graphics.snap_to_bitmap
  data = map.data
  prio = ts.priorities
  tile_of = proc { |x, y, z| data[x % map.width, y % map.height, z] }
  only = proc { |x, y| tile_of.call(x, y, 1) == 0 && tile_of.call(x, y, 2) == 0 }

  plain = [0, 0, 0]
  auto = [0, 0, 0]
  prio_cell = nil
  for cy in (oy / 32)..((oy + 479) / 32)
    for cx in (ox / 32)..((ox + 639) / 32)
      id = tile_of.call(cx, cy, 0)
      x = cx * 32 - ox
      y = cy * 32 - oy
      if id >= 384 && prio[id] == 0 && only.call(cx, cy)
        n, bad = compare_block(shot, tilemap.tileset, (id - 384) % 8 * 32, (id - 384) / 8 * 32, x, y)
        plain[0] += 1
        plain[1] += n
        plain[2] += bad
      elsif id >= 48 && id < 384 && prio[id] == 0 && only.call(cx, cy) &&
            (id % 48 == 0 || id % 48 == 47) && autotiles[id / 48 - 1] &&
            autotiles[id / 48 - 1].height >= 128
        sx, sy = id % 48 == 0 ? [32, 64] : [0, 0]
        n, bad = compare_block(shot, autotiles[id / 48 - 1], sx, sy, x, y)
        auto[0] += 1
        auto[1] += n
        auto[2] += bad
      end
      # a priority-1 tileset tile fully on screen, on the top layer, to test z
      for z in 0..2
        t = tile_of.call(cx, cy, z)
        if prio_cell.nil? && t >= 384 && prio[t] == 1 && x >= 0 && y >= 0 && x <= 608 && y <= 448 &&
           (z + 1..2).all? { |zz| tile_of.call(cx, cy, zz) == 0 }
          prio_cell = [cx, cy, t]
        end
      end
    end
  end
  if plain[0] > 0
    check("plain tiles match the tileset", plain[2] == 0,
          "#{plain[0]} cells, #{plain[1]} pixels, #{plain[2]} differ")
  else
    __ttm_print "  --    no plain tileset cell on screen\n"
  end
  if auto[0] > 0
    check("autotile patterns 0 and 47 match the sheet", auto[2] == 0,
          "#{auto[0]} cells, #{auto[1]} pixels, #{auto[2]} differ")
  else
    __ttm_print "  --    no plain autotile cell with pattern 0 or 47 on screen\n"
  end

  if prio_cell
    cx, cy, t = prio_cell
    x = cx * 32 - ox
    y = cy * 32 - oy
    # an opaque pixel of that tile
    sx = (t - 384) % 8 * 32
    sy = (t - 384) / 8 * 32
    spot = nil
    for j in 0...32
      for i in 0...32
        spot ||= [i, j] if tilemap.tileset.get_pixel(sx + i, sy + j).alpha == 255
      end
    end
    tile_z = (cy * 32 - oy) + 32 + 32
    marker = Bitmap.new(32, 32)
    marker.fill_rect(0, 0, 32, 32, Color.new(255, 0, 255))
    sprite = Sprite.new(viewport)
    sprite.bitmap = marker
    sprite.x = x
    sprite.y = y
    results = [tile_z - 1, tile_z].collect do |sz|
      sprite.z = sz
      Graphics.update
      Graphics.snap_to_bitmap.get_pixel(x + spot[0], y + spot[1])
    end
    tile_rgb = over_black(tilemap.tileset.get_pixel(sx + spot[0], sy + spot[1]))
    check("priority-1 tile covers a sprite at z-1", same_rgb(results[0], tile_rgb),
          "cell #{cx},#{cy}, z #{tile_z}")
    check("a sprite at the same z covers the tile", same_rgb(results[1], [255, 0, 255]))
    sprite.dispose
    marker.dispose
  else
    __ttm_print "  --    no priority-1 tile fully on screen\n"
  end

  tilemap.dispose
  viewport.dispose
  tilemap.tileset.dispose
  autotiles.each { |b| b.dispose if b }
  shot.dispose
end

__ttm_print format("%d checks passed, %d failed\n", $ttm_passed, $ttm_failed.size)
