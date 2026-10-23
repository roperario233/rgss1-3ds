# marshal_roundtrip.rb -- what this engine's Marshal.dump writes, against bytes
# the real Ruby wrote.
#
#   ruby   work/probe_marshal.rb      # (re)write the fixtures, with CRuby
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\marshal_roundtrip.rb
#
# Every file in work/marshal_fixtures/ is one Marshal stream CRuby produced for a
# construct a To the Moon save contains (work/probe_marshal.rb). Reading one with
# Marshal.load and writing it back with Marshal.dump has to give the same bytes:
# this engine's saves and the PC version's have to be interchangeable, and the
# game's own save file is thirteen streams in a row that must read back as the
# objects that went in (083_Scene_Save.rb / 084_Scene_Load.rb, 096_TtM Util.rb).
#
# A graph fingerprint that walks cycles is compared too, so a failure says whether
# the bytes differ over the same object graph or the graph itself is wrong.
# Prints one line per fixture; a failure prints the first differing byte and the
# bytes around it on both sides.

FIXTURES = $ttm_fixtures || "../work/marshal_fixtures"

# the classes the fixtures name, as the engine's own scripts define them
class Point
  def initialize(x, y); @x = x; @y = y; end
end

# RGSS's RPG::AudioFile is a Struct, and so are the game's own ones
Audio = Struct.new(:name, :volume, :pitch)

class UserMarshal
  def _dump(_limit); "abc"; end
  def self._load(_data); new; end        # the fixture was written by a _dump
end

module RPG
  class Test
    def initialize; @name = "n"; @table = nil; end
  end
end

def fixture(name)
  path = "#{FIXTURES}/cruby_#{name}.bin"
  data = TTM.read_file(path)
  unless data
    raise "cannot read #{path} -- run work/probe_marshal.rb, and run this from engine/"
  end
  data
end

# byte-based: String#[]/#size count characters under MRB_UTF8_STRING, and a
# Marshal stream is bytes (a fixture with 0xff in it is not text)
def hex(bytes)
  s = bytes.to_s
  s = s.byteslice(0, 24).to_s if s.bytesize > 24
  s.unpack("H*")[0] + (bytes.to_s.bytesize > 24 ? "..." : "")
end

def first_diff(a, b)
  x = a.unpack("C*")
  y = b.unpack("C*")
  n = x.size < y.size ? x.size : y.size
  i = 0
  i += 1 while i < n && x[i] == y[i]
  i
end

# counts per class, integer and string bytes -- the same walk as
# ruby-spike/test_marshal.rb, so a shared object is counted once and a cycle ends
def fingerprint(root)
  seen = {}
  counts = Hash.new(0)
  ints = 0
  strbytes = 0
  floats = 0.0
  stack = [root]
  until stack.empty?
    o = stack.pop
    if o.nil? || o == true || o == false || o.is_a?(Symbol)
      next
    elsif o.is_a?(Integer)
      ints = (ints + o) % 1_000_000_007
    elsif o.is_a?(Float)
      counts["Float"] += 1
      floats += o
    elsif seen[o.object_id]
      next
    else
      seen[o.object_id] = true
      if o.is_a?(String)
        counts["String"] += 1
        strbytes += o.bytesize
      elsif o.is_a?(Array)
        counts["Array"] += 1
        stack.concat(o)
      elsif o.is_a?(Hash)
        counts["Hash"] += 1
        o.each { |k, v| stack << k << v }
      elsif o.is_a?(Struct)
        counts[o.class.to_s] += 1
        o.each { |v| stack << v }
      else
        counts[o.class.to_s] += 1
        o.instance_variables.each { |iv| stack << o.instance_variable_get(iv) }
      end
    end
  end
  "#{counts.keys.sort.collect { |k| "#{k}=#{counts[k]}" }.join(",")} " +
    format("ints=%d str=%d floats=%.3f", ints, strbytes, floats)
end

NAMES = %w[
  primitives ints bignum bignum64 floats floats_more mixed default_hash
  objects usermarshal usermarshal_shared shared cycle namespaced struct
  float_links fixnum_links symbol_links class_value module_value
  class_links module_links
]

passed = 0
failed = []
NAMES.each do |name|
  want = fixture(name)
  obj = Marshal.load(want)
  got = Marshal.dump(obj)
  if got == want
    passed += 1
    print format("  ok    %-18s %4d bytes\n", name, want.bytesize)
  else
    failed << name
    i = first_diff(want, got)
    lo = i > 8 ? i - 8 : 0
    print format("  FAIL  %-18s %d bytes in, %d out, first difference at byte %d\n",
                 name, want.bytesize, got.bytesize, i)
    print format("        CRuby   %s\n", hex(want.byteslice(lo, 24)))
    print format("        engine  %s\n", hex(got.byteslice(lo, 24)))
  end
  # the stream has to be readable again, and describe the same object graph
  back = Marshal.load(got)
  fp_want = fingerprint(obj)
  fp_back = fingerprint(back)
  if fp_want != fp_back
    failed << "#{name}(graph)"
    print format("  FAIL  %-18s graph changed: %s -> %s\n", name, fp_want, fp_back)
  elsif got != want
    print format("        the graph is the same, only the bytes differ\n")
  end
end

# The one deliberate difference from CRuby 3.x: this writer gives a String no
# encoding instance variable, because RGSS (Ruby 1.8) has none. CRuby wraps a
# UTF-8 string in 'I' with :E -> true (that is the fixture); this engine writes
# the bytes alone, and both read back as the same String.
cruby_utf8 = fixture("utf8_string")
one_eight = Marshal.dump(["abc"])
if Marshal.load(cruby_utf8) == ["abc"] && one_eight == "\x04\x08\x5b\x06\x22\x08abc" &&
   one_eight != cruby_utf8
  passed += 1
  print format("  ok    utf8 string        %d bytes here, %d in CRuby (no encoding ivar)\n",
               one_eight.bytesize, cruby_utf8.bytesize)
else
  failed << "utf8"
  print format("  FAIL  utf8 string        engine %s\n        CRuby  %s\n",
               hex(one_eight), hex(cruby_utf8))
end

# The save file's shape: thirteen Marshal streams in a row in one file object,
# read back with thirteen Marshal.load(file) calls. Only write/read/pos/pos= are
# involved -- what the engine's File.open hands Marshal (rgss/compat.rb MemIO).
class Stream
  def initialize; @data = ""; @pos = 0; end
  def write(s); @data << s; s.bytesize; end
  def read(n = nil)
    return nil if @pos >= @data.bytesize
    s = n ? @data.byteslice(@pos, n).to_s : @data.byteslice(@pos, @data.bytesize - @pos).to_s
    @pos += s.bytesize
    s
  end
  def pos; @pos; end
  def pos=(v); @pos = v; end
  def rewind; @pos = 0; self; end
  def size; @data.bytesize; end
end

objs = [1, "two", [3, 4.5], { :k => "v" }, nil, Audio.new("a", 1, 2),
        [Audio.new, Audio.new], :sym, true, false, 2**40, -(2**63), [1, "two"]]
file = Stream.new
objs.each { |o| Marshal.dump(o, file) }
file.rewind
back = []
objs.size.times { back << Marshal.load(file) }
if back == objs
  passed += 1
  print format("  ok    13 streams in one file object, %d bytes\n", file.size)
else
  failed << "streams"
  print "  FAIL  13 streams in one file object\n"
  print format("        read back: %s\n", back.inspect)
end

print format("\n%d checks passed", passed)
if failed.empty?
  print ", 0 failed\n"
else
  print format(", %d FAILED: %s\n", failed.size, failed.join(", "))
end
