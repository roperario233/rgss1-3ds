# tone_check.rb -- tone, viewport colour and flash, freeze and transition
# (gems/mruby-ttm-gfx: graphics.c, backend_soft.c), against RGSS's formulas.
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\tone_check.rb
#
# Tone (RGSS): each pixel moves toward its luma (0.299 R + 0.587 G + 0.114 B)
# by gray/255, then r, g, b are added, clamped. A viewport's tone, colour and
# flash cover its rect once its contents are drawn. Graphics.freeze keeps the
# frame last shown, Graphics.update shows that frame until Graphics.transition,
# which fades to the new scene, evenly or in a transition image's order.
# This is the software renderer: the reference for the 3DS's GPU version.

$ttm_game_dir = "C:/Users/davao/OneDrive/Desktop/3ds project/work/game/"
%w[rgss/compat.rb rgss/rgss.rb].each do |f|
  TTM.load_script(TTM.read_file("romfs/" + f), f)
end
TTM.archive_open($ttm_game_dir + "To the Moon.rgssad")

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

def px(x, y)
  b = Graphics.snap_to_bitmap
  c = b.get_pixel(x, y)
  b.dispose
  [c.red.to_i, c.green.to_i, c.blue.to_i]
end

def near(a, b, tol = 1)
  a.zip(b).all? { |p, q| (p - q).abs <= tol }
end

def tone_ref(rgb, t)
  r, g, b = rgb
  if t[3] > 0
    l = (r * 299 + g * 587 + b * 114 + 500) / 1000
    r += (l - r) * t[3] / 255
    g += (l - g) * t[3] / 255
    b += (l - b) * t[3] / 255
  end
  [r + t[0], g + t[1], b + t[2]].map { |v| v < 0 ? 0 : v > 255 ? 255 : v }
end

# a 640x480 picture of four colours, quarter by quarter, in a full-screen viewport
COLS = [[200, 60, 30], [40, 180, 90], [20, 50, 220], [250, 240, 230]]
SPOTS = [[100, 100], [500, 100], [100, 400], [500, 400]]
pic = Bitmap.new(640, 480)
pic.fill_rect(0, 0, 320, 240, Color.new(*COLS[0]))
pic.fill_rect(320, 0, 320, 240, Color.new(*COLS[1]))
pic.fill_rect(0, 240, 320, 240, Color.new(*COLS[2]))
pic.fill_rect(320, 240, 320, 240, Color.new(*COLS[3]))
vp = Viewport.new(0, 0, 640, 480)
spr = Sprite.new(vp)
spr.bitmap = pic

__ttm_print "viewport tone\n"
[[0, 0, 0, 0], [60, -40, 0, 0], [0, 0, 0, 255], [-68, -68, 0, 68], [100, 100, 100, 128], [-255, 0, 255, 200]].each do |t|
  vp.tone = Tone.new(*t)
  Graphics.update
  got = SPOTS.map { |x, y| px(x, y) }
  want = COLS.map { |c| tone_ref(c, t) }
  check("tone #{t.inspect}", got.zip(want).all? { |g, w| near(g, w) }, got.first.inspect + " / " + want.first.inspect)
end
vp.tone = Tone.new(0, 0, 0, 0)

__ttm_print "a viewport's tone covers only its rect\n"
small = Viewport.new(320, 0, 320, 240)
small.z = 10
small.tone = Tone.new(0, 0, 0, 255)
Graphics.update
check("inside: grey", near(px(500, 100), tone_ref(COLS[1], [0, 0, 0, 255])))
check("outside: untouched", near(px(100, 100), COLS[0]) && near(px(500, 400), COLS[3]))
small.dispose

__ttm_print "sprite tone\n"
spr.tone = Tone.new(-50, 30, 0, 255)
Graphics.update
check("a sprite's own tone", SPOTS.each_with_index.all? { |(x, y), i| near(px(x, y), tone_ref(COLS[i], [-50, 30, 0, 255])) })
spr.tone = Tone.new(0, 0, 0, 0)

__ttm_print "viewport colour and flash\n"
vp.color = Color.new(255, 255, 255, 128)
Graphics.update
want = COLS[0].map { |c| (255 * 128 + c * 127) / 255 }
check("colour: half white", near(px(100, 100), want), px(100, 100).inspect)
vp.color = Color.new(0, 0, 0, 0)
vp.flash(Color.new(255, 0, 0, 255), 4)
Graphics.update
first = px(100, 100)
vp.update; vp.update; vp.update
Graphics.update
last = px(100, 100)
check("flash: strong, then fading", first[0] == 255 && last[0] < first[0] && last[0] > COLS[0][0], "#{first.inspect} -> #{last.inspect}")
vp.update
Graphics.update
check("flash: over", near(px(100, 100), COLS[0]))

__ttm_print "freeze and transition\n"
Graphics.update                                  # the old scene: the four colours
Graphics.freeze
spr.visible = false                              # the new scene: black
Graphics.update
check("while frozen, the screen is the old frame", near(px(100, 100), COLS[0]))
frames = []
$ttm_frame_hook = proc { frames << px(100, 100)[0] }
Graphics.transition(4)
$ttm_frame_hook = nil
Graphics.update
check("transition(4): four frames, fading out", frames.size == 4 && frames == frames.sort.reverse && frames.first > frames.last,
      frames.inspect)
check("after it, the new scene", px(100, 100) == [0, 0, 0])

spr.visible = true
Graphics.update
Graphics.freeze
spr.visible = false
mids = []
$ttm_frame_hook = proc { b = Graphics.snap_to_bitmap; mids << b; }
Graphics.transition(10, "Graphics/Transitions/003-Blind03", 40)
$ttm_frame_hook = nil
mask = Bitmap.new("Graphics/Transitions/003-Blind03")
lo = hi = nil
mask.height.times { |y| v = mask.get_pixel(0, y).red; lo = y if lo.nil? && v < 40; hi = y if hi.nil? && v > 215 }
mid = mids[5]
check("an image transition: low values turn first",
      lo && hi && mid.get_pixel(0, lo).red == 0 && mid.get_pixel(0, hi).red == COLS[0][0],
      "rows #{lo}/#{hi}: #{mid.get_pixel(0, lo).red}, #{mid.get_pixel(0, hi).red}")
check("an image transition: ten frames", mids.size == 10)
Graphics.update
check("after it, the new scene", px(100, 100) == [0, 0, 0])
check("transition without freeze does nothing", (Graphics.transition(5); true))

__ttm_print format("%d checks passed, %d failed%s\n", $ttm_passed, $ttm_failed.size,
                   $ttm_failed.empty? ? "" : ": " + $ttm_failed.join(", "))
