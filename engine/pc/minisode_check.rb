# minisode_check.rb -- run one of the two Sigmund minisodes as its own game.
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\minisode_check.rb hs1 [frames]
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\minisode_check.rb hs2 [frames]
#
# Run 16. The two minisodes the To the Moon download carries are not content
# inside the main game: each is a whole RGSS1 project of its own, with its own
# .rgssad, its own 114-odd scripts (the same stock RGSS + "Commercial" set as the
# main game, without the GUBID menu, the Achievements script and the translator),
# its own maps and its own Audio/ as loose files. So the engine runs them the way
# it runs the main game -- a different archive, a different folder to write in,
# and nothing else -- and this check is what says whether that is true.
#
# What it does today: boots the archive, runs `frames` frames with confirm
# pressed every 40 (as pc/run_pc.rb does), saves screenshots and prints where the
# time goes. What it will assert (run 16.1/16.3): no exception, the archive
# indexed, the title and the first map drawn, no TTMBottomMenu, and every file it
# writes inside its own data folder.
#
# The game folder is $ttm_game_dir (for Audio/ and Fonts/), $ttm_archive the
# archive inside it, and $ttm_data_dir a folder of its own so a minisode can
# never touch the main game's saves and settings.

GAMES = {
  "hs1" => ["../work/Sigmund Holiday Special 1/", "Siggy - Holiday Special.rgssad",
            "SigCorp - Holiday Special"],
  "hs2" => ["../work/Sigmund Holiday Special 2/", "SigCorp Minisode 2.rgssad",
            "SigCorp Minisode 2"],
}

$ttm_game = ARGV[0].to_s
$ttm_game = "hs1" if $ttm_game.empty?
spec = GAMES[$ttm_game]
raise "unknown game #{$ttm_game.inspect} (hs1 or hs2)" unless spec
$ttm_game_dir, archive, title = spec
$ttm_archive = $ttm_game_dir + archive
$ttm_data_dir = "../work/pc_#{$ttm_game}/"
$ttm_audio_pack = nil          # their sounds are Audio/ in the game folder
$ttm_max_frames = (ARGV[1] || 4200).to_i
$ttm_confirm_every = 40
$ttm_shots = [1, 20, 40, 60, 120, 240, 360, 480, 600, 720, 840]

# A game folder without Fonts/ gets the engine's shared Open Sans (boot.rb falls
# back to $ttm_font_file; main.c will set it to the app's Fonts/OpenSans.ttf).
$ttm_font_file = "../work/game/Fonts/OpenSans.ttf"

$ttm_frame = 0
$ms = { shots: 0, last: nil, menu: false, msg_at: nil, msg_text: nil, msg_shot: false }
$ttm_keys_source = proc do
  $ttm_frame > 0 && $ttm_frame % $ttm_confirm_every == 0 ? [13] : []
end
$ttm_frame_hook = proc do
  $ttm_frame += 1
  Audio.ttm_advance(1000 / Graphics.frame_rate)
  $ms[:menu] ||= !TTMBottomMenu.menu.nil?     # the patch that makes it lives in "Achievements"
  # the first line of dialogue: the intro is long (HS1's first line is ~3,100
  # frames in at 40 fps), so this is what says the message system works at all
  if $ms[:msg_at].nil? && $game_temp && $game_temp.message_window_showing
    $ms[:msg_at] = $ttm_frame
    $ms[:msg_text] = $game_temp.message_text.to_s.dup   # UMS edits the string in place
    $ms[:shot_at] = $ttm_frame + 30                     # once the typewriter has run
    ttm_log(format("frame %4d: first message: %s", $ttm_frame, $ms[:msg_text][0, 70].inspect))
  end
  if $ms[:shot_at] && $ttm_frame == $ms[:shot_at]
    Graphics.ttm_screenshot(format("pc/shots/%s_dialogue.png", $ttm_game))
    $ms[:msg_shot] = true
  end
  if $ttm_shots.include?($ttm_frame)
    Graphics.ttm_screenshot(format("pc/shots/%s_%04d.png", $ttm_game, $ttm_frame))
    $ms[:shots] += 1
    tex = Graphics.ttm_tex_stats
    $ms[:last] = [$ttm_frame, $scene ? $scene.class.to_s : "-",
                  $game_map ? $game_map.map_id : nil, Graphics.ttm_drawables]
    ttm_log(format("frame %4d: scene %-14s map %-4s %4d drawables, render %.1f ms, " +
                   "textures: %d bitmaps %d KB, %d not made",
                   $ms[:last][0], $ms[:last][1], $ms[:last][2] ? $ms[:last][2] : "-",
                   $ms[:last][3], Graphics.ttm_render_us / 1000.0, *tex[0, 3]))
  end
  $scene = nil if $ttm_frame >= $ttm_max_frames
end

# compat.rb first: it owns the log shim (ttm_log), and this mruby has no puts
COMPAT = TTM.read_file("romfs/rgss/compat.rb")
raise "missing romfs/rgss/compat.rb" unless COMPAT
TTM.load_script(COMPAT, "rgss/compat.rb")

ttm_log("minisode: #{$ttm_game} -- #{title}, #{$ttm_archive}, writing in #{$ttm_data_dir}")
begin
  %w[rgss/rgss.rb rgss/ttm_patches.rb boot.rb].each do |f|
    src = TTM.read_file("romfs/" + f)
    raise "missing romfs/#{f}" unless src
    TTM.load_script(src, f)
  end
rescue => e
  ttm_log("ERROR #{e.class}: #{e.message}")
  (e.backtrace || []).each { |l| ttm_log("    " + l.to_s) }
end
ttm_log("minisode: #{$ttm_game} stopped after #{$ttm_frame} frames")

# ---- what the run proves ---------------------------------------------------------
$ttm_passed = 0
$ttm_failed = []
def check(name, cond, extra = nil)
  if cond
    $ttm_passed += 1
    ttm_log(format("  ok    %s%s", name, extra ? "  (#{extra})" : ""))
  else
    $ttm_failed << name
    ttm_log(format("  FAIL  %s%s", name, extra ? "  (#{extra})" : ""))
  end
end

last = $ms[:last] || [0, "-", nil, 0]
check("the archive opened and its scripts ran", $ttm_frame > 0, "#{$ttm_frame} frames")
check("it reached a map", last[1] == "Scene_Map" && last[2].to_i > 0,
      "scene #{last[1]}, map #{last[2]}")
check("the map draws", last[3].to_i > 20, "#{last[3]} drawables")
check("screenshots were saved", $ms[:shots] > 4, "#{$ms[:shots]} shots")
check("it has no bottom-screen menu of the main game's", $ms[:menu] == false)
check("it shows dialogue", $ms[:msg_at].to_i > 0,
      $ms[:msg_at] ? "first line at frame #{$ms[:msg_at]}: #{$ms[:msg_text][0, 40].inspect}" : "no message in #{$ttm_frame} frames")
check("... and the line is real text", $ms[:msg_text].to_s.strip.size > 2)
check("... and it was drawn", $ms[:msg_shot] == true)
check("it is English (no language pack of its own)", $translator.nil?)
check("the app's shared font stood in for its missing Fonts/", $ttm_font_shared == true)
mx, my = Mouse.pos
check("the mouse reports a position, not nil", !mx.nil? && !my.nil?, "#{mx},#{my}")
check("its writes land in its own folder",
      TTMFiles.norm("save/music.txt") == $ttm_data_dir + "save/music.txt" &&
      TTMFiles.norm("save/window.txt") == $ttm_data_dir + "save/window.txt",
      $ttm_data_dir)
check("the two Ruby 1.8 scripts were rewritten, not refused",
      TTMPatch.applied.include?("Caterpillar") && TTMPatch.applied.include?("Mouse 1"),
      TTMPatch.applied.uniq.join(","))

ttm_log(format("\n%d checks passed, %d failed%s", $ttm_passed, $ttm_failed.size,
               $ttm_failed.empty? ? "" : ": " + $ttm_failed.join(", ")))
