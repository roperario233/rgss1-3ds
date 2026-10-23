# plane_check.rb -- Plane (gems/mruby-ttm-gfx/src/plane.c): the game's
# panoramas and fogs, repeated across a viewport.
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\plane_check.rb
#
# Each case draws one plane and checks every 5th pixel of the frame against
# the pixel worked out from the bitmap directly: inside the viewport, screen
# pixel (x, y) shows texel ((x - vx + ox) / zoom mod w, (y - vy + oy) / zoom
# mod h), faded by opacity, over black; outside the viewport, black. Slices are
# 256 px, so NightCliff (1645x575) wraps across several of them.

$ttm_game_dir = "C:/Users/davao/OneDrive/Desktop/3ds project/work/game/"
%w[rgss/compat.rb rgss/rgss.rb].each do |f|
  TTM.load_script(TTM.read_file("romfs/" + f), f)
end
TTM.archive_open($ttm_game_dir + "To the Moon.rgssad")
$ttm_max_tex_dim = 256

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

# name, image, viewport rect (nil: none), ox, oy, zoom, opacity
CASES = [
  ["panorama, scrolled", "Graphics/Panoramas/NightCliff", nil, 123, 45, 1, 255],
  ["panorama, wrapped past its width", "Graphics/Panoramas/NightCliff", nil, 1645 * 2 + 700, 575 + 300, 1, 255],
  ["negative ox/oy", "Graphics/Panoramas/NightCliff", nil, -77, -19, 1, 255],
  ["inside a smaller viewport", "Graphics/Panoramas/NRS1001", [100, 60, 300, 200], 40, 10, 1, 255],
  ["zoom 2", "Graphics/Panoramas/NightCliff", nil, 31, 7, 2, 255],
  ["half opacity", "Graphics/Panoramas/NightCliff", nil, 5, 5, 1, 128],
]

def mod(a, n)
  m = a % n
  m < 0 ? m + n : m
end

CASES.each do |name, image, rect, ox, oy, zoom, opacity|
  bmp = Bitmap.new(image)
  vp = rect ? Viewport.new(*rect) : nil
  plane = Plane.new(vp)
  plane.bitmap = bmp
  plane.ox = ox
  plane.oy = oy
  plane.zoom_x = plane.zoom_y = zoom
  plane.opacity = opacity
  Graphics.update
  shot = Graphics.snap_to_bitmap
  vx, vy, vw, vh = rect || [0, 0, 640, 480]
  n = 0
  bad = 0
  y = 0
  while y < 480
    x = 0
    while x < 640
      got = shot.get_pixel(x, y)
      if x >= vx && x < vx + vw && y >= vy && y < vy + vh
        tx = mod(((x - vx) + ox) / zoom, bmp.width)
        ty = mod(((y - vy) + oy) / zoom, bmp.height)
        c = bmp.get_pixel(tx, ty)
        a = c.alpha * opacity / 255
        want = [c.red * a / 255, c.green * a / 255, c.blue * a / 255]
      else
        want = [0, 0, 0]
      end
      n += 1
      bad += 1 if (got.red - want[0]).abs > 1 || (got.green - want[1]).abs > 1 || (got.blue - want[2]).abs > 1
      x += 5
    end
    y += 5
  end
  check(name, bad == 0, "#{n} pixels, #{bad} differ")
  plane.dispose
  vp.dispose if vp
  bmp.dispose
  shot.dispose
end

__ttm_print format("%d checks passed, %d failed\n", $ttm_passed, $ttm_failed.size)
