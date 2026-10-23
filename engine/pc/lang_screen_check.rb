# lang_screen_check.rb -- the game's own language screen, in the running game,
# with the localisation pack mounted: the first-run path of a console with
# lang.dat beside the archive (no save/lang yet).
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\lang_screen_check.rb
#
# MAIN asks File.exist?(save/lang); with the pack and no file that is false, so
# it makes Scene_Language instead of Scene_Map. This run:
#  - waits for the screen, checks the window it made (12 rows, one per language,
#    each drawing its own name in its own font) and saves pc/shots/lang_screen.png
#  - moves the cursor to German and presses C, which is what a player does
#  - checks the choice was written to work/pc_lang/save/lang, that the translator
#    is German from then on, and that the game carries on into Scene_Map
#  - saves pc/shots/lang_german.png of the German map
#
# Needs what pc/lang_check.rb needs: work/lang.dat and the two CJK fonts.

$ttm_lang_pack = "../work/lang.dat"
# A first run: no choice yet. pc/run_pc.rb (loaded below) sets $ttm_data_dir to
# ../work/pc_data/, shared with the other runs, so the setting file is removed
# from there -- and again at the end, so the other checks go on starting in
# English.
SETTING = "../work/pc_data/save/lang"
TTM.file_delete(SETTING)

$ttm_max_frames = 1400
$ttm_confirm_every = 1_000_000                   # only the presses below
$ttm_shots = []
$ls = { step: :screen }

$ttm_extra_frame_hook = proc do
  $ttm_extra_keys = nil
  s = $ls
  case s[:step]
  when :screen
    # the screen is up once MAIN has made it and its transition has run
    if $scene.is_a?(Scene_Language) && $ttm_frame > 60
      w = $scene.instance_variable_get(:@window)
      s[:window] = w
      s[:item_max] = w.instance_variable_get(:@item_max)
      s[:names] = (0...LANGUAGES.size).collect { |i| w.native_name(i) }
      s[:fonts] = (0...LANGUAGES.size).collect { |i| w.font_name(i) }
      s[:rect] = [w.x, w.y, w.width, w.height]
      Graphics.ttm_screenshot("pc/shots/lang_screen.png")
      s[:shot_bytes] = TTM.file_size("pc/shots/lang_screen.png")
      # choose German, as a player would: the cursor, then C
      s[:german] = LANGUAGES.index("de")
      w.index = s[:german]
      $ttm_extra_keys = [13]
      s[:at] = $ttm_frame
      s[:step] = :chosen
    end
  when :chosen
    if $ttm_frame == s[:at] + 1
      s[:setting] = File.exist?($appPath + "lang") ? File.read($appPath + "lang") : nil
      s[:code] = $translator && $translator.code
      s[:on_map] = $scene.is_a?(Scene_Map)
      # from here the intro advances on its own, as in pc/run_pc.rb
      $ttm_keys_source = proc { $ttm_frame % 40 == 0 ? [13] : [] }
    end
    if $scene.is_a?(Scene_Map) && $ttm_frame == s[:at] + 240
      s[:map_id] = $game_map && $game_map.map_id
      s[:default_font] = Font.default_name
      s[:interp] = $game_system.map_interpreter.running?
      Graphics.ttm_screenshot("pc/shots/lang_after_choice.png")
      s[:step] = :done
      $ttm_frame = $ttm_max_frames - 1           # run_pc.rb stops the game next
    end
  end
end

# the runner: globals, keys, frame hook, then romfs/ and boot.rb
TTM.load_script(TTM.read_file("pc/run_pc.rb"), "pc/run_pc.rb")

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

check("the game opened on its language screen", !!$ls[:window])
check("every language has a row", $ls[:item_max] == LANGUAGES.size, "#{$ls[:item_max]} rows")
check("the rows are the pack's languages",
      $ls[:names].size == 12 && $ls[:names][0] == "English", $ls[:names].join(" / "))
# each row's name comes from that language's meta.rxdata (local_txt in
# Window_Language), so the pack itself says what to expect
ko_name = load_data(TR.meta_file_path("ko"))[:lang_name_native]
zh_name = load_data(TR.meta_file_path("zh-CN"))[:lang_name_native]
check("... each named in its own language",
      $ls[:names].include?("Deutsch") && $ls[:names].include?(ko_name) &&
      $ls[:names].include?(zh_name),
      "ko #{ko_name.bytesize} bytes, zh #{zh_name.bytesize} bytes")
check("... and each drawn in its own font",
      $ls[:fonts][0] == "Arial" && $ls[:fonts][LANGUAGES.index("ko")] == "NanumBarunGothic" &&
      $ls[:fonts][LANGUAGES.index("zh-CN")] == "WenQuanYi Zen Hei", $ls[:fonts].uniq.join(","))
check("the window fits the 640x480 screen",
      $ls[:rect][0] >= 0 && $ls[:rect][1] >= 0 &&
      $ls[:rect][0] + $ls[:rect][2] <= 640 && $ls[:rect][1] + $ls[:rect][3] <= 480,
      $ls[:rect].inspect)
check("a screenshot was saved", $ls[:shot_bytes].to_i > 2000, "#{$ls[:shot_bytes]} bytes")

check("choosing German writes the setting", $ls[:setting] == "de", $ls[:setting].inspect)
check("... the translator is German", $ls[:code] == "de")
check("... and the game carries on into the map", $ls[:on_map] && $ls[:map_id].to_i > 0,
      "map #{$ls[:map_id]}")
check("... with the game's own font (German needs no other)",
      $ls[:default_font] == "Arial", $ls[:default_font].inspect)
check("... and the intro event running", $ls[:interp] == true)
check("the intro was saved", TTM.file_size("pc/shots/lang_after_choice.png").to_i > 2000)

__ttm_print format("\n%d checks passed", $ttm_passed)
if $ttm_failed.empty?
  __ttm_print ", 0 failed\n"
else
  __ttm_print format(", %d FAILED: %s\n", $ttm_failed.size, $ttm_failed.join(", "))
end
# the shared PC data folder starts in English again for the other checks
TTM.file_delete(SETTING)
