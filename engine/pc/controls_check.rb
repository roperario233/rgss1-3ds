# controls_check.rb -- the 3DS has no mouse (romfs/rgss: rgss.rb Input,
# compat.rb TTMControls, ttm_patches.rb "Mouse 3").
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\controls_check.rb
#
# The pointer is outside the window (-20,-20) unless L is held, which puts it
# at the top of the screen (the game's timeline); the game's cursor sprite
# stays hidden; the one message and two pictures that teach mouse controls
# name the 3DS's buttons. Saves the repainted pictures to pc/shots/.

TTM.load_script(TTM.read_file("pc/game_dir.rb"), "pc/game_dir.rb")
$ttm_game_dir = ttm_check_game_dir
%w[rgss/compat.rb rgss/rgss.rb rgss/ttm_patches.rb].each do |f|
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

__ttm_print "the pointer\n"
keys = []
$ttm_keys_source = proc { keys }
Input.update
check("no mouse: outside the window", [Input.mouse_x, Input.mouse_y] == [-20, -20])
check("so the timeline condition is false", !(Input.mouse_y < 64 && Input.mouse_y >= 0))
keys = [Input::TTM_TIMELINE]    # what source/main.c reports for the 3DS's L
Input.update
check("L held: at the top of the screen", Input.mouse_y >= 0 && Input.mouse_y < 64, [Input.mouse_x, Input.mouse_y].inspect)
check("L held: not RGSS's L (common event 85 fast-forwards on it)", !Input.press?(Input::L))
keys = []
Input.update
check("L released: outside again", Input.mouse_y == -20)

__ttm_print "the cursor sprite\n"
# the game's own scripts up to and including "Mouse 3", patched as boot.rb
# patches them (all class definitions; the game itself starts in MAIN)
Font.ttm_face = TTM.read_file($ttm_game_dir + "Fonts/OpenSans.ttf")
scripts = load_data("Data/Scripts.rxdata")
scripts.each_with_index do |entry, i|
  name = format("%03d_%s", i + 1, entry[1])
  code = TTM.inflate(entry[2])
  TTM.load_script(TTMPatch.apply(name, code), name) unless code.empty?
  break if name =~ /Mouse 3\z/
end
check("patched", TTMPatch.applied.include?("Mouse 3"))
check("hidden once made", $mouse_sprite && !$mouse_sprite.visible)
$mouse_sprite.visible = true
check("stays hidden when the scripts show it", !$mouse_sprite.visible)

Graphics.transition(0)    # "Start Game" freezes the screen as it loads; show frames again

__ttm_print "the instructions\n"
m = load_data("Data/Map022.rxdata")
lines = []
m.events.each_value { |e| e.pages.each { |pg| pg.list.each { |c| lines << c.parameters[0] if c.code == 401 || c.code == 101 } } }
check("map 22: no mouse in the text", lines.none? { |t| t.to_s =~ /moving the mouse/ })
check("map 22: the L button instead", lines.include?("holding the L button."))
TTMControls::PICTURES.each do |path, (button, words)|
  orig = Bitmap.new(path)
  pic = RPG::Cache.picture(path.split("/").last)
  box = TTMControls.banner(pic)
  check("#{path.split('/').last}: banner found", box, box.inspect)
  changed = 0
  if box
    x = box[0]
    while x <= box[2]
      y = box[1]
      while y <= box[3]
        changed += 1 if pic.get_pixel(x, y).red != orig.get_pixel(x, y).red
        y += 3
      end
      x += 3
    end
  end
  check("#{path.split('/').last}: repainted", changed > 50, "#{changed} samples changed")
  check("#{path.split('/').last}: outside the banner untouched",
        pic.get_pixel(10, 10).alpha == orig.get_pixel(10, 10).alpha && pic.get_pixel(320, 100).alpha == 0)
  # a frame showing it, for the eye
  spr = Sprite.new
  bgm = Bitmap.new(640, 480)
  bgm.fill_rect(0, 0, 640, 480, Color.new(40, 60, 50))
  back = Sprite.new
  back.bitmap = bgm
  back.z = -1
  spr.bitmap = pic
  Graphics.update
  Graphics.ttm_screenshot("pc/shots/controls_#{button}.png")
  spr.dispose
  back.dispose
end

__ttm_print "the zombie phase (map 129)\n"
# The minigame shoots with the keyboard's W A S D (RGSS R/X/Y/Z). The 3DS emits
# only two of those, so while its overlay picture (41) is up the D-pad's four
# directions press the four keys as well.
class TTMTestPicture
  def initialize(name); @name = name; end
  def name; @name; end
end
class TTMTestScreen
  def initialize(pics); @pics = pics; end
  def pictures; @pics; end
end

pics = Array.new(101)
$game_screen = nil
keys = [Input::UP]
$ttm_keys_source = proc { TTMControls.keys_for(keys) }
Input.update
check("no overlay: the D-pad is just the arrows", Input.press?(Input::UP) && !Input.press?(Input::R))

pics[41] = TTMTestPicture.new(TTMControls::PVZ_OVERLAY)
$game_screen = TTMTestScreen.new(pics)
{ Input::UP => Input::R, Input::LEFT => Input::X,
  Input::DOWN => Input::Y, Input::RIGHT => Input::Z }.each do |arrow, shot|
  keys = [arrow]
  Input.update
  check("overlay up: D-pad also presses the shoot key", Input.press?(shot), "#{arrow} -> #{shot}")
end
keys = [Input::UP, Input::RIGHT]
Input.update
check("two directions at once", Input.press?(Input::R) && Input.press?(Input::Z))
pics[41] = TTMTestPicture.new("")
keys = [Input::DOWN]
Input.update
check("overlay erased: plain arrows again", !Input.press?(Input::Y))

__ttm_print "the zombie phase's pictures\n"
[["instruction_shootplants", "Graphics/Pictures/instruction_shootplants"],
 ["MG PvZ Overlay Instr", "Graphics/Pictures/MG PvZ Overlay Instr"],
 ["Instruction - Horse Gallop", "Graphics/Pictures/Instruction - Horse Gallop"]].each do |name, path|
  orig = Bitmap.new(path)
  pic = RPG::Cache.picture(name)
  changed = 0
  y = 0
  while y < pic.height
    x = 0
    while x < pic.width
      a = pic.get_pixel(x, y)
      b = orig.get_pixel(x, y)
      changed += 1 if a.red != b.red || a.alpha != b.alpha
      x += 4
    end
    y += 4
  end
  check("#{name}: repainted", changed > 20, "#{changed} samples changed")
  spr = Sprite.new
  back = Sprite.new
  bgm = Bitmap.new(640, 480)
  bgm.fill_rect(0, 0, 640, 480, Color.new(30, 34, 44))
  back.bitmap = bgm
  back.z = -1
  spr.bitmap = pic
  Graphics.update
  Graphics.ttm_screenshot("pc/shots/controls_#{name.downcase.gsub(/[^a-z0-9]+/, '_')}.png")
  spr.dispose
  back.dispose
end

txt = RPG::Cache.picture("instruction_shootplants")
gold = 0
(337...363).each { |y| (0...640).each { |x| c = txt.get_pixel(x, y); gold += 1 if c.red > 150 && c.green > 120 && c.blue < 200 } }
check("text banner: gold capitals, no keyboard", gold > 300, "#{gold} gold pixels")
pad = RPG::Cache.picture("MG PvZ Overlay Instr")
check("overlay: the WASD keys are gone", pad.get_pixel(300, 390).alpha == 0)
check("overlay: a D-pad cross instead", pad.get_pixel(320, 424).alpha == 255)
gal = RPG::Cache.picture("Instruction - Horse Gallop")
black = 0
(404...424).each { |y| (250...400).each { |x| c = gal.get_pixel(x, y); black += 1 if c.alpha > 200 && c.red < 60 } }
check("horse: one letter on the key", black > 30, "#{black} dark pixels")
check("horse: panel's border intact", gal.get_pixel(226, 374).alpha == 255 && gal.get_pixel(422, 449).alpha == 255)

__ttm_print "the whack-a-mole's question (map 71)\n"
m = load_data("Data/Map071.rxdata")
labels = nil
targets = []
lines71 = []
m.events.each_value do |e|
  e.pages.each do |pg|
    list = pg.list
    at = list.index { |c| c.code == 102 && c.parameters[0].is_a?(Array) &&
                          c.parameters[0].include?("Use the gamepad (Recommended)") }
    next unless at
    labels = list[at].parameters[0]
    j = at + 1
    while j < list.size && list[j].code != 404
      targets << list[j].parameters[1] if list[j].code == 201 && list[j].parameters[0] == 0
      j += 1
    end
  end
  e.pages.each { |pg| pg.list.each { |c| lines71 << c.parameters[0].to_s if c.code == 401 } }
end
check("map 71: the mouse answer is gone", labels == ["Use the gamepad (Recommended)", "Show the controls"], labels.inspect)
check("map 71: both answers reach map 82", targets == [82, 82], targets.inspect)
check("map 71: the D-pad and A in the hints", lines71.include?('Move mallet: \c[6]D-pad') &&
      lines71.include?('\c[0]Hit: \c[6]A button'))

__ttm_print format("%d checks passed, %d failed%s\n", $ttm_passed, $ttm_failed.size,
                   $ttm_failed.empty? ? "" : ": " + $ttm_failed.join(", "))
