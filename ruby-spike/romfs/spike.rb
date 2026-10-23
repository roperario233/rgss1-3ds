# spike.rb -- M2 Ruby-host spike for the To the Moon 3DS port.
#
# An RGSS-shaped workload: the class / alias / super style of the game's Game_*
# scripts, RPG:: data objects, an RGSS Table with its _dump/_load packing,
# event-script eval, and the hot paths of a frame (sprite updates, an
# Interpreter-style command loop, message text). It reports pass/fail per
# feature with timings, heap use, and the Ruby 1.8 features mruby lacks.
#
# The same file runs on the PC (host mruby, or CRuby to check the expectations)
# and on the 3DS. On the 3DS the C host defines Spike; elsewhere the fallback
# below stands in.

unless Object.const_defined?(:Spike)
  module Spike
    def self.log(s); print(s, "\n"); end
    def self.ms; Time.now.to_f * 1000.0; end
    def self.heap_used; 0; end
    def self.platform; "pc"; end
  end
end

$checks = 0
$fails = 0

def check(name)
  $checks += 1
  t = Spike.ms
  begin
    detail = yield.to_s
    status = "ok"
  rescue Exception => e
    detail = "#{e.class}: #{e.message}"
    status = "FAIL"
    $fails += 1
  end
  Spike.log(format("%-4s %-18s%7.1f %s", status, name, Spike.ms - t, detail))
end

def expect(actual, expected)
  raise "got #{actual.inspect}, want #{expected.inspect}" unless actual == expected
end

def kb(bytes)
  format("%dKB", bytes / 1024)
end

Spike.log("== mruby #{MRUBY_VERSION} on #{Spike.platform} ==")

# ---- the shape of Game_Battler / Game_Actor -------------------------------
class Game_Battler
  attr_reader   :name, :states
  attr_accessor :sp
  def initialize(name, hp)
    @name = name
    @hp = hp
    @sp = 0
    @states = []
  end
  def hp; @hp; end
  def hp=(v); @hp = [[v, 0].max, maxhp].min; end
  def maxhp; 999; end
  def dead?; @hp == 0; end
  def add_state(id)
    @states.push(id) unless @states.include?(id)
    @states.sort!
  end
end

class Game_Actor < Game_Battler
  attr_reader :level
  def initialize(name, hp, level)
    super(name, hp)
    @level = level
  end
  def maxhp; super + @level * 10; end
  # the RGSS add-on idiom: alias the old method, redefine, call through
  alias base_maxhp maxhp
  def maxhp; base_maxhp + 1; end
end

check("class/super/alias") do
  a = Game_Actor.new("Johnny", 100, 5)
  expect(a.maxhp, 1050)
  a.hp = 5000
  expect(a.hp, 1050)
  a.hp -= 2000
  expect(a.dead?, true)
  a.add_state(3); a.add_state(1); a.add_state(3)
  expect(a.states, [1, 3])
  "maxhp #{a.maxhp}"
end

# ---- RPG:: data classes, Comparable, sorting ------------------------------
module RPG
  class Item
    include Comparable
    attr_accessor :id, :name, :price
    def initialize(id, name, price)
      @id = id
      @name = name
      @price = price
    end
    def <=>(other); @price <=> other.price; end
  end
end

check("module/Comparable") do
  items = [RPG::Item.new(1, "Potion", 50), RPG::Item.new(2, "Ether", 300),
           RPG::Item.new(3, "Tonic", 20)]
  expect(items.sort.collect { |i| i.name }, ["Tonic", "Potion", "Ether"])
  expect(items.max.name, "Ether")
  expect(items[0].between?(items[2], items[1]), true)
  "#{items.size} items"
end

# ---- Game_Party-style Hash/Array work ---------------------------------------
check("Hash/Array") do
  bag = {}
  gain = lambda { |id, n| bag[id] = [[(bag[id] || 0) + n, 0].max, 99].min }
  gain.call(1, 5); gain.call(1, 200); gain.call(2, 3); gain.call(2, -10)
  expect(bag, {1 => 99, 2 => 0})
  bag.delete_if { |k, v| v == 0 }
  expect(bag.keys, [1])
  expect((1..10).select { |x| x % 2 == 0 }.inject(0) { |s, x| s + x }, 30)
  expect([3, nil, 1, [2, [4]]].flatten.compact.sort, [1, 2, 3, 4])
  expect(%w[b a b c].uniq.sort_by { |s| s }, ["a", "b", "c"])
  idx = []
  %w[x y].each_with_index { |s, i| idx << "#{i}#{s}" }
  expect(idx.join(","), "0x,1y")
  h = Hash.new(0)
  "hello".each_char { |c| h[c] += 1 }
  expect(h["l"], 2)
  "ok"
end

# ---- message text: format, concatenation, substitution ---------------------
check("String/format") do
  expect(sprintf("%03d", 7), "007")
  expect(format("%s Lv.%2d", "Johnny", 5), "Johnny Lv. 5")
  expect("%d/%d" % [3, 4], "3/4")
  expect("ab" * 3, "ababab")
  expect("Johnny".ljust(8, ".") + "|", "Johnny..|")
  expect("a,b,,c".split(","), ["a", "b", "", "c"])
  s = "HP "
  s << 12.to_s
  s += "/99"
  expect(s, "HP 12/99")
  expect("Tommy"[1, 3], "omm")
  expect("\\N[1] waves".sub("\\N[1]", "Johnny"), "Johnny waves")
  expect("café".size, 4)    # UTF-8 aware; Ruby 1.8 says 5 (bytes)
  "ok"
end

# ---- RGSS Table: flat int16 storage, _dump/_load via pack -----------------
class Table
  attr_reader :xsize, :ysize, :zsize
  def initialize(x, y = 1, z = 1)
    @xsize = x
    @ysize = y
    @zsize = z
    @data = Array.new(x * y * z, 0)
  end
  def [](x, y = 0, z = 0)
    @data[x + y * @xsize + z * @xsize * @ysize]
  end
  def []=(*args)
    v = args.pop
    x = args[0]
    y = args[1] || 0
    z = args[2] || 0
    @data[x + y * @xsize + z * @xsize * @ysize] = v
  end
  def _dump(level = -1)
    [3, @xsize, @ysize, @zsize, @data.size].pack("V5") + @data.pack("s*")
  end
  def self._load(s)
    dim, x, y, z, n = s.unpack("V5")
    t = Table.new(x, y, z)
    t.instance_variable_set(:@data, s.byteslice(20, n * 2).unpack("s*"))
    t
  end
end

check("Table _dump/_load") do
  t = Table.new(20, 15, 3)
  20.times { |x| 15.times { |y| t[x, y, 1] = x * 100 + y - 50 } }
  data = t._dump
  t2 = Table._load(data)
  expect([t2.xsize, t2.ysize, t2.zsize], [20, 15, 3])
  expect(t2[19, 14, 1], 1914 - 50)
  expect(t2[0, 0, 1], -50)
  "#{data.bytesize} bytes"
end

# ---- errors: the game's scripts rescue and retry --------------------------
class SpikeError < StandardError; end

check("raise/rescue/retry") do
  tries = 0
  trail = []
  begin
    tries += 1
    raise SpikeError, "try #{tries}" if tries < 3
    trail << "done"
  rescue SpikeError => e
    trail << e.message
    retry
  ensure
    trail << "ensure"
  end
  expect(trail, ["try 1", "try 2", "done", "ensure"])
  r = catch(:found) do
    [1, 2, 3].each { |x| throw :found, x * 10 if x == 2 }
    nil
  end
  expect(r, 20)
  "#{tries} tries"
end

# ---- eval and metaprogramming: event "Script" commands, Marshal's needs ---
class Interpreter_Spike
  def initialize; @index = 41; end
  def command_355(script); eval(script); end    # RGSS: the Script command
end

check("eval/metaprog") do
  $game_var = 7
  expect(eval("1 + 2 * $game_var"), 15)
  expect(Interpreter_Spike.new.command_355("@index + 1"), 42)
  o = Game_Battler.new("Eva", 10)
  o.instance_variable_set(:@sp, 5)
  expect(o.instance_variable_get(:@sp), 5)
  expect(o.instance_variables.sort, [:@hp, :@name, :@sp, :@states])
  expect(o.send(:dead?), false)
  Game_Battler.send(:define_method, :tag) { "<#{@name}>" }
  expect(o.tag, "<Eva>")
  expect(Object.const_get(:Game_Actor).superclass, Game_Battler)
  expect(o.respond_to?(:hp=), true)
  "ok"
end

# ---- numbers ---------------------------------------------------------------
check("Integer/Float/Math") do
  expect([7 / 2, -7 / 2, 7 % 3, -7 % 3], [3, -4, 1, 2])
  expect(17.divmod(5), [3, 2])
  expect([1.5.round, 2.5.round, -1.5.round], [2, 3, -2])
  expect((Math.sqrt(2) * 1000).to_i, 1414)
  expect(Math.sin(Math::PI / 2), 1.0)
  expect(2 ** 40, 1099511627776)    # past 32 bits: needs bigint
  srand(42)
  expect(rand(100).between?(0, 99), true)
  "2**40 ok"
end

# ---- Regexp the way the message system uses it ------------------------------
# (UMS: 111 gsub! / 157 sub! in the game, $1 inside blocks, non-greedy groups,
# and character-by-character slice!(/./m) when drawing text)
UMS_TEXT = "\\N[1] has \\V[1] gold. \\C[2]Hi\\C[0]! \\FACE[lily_1]"

check("Regexp (UMS-style)") do
  raise "no Regexp class" unless Object.const_defined?(:Regexp)
  $game_variables = [0, 42]
  names = ["", "Johnny"]
  text = UMS_TEXT.dup
  text.gsub!(/\\[Nn]\[([0-9]+)\]/) { names[$1.to_i] }
  text.gsub!(/\\[Vv]\[([0-9]+)\]/) { $game_variables[$1.to_i].to_s }
  text.gsub!(/\\[Cc]\[([0-9]+)\]/) { "\001[#{$1}]" }
  text.sub!(/\\[Ff][Aa][Cc][Ee]\[(.*?)\]/, "")
  expect(text, "Johnny has 42 gold. \001[2]Hi\001[0]! ")
  chars = []
  while (c = text.slice!(/./m)) != nil
    chars << c
  end
  expect([chars.size, chars[0], text], [32, "J", ""])
  expect("a, b,c".split(/,\s*/), ["a", "b", "c"])
  m = /(\w+)@(\w+)/.match("mail tommy@sigcorp now")
  expect([m[1], m[2], m.pre_match], ["tommy", "sigcorp", "mail "])
  expect("café été".gsub(/é/, "e"), "cafe ete")
  "ok"
end

# ---- what RGSS 1.x (Ruby 1.8) code expects that mruby does not have -------
missing = []
missing << "Regexp" unless Object.const_defined?(:Regexp)
missing << "Marshal" unless Object.const_defined?(:Marshal)
missing << "Hash#index" unless {}.respond_to?(:index)
missing << "Array#nitems" unless [].respond_to?(:nitems)
missing << "Object#type" unless Object.new.respond_to?(:type)
missing << "String#each" unless "".respond_to?(:each)
missing << "Integer#chr" unless 65.respond_to?(:chr)
differs = []
differs << "str[0]" unless "a"[0] == 97          # 1.8: Integer
differs << "?a" unless ?a == 97                  # 1.8: Integer
differs << "str.size" unless "é".size == 2  # 1.8: bytes
Spike.log("missing vs 1.8: #{missing.empty? ? 'none' : missing.join(' ')}")
Spike.log("1.8 semantics differ: #{differs.join(' ')}")

# ---- performance -----------------------------------------------------------
Spike.log("-- performance --")

check("100k allocations") do
  h0 = Spike.heap_used
  last = nil
  100_000.times { |i| last = Game_Battler.new("x", i) }
  "heap +#{kb(Spike.heap_used - h0)}"
end

check("300k method calls") do
  b = Game_Actor.new("y", 10, 1)
  s = 0
  t = Spike.ms
  300_000.times { s += b.maxhp }
  format("%.0fk calls/s", 300.0 / ((Spike.ms - t) / 1000.0))
end

# Preparing one message: UMS runs dozens of gsub!/sub! passes over the text
# once per message (not per frame).
UMS_CODES = %w[N V C S I H W F E G P T B U L R A M J O Q K D X Y Z]
$message_ms = 0.0
check("msg prep x50") do
  raise "no Regexp class" unless Object.const_defined?(:Regexp)
  pats = UMS_CODES.collect { |k| Regexp.new("\\\\[#{k}#{k.downcase}]\\[([0-9]+)\\]") }
  pats << /\\[Ss][Ll][Vv]\[(.*?)\]/ << /\\[Ii][Nn][Dd]\[(.*?)\]/ << /\\[Mm]/ << /\\[Jj][Rr]/
  base = (UMS_TEXT + " the lighthouse and the stars, ") * 3
  t = Spike.ms
  50.times do
    text = base.dup
    pats.each { |re| text.gsub!(re) { $1.to_s } }
  end
  $message_ms = (Spike.ms - t) / 50
  format("%.2fms/message", $message_ms)
end

class Sprite_Spike
  attr_reader :x, :y, :opacity, :visible
  def initialize(i)
    @x = i * 16
    @y = 0
    @opacity = 255
    @visible = true
    @phase = i
  end
  def update
    @phase = (@phase + 1) % 40
    @y = (Math.sin(@phase * Math::PI / 20) * 4).to_i
    @opacity = @phase < 20 ? 255 : 255 - (@phase - 20) * 8
    @visible = @opacity > 0
  end
end

# Interpreter#execute_command-style dispatch over an event list
class Interp_Spike
  Command = Struct.new(:code, :indent, :parameters)
  def initialize
    codes = [101, 401, 111, 121, 122, 355]
    @list = Array.new(60) { |i| Command.new(codes[i % 6], 0, [i, i % 7]) }
    @switches = Array.new(100, false)
    @variables = Array.new(100, 0)
  end
  def run_frame
    @list.each do |c|
      case c.code
      when 101, 401 then @text = "Line #{c.parameters[0]}"
      when 111 then @branch = @switches[c.parameters[1]]
      when 121 then @switches[c.parameters[1]] = !@switches[c.parameters[1]]
      when 122 then @variables[c.parameters[1]] += c.parameters[0]
      when 355 then @variables[0] = @variables[1] * 2
      end
    end
  end
end

$frame_ms = 0.0
check("frame sim x60") do
  sprites = Array.new(40) { |i| Sprite_Spike.new(i) }
  interp = Interp_Spike.new
  t = Spike.ms
  60.times do
    sprites.each { |s| s.update }
    interp.run_frame
    format("%s: %d", "Tommy", 3)
  end
  $frame_ms = (Spike.ms - t) / 60.0
  format("%.2fms/frame", $frame_ms)
end

$gc_ms = 0.0
check("full GC, 200k live") do
  keep = Array.new(100_000) { |i| [i, "s"] }
  t = Spike.ms
  GC.start
  $gc_ms = Spike.ms - t
  keep = nil
  format("pause %.1fms", $gc_ms)
end

# compile throughput: generate ~100 KB of Game_*-style source and eval it
$compile_kbps = 0.0
check("compile ~100KB") do
  src = ""
  i = 0
  while src.bytesize < 100_000
    src << "class Gen_#{i} < Game_Battler\n"
    src << "  def initialize(a, b)\n    super(a, b)\n    @list = []\n    @count = #{i}\n  end\n"
    src << "  def update\n    @list.each_with_index do |x, j|\n"
    src << "      if x > #{i} && j.even?\n        @count += x\n"
    src << "      elsif x == 0\n        @count -= 1\n"
    src << "      else\n        @count = (@count * 3) % 1000\n      end\n    end\n  end\n"
    src << "  def refresh(mode)\n    case mode\n"
    src << "    when 0 then @text = format(\"%03d\", @count)\n"
    src << "    when 1 then @text = @name.to_s + \" Lv\" + @count.to_s\n"
    src << "    else @text = nil\n    end\n  end\nend\n"
    i += 1
  end
  t = Spike.ms
  eval(src)
  dt = Spike.ms - t
  $compile_kbps = src.bytesize / 1.024 / dt
  format("%dKB, %.0fKB/s", src.bytesize / 1024, $compile_kbps)
end

# ---- summary ---------------------------------------------------------------
Spike.log("-- result: #{$checks - $fails}/#{$checks} checks passed --")

# the game's 124 scripts are 850 KB of Ruby (check_game_scripts.rb)
game_compile_s = $compile_kbps > 0 ? 850.0 / $compile_kbps : 0.0
$spike_summary = [
  "checks    #{$checks - $fails}/#{$checks} passed",
  format("frame sim %.2f ms (40fps=25ms)", $frame_ms),
  format("msg prep  %.2f ms (30 regex passes)", $message_ms),
  format("full GC   %.1f ms, 200k live", $gc_ms),
  format("compile   %.0f KB/s", $compile_kbps),
  format(" -> game's 850KB: ~%.1f s", game_compile_s),
  "missing:  #{missing.empty? ? 'none' : missing.join(' ')}",
]
