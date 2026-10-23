# ending_check.rb -- the credit roll's Steam achievement hook (map 167).
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc/ending_check.rb
#
# The ending makes this Script Call (map 167):
#
#     if defined? _ttm_unlock_achievement
#     _ttm_unlock_achievement
#     end
#
# and MAIN's _ttm_unlock_achievement has the same guard around a Steamworks
# call, _steam_achievement_unlock("ACH_DEBUG_MOON"). Two things went wrong on
# the console (run 16 log):
#   * this mruby has no `defined?` keyword -- it is a call to a method of that
#     name whose argument is evaluated first -- so the guard raised on the
#     missing Steam method, and would raise on `defined?` itself once it existed;
#   * nothing defines the Steam function.
# So: compat.rb defines it as a logged no-op, and ttm_patches.rb removes both
# guards (the method, and map 167's Script Call, rewritten as the map loads).
# This runs both patched pieces as the game would, out of the archive.

$ttm_game_dir = "C:/Users/davao/OneDrive/Desktop/3ds project/work/game/"
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

__ttm_print "the Steam shim (compat.rb)\n"
check("_steam_achievement_unlock is defined", _steam_achievement_unlock.nil?)
check("...and takes the name", _steam_achievement_unlock("ACH_DEBUG_MOON").nil?)

scripts = load_data("Data/Scripts.rxdata")
main_src = nil
scripts.each do |entry|
  next unless entry[1].to_s == "MAIN"
  src = TTM.inflate(entry[2])
  main_src = src unless src.empty?
end
check("MAIN is in the archive", !main_src.nil?)

__ttm_print "MAIN's _ttm_unlock_achievement, as the patch leaves it\n"
patched_main = main_src ? TTMPatch.apply("123_MAIN", main_src) : ""
check("the MAIN patch applied", TTMPatch.applied.include?("MAIN"))
defn = patched_main[/def _ttm_unlock_achievement.*?^end/m]
check("the method is there", !defn.nil?)
check("its guard is gone", defn && !defn.include?("defined?"))
if defn
  eval(defn)
  before = $ttm_achievement_unlocks
  err = nil
  begin
    _ttm_unlock_achievement
  rescue => e
    err = e
  end
  check("it runs, as the credit event calls it", err.nil?, err && err.message)
  check("...and reaches the shim", $ttm_achievement_unlocks > before,
        "#{$ttm_achievement_unlocks - before} call(s)")
end

__ttm_print "map 167's Script Call, as the map patch leaves it\n"
map = load_data("Data/Map167.rxdata")
script = nil
map.events.each_value do |e|
  e.pages.each do |pg|
    pg.list.each_with_index do |c, i|
      next unless c.code == 355 && c.parameters[0].to_s == "_ttm_unlock_achievement"
      text = c.parameters[0].to_s
      j = i + 1
      while j < pg.list.size && pg.list[j].code == 655
        text += "\n" + pg.list[j].parameters[0].to_s
        j += 1
      end
      script = text
    end
  end
end
check("the call is there", !script.nil?)
check("its guard is gone", script && !script.include?("defined?"))
if script
  before = $ttm_achievement_unlocks
  err = nil
  begin
    eval(script)
  rescue => e
    err = e
  end
  check("the credit event's call runs", err.nil?, err && err.message)
  check("...and reaches the shim", $ttm_achievement_unlocks > before,
        "#{$ttm_achievement_unlocks - before} call(s)")
end

__ttm_print "the game's own Achievement module (122_Achievements)\n"
ach_src = nil
scripts.each do |entry|
  next unless entry[1].to_s == "Achievements"
  src = TTM.inflate(entry[2])
  ach_src = src unless src.empty?
end
check("the script is in the archive", !ach_src.nil?)
if ach_src
  TTM.load_script(ach_src, "122_Achievements.rb")
  before = $ttm_achievement_unlocks
  err = nil
  begin
    Achievements.unlock(:finish)
  rescue => e
    err = e
  end
  check("Achievements.unlock(:finish) runs", err.nil?, err && err.message)
  check("...and reaches the shim once", $ttm_achievement_unlocks == before + 1,
        "#{$ttm_achievement_unlocks - before} call(s)")
end

__ttm_print format("%d checks passed, %d failed%s\n", $ttm_passed, $ttm_failed.size,
                   $ttm_failed.empty? ? "" : ": " + $ttm_failed.join(", "))
