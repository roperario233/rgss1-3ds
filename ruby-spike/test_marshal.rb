# test_marshal.rb -- check mruby-ttm-core's Marshal.load and archive reader
# against CRuby, on every .rxdata the game has (except Scripts).
#
#   mruby (reads the files out of the .rgssad with the C archive reader):
#     mruby.exe test_marshal.rb  > m.txt
#   CRuby (reads the extracted copies in work/raw/Data with its own Marshal):
#     ruby test_marshal.rb       > c.txt
#   then the two outputs must be identical.
#
# Each line fingerprints one file's object graph: object counts per class,
# integer sum, string bytes and byte sum, float sum, Table/Color/Tone contents.
# Prints numbers only.

ROOT = "C:/Users/davao/OneDrive/Desktop/3ds project"
ARCHIVE = "#{ROOT}/work/game/To the Moon.rgssad"
MRUBY = Object.const_defined?(:MRUBY_VERSION)

if MRUBY
  eval(TTM.read_file("#{ROOT}/ruby-spike/romfs/rgss_headless.rb"), nil, "rgss_headless.rb")
  TTM.archive_open(ARCHIVE)
  def read_rx(name); Marshal.load(TTM.archive_read("Data/#{name}")); end
  NAMES = (TTM.read_file("#{ROOT}/work/raw/Data/../../tables/.names") || "").split("\n")
else
  class Table
    attr_reader :xsize, :ysize, :zsize
    def [](x, y = 0, z = 0); @data[x + (y + z * @ysize) * @xsize]; end
    def self._load(s)
      t = allocate
      _, x, y, z, n = s.unpack("l<5")
      t.instance_variable_set(:@xsize, x); t.instance_variable_set(:@ysize, y)
      t.instance_variable_set(:@zsize, z)
      t.instance_variable_set(:@data, s.byteslice(20, n * 2).unpack("s<*"))
      t
    end
  end
  class Color; attr_reader :red, :green, :blue, :alpha
    def self._load(s); c = allocate; r, g, b, a = s.unpack("E4")
      c.instance_variable_set(:@red, r); c.instance_variable_set(:@green, g)
      c.instance_variable_set(:@blue, b); c.instance_variable_set(:@alpha, a); c; end
  end
  class Tone; attr_reader :red, :green, :blue, :gray
    def self._load(s); c = allocate; r, g, b, a = s.unpack("E4")
      c.instance_variable_set(:@red, r); c.instance_variable_set(:@green, g)
      c.instance_variable_set(:@blue, b); c.instance_variable_set(:@gray, a); c; end
  end
  def define_path(name)
    name.split("::").inject(Object) do |mod, part|
      mod.const_defined?(part, false) ? mod.const_get(part, false) : mod.const_set(part, Class.new)
    end
  end
  def read_rx(name)
    data = File.binread("#{ROOT}/work/raw/Data/#{name}")
    begin
      Marshal.load(data)
    rescue ArgumentError => e
      raise unless e.message =~ %r{undefined class/module (\S+)}
      define_path($1)
      retry
    end
  end
end

MOD = 1_000_000_007

def fingerprint(root)
  seen = {}
  counts = Hash.new(0)
  ints = 0; strbytes = 0; strsum = 0; floats = 0.0; tablesum = 0
  stack = [root]
  until stack.empty?
    o = stack.pop
    if o.nil? || o == true || o == false || o.is_a?(Symbol)
      next
    elsif o.is_a?(Integer)
      ints = (ints + o) % MOD
    elsif o.is_a?(Float)
      floats += o
    else
      id = o.object_id
      next if seen[id]
      seen[id] = true
      if o.is_a?(String)
        counts["String"] += 1
        strbytes += o.bytesize
        o.each_byte { |b| strsum = (strsum + b) % MOD }
      elsif o.is_a?(Array)
        counts["Array"] += 1
        stack.concat(o)
      elsif o.is_a?(Hash)
        counts["Hash"] += 1
        o.each { |k, v| stack << k << v }
      elsif o.is_a?(Table)
        counts["Table"] += 1
        tablesum = (tablesum + o.xsize * 7 + o.ysize * 11 + o.zsize * 13) % MOD
        o.zsize.times { |z| o.ysize.times { |y| o.xsize.times { |x|
          tablesum = (tablesum + o[x, y, z]) % MOD } } }
      elsif o.is_a?(Color)
        counts["Color"] += 1
        floats += o.red + o.green * 2 + o.blue * 3 + o.alpha * 5
      elsif o.is_a?(Tone)
        counts["Tone"] += 1
        floats += o.red + o.green * 2 + o.blue * 3 + o.gray * 5
      else
        counts[o.class.to_s] += 1
        o.instance_variables.sort.each { |iv| stack << o.instance_variable_get(iv) }
      end
    end
  end
  c = counts.keys.sort.collect { |k| "#{k}=#{counts[k]}" }.join(",")
  "#{c} ints=#{ints} str=#{strbytes}/#{strsum} floats=#{format('%.3f', floats)} table=#{tablesum}"
end

names = %w[Actors Classes Skills Items Weapons Armors Enemies Troops States Animations
           Tilesets CommonEvents System MapInfos]
(1..999).each { |i| names << format("Map%03d", i) }
names.each do |n|
  file = "#{n}.rxdata"
  have = MRUBY ? TTM.archive_has?("Data/#{file}") : File.exist?("#{ROOT}/work/raw/Data/#{file}")
  next unless have
  print "#{n}: #{fingerprint(read_rx(file))}\n"
end
