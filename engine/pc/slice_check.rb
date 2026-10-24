# slice_check.rb -- a bitmap larger than the texture limit is held as slices
# (gems/mruby-ttm-gfx/src/bitmap.c, ttm_bitmap_draw); it must draw exactly as
# it does as one texture.
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\slice_check.rb
#
# The game's own oversized images (the eight the console could not draw before
# M6), each drawn twice through the software renderer: once as one texture
# ($ttm_max_tex_dim = 0) and once cut into 256 px slices -- smaller than the
# console's 1024, so every case crosses several slice edges. The two frames are
# compared pixel for pixel. Cases whose pieces land on whole pixels must be
# identical; zoomed-by-a-fraction and rotated cases are reported with how many
# pixels differ, since sampling at a slice edge may move by one texel there.

TTM.load_script(TTM.read_file("pc/game_dir.rb"), "pc/game_dir.rb")
$ttm_game_dir = ttm_check_game_dir
%w[rgss/compat.rb rgss/rgss.rb].each do |f|
  TTM.load_script(TTM.read_file("romfs/" + f), f)
end
TTM.archive_open(ttm_check_archive($ttm_game_dir))

SLICE = 256
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

# name, image, src_rect, and the sprite's settings; exact: must match pixel
# for pixel (else: reported, allowed `tolerance` differing pixels)
CASES = [
  ["picture across slice rows", "Graphics/Pictures/Title Scroll", [0, 900, 640, 480], {}, true],
  ["tileset strip, offset", "Graphics/Tilesets/town2", [0, 1000, 256, 400], { x: 100, y: 37 }, true],
  ["tileset 32px tiles at slice edges", "Graphics/Tilesets/town2", [96, 2032, 160, 64], { x: 300, y: 200, zoom_x: 2.0, zoom_y: 2.0 }, true],
  ["sheet, half size", "Graphics/Characters/Vehicles", [0, 0, 1143, 686], { zoom_x: 0.5, zoom_y: 0.5 }, true],
  ["sheet, mirrored", "Graphics/Characters/Light (x4Size)", [200, 100, 600, 400], { x: 10, y: 20, mirror: true }, true],
  ["panorama, partly off screen", "Graphics/Panoramas/NightCliff", [0, 0, 1645, 575], { x: -500, y: -40 }, true],
  ["wide strip, add blend, half opacity", "Graphics/Pictures/heartoverlay2", [0, 0, 1390, 51], { x: -300, y: 200, opacity: 128, blend_type: 1 }, true],
  ["bush depth across a slice edge", "Graphics/Characters/Vehicles 2", [0, 100, 600, 300], { bush_depth: 180 }, true],
  ["zoom 1.3 (fractional)", "Graphics/Panoramas/NightCliff", [300, 0, 600, 400], { zoom_x: 1.3, zoom_y: 1.3 }, false, 2000],
  ["rotated 30 degrees", "Graphics/Characters/Vehicles", [0, 0, 700, 500], { x: 320, y: 240, ox: 350, oy: 250, angle: 30 }, false, 2000],
]

def frame_of(image, rect, opts, slice)
  $ttm_max_tex_dim = slice
  bmp = Bitmap.new(image)
  sprite = Sprite.new
  sprite.bitmap = bmp
  sprite.src_rect.set(*rect)
  opts.each { |k, v| sprite.send("#{k}=", v) }
  Graphics.update
  shot = Graphics.snap_to_bitmap
  sprite.dispose
  bmp.dispose
  shot
end

def differing(a, b)
  n = 0
  y = 0
  while y < 480
    x = 0
    while x < 640
      p = a.get_pixel(x, y)
      q = b.get_pixel(x, y)
      n += 1 if p.red != q.red || p.green != q.green || p.blue != q.blue
      x += 1
    end
    y += 1
  end
  n
end

def lit(a)
  n = 0
  y = 0
  while y < 480
    x = 0
    while x < 640
      c = a.get_pixel(x, y)
      n += 1 if c.red + c.green + c.blue > 0
      x += 4
    end
    y += 4
  end
  n
end

__ttm_print "slices of #{SLICE} px against one texture:\n"
CASES.each do |name, image, rect, opts, exact, tolerance|
  whole = frame_of(image, rect, opts, 0)
  sliced = frame_of(image, rect, opts, SLICE)
  diff = differing(whole, sliced)
  drawn = lit(whole)
  if exact
    check(name, diff == 0 && drawn > 0, "#{diff} pixels differ, #{drawn} sampled pixels drawn")
  else
    check(name, diff <= tolerance && drawn > 0, "#{diff} pixels differ (allowed #{tolerance}), #{drawn} sampled pixels drawn")
  end
  whole.dispose
  sliced.dispose
end

# the console's own slice size, on the biggest image there is
$ttm_max_tex_dim = 1024
big = Bitmap.new("Graphics/Tilesets/town2")
check("town2 loads whole at the console's limit", big.width == 256 && big.height == 9332,
      "#{big.width}x#{big.height}")
big.dispose

__ttm_print format("%d checks passed, %d failed\n", $ttm_passed, $ttm_failed.size)
