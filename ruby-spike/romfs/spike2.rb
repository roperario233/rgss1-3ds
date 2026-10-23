# spike2.rb -- M2 part 2: frame times under mruby's GC, at the game's real size.
#
# Part 1 measured a 416 ms full collection with 200k live objects. The game
# keeps far fewer: count_live_set.rb measured ~36k objects (database + largest
# map + methods) in English and ~70k with a translation resident. This script
#   1. builds a live set of that size, shaped like the census (plain objects
#      with ~4 ivars, short arrays, ~11-byte strings, C-data-like objects,
#      connected as one graph);
#   2. runs 600 frames of a map-shaped workload sized to the busiest map (124
#      events): character movement and animation, their sprites, 20 moving
#      pictures, an event interpreter and a message window -- which allocate the
#      way RGSS does (Floats are heap objects on the 3DS), plus optional extra
#      garbage per frame;
#   3. reports avg / p99 / max frame time and frames over the 25 ms budget of
#      40 fps, with mruby's incremental GC and with its generational GC.
#
# The workload is a model written for this test, not the game's scripts; it
# estimates per-frame Ruby cost, it does not reproduce it.
#
# Runs on the PC too (host mruby): the fallback Spike below stands in.

unless Object.const_defined?(:Spike)
  module Spike
    def self.log(s); print(s, "\n"); end
    def self.us; (Time.now.to_f * 1_000_000).to_i; end
    def self.heap_used; 0; end
    def self.gc_live
      c = ObjectSpace.count_objects
      c[:TOTAL] - c[:FREE]
    end
    def self.platform; "pc"; end
  end
end

FRAMES = 600
BUDGET_MS = 25.0           # 40 fps, RGSS's native rate
EVENTS = 124               # busiest map (Map096)
PICTURES = 20

def log(s); Spike.log(s); end

# ---- 1. the live set ----------------------------------------------------------
class LiveNode
  def initialize(a, b, c, d)
    @a = a
    @b = b
    @c = c
    @d = d
  end
end

# Census shape (database + maps): 43% objects, 37% arrays, 16% strings, 4% data.
# Containers adopt orphans first, so everything stays reachable from one root.
def build_live_set(n, seed)
  srand(seed)
  n_str = n * 16 / 100
  n_data = n * 4 / 100
  n_cont = n - n_str - n_data
  obj_share = 43.0 / 80.0
  nodes = []
  orphans = []
  n_str.times { |i| s = "x" * (4 + i % 15); nodes << s; orphans << s }
  n_data.times { o = Object.new; nodes << o; orphans << o }
  pick = lambda do
    if !orphans.empty? && rand < 0.7
      orphans.delete_at(rand(orphans.size))
    else
      nodes[rand(nodes.size)]
    end
  end
  n_cont.times do |i|
    c = if rand < obj_share
          LiveNode.new(pick.call, rand(2) == 0 ? pick.call : i, i & 255, rand(3) == 0 ? nil : pick.call)
        else
          case rand(4)
          when 0 then []
          when 1 then [pick.call]
          when 2 then [i, pick.call]
          else [pick.call, pick.call, i]
          end
        end
    nodes << c
    orphans << c
  end
  orphans    # the root: whatever was never adopted
end

# ---- 2. the map-shaped workload ---------------------------------------------------
$display_x = 0
$display_y = 0
$frame = 0
$passages = Array.new(40 * 30) { |i| i % 7 == 0 ? 0 : 1 }

# Game_Character-shaped: moves a tile, animates, waits, picks a new step.
class Walker
  attr_reader :x, :y, :real_x, :real_y, :direction, :pattern, :opacity
  def initialize(i)
    @id = i
    @x = i % 40
    @y = (i / 40) % 30
    @real_x = @x * 128
    @real_y = @y * 128
    @direction = 2
    @pattern = 0
    @anime_count = 0
    @stop_count = 0
    @move_speed = 3 + i % 2
    @move_frequency = 3 + i % 4
    @walk_anime = true
    @step_anime = i % 5 == 0
    @jump_count = 0
    @wait_count = 0
    @opacity = 255
  end
  def moving?; @real_x != @x * 128 || @real_y != @y * 128; end
  def jumping?; @jump_count > 0; end
  def screen_x; (@real_x - $display_x + 3) / 4 + 16; end
  def screen_y; (@real_y - $display_y + 3) / 4 + 32; end
  def screen_z(height); screen_y + (height > 32 ? 31 : 0); end
  def passable?(x, y, d)
    nx = x + (d == 6 ? 1 : (d == 4 ? -1 : 0))
    ny = y + (d == 2 ? 1 : (d == 8 ? -1 : 0))
    nx >= 0 && ny >= 0 && nx < 40 && ny < 30 && $passages[nx + ny * 40] != 0
  end
  def update
    if jumping?
      @jump_count -= 1
    elsif moving?
      update_move
    else
      update_stop
    end
    update_animation
    if @wait_count > 0
      @wait_count -= 1
      return
    end
    update_route if @stop_count > (40 - @move_frequency * 2) * (6 - @move_frequency)
  end
  def update_move
    distance = 2 ** @move_speed
    @real_y = [@real_y + distance, @y * 128].min if @y * 128 > @real_y
    @real_x = [@real_x - distance, @x * 128].max if @x * 128 < @real_x
    @real_x = [@real_x + distance, @x * 128].min if @x * 128 > @real_x
    @real_y = [@real_y - distance, @y * 128].max if @y * 128 < @real_y
    @anime_count += 1.5 if @walk_anime        # a Float: a heap object on the 3DS
  end
  def update_stop
    @anime_count += 1 if @step_anime
    @stop_count += 1
  end
  def update_animation
    if @anime_count > 18 - @move_speed * 2
      @pattern = (@pattern + 1) % 4
      @anime_count = 0
    end
  end
  def update_route
    d = [2, 4, 6, 8][(@id + $frame) % 4]
    if passable?(@x, @y, d)
      @x += (d == 6 ? 1 : (d == 4 ? -1 : 0))
      @y += (d == 2 ? 1 : (d == 8 ? -1 : 0))
      @direction = d
    end
    @stop_count = 0
    @wait_count = @id % 3
  end
end

# Sprite_Character-shaped: copies character state to sprite properties (C
# setters in the engine; plain ivars here).
class WalkerSprite
  def initialize(ch)
    @ch = ch
    @src = [0, 0, 32, 48]
    @x = 0
    @y = 0
    @z = 0
    @opacity = 255
    @visible = true
  end
  def update
    @src[0] = @ch.pattern * 32
    @src[1] = (@ch.direction - 2) / 2 * 48
    @x = @ch.screen_x
    @y = @ch.screen_y
    @z = @ch.screen_z(48)
    @opacity = @ch.opacity
    @visible = @x > -32 && @x < 432 && @y > -48 && @y < 288
  end
end

# Game_Picture-shaped: eases position, zoom and opacity toward a target --
# Float arithmetic every frame, as RGSS does.
class Picture
  def initialize(i)
    @x = i * 10.0
    @y = 0.0
    @zoom = 100.0
    @opacity = 255.0
    @duration = 0
    @i = i
  end
  def update
    if @duration < 1
      @duration = 40
      @tx = (@i * 37 + $frame) % 400
      @ty = (@i * 53 + $frame) % 240
      @tzoom = 50 + (@i * 13 + $frame) % 100
      @topacity = (@i * 29 + $frame) % 256
    end
    d = @duration
    @x = (@x * (d - 1) + @tx) / d
    @y = (@y * (d - 1) + @ty) / d
    @zoom = (@zoom * (d - 1) + @tzoom) / d
    @opacity = (@opacity * (d - 1) + @topacity) / d
    @duration -= 1
  end
end

# Interpreter#execute_command-shaped dispatch over an event list.
class EventRunner
  Command = Struct.new(:code, :indent, :parameters)
  def initialize
    codes = [101, 401, 111, 121, 122, 106, 209, 250, 355, 0]
    @list = Array.new(60) { |i| Command.new(codes[i % codes.size], i % 3, [i, i % 7, "p#{i}"]) }
    @switches = Array.new(200, false)
    @variables = Array.new(200, 0)
    @index = 0
  end
  def update
    8.times do
      c = @list[@index]
      @index = (@index + 1) % @list.size
      case c.code
      when 101, 401 then @text = "#{c.parameters[2]}: line #{c.parameters[0]}"
      when 111 then @branch = @switches[c.parameters[1]]
      when 121 then @switches[c.parameters[1]] = !@switches[c.parameters[1]]
      when 122 then @variables[c.parameters[1]] += c.parameters[0]
      when 106 then @wait = c.parameters[0] % 5
      when 209 then @route = [c.parameters[1], c.indent]
      when 250 then @se = ["Audio/SE/step", 80, 100]
      when 355 then @variables[0] = @variables[1] * 2
      end
    end
  end
end

# Window_Message-shaped: reveals text a character per frame with name / colour
# substitution done the string way (the real UMS uses Regexp, which mruby lacks).
class MessageBox
  TEXT = "\\N[1] looked at the lighthouse for a long time. \\C[2]Anya\\C[0] " \
         "was there, as always, under the stars."
  def initialize; reset; end
  def reset
    @text = TEXT.sub("\\N[1]", "Johnny").sub("\\C[2]", "").sub("\\C[0]", "")
    @shown = ""
    @pos = 0
  end
  def update
    if @pos < @text.size
      c = @text[@pos]
      @shown << c
      @pos += 1
      @width = @shown.size * 6 + (c == " " ? 2 : 0)
    else
      reset
    end
  end
end

def extra_garbage(n)
  i = 0
  while i < n
    case i & 3
    when 0 then format("%d/%d", i, n)
    when 1 then [i, i + 1]
    when 2 then i * 0.5
    else "x" + i.to_s
    end
    i += 1
  end
end

WALKERS = Array.new(EVENTS) { |i| Walker.new(i) }
SPRITES = WALKERS.collect { |w| WalkerSprite.new(w) }
PICS = Array.new(PICTURES) { |i| Picture.new(i) }
RUNNER = EventRunner.new
MESSAGE = MessageBox.new

def frame(garbage)
  $frame += 1
  WALKERS.each { |w| w.update }
  SPRITES.each { |s| s.update }
  PICS.each { |p| p.update }
  RUNNER.update
  MESSAGE.update
  extra_garbage(garbage)
end

# ---- 3. measure ----------------------------------------------------------------
$summary = []

def run(label, garbage)
  times = Array.new(FRAMES, 0)
  i = 0
  while i < FRAMES
    t = Spike.us
    frame(garbage)
    times[i] = Spike.us - t
    i += 1
  end
  sorted = times.sort
  avg = times.inject(0) { |s, x| s + x } / FRAMES / 1000.0
  p99 = sorted[FRAMES * 99 / 100] / 1000.0
  max = sorted[-1] / 1000.0
  over = times.count { |x| x > BUDGET_MS * 1000 }
  line = format("%-9s%6.1f%6.1f%7.1f%5d", label, avg, p99, max, over)
  log(line)
  $summary << line
end

TABLE_HEAD = "ms         avg   p99    max >25ms"

def full_gc_ms
  t = Spike.us
  GC.start
  (Spike.us - t) / 1000.0
end

def scenario(name, extra_objects)
  log("-- #{name} --")
  t = Spike.us
  m0 = Spike.heap_used
  $live = (Array($live) + build_live_set(extra_objects, extra_objects))
  GC.start
  log(format("live set +%d objs: %.0f ms, +%d KB", extra_objects,
             (Spike.us - t) / 1000.0, (Spike.heap_used - m0) / 1024))
  pause = full_gc_ms
  log(format("gc_live %d, full GC %.1f ms", Spike.gc_live, pause))
  $summary << format("%s: %d live, full GC %.0f ms", name, Spike.gc_live, pause)
  log(TABLE_HEAD)
  $summary << TABLE_HEAD

  GC.generational_mode = false
  GC.start
  run("incr +0", 0)
  run("incr +2k", 2000)
  GC.generational_mode = true
  GC.start
  run("gen +0", 0)
  run("gen +2k", 2000)
  GC.generational_mode = false
end

log("== M2 part 2 on #{Spike.platform}: #{FRAMES} frames, #{EVENTS} events ==")

# Ruby work alone: GC off, so no collection can land in the timing.
GC.start
GC.disable
log(TABLE_HEAD)
run("GC off", 0)
$summary.unshift("Ruby work alone (GC off):")
GC.enable
GC.start

scenario("English", 38_000)      # 36k measured + game-object headroom
scenario("translated", 34_000)   # + the largest translation file

$spike_summary = $summary
