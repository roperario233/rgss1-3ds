# shed_check.rb -- an image's decoded pixels are a cache: when RAM is short
# they are freed and decoded again on demand (gems/mruby-ttm-gfx/src/bitmap.c,
# "decoded pixels are a cache too").
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\shed_check.rb
#
# Console run 9 ran out of RAM on its first long map: RPG::Cache holds every
# image the game loads, and each one held its pixels as well as its textures.
# $ttm_ram_budget_kb makes the PC behave as if the pixels had only that much
# RAM. Here: the budget holds, a shed image comes back pixel for pixel, a shed
# image still draws from its textures (and is decoded again when those are
# gone), and nothing the game drew into is ever shed.

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

def ram; Graphics.ttm_ram_stats; end    # [px KB, sheddable KB, shed, reloaded, failed]

BUDGET_KB = 12 * 1024
$ttm_ram_budget_kb = BUDGET_KB

# the run 9 log's big images, loaded the way the game loads them
IMAGES = [
  [:picture, "Title Scroll"], [:tileset, "town2"], [:tileset, "Tileset"],
  [:panorama, "NightCliff"], [:character, "Light (x4Size)"], [:character, "Vehicles"],
  [:character, "Vehicles 2"], [:picture, "heartoverlay2"], [:panorama, "NRS1001"],
  [:picture, "IC_7-Written"],
]

# reference checksums, each image decoded alone with no budget
$ttm_ram_budget_kb = nil
REF = {}
IMAGES.each do |kind, name|
  b = Bitmap.new("Graphics/#{{ picture: "Pictures", tileset: "Tilesets", panorama: "Panoramas",
                               character: "Characters" }[kind]}/#{name}")
  REF[name] = b.ttm_checksum
  b.dispose
end
$ttm_ram_budget_kb = BUDGET_KB

__ttm_print "loading the big images through RPG::Cache under a #{BUDGET_KB / 1024} MB budget\n"
bmps = {}
peak = 0
IMAGES.each do |kind, name|
  bmps[name] = RPG::Cache.send(kind, name)
  peak = ram[0] if ram[0] > peak
end
total_kb = bmps.values.inject(0) { |s, b| s + b.width * b.height * 4 / 1024 }
check("the images together are over the budget", total_kb > BUDGET_KB * 2, "#{total_kb} KB")
check("pixels stay within the budget (plus the image being decoded)", peak <= BUDGET_KB + 9500,
      "peak #{peak} KB")
check("images were shed", ram[2] > 0, "#{ram[2]} shed")
check("the latest image is still in RAM", bmps["IC_7-Written"].ttm_resident?)
shed_names = bmps.keys.reject { |n| bmps[n].ttm_resident? }
check("older ones are not", shed_names.include?("Title Scroll") && shed_names.include?("town2"),
      shed_names.join(", "))
check("width and height need no pixels", bmps["town2"].width == 256 && bmps["town2"].height == 9332 &&
      !bmps["town2"].ttm_resident?)

__ttm_print "decoded again on demand, pixel for pixel\n"
r0 = ram[3]
same = IMAGES.select { |_, n| bmps[n].ttm_checksum == REF[n] }.size
check("every image, read back, is its file", same == IMAGES.size, "#{same}/#{IMAGES.size}")
check("the shed ones were decoded again", ram[3] - r0 >= shed_names.size, "#{ram[3] - r0} reloads")
check("no reload failed", ram[4] == 0)
check("still within the budget", ram[0] <= BUDGET_KB + 9500, "#{ram[0]} KB")
again = RPG::Cache.picture("Title Scroll")
check("RPG::Cache hands back the same object", again.equal?(bmps["Title Scroll"]))

__ttm_print "drawing a shed image\n"
pic = bmps["Title Scroll"]
spr = Sprite.new
spr.bitmap = pic
spr.src_rect.set(0, 600, 640, 480)
Graphics.update
want = Graphics.snap_to_bitmap.ttm_checksum
# load others until it is shed (it has current textures: shed first)
%w[NightCliff NRS1001].each { |n| RPG::Cache.panorama(n).ttm_checksum }
RPG::Cache.tileset("town2").ttm_checksum
check("a drawn image with current textures gets shed", !pic.ttm_resident?)
r0 = ram[3]
Graphics.update
check("it draws the same from its textures", Graphics.snap_to_bitmap.ttm_checksum == want)
check("without being decoded again", ram[3] == r0 && !pic.ttm_resident?)
# its textures go too: a small texture budget and other images drawn
$ttm_tex_budget_kb = 8 * 1024
spr.visible = false
others = %w[town2 Tileset].map { |n| s = Sprite.new; s.bitmap = RPG::Cache.tileset(n); s }
Graphics.update
others.each { |s| s.visible = false }
o2 = Sprite.new
o2.bitmap = RPG::Cache.character("Light (x4Size)")
Graphics.update
o2.visible = false
spr.visible = true
r0 = ram[3]
Graphics.update
check("with its textures gone too, it is decoded again to draw", ram[3] > r0, "#{ram[3] - r0} reloads")
check("and draws the same", Graphics.snap_to_bitmap.ttm_checksum == want)
$ttm_tex_budget_kb = 0
Graphics.update
(others + [o2, spr]).each(&:dispose)

__ttm_print "what the game draws into is never shed\n"
own = Bitmap.new("Graphics/Pictures/IC_7-Written")
own.fill_rect(10, 10, 50, 50, Color.new(255, 0, 0))
mine = Bitmap.new(300, 200)
mine.draw_text(0, 0, 300, 40, "kept", 1)
own_sum, mine_sum = own.ttm_checksum, mine.ttm_checksum
IMAGES.each { |_, n| bmps[n].ttm_checksum }    # every image read again: lots of shedding
check("an image drawn into stays in RAM", own.ttm_resident? && own.ttm_checksum == own_sum)
check("a Bitmap.new(w, h) stays in RAM", mine.ttm_resident? && mine.ttm_checksum == mine_sum)

__ttm_print "blt from a shed image, dup, dispose\n"
src = bmps["Vehicles"]
d1 = Bitmap.new(300, 300)
d1.blt(0, 0, src, Rect.new(400, 100, 300, 300))
IMAGES.each { |_, n| bmps[n].ttm_checksum unless n == "Vehicles" }
check("the source is shed", !src.ttm_resident?)
d2 = Bitmap.new(300, 300)
d2.blt(0, 0, src, Rect.new(400, 100, 300, 300))
check("a blt from it is the same", d1.ttm_checksum == d2.ttm_checksum)
dup = bmps["NightCliff"].dup
check("a dup is the same pixels", dup.ttm_checksum == REF["NightCliff"])
IMAGES.each { |_, n| bmps[n].ttm_checksum }
check("a dup of a file image can be shed", !dup.ttm_resident?)
check("and comes back the same", dup.ttm_checksum == REF["NightCliff"])
gone = bmps["town2"]
IMAGES.each { |_, n| bmps[n].ttm_checksum unless n == "town2" }
gone.dispose
fresh = RPG::Cache.tileset("town2")
check("dispose of a shed image, then RPG::Cache loads it again",
      gone.disposed? && !fresh.equal?(gone) && fresh.ttm_checksum == REF["town2"])
check("no reload failed in all of this", ram[4] == 0, ram.inspect)

__ttm_print "\n#{$ttm_passed} checks passed, #{$ttm_failed.size} failed\n"
$ttm_failed.each { |n| __ttm_print "  failed: #{n}\n" }
