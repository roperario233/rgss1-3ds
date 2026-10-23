# harness.rb -- M2 part 3: run To the Moon's own scripts, headless, and time
# every frame.
#
# Boot is what RGSS does: open the player's archive, read Data/Scripts.rxdata,
# inflate each script and run it at top level, in order. The game's MAIN
# script then loads the database, sets up the start map and runs its scene
# loop -- the real one. Graphics are rgss_headless.rb's state-only stand-ins,
# so the time between two Graphics.update calls is the Ruby work of one frame.
#
# $ttm_frame_hook (called by Graphics.update) records that time and the
# objects allocated, and drives the test plan: FRAMES_PER_MAP frames on the
# start map, then a forced transfer to each test map in turn; when the plan is
# done it sets $scene = nil and the game's loop ends normally. Input presses
# "confirm" every 20 frames so messages and choices move on.
#
# Expects $ttm_archive (path of "To the Moon.rgssad") and rgss_headless.rb
# already loaded. Results go through ttm_log; $spike_summary feeds the 3DS
# bottom screen.

FRAMES_PER_MAP = ($ttm_frames_per_map || 300)
BUDGET_US = 25_000                         # 40 fps

# start map (10) first, then the heaviest maps by events, parallel/autorun
# events, commands and objects (picked from the data, see ruby-spike/README.md).
# [96, :nogc] runs Map096 again with the GC paused: the difference from the
# first Map096 run is what garbage collection costs per frame.
TEST_MAPS = [96, 29, 129, 8, 62, 77, 1, [96, :nogc]]

module Harness
  @plan = TEST_MAPS.dup
  @results = []
  @errors = []
  @cur = nil
  @last_t = nil
  @last_a = 0
  @done = false

  class << self
    attr_reader :results, :errors
    def done?; @done; end
  end

  def self.start_map(map_id, load_us)
    Prof.reset
    Prof.reset_types if $ttm_profile
    Prof.lines_start
    nogc = @nogc_next
    @nogc_next = false
    GC.disable if nogc
    @cur = { map: map_id, load_us: load_us, times: [], allocs: [], nogc: nogc,
             events: ($game_map && $game_map.events ? $game_map.events.size : 0) }
    @results << @cur
  end

  def self.tick
    now = TTM.us
    allocs = TTM.obj_allocs
    if @last_t.nil?                       # first frame: start-up is not a frame
      Prof.install if $ttm_profile
      @last_t = now
      @last_a = allocs
      start_map($game_map.map_id, 0)
      return
    end
    dt = now - @last_t
    da = allocs - @last_a
    @last_t = now
    @last_a = allocs
    map_id = $game_map ? $game_map.map_id : 0
    if map_id != @cur[:map]
      finish_map
      start_map(map_id, dt)               # this frame loaded the map
    else
      @cur[:times] << dt
      @cur[:allocs] << da
      # spike frames: the run is deterministic, so a second run can profile one
      # ($ttm_profile_frame = [map, frame])
      (@cur[:spikes] ||= []) << [@cur[:times].size, dt, da] if da > 5000
    end
    Prof.spike_frame(map_id, @cur[:times].size) if $ttm_profile_frame
    Prof.sample_types(@cur[:times].size) if $ttm_profile
    next_map if @cur[:times].size >= FRAMES_PER_MAP
  end

  def self.next_map
    if @plan.empty?
      finish_map
      @done = true
      $scene = nil
      return
    end
    id = @plan.shift
    if id.is_a?(Array)
      id, mode = id
      @nogc_next = (mode == :nogc)
    end
    $game_temp.player_transferring = true
    $game_temp.player_new_map_id = id
    $game_temp.player_new_x = 8
    $game_temp.player_new_y = 6
    $game_temp.player_new_direction = 2
  end

  def self.finish_map
    return if @cur.nil? || @cur[:reported]
    @cur[:reported] = true
    if @cur[:nogc]
      GC.enable
      GC.start
      ttm_log("(the next line is Map%03d again with the GC paused)" % @cur[:map])
    end
    Prof.lines_report
    t = @cur[:times]
    if t.empty?
      ttm_log(format("map %03d: no frames (load %.0f ms)", @cur[:map], @cur[:load_us] / 1000.0))
      return
    end
    s = t.sort
    n = t.size
    a = @cur[:allocs]
    @cur[:avg] = t.inject(0) { |x, y| x + y } / n / 1000.0
    @cur[:p99] = s[n * 99 / 100] / 1000.0
    @cur[:max] = s[-1] / 1000.0
    @cur[:over] = t.count { |x| x > BUDGET_US }
    @cur[:alloc_avg] = a.inject(0) { |x, y| x + y } / n
    @cur[:alloc_max] = a.max
    ttm_log(format("map%03d ev%3d avg%6.2f p99%5.1f max%6.1f >25:%d",
                   @cur[:map], @cur[:events], @cur[:avg], @cur[:p99], @cur[:max], @cur[:over]))
    ttm_log(format("  %d frames, load %.0f ms, allocs avg %d max %d", n,
                   @cur[:load_us] / 1000.0, @cur[:alloc_avg], @cur[:alloc_max]))
    (@cur[:spikes] || []).first(3).each do |f, us, al|
      ttm_log(format("  spike: frame %d, %.1f ms, %d allocs", f, us / 1000.0, al))
    end
    @cur[:digest] = state_digest
    ttm_log("  state #{@cur[:digest]}")
    Prof.report(n) if $ttm_profile
  end

  # Digest of the game state: player, every event, switches, variables, self
  # switches, pictures. The run is deterministic (seeded rand, scripted input),
  # so two runs that behave the same -- with and without the engine's patches,
  # or on the PC and on the 3DS -- produce the same digests.
  def self.state_digest
    parts = []
    pl = $game_player
    parts << [pl.x, pl.y, pl.real_x, pl.real_y, pl.direction, pl.pattern].join(",")
    $game_map.events.keys.sort.each do |id|
      e = $game_map.events[id]
      parts << [id, e.x, e.y, e.real_x, e.real_y, e.direction, e.pattern, e.character_name,
                e.opacity, e.instance_variable_get(:@erased) ? 1 : 0].join(",")
    end
    parts << ($game_switches.instance_variable_get(:@data) || []).collect { |v| v ? 1 : 0 }.join
    parts << ($game_variables.instance_variable_get(:@data) || []).join(",")
    ss = $game_self_switches.instance_variable_get(:@data) || {}
    parts << ss.keys.collect { |k| k.join(":") + (ss[k] ? "1" : "0") }.sort.join(";")
    ($game_screen.pictures || []).each do |pic|
      parts << [pic.name, pic.x, pic.y, pic.opacity].join(",") if pic
    end
    s = parts.join("|")
    h = 0
    s.each_byte { |b| h = (h * 31 + b) & 0x3fffffff }
    if $ttm_dbg_digest
      ttm_log("  dbg #{s.bytesize} bytes, player #{parts[0]}, map #{$game_map.map_id}, " \
              "#{$game_map.events.size} events, input frame #{$ttm_input_frame}")
      ttm_log("  dbg first event: #{parts[1]}")
    end
    format("%08x", h)
  end

  # after an exception escaped the scene loop: log it, move on to the next map
  def self.error(e)
    @errors << e
    ttm_log("ERROR #{e.class}: #{e.message}")
    bt = e.backtrace || []
    bt.first(6).each { |l| ttm_log("  " + l.to_s) }
    finish_map
  end
end

# Optional per-phase profile ($ttm_profile, PC only: the wrappers' own cost would
# distort 3DS timings). Times are inclusive -- nested phases are also counted in
# their parents (Game_Event inside Game_Map, Sprite_Character inside Spriteset).
module Prof
  PHASES = [["Input", :update, true], ["Mouse", :update, true],
            ["Game_Map", :update], ["Game_Event", :update], ["Interpreter", :update],
            ["Game_Player", :update], ["Game_Screen", :update], ["Game_System", :update],
            ["Spriteset_Map", :update], ["Sprite_Character", :update],
            ["Sprite_Picture", :update], ["Window_Message", :update]]
  @acc = Hash.new(0)
  @calls = Hash.new(0)
  class << self; attr_reader :acc, :calls; end
  def self.add(label, us); @acc[label] += us; @calls[label] += 1; end
  def self.reset; @acc = Hash.new(0); @calls = Hash.new(0); end

  def self.install
    PHASES.each do |cname, meth, singleton|
      next unless Object.const_defined?(cname)
      target = singleton ? Object.const_get(cname).singleton_class : Object.const_get(cname)
      next unless target.method_defined?(meth)
      label = singleton ? "#{cname}.#{meth}" : "#{cname}##{meth}"
      orig = "__prof_#{meth}".to_sym
      next if target.method_defined?(orig)
      target.send(:alias_method, orig, meth)
      target.send(:define_method, meth) do |*args, &blk|
        t = TTM.us
        r = send(orig, *args, &blk)
        Prof.add(label, TTM.us - t)
        r
      end
    end
  end

  # Allocations by type: GC paused across one frame, object counts diffed.
  @types = Hash.new(0)
  @sampled = 0
  class << self; attr_reader :types, :sampled; end
  def self.sample_types(frame_no)
    if @snap
      after = ObjectSpace.count_objects
      after.each { |k, v| d = v - (@snap[k] || 0); @types[k] += d if d > 0 && k != :FREE && k != :TOTAL }
      @sampled += 1
      @snap = nil
      GC.enable
    elsif frame_no % 25 == 0
      GC.start
      GC.disable
      @snap = ObjectSpace.count_objects
    end
  end
  def self.reset_types; @types = Hash.new(0); @sampled = 0; @snap = nil; GC.enable; end

  # Line-level sampling ($ttm_profile_lines, PC build: TTM.profile_start needs
  # mruby's debug hook). Prints the hottest methods and lines with their source.
  def self.lines_start
    TTM.profile_start(50) if $ttm_profile_lines && TTM.respond_to?(:profile_start)
  end

  # profile exactly one frame: the one before frame f's tick is frame f's work
  def self.spike_frame(map_id, frame)
    want_map, want_frame = $ttm_profile_frame
    return unless map_id == want_map
    if frame == want_frame - 1
      TTM.profile_start(5)
    elsif frame == want_frame
      ttm_log("  profile of map #{map_id} frame #{frame}:")
      lines_report(true)
    end
  end

  def self.lines_report(force = false)
    return unless ($ttm_profile_lines || force) && TTM.respond_to?(:profile_stop)
    h = TTM.profile_stop
    return if h.nil? || h.empty?
    total = h.values.inject(0) { |s, v| s + v }
    by_method = Hash.new(0)
    rows = h.collect do |label, n|
      i = label.rindex(" ")
      loc = label[0, i]; meth = label[i + 1..-1]
      j = loc.rindex(":")
      file = loc[0, j]; line = loc[j + 1..-1].to_i
      by_method["#{file[0, 4]}#{meth}"] += n
      [n, file, line, meth]
    end
    ttm_log("    line profile: #{total} samples; top methods (self time):")
    by_method.keys.sort_by { |k| -by_method[k] }.first(15).each do |k|
      ttm_log(format("    %5.1f%%  %s", by_method[k] * 100.0 / total, k))
    end
    ttm_log("    top lines:")
    rows.sort_by { |r| -r[0] }.first(30).each do |n, file, line, meth|
      src = (($ttm_sources || {})[file] || [])[line - 1].to_s.strip[0, 70]
      ttm_log(format("    %5.1f%% %s:%d %s | %s", n * 100.0 / total, file[0, 24], line, meth, src))
    end
  end

  def self.report(frames)
    return if frames == 0
    if @sampled > 0
      top = @types.keys.sort_by { |k| -@types[k] }.first(6)
      ttm_log("    allocated per frame by type: " +
              top.collect { |k| "#{k} #{@types[k] / @sampled}" }.join(", "))
    end
    @acc.keys.sort_by { |k| -@acc[k] }.each do |k|
      ttm_log(format("    %-24s %6.3f ms/frame  %5d calls/frame", k,
                     @acc[k] / 1000.0 / frames, @calls[k] / frames))
    end
  end
end

def ttm_boot
  ttm_log("== M2 part 3: the game's own scripts, headless ==")
  t = TTM.us
  n = TTM.archive_open($ttm_archive)
  ttm_log(format("archive: %d files, indexed in %.0f ms", n, (TTM.us - t) / 1000.0))
  t = TTM.us
  scripts = load_data("Data/Scripts.rxdata")
  ttm_log(format("Scripts.rxdata: %d scripts, %.0f ms", scripts.size, (TTM.us - t) / 1000.0))
  scripts
end

srand($ttm_seed || 20261019)  # random-moving events: same moves on every run and platform
$ttm_scripts = ttm_boot
$ttm_compile_us = 0
$ttm_frame_hook = proc { Harness.tick }
$ttm_main_ran = false

begin
  # every script in order, as RGSS does; the last real one (MAIN) runs the game
  $ttm_scripts.each_with_index do |entry, i|
    code = TTM.inflate(entry[2])
    next if code.empty?
    name = format("%03d_%s", i + 1, entry[1])
    code = TTMPatch.apply(name, code) unless $ttm_no_patches
    ($ttm_sources ||= {})[name] = code.split("\n") if $ttm_profile_lines
    $ttm_main_ran = true if entry[1] =~ /\AMAIN\z/i
    t = TTM.us
    TTM.load_script(code, name)
    $ttm_compile_us += TTM.us - t unless $ttm_main_ran
  end
rescue Exception => e
  Harness.error(e)
end
ttm_log(format("scripts compiled+run before the game loop: %.0f ms", $ttm_compile_us / 1000.0))
ttm_log("patches: " + ($ttm_no_patches ? "off" :
        "#{TTMPatch.applied.size} applied, #{TTMPatch.skipped.size} skipped #{TTMPatch.skipped.inspect}"))

# If something escaped the game's loop, carry on with the next test map.
tries = 0
while !Harness.done? && $ttm_main_ran && $game_map && tries < 8
  tries += 1
  begin
    Harness.next_map
    break if Harness.done?
    $scene = Scene_Map.new
    $scene.main while $scene
  rescue Exception => e
    Harness.error(e)
  end
end

# ---- summary ------------------------------------------------------------------
rs = Harness.results.select { |r| r[:avg] }
frames = rs.inject(0) { |s, r| s + r[:times].size }
over = rs.inject(0) { |s, r| s + r[:over] }
worst = rs.max_by { |r| r[:max] }
heaviest = rs.max_by { |r| r[:avg] }
ttm_log(format("-- %d maps, %d frames, %d over 25 ms, %d errors --", rs.size, frames, over,
               Harness.errors.size))
ttm_log("missing graphics: #{Bitmap.missing.size}") unless Bitmap.missing.empty?
$spike_summary = ["real scripts, headless:",
                  format("%d maps %d frames, %d over 25ms", rs.size, frames, over)]
if heaviest
  $spike_summary << format("heaviest map %03d avg %.1f ms", heaviest[:map], heaviest[:avg])
  $spike_summary << format("worst frame %.1f ms (map %03d)", worst[:max], worst[:map])
  $spike_summary << format("allocs/frame up to %d", rs.collect { |r| r[:alloc_max] }.max)
end
$spike_summary << "errors: #{Harness.errors.size}"
Harness.errors.first(2).each { |e| $spike_summary << "#{e.class}: #{e.message}"[0, 80] }
