# count_live_set.rb -- how many Ruby objects does To the Moon keep alive?
#
# The GC cost on the 3DS scales with the live set (part 1 measured ~2 us per
# live object for a full collection), so this counts it from the game data, as
# mruby on the 3DS would hold it:
#   * RGSS keeps the database (13 files, loaded by Scene_Title / MAIN) plus the
#     CURRENT map. Each map is counted on its own; the worst one matters.
#   * With a translation selected, TTMTranslator also keeps the whole
#     lang/<code>/lang.rxdata resident (all maps' text) -- counted separately.
#   * Heap objects in mruby on the 3DS (32-bit word boxing, double floats):
#     String, Array, Hash, Float (always boxed on 32-bit), Bignum, plain objects,
#     and Table/Color/Tone (C data objects in the engine). Integers that fit in
#     31 bits, Symbols, nil/true/false are immediates and cost nothing.
#
# Run with the private CRuby (Marshal 4.8 loads fine in Ruby 3.4):
#   C:\Users\davao\devkitpro\tools\rubyinstaller-3.4.11-1-x64\bin\ruby.exe ruby-spike\count_live_set.rb
#
# Read-only on work/raw. Prints counts only; no game content is written anywhere.

require "prism"

ROOT = File.expand_path("..", __dir__)
DATA_DIR = File.join(ROOT, "work", "raw", "Data")
LANG_DIR = File.join(ROOT, "work", "raw_lang")
SCRIPT_DIR = File.join(ROOT, "work", "scripts")

DATABASE = %w[Actors Classes Skills Items Weapons Armors Enemies Troops States
              Animations Tilesets CommonEvents System]

# RGSS's user-marshalled types: in the engine they are C data objects.
class Table; def self._load(s); allocate; end; end
class Color; def self._load(s); allocate; end; end
class Tone;  def self._load(s); allocate; end; end
DATA_TYPES = [Table, Color, Tone]

# Define RPG::Whatever (or anything else) the first time Marshal asks for it.
def define_path(name)
  name.split("::").inject(Object) do |mod, part|
    mod.const_defined?(part, false) ? mod.const_get(part, false) : mod.const_set(part, Class.new)
  end
end

def load_rx(path)
  data = File.binread(path)
  begin
    Marshal.load(data)
  rescue ArgumentError => e
    raise unless e.message =~ %r{undefined class/module (\S+)}
    define_path($1)
    retry
  end
end

FIXNUM = (-(2**30))..(2**30 - 1)    # mruby MRB_INT32 + word boxing: 31-bit immediates

# Walk the graph once per object; count mruby heap objects and references.
class Census
  attr_reader :counts, :refs, :string_bytes
  def initialize
    @seen = {}.compare_by_identity
    @counts = Hash.new(0)
    @refs = 0
    @string_bytes = 0
  end

  def add(root)
    stack = [root]
    until stack.empty?
      o = stack.pop
      next if o.nil? || o == true || o == false || o.is_a?(Symbol)
      next if o.is_a?(Integer) && FIXNUM.cover?(o)
      next if @seen[o]
      @seen[o] = true
      case o
      when Integer then @counts[:bignum] += 1
      when Float   then @counts[:float] += 1
      when String
        @counts[:string] += 1
        @string_bytes += o.bytesize
      when Array
        @counts[:array] += 1
        @refs += o.size
        stack.concat(o)
      when Hash
        @counts[:hash] += 1
        @refs += o.size * 2
        o.each { |k, v| stack << k << v }
      when *DATA_TYPES then @counts[:data] += 1
      else
        @counts[:object] += 1
        ivs = o.instance_variables
        @counts[:ivars] += ivs.size
        @refs += ivs.size
        ivs.each { |iv| stack << o.instance_variable_get(iv) }
      end
    end
    self
  end

  def total
    @counts.sum { |k, v| k == :ivars ? 0 : v }
  end

  def line(label)
    c = @counts
    format("%-26s %7d objs  (obj %d, ary %d, str %d, hash %d, float %d, data %d)  refs %d",
           label, total, c[:object], c[:array], c[:string], c[:hash], c[:float], c[:data], @refs)
  end
end

# ---- database ----------------------------------------------------------------
db = Census.new
DATABASE.each { |name| db.add(load_rx(File.join(DATA_DIR, "#{name}.rxdata"))) }
puts db.line("database (13 files)")
puts format("  avg ivars per object %.1f, avg string %.0f bytes",
            db.counts[:ivars].to_f / [db.counts[:object], 1].max,
            db.string_bytes.to_f / [db.counts[:string], 1].max)

# ---- maps, each alone (only the current one is resident) --------------------
maps = Dir.glob(File.join(DATA_DIR, "Map[0-9][0-9][0-9].rxdata")).sort.map do |path|
  map = load_rx(path)
  c = Census.new.add(map)
  events = map.instance_variable_get(:@events)
  { name: File.basename(path, ".rxdata"), census: c, events: events ? events.size : 0 }
end
by_size = maps.sort_by { |m| m[:census].total }
pick = ->(q) { by_size[((by_size.size - 1) * q).round] }
puts
puts "maps: #{maps.size}"
[["median map", 0.5], ["90th percentile map", 0.9], ["largest map", 1.0]].each do |label, q|
  m = pick.(q)
  puts m[:census].line("#{label} (#{m[:name]})") + "  events #{m[:events]}"
end
most_events = maps.max_by { |m| m[:events] }
puts "most events on one map: #{most_events[:events]} (#{most_events[:name]})"

# ---- translation file (resident for the whole session when selected) ---------
langs = Dir.glob(File.join(LANG_DIR, "*_lang.rxdata")).map do |path|
  [File.basename(path, "_lang.rxdata"), Census.new.add(load_rx(path))]
end
puts
if langs.empty?
  puts "no translation files found"
else
  biggest = langs.max_by { |_, c| c.total }
  langs.sort_by { |_, c| -c.total }.first(3).each { |code, c| puts c.line("translation #{code}") }
end

# ---- the scripts' own heap: one RProc per method --------------------------------
defs = Dir.glob(File.join(SCRIPT_DIR, "*.rb")).sum do |path|
  src = File.binread(path).gsub("\r\r\n", "\r\n").force_encoding("UTF-8")
  count = 0
  stack = [Prism.parse(src).value]
  until stack.empty?
    n = stack.pop
    next unless n
    count += 1 if n.is_a?(Prism::DefNode)
    stack.concat(n.compact_child_nodes)
  end
  count
end
puts
puts "methods defined by the scripts: #{defs} (one RProc each in mruby)"

# ---- the totals the GC test should use ---------------------------------------
largest = by_size.last[:census]
english = db.total + largest.total + defs
worst = english + (biggest ? biggest[1].total : 0)
puts
puts "LIVE SET, English, largest map:      #{english} objects (#{db.refs + largest.refs} refs)"
puts "LIVE SET, worst translation too:     #{worst} objects" if biggest
puts format("full-GC estimate at part 1's 2.08 us/object: %.0f ms (English), %.0f ms (worst)",
            english * 0.00208, worst * 0.00208)
