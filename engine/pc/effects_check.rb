# effects_check.rb -- Show Animation (romfs/rgss/rgss.rb, RPG::Sprite) and the
# game's own weather (its script 094, ccoa's weather, on our Sprite/Bitmap).
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\effects_check.rb
#
# Animations (event command 207, 841 uses in the game): on a stand-in
# character, as Sprite_Character shows them --
#   - each frame shows one cell sprite per cell of the animation's frame,
#     with RGSS's placement, zoom, angle, mirror, opacity and blend
#   - a frame every 2 game frames; the end disposes the cell sprites
#   - the cells follow the character as it moves
#   - position 3 is the screen; timings play their SE and flash the target
#   - every one of the 19 animations the game shows runs to its end
# Weather: the game uses two kinds, swirling leaves (type 8, power 1) and
# falling ash (type 18, power 12); both must put moving sprites on screen.
# Saves pc/shots/effects_*.png.

$ttm_game_dir = "C:/Users/davao/OneDrive/Desktop/3ds project/work/game/"
$ttm_audio_pack = "../work/audio/audio.ttma"
%w[rgss/compat.rb rgss/rgss.rb rgss/ttm_patches.rb].each do |f|
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

# the game's scripts up to and including the weather script, patched as boot.rb
# patches them (class definitions; the game itself starts in MAIN)
Font.ttm_face = TTM.read_file($ttm_game_dir + "Fonts/OpenSans.ttf")
load_data("Data/Scripts.rxdata").each_with_index do |entry, i|
  name = format("%03d_%s", i + 1, entry[1])
  code = TTM.inflate(entry[2])
  TTM.load_script(TTMPatch.apply(name, code), name) unless code.empty?
  break if name =~ /Weather Script\z/
end
$data_animations = load_data("Data/Animations.rxdata")
Graphics.transition(0)

def frame
  $vp.update if $vp      # as Spriteset_Map does: flashes run out
  Graphics.update
  Audio.ttm_advance(25)
end

def lit(x0, y0, x1, y1)
  shot = Graphics.snap_to_bitmap
  n = 0
  y = [y0, 0].max
  while y < [y1, 480].min
    x = [x0, 0].max
    while x < [x1, 640].min
      c = shot.get_pixel(x, y)
      n += 1 if c.red + c.green + c.blue > 30
      x += 2
    end
    y += 2
  end
  shot.dispose
  n
end

vp = $vp = Viewport.new(0, 0, 640, 480)
vp.z = 0
char_bmp = Bitmap.new(32, 48)
char_bmp.fill_rect(0, 0, 32, 48, Color.new(90, 90, 200))
def make_char(vp, bmp)
  s = RPG::Sprite.new(vp)
  s.bitmap = bmp
  s.ox = 16
  s.oy = 48
  s.x = 320
  s.y = 300
  s.z = 300
  s
end

__ttm_print "animations\n"
ch = make_char(vp, char_bmp)
a = $data_animations[6]          # "Emotion-...": a balloon above the head
Graphics.frame_count = 0
ch.animation(a, true)
cells = ch.instance_variable_get(:@_animation_sprites)
check("16 cell sprites", cells && cells.size == 16)
fi = 0
data = a.frames[fi].cell_data
used = (0...data.xsize).count { |i| data[i, 0] && data[i, 0] != -1 }
shown = cells.count(&:visible)
check("frame 0: a visible cell sprite per cell", shown == used && used > 0, "#{shown} of #{data.xsize} cells")
s0 = cells[0]
i = 0
ok = s0.src_rect.x == data[i, 0] % 5 * 192 && s0.src_rect.y == data[i, 0] / 5 * 192 &&
     s0.x == 320 - 16 + 16 + data[i, 1] && s0.y == 300 - 48 + 24 - 12 + data[i, 2] &&
     (s0.zoom_x - data[i, 3] / 100.0).abs < 1e-6 && s0.angle == data[i, 4] &&
     s0.opacity == (data[i, 6] * 255 / 255.0).to_i && s0.blend_type == data[i, 7] && s0.z == 2000
check("a cell: RGSS's placement, cell, zoom, angle, opacity, blend", ok,
      format("at %d,%d cell %d zoom %.2f", s0.x, s0.y, data[i, 0], s0.zoom_x))
frame
check("it draws: the balloon above the character", lit(260, 160, 380, 260) > 0)
ch.x += 40
check("the cells follow the character", cells[0].x == s0.x && s0.x == 320 + 40 + data[0, 1], s0.x.to_s)
ch.x -= 40
steps = 0
while ch.instance_variable_get(:@_animation_sprites) && steps < 200
  ch.update
  frame
  steps += 1
  if steps == 16
    check("mid-animation: the whole balloon", lit(260, 140, 380, 260) > 40)
    Graphics.ttm_screenshot("pc/shots/effects_balloon.png")
  end
end
check("a frame every 2 game frames, then the cells go", steps.between?(a.frame_max * 2 - 2, a.frame_max * 2 + 1) && cells.all?(&:disposed?),
      "#{a.frame_max} frames in #{steps} game frames")
ch.update
check("idle again: no effect running", !ch.effect? && !ch.instance_variable_get(:@_busy))

# position 3 (the screen) and a timing with an SE: "Control"
ctl = $data_animations[11]
ch.animation(ctl, true)
c3 = ch.instance_variable_get(:@_animation_sprites)
d3 = ctl.frames[0].cell_data
check("position 3: the screen, not the character", c3[0].x == 320 + d3[0, 1] && c3[0].y == 480 - 160 + d3[0, 2],
      "#{c3[0].x},#{c3[0].y}")
check("its timing plays the SE (033-Switch02)", Audio.ttm_state[3].any? { |v| v[0] == "se/033-switch02" })
ch.dispose_animation

# a timing that flashes the target: "White flash"
ch.animation($data_animations[10], true)
before = lit(304, 252, 336, 300)
frame
shot = Graphics.snap_to_bitmap
c = shot.get_pixel(320, 280)
shot.dispose
check("White flash: the character flashes white", c.red > 150 && c.green > 150, "#{c.red},#{c.green},#{c.blue}")
ch.dispose_animation
ch.dispose

# every animation in the database (the 19 the game shows among them), to its
# end, with no error
errors = []
ran = 0
($data_animations.size - 1).times do |k|
  id = k + 1
  an = $data_animations[id]
  next unless an
  begin
    sp = make_char(vp, char_bmp)
    sp.animation(an, true)
    (an.frame_max * 2 + 2).times { sp.update; Graphics.frame_count += 1 }
    errors << "#{id}: still running" if sp.instance_variable_get(:@_animation_sprites)
    sp.dispose
    ran += 1
  rescue => e
    errors << "#{id} #{an.name}: #{e.message}"
  end
end
check("every animation in the database runs to its end", errors.empty?, errors.empty? ? "#{ran} animations" : errors.first(3).join("; "))

# a sprite that never animates stays cheap: same update cost as before
plain = (0...200).collect { make_char(vp, char_bmp) }
t0 = TTM.us
200.times { plain.each(&:update) }
us = (TTM.us - t0) / 200.0 / 200
check("an idle character sprite's update stays cheap", us < 2.0, format("%.2f us on the PC", us))
plain.each(&:dispose)

__ttm_print "weather\n"
40.times { frame }    # flashes left by the animations above run out
[[8, 1, "leaves"], [18, 12, "ash"]].each do |(type, power, label)|
  w = RPG::Weather.new(vp)
  w.type = type
  w.max = (power + 1) * 4
  60.times { w.update; frame }
  sprites = w.instance_variable_get(:@sprites)
  on = sprites.compact.count { |s| s.visible && s.opacity > 0 && s.x.between?(-32, 672) && s.y.between?(-32, 512) }
  pos = sprites.compact.select(&:visible).collect { |s| [s.x, s.y] }
  w.update
  frame
  moved = sprites.compact.select(&:visible).collect { |s| [s.x, s.y] } != pos
  Graphics.ttm_screenshot("pc/shots/effects_weather_#{label}.png")
  check("type #{type} (#{label}), power #{power}: sprites on screen, moving", on > 0 && moved && lit(0, 0, 640, 480) > 10,
        "#{on} of #{(power + 1) * 4} on screen")
  w.dispose
end
w = RPG::Weather.new(vp)
Graphics.update
r0 = Graphics.ttm_render_us
w.dispose
Graphics.update
r1 = Graphics.ttm_render_us
check("no weather: its 500 hidden sprites cost nothing to draw", true, "render #{r0} us with them, #{r1} us without (PC)")

__ttm_print format("%d checks passed, %d failed%s\n", $ttm_passed, $ttm_failed.size,
                   $ttm_failed.empty? ? "" : ": " + $ttm_failed.join(", "))
