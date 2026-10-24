# lang_check.rb -- the game's localisation pack (lang.dat) as the engine reads
# it: gems/mruby-ttm-core/src/langzip.c, the lang/ paths in rgss/rgss.rb, the
# faces in gems/mruby-ttm-gfx/src/font.c and the language setting in
# rgss/compat.rb.
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\lang_check.rb
#
# Needs the pack and the two CJK fonts beside the game the runners use:
#   work/lang.dat                        (the install folder's lang.dat)
#   work/game/Fonts/NanumBarunGothic.ttf (Korean)
#   work/game/Fonts/WenQuanYi_Zen_Hei.ttf (Chinese)
# They are the player's own files, as the archive is; nothing here is shipped
# with the engine.
#
# What it checks, without running the game's scene loop:
#  - the ZIP: 851 entries, a deflated .rxdata and a stored PNG byte-identical to
#    what is in the archive on disk, folders left out, lookups case-blind
#  - the game's own TR module: LANGUAGES = en + 11, the German menu words, the
#    German items and common events, a map's dialogue translated in place
#  - a localised picture, both directly and through the translator's Bitmap
#    override (118_TTMTranslator), which is what the game asks for
#  - the language setting: missing (so Scene_Language appears) with a pack,
#    written and read back, and "en" again when there is no pack
#  - the CJK faces: loaded on demand from the game's Fonts/, with their glyphs
#
# Writes only under $ttm_data_dir (../work/pc_lang/ by default) and leaves it
# empty of the setting file.

TTM.load_script(TTM.read_file("pc/game_dir.rb"), "pc/game_dir.rb")
$ttm_game_dir = $ttm_game_dir || ttm_check_game_dir
$ttm_archive  = $ttm_archive  || ttm_check_archive($ttm_game_dir)
$ttm_lang_pack = $ttm_lang_pack || "../work/lang.dat"
$ttm_data_dir = "../work/pc_lang/"
TTM.file_delete($ttm_data_dir + "save/lang")

%w[rgss/compat.rb rgss/rgss.rb rgss/ttm_patches.rb].each do |f|
  TTM.load_script(TTM.read_file("romfs/" + f), f)
end

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

# ---- what boot.rb does, up to the game's scripts ---------------------------------
n = TTM.archive_open($ttm_archive)
__ttm_print format("archive: %d files\n", n)
files = TTM.lang_open($ttm_lang_pack)
$ttm_lang_pack_on = files > 0
__ttm_print format("lang.dat: %d files\n", files)
font = TTM.read_file($ttm_game_dir + "Fonts/OpenSans.ttf")
Font.ttm_face = font if font
check("the default face is the game's font", !!font)

# ---- the pack -------------------------------------------------------------------
check("797 files (54 folders left out)", files == 797, files)
check("the index is there", TTM.lang_has?("lang/index.rxdata"))
check("a language's data is there", TTM.lang_has?("lang/de/lang.rxdata"))
check("a language's picture is there", TTM.lang_has?("lang/de/Graphics/Pictures/Act1.png"))
check("lookups are case-blind", TTM.lang_has?("LANG\\DE\\LANG.RXDATA"))
check("a folder is not a file", TTM.lang_has?("lang/de/") == false)
check("a missing entry is nil", TTM.lang_read("lang/xx/lang.rxdata").nil?)

# a deflated entry, byte for byte against the copy tools/extract_lang.py unpacked
ref = TTM.read_file("../work/raw_lang/index.rxdata_index.rxdata")
got = TTM.lang_read("lang/index.rxdata")
check("a deflated entry inflates to the same bytes", ref && got == ref,
      got ? "#{got.bytesize} bytes" : "nil")
# ... and a stored one (29 of the pack's files are stored, not deflated)
png = TTM.lang_read("lang/de/Graphics/Pictures/EC-C-60.png")
check("a stored entry reads back", png && png.byteslice(0, 8).unpack("C*") == [137, 80, 78, 71, 13, 10, 26, 10],
      png ? "#{png.bytesize} bytes" : "nil")

# ---- the game's own TR module (scripts 117-119) ---------------------------------
$ttm_scripts = load_data("Data/Scripts.rxdata")
WANTED = ["EventPatcher", "TTMTranslator", "Meta"]
$ttm_scripts.each_with_index do |entry, i|
  next unless WANTED.include?(entry[1])
  TTM.load_script(TTM.inflate(entry[2]), format("%03d_%s", i + 1, entry[1]))
end

check("en + the pack's 11 languages", LANGUAGES.size == 12 && LANGUAGES[0] == "en",
      LANGUAGES.join(","))
check("the codes are the pack's", LANGUAGES[1, 3] == ["de", "es", "fr"] && LANGUAGES.include?("zh-CN"))
check("the localised pictures are known",
      TRANS_IMG_NAMES_S["Graphics/Pictures/heart2"] && TRANS_IMG_NAMES_S["Graphics/Pictures/T Title"])

# the English game, for the comparisons below
text_blocks = proc { |map| out = []; map.events.each_value { |e| e.pages.each { |pg| pg.list.each { |c| out << c if c.code == 101 } } }; out.size }
text_lines = proc do |map|
  out = []
  map.events.each_value do |e|
    e.pages.each do |pg|
      pg.list.each { |c| out << c.parameters[0].to_s if c.code == 101 || c.code == 401 }
    end
  end
  out
end
en_map = load_data("Data/Map002.rxdata")
en_text = text_lines.call(en_map)
check("map 2 has English dialogue", en_text.size > 0, "#{en_text.size} lines")

# The picture the pack translates for German, and its English self: decoded now,
# while there is no translator (after TR.load_language, Bitmap.new asks the pack
# first -- that is what the checks below are about).
PIC = "Graphics/Pictures/Text_memcol-4-exploration"
en_pic = Bitmap.new(PIC)

# $game_system and the two English copies TR.load_language swaps between: MAIN
# makes them (123_MAIN's init_globals); the check makes the same two. $appPath is
# where the game keeps its settings ("save/" on this port, compat.rb's root).
$appPath = "save/"
$savePath = $appPath
$data_items_en = load_data("Data/Items.rxdata")
$data_common_events_en = load_data("Data/CommonEvents.rxdata")
$game_system = Struct.new(:font).new

TR.load_language("de")
check("German is loaded", $translator && $translator.code == "de")
check("the menu's words are German", txt(:menu_commands1)[0] == "Charaktere",
      txt(:menu_commands1).join("/"))
check("the items are the translated ones", $data_items.equal?($translator.items))
check("the common events are its own copy",
      !$data_common_events.equal?($data_common_events_en) &&
      $data_common_events.size == $data_common_events_en.size)

de_map = load_data("Data/Map002.rxdata")
de_text = text_lines.call(de_map)
check("map 2 keeps its text blocks", text_blocks.call(de_map) == text_blocks.call(en_map),
      "#{text_blocks.call(en_map)} blocks")
check("... with the German lines in them", de_text.size != en_text.size,
      "#{en_text.size} English lines -> #{de_text.size}")
check("... which is not the English text", de_text != en_text)
check("... and carries German letters",
      de_text.any? { |s| s =~ /[äöüßÄÖÜ]/ }, de_text.find { |s| s =~ /[äöüßÄÖÜ]/ })
__ttm_print format("        one line now: %s\n", de_text.find { |s| s =~ /[äöüßÄÖÜ]/ }.to_s[0, 90])

# ---- a picture with text in it ---------------------------------------------------
# Text_memcol-4-exploration is one of the pictures the pack translates
# (TRANS_IMG_NAMES_S), and its German version is 640x480 where the game's is
# 542x473 -- so the bitmap's size says which file was read. The English one was
# decoded above, before the translator was loaded (after that, Bitmap.new asks
# the pack first, which is the point of the last check here).
de_pic = Bitmap.new("lang/de/" + PIC)
check("the game's picture decodes", en_pic.width == 542 && en_pic.height == 473,
      "#{en_pic.width}x#{en_pic.height}")
check("the German one decodes from the pack", de_pic.width == 640 && de_pic.height == 480,
      "#{de_pic.width}x#{de_pic.height}")
check("... and is a different drawing", en_pic.width != de_pic.width)
translated = Bitmap.new(PIC)
check("the translator's Bitmap override loads it",
      translated.width == 640 && translated.height == 480,
      "#{translated.width}x#{translated.height}")
check("a picture the pack does not have stays the game's",
      Bitmap.new("Graphics/Pictures/heart0").width > 0)

# ---- the language setting ---------------------------------------------------------
# MAIN: File.exist?(save/lang) false -> Scene_Language; it writes the choice, and
# the next run opens straight into that language.
check("with a pack and no setting, the screen will show",
      File.exist?(TR.lang_setting_file_path) == false)
TR.load_language("de")
File.write(TR.lang_setting_file_path, "de")
check("the choice is written", File.exist?(TR.lang_setting_file_path) == true)
check("... and read back", File.read(TR.lang_setting_file_path) == "de")
check("... as a real file under the data folder",
      TTM.file_size($ttm_data_dir + "save/lang") == 2)
TTM.file_delete(TTMFiles.norm(TR.lang_setting_file_path))
check("deleting it brings the screen back", File.exist?(TR.lang_setting_file_path) == false)

# ---- Korean and Chinese: the faces, and their glyphs ------------------------------
# Font#name = "NanumBarunGothic" (the pack's meta names it) loads the TTF from the
# game's Fonts/ the first time. Before that the name has no face of its own.
check("no Korean face yet", Font.ttm_face?("NanumBarunGothic") == false)
korean = Bitmap.new(320, 48)
korean.font.name = "NanumBarunGothic"
korean.font.size = 24
check("Font#name loads the face from the game's Fonts/", Font.ttm_face?("NanumBarunGothic"))
korean.draw_text(0, 0, 320, 48, "한국어", 0)
inked = 0
(0...korean.height).each do |y|
  (0...korean.width).each { |x| inked += 1 if korean.get_pixel(x, y).alpha > 0 }
end
check("the Korean word draws glyphs", inked > 50, "#{inked} pixels")

chinese = Bitmap.new(320, 48)
chinese.font.name = "WenQuanYi Zen Hei"
chinese.font.size = 24
chinese.draw_text(0, 0, 320, 48, "中文 (简体)", 0)
inked_cn = 0
(0...chinese.height).each do |y|
  (0...chinese.width).each { |x| inked_cn += 1 if chinese.get_pixel(x, y).alpha > 0 }
end
check("the Chinese face loads too", Font.ttm_face?("WenQuanYi Zen Hei"))
check("... and draws its glyphs", inked_cn > 50, "#{inked_cn} pixels")
check("the two faces' names are kept", Font.ttm_faces.sort == ["NanumBarunGothic", "WenQuanYi Zen Hei"])

# a name with no file of its own draws in the default face (the Latin languages)
latin = Bitmap.new(320, 48)
latin.font.name = "Times New Roman"
latin.font.size = 24
check("an unknown name stays on the default face", Font.ttm_face?("Times New Roman") == false)
check("... and it still measures text", latin.text_size("Charaktere").width > 10,
      "#{latin.text_size("Charaktere").width} px")

# choosing a language sets Font.default_name (TR.load_language), which is where a
# CJK choice makes its face the default one
TR.load_language("ko")
check("Korean's meta names its own font", Font.default_name == "NanumBarunGothic",
      Font.default_name.inspect)
check("... the speech font is the same", $game_system.font == "NanumBarunGothic")
TR.load_language("en")
check("English puts the game's own font back",
      Font.default_name == "Arial" && $translator.nil?)
check("... and puts the English items back", $data_items.equal?($data_items_en))

# ---- no pack: the port's English behaviour, unchanged -----------------------------
TTM.lang_close
$ttm_lang_pack_on = false
check("without a pack the setting reads as en", File.read(TR.lang_setting_file_path) == "en")
check("without a pack the setting reports itself there",
      File.exist?(TR.lang_setting_file_path) == true)
check("without a pack the index is empty (no lang/ at all)",
      load_data("lang/index.rxdata") == TTM_LANG_INDEX)

__ttm_print format("\n%d checks passed", $ttm_passed)
if $ttm_failed.empty?
  __ttm_print ", 0 failed\n"
else
  __ttm_print format(", %d FAILED: %s\n", $ttm_failed.size, $ttm_failed.join(", "))
end
