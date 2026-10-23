# boot.rb -- start To the Moon from the player's own files, as RGSS does.
#
# Expects (set by the 3DS app or the PC runner):
#   $ttm_game_dir   the game folder, ending in "/" (Fonts/ is read from it)
#   $ttm_archive    path of "To the Moon.rgssad"
#   $ttm_data_dir   (optional) where the app may write; its cache/ folder keeps
#                   the archive's index and the compiled scripts between starts
# and rgss/compat.rb, rgss/rgss.rb, rgss/ttm_patches.rb already loaded.
#
# Every script in Data/Scripts.rxdata is inflated, patched (ttm_patches.rb) and
# run at top level, in order. The last one, MAIN, runs the game's own scene
# loop; when it returns, the game has ended.
#
# The two slow parts of a start on the 3DS are cached (console M2 run: 1.9 s
# to index the archive, 2.0 s to compile the scripts): the index is saved as
# cache/archive.idx, each script's bytecode as cache/NNN_<crc of its patched
# source>.mrb. A changed archive, patch or engine makes new ones; files no
# longer used are deleted.

boot_t0 = TTM.us
cache_dir = $ttm_data_dir ? $ttm_data_dir + "cache/" : nil
cache_dir = nil if cache_dir && !TTM.dir_mkdir(cache_dir)

ttm_log("To the Moon: opening #{$ttm_archive}")
t0 = TTM.us
n = TTM.archive_open($ttm_archive, cache_dir && cache_dir + "archive.idx")
ttm_log(format("archive: %d files, index %s in %d ms", n,
               TTM.archive_cached? ? "from the cache" : "built", (TTM.us - t0) / 1000))

# The localisation pack: the install folder's lang.dat (a ZIP), mounted beside
# the archive as mkxp does, so the game's own TR module finds lang/index.rxdata
# and lang/<code>/... as ordinary files. Optional: without it the game is
# English, and a pack that will not open must not end the game.
lang_pack = $ttm_lang_pack || ($ttm_game_dir ? $ttm_game_dir + "lang.dat" : nil)
$ttm_lang_pack_on = false
if lang_pack && TTM.file_size(lang_pack)
  t0 = TTM.us
  begin
    n = TTM.lang_open(lang_pack)
    $ttm_lang_pack_on = n > 0
    ttm_log(format("lang: %d files in %s, opened in %d ms", n, lang_pack,
                   (TTM.us - t0) / 1000))
  rescue => e
    ttm_log("lang: #{lang_pack} is not usable (#{e.message}) -- English only")
  end
else
  ttm_log("lang: no lang.dat -- English only")
end

font = TTM.read_file($ttm_game_dir + "Fonts/OpenSans.ttf")
if font.nil? && $ttm_font_file                       # a game with no Fonts/ of its own
  font = TTM.read_file($ttm_font_file)
  if font
    $ttm_font_shared = true
    ttm_log("font: #{$ttm_font_file} (this game carries no Fonts/ of its own)")
  end
end
if font
  Font.ttm_face = font
else
  ttm_log("warning: #{$ttm_game_dir}Fonts/OpenSans.ttf not found -- text will not show")
end

$ttm_scripts = load_data("Data/Scripts.rxdata")
scripts = []
$ttm_scripts.each_with_index do |entry, i|
  code = TTM.inflate(entry[2])
  next if code.empty?
  name = format("%03d_%s", i + 1, entry[1])
  code = TTMPatch.apply(name, code)
  bin = cache_dir && format("%03d_%08x.mrb", i + 1, TTM.crc32(code))
  scripts << [name, code, bin]
end

if cache_dir
  keep = {}
  scripts.each { |s| keep[s[2]] = true }
  (TTM.dir_entries(cache_dir) || []).each do |f|
    TTM.file_delete(cache_dir + f) if f.end_with?(".mrb") && !keep[f]
  end
end

t0 = TTM.us
# shift, not each: MAIN never returns, and the list would keep all 850 KB of
# source alive for the whole game
while (script = scripts.shift)
  name, code, bin = script
  script = nil
  if name =~ /_MAIN\z/ && !$ttm_boot_logged    # the game loop starts here
    $ttm_boot_logged = true
    hits, compiled, bad = TTM.script_cache_stats
    ttm_log(format("scripts: %d from bytecode, %d compiled%s, in %d ms; %d ms since boot.rb started",
                   hits, compiled, bad > 0 ? " (#{bad} cache files unreadable)" : "",
                   (TTM.us - t0) / 1000, (TTM.us - boot_t0) / 1000))
  end
  if bin
    TTMPatch.load_script(name, code, cache_dir + bin)
  else
    TTMPatch.load_script(name, code, nil)
  end
end
ttm_log("the game's main loop has ended")
