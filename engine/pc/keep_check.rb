# keep_check.rb -- a disposed image's decoded pixels are kept for the next
# Bitmap.new of the same file (gems/mruby-ttm-gfx/src/bitmap.c, "kept images").
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\keep_check.rb
#
# Spriteset_Map#dispose disposes the tileset on every map change and the next
# map loads it again through RPG::Cache. On the console that second decode of
# the 6 MB Tileset could fail, and the map drew only its autotiles (console
# run 7). Here: the reload must take the kept pixels, be the same image pixel
# for pixel, and never hand back pixels the game drew into.

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

# every 7th pixel down the image and every 5th across, as one string
def sample(bmp)
  s = []
  y = 0
  while y < bmp.height
    x = 0
    while x < bmp.width
      c = bmp.get_pixel(x, y)
      s << [c.red.to_i, c.green.to_i, c.blue.to_i, c.alpha.to_i].join(",")
      x += 5
    end
    y += 7
  end
  s.join(" ")
end

def reused; Bitmap.ttm_kept[0]; end

TILESET = "Graphics/Tilesets/Tileset"

__ttm_print "the map change: tileset disposed, then loaded again through RPG::Cache\n"
a = RPG::Cache.tileset("Tileset")
before = sample(a)
check("Tileset decodes", a.width == 256 && a.height == 6096, "#{a.width}x#{a.height}")
a.dispose
check("its pixels are kept on dispose", Bitmap.ttm_kept[1] == 1, Bitmap.ttm_kept.inspect)
r0 = reused
b = RPG::Cache.tileset("Tileset")
check("the reload takes them instead of decoding", reused == r0 + 1 && Bitmap.ttm_kept[1] == 0)
check("same size", b.width == 256 && b.height == 6096, "#{b.width}x#{b.height}")
check("same pixels", sample(b) == before)
check("a new Bitmap object", !b.equal?(a) && a.disposed? && !b.disposed?)

__ttm_print "ten map changes in a row (the console's failing case)\n"
r0 = reused
10.times do
  RPG::Cache.tileset("Tileset").dispose
  RPG::Cache.tileset("Tileset")
end
t = RPG::Cache.tileset("Tileset")
check("decoded once, reused ten times", reused == r0 + 10, "#{reused - r0} reuses")
check("still the same pixels", sample(t) == before)

__ttm_print "pixels the game drew into are never kept\n"
c = Bitmap.new(TILESET)
orig = c.get_pixel(40, 40)
c.fill_rect(0, 0, 100, 100, Color.new(255, 0, 255, 255))
c.dispose
d = Bitmap.new(TILESET)
p = d.get_pixel(40, 40)
check("fill_rect then dispose: the reload is the file again",
      [p.red, p.green, p.blue, p.alpha] == [orig.red, orig.green, orig.blue, orig.alpha])
e = Bitmap.new(TILESET)
e.draw_text(0, 0, 200, 40, "hello")
e.dispose
e2 = Bitmap.new(TILESET)
check("draw_text then dispose: not kept", sample(e2) == before)
h = Bitmap.new(TILESET)
h.hue_change(90)
h.dispose
h2 = Bitmap.new(TILESET)
check("hue_change then dispose: not kept", sample(h2) == before)

__ttm_print "what is not kept\n"
small = RPG::Cache.icon("Arrow")
size = "#{small.width}x#{small.height}"
n = Bitmap.ttm_kept[1]
small.dispose
check("a tiny image (under 16 KB) is just freed", Bitmap.ttm_kept[1] == n, size)
# a picture-sized one is kept since run 12: Sprite_Picture#dispose disposes
# RPG::Cache's own pictures on every map change
mid = RPG::Cache.autotile("046-Road03")
size = "#{mid.width}x#{mid.height}"
n = Bitmap.ttm_kept[1]
mid.dispose
check("a mid-sized file image (16 KB up) is kept", Bitmap.ttm_kept[1] == n + 1, size)
x = Bitmap.new(640, 480)
n = Bitmap.ttm_kept[1]
x.dispose
check("a bitmap not from a file is just freed", Bitmap.ttm_kept[1] == n)
o = Bitmap.new("Graphics/Tilesets/town2")
o.dispose
Bitmap.new("Graphics/Panoramas/NightCliff").dispose
Bitmap.new("Graphics/Characters/Light (x4Size)").dispose
k = Bitmap.ttm_kept
check("kept images stay under 16 MB together", k[2] <= 16 * 1024 * 1024, "#{k[1]} kept, #{k[2] / 1024} KB")

__ttm_print format("%d passed, %d failed%s\n", $ttm_passed, $ttm_failed.size,
                   $ttm_failed.empty? ? "" : ": " + $ttm_failed.join(", "))
