# compat.rb -- what the game's scripts need from the platform, on mruby.
#
# Logging, the CRuby core methods mruby lacks that the game uses, and stand-ins
# for the PC-only things the game touches: Win32API, mkxp's MKXP module, and a
# small in-memory file store for its settings files. Each shim was found by
# running the game's scripts (ruby-spike/README.md, M2 part 3).

# ---- logging that works on the 3DS (Spike, from C) and on the PC -------------
module Kernel
  alias_method :__ttm_print, :print
  def ttm_log(s)
    if TTM.respond_to?(:log)                     # the 3DS engine app
      TTM.log(s.to_s)
    elsif Object.const_defined?(:Spike) && Spike.respond_to?(:log)
      Spike.log(s.to_s)
    else
      __ttm_print(s.to_s, "\n")
    end
  end
  # RGSS's print and p open a message box; here they go to the log
  def print(*args); ttm_log("[print] " + args.join); nil; end
  def p(*args); args.each { |a| ttm_log("[p] " + a.inspect) }; args.size <= 1 ? args[0] : args; end
end

# ---- CRuby core methods the game uses that mruby lacks ----------------------------
# (each one found by running the game's scripts; see ruby-spike/README.md)
class Symbol
  def id2name; to_s; end
end

# `module_function` with no arguments (every later def becomes a module
# function) is a no-op in mruby; the game's TR module relies on it. mruby does
# call method_added for each def, so the mode is kept as a flag on the module.
class Module
  alias_method :__ttm_module_function, :module_function
  def module_function(*names)
    if names.empty?
      @__ttm_modfunc = true
      self
    else
      __ttm_module_function(*names)
    end
  end
  def method_added(name)
    __ttm_module_function(name) if @__ttm_modfunc
  end
  private :module_function, :method_added
end

# Hash#index (Ruby 1.8): the first key whose value is the argument -- the
# minisodes' mouse script maps mouse buttons to Input constants in a Hash and
# asks it that way (103_Mouse 1's trigger?). Removed in Ruby 1.9, so mruby has
# no Hash#index at all.
class Hash
  def index(value)
    each { |k, v| return k if v == value }
    nil
  end
end

# Window_SaveFile prints each slot's date with strftime("%Y/%m/%d %H:%M");
# mruby's Time has the fields but not the formatter (found on the console:
# the save/load screens crashed on the first slot that had a file).
class Time
  STRFTIME_DAYS = %w[Sunday Monday Tuesday Wednesday Thursday Friday Saturday]
  STRFTIME_MONTHS = %w[January February March April May June July August
                       September October November December]
  unless method_defined?(:strftime)
    def strftime(fmt)
      fmt.gsub(/%([%YCymdeHIMSpjaAbBhyL])/) do
        case $1
        when "%" then "%"
        when "Y" then year.to_s
        when "C" then format("%02d", year / 100)
        when "y" then format("%02d", year % 100)
        when "m" then format("%02d", mon)
        when "d" then format("%02d", mday)
        when "e" then format("%2d", mday)
        when "H" then format("%02d", hour)
        when "I" then format("%02d", (hour % 12).zero? ? 12 : hour % 12)
        when "M" then format("%02d", min)
        when "S" then format("%02d", sec)
        when "L" then format("%03d", usec / 1000)
        when "p" then hour < 12 ? "AM" : "PM"
        when "j" then format("%03d", yday)
        when "a" then STRFTIME_DAYS[wday][0, 3]
        when "A" then STRFTIME_DAYS[wday]
        when "b", "h" then STRFTIME_MONTHS[mon - 1][0, 3]
        when "B" then STRFTIME_MONTHS[mon - 1]
        end
      end
    end
  end
end

# ---- platform stubs ------------------------------------------------------------
module Errno
  class ENOENT < StandardError
    def initialize(msg = nil); super("No such file or directory - #{msg}"); end
  end
  class EACCES < StandardError; end
end

# Windows DLL calls (mouse, window, Steam helpers): accepted, answered with 0
class Win32API
  def initialize(dll, func, *types); @func = func; end
  def call(*args); 0; end
  alias_method :Call, :call
end

# mkxp's ENV, as far as the game's scripts use it. The two minisodes ask
# Utils.dll for the Windows appdata folder (AVSetEnv) and read it back as
# ENV['AV_APPDATA'] to build $appPath. Every path is mapped under one root
# anyway (TTMFiles), so "." keeps the folder the game makes at the top of that
# root instead of a copy of a Windows path. A Hash answers [] and has_key?,
# which is all the scripts ask of it.
ENV = { "AV_APPDATA" => "." } unless Object.const_defined?(:ENV)

# old mkxp's module: the game asks it for the folder its settings and saves go in
module MKXP
  def self.data_directory; "save/"; end
end

# ---- the ending's Steam achievement hook ---------------------------------------
# The credit roll (map 167) calls MAIN's _ttm_unlock_achievement, and
# 122_Achievements#unlock calls the same function directly; both end at
# _steam_achievement_unlock, which the PC build gets from Steamworks. This mruby
# has no `defined?` keyword either: `defined? x` is a call to a method named
# `defined?` with x as its argument, so the game's own guard raised at the
# credits -- first on the missing Steam method (console run 16), then on
# `defined?` itself once a shim existed. ttm_patches.rb drops the two guards (the
# script's method and map 167's Script Call); the call lands here instead, a
# no-op: the achievement is Steam's bookkeeping, with nothing to show in the
# game. The log names it once so a run says the ending asked for it, and the
# argument is optional so a bare name cannot raise.
$ttm_achievement_unlocks = 0
def _steam_achievement_unlock(name = nil)
  if $ttm_achievement_unlocks == 0 && name
    ttm_log("achievement: #{name} (no Steam here)")
  end
  $ttm_achievement_unlocks += 1
  nil
end

# The game's settings and saves. These are real files under $ttm_data_dir: on the
# 3DS the app's folder on the SD card (main.c sets "sdmc:/3ds/ttm3ds/"), on the PC
# a scratch folder the runners set ("pc_data/" by default). The paths stay the
# game's own -- "save/" is $savePath, "music.txt" and "window.txt" sit in it --
# and every one is mapped under that single root, so the game can read and write
# only where it should, no matter what path its scripts pass in.
#
# The language setting ("lang", beside the saves) decides whether the game opens
# on its language screen (120_Scene_Language, from MAIN's TR.load_language when
# File.exist? is false). Two cases:
#   - no language pack (no lang.dat): the setting reads as "en" and reports
#     itself present, so the game stays in English, as the port always has;
#   - a pack (boot.rb set $ttm_lang_pack_on): it reports itself missing until the
#     player chooses, and the choice is written as a real file, so later runs
#     open straight into that language.
module TTMFiles
  LANG_SETTING = /lang\z/
  def self.root
    r = ($ttm_data_dir || "pc_data/").to_s.tr("\\", "/")
    r.empty? || r.end_with?("/") ? r : r + "/"
  end
  def self.lang_pack?; $ttm_lang_pack_on ? true : false; end
  # the game's path as a real one: under the root, forward slashes, no drive
  # letter, no ".." that could climb out of it
  def self.norm(path)
    p = path.to_s.tr("\\", "/")
    p = p.sub(/\A[A-Za-z]:/, "")
    parts = p.split("/").reject { |s| s.empty? || s == "." || s == ".." }
    root + parts.join("/")
  end
  def self.exist?(path)
    p = norm(path)
    return true unless TTM.file_size(p).nil?
    return true if TTM.dir?(p)
    p =~ LANG_SETTING ? !lang_pack? : false
  end
  def self.read(path)
    p = norm(path)
    data = TTM.read_file(p)
    return data if data
    return "en" if p =~ LANG_SETTING && !lang_pack?
    raise Errno::ENOENT, path
  end
  def self.write(path, data)
    p = norm(path)
    n = TTM.file_write(p, data.to_s)
    ttm_log("files: cannot write #{p}") if n.nil?
    n
  end
  def self.mkdir(path); TTM.dir_mkdir(norm(path)); end
  def self.directory?(path); TTM.dir?(norm(path)); end
  def self.delete(path); TTM.file_delete(norm(path)); end
  # RGSS's File#mtime is a Time (Window_SaveFile shows it under each save slot)
  def self.mtime(path)
    t = TTM.file_mtime(norm(path))
    t.nil? ? nil : Time.at(t)
  end
  def self.entries(path); TTM.dir_entries(norm(path)) || []; end
end

class File
  def self.exist?(path); TTMFiles.exist?(path); end
  def self.exists?(path); TTMFiles.exist?(path); end
  def self.file?(path); TTMFiles.exist?(path) && !TTMFiles.directory?(path); end
  def self.directory?(path); TTMFiles.directory?(path); end
  def self.read(path); TTMFiles.read(path); end
  def self.binread(path); TTMFiles.read(path); end
  def self.write(path, data); TTMFiles.write(path, data); data.to_s.bytesize; end
  def self.binwrite(path, data); write(path, data); end
  def self.size(path); TTM.file_size(TTMFiles.norm(path)); end
  def self.open(path, mode = "r", *rest)
    m = mode.to_s
    io = if m.include?("w")
           MemIO.new(path, "")
         elsif m.include?("a")
           MemIO.new(path, TTMFiles.exist?(path) ? TTMFiles.read(path) : "")
         else
           MemIO.new(path, TTMFiles.read(path), true)
         end
    return io unless block_given?
    begin
      yield io
    ensure
      io.close
    end
  end
  def self.new(path, mode = "r", *rest); open(path, mode); end
  def self.delete(*paths); paths.each { |p| TTMFiles.delete(p) }; paths.size; end
  def self.basename(path, suffix = nil)
    b = path.to_s.split(%r{[/\\]}).last.to_s
    suffix && b.end_with?(suffix) ? b[0, b.size - suffix.size] : b
  end
  def self.dirname(path)
    parts = path.to_s.split(%r{[/\\]})
    parts.size > 1 ? parts[0..-2].join("/") : "."
  end
  def self.join(*parts); parts.join("/"); end
  def self.extname(path); (m = /\.[^.\/\\]*\z/.match(path.to_s)) ? m[0] : ""; end
end

# a file handle on TTMFiles; written content is stored on close.
#
# Byte-based throughout: MRB_UTF8_STRING makes String#size and String#[] count
# *characters* (right for the game's text, wrong for a save or a .rxdata), so
# file content is sliced with byteslice/bytesize. Marshal.load(file) has to leave
# the file just after the object it read, and a save is thirteen streams in a row
# (083_Scene_Save.rb writes them, 084_Scene_Load.rb reads them back) -- one
# character counted as two bytes and every later stream is read from the wrong
# offset.
class MemIO
  def initialize(path, content, read_only = false)
    @path = path; @buf = content.dup; @pos = 0; @ro = read_only; @closed = false
  end
  def write(*a); s = a.join; @buf << s; s.bytesize; end
  def print(*a); @buf << a.join; nil; end
  def puts(*a); a.each { |x| @buf << x.to_s << "\n" }; @buf << "\n" if a.empty?; nil; end
  def <<(s); @buf << s.to_s; self; end
  def read(n = nil)
    if n
      return nil if @pos >= @buf.bytesize
      s = @buf.byteslice(@pos, n).to_s
      @pos += s.bytesize
      s
    else
      s = @buf.byteslice(@pos, @buf.bytesize - @pos).to_s
      @pos = @buf.bytesize
      s
    end
  end
  def pos; @pos; end
  def pos=(v); @pos = v; end
  def rewind; @pos = 0; self; end
  def gets
    rest = @buf.byteslice(@pos, @buf.bytesize - @pos).to_s
    return nil if rest.empty?
    i = rest.index("\n")
    line = i ? rest.byteslice(0, i + 1) : rest
    @pos += line.bytesize
    line
  end
  def each_line; while (l = gets); yield l; end; end
  def eof?; @pos >= @buf.bytesize; end
  # Window_SaveFile reads mtime from an open file to show the slot's timestamp
  def mtime; TTMFiles.mtime(@path); end
  def close
    TTMFiles.write(@path, @buf) unless @ro || @closed
    @closed = true
    nil
  end
  def closed?; @closed; end
end

module Kernel
  def open(path, mode = "r", *rest, &block); File.open(path, mode, &block); end
end

module FileTest
  def self.exist?(path); TTMFiles.exist?(path); end
  def self.file?(path); File.file?(path); end
  def self.directory?(path); TTMFiles.directory?(path); end
end

class Dir
  def self.exist?(path); TTMFiles.directory?(path); end
  def self.mkdir(path, *a); TTMFiles.mkdir(path); 0; end
  # the game never walks a folder (no Dir.glob/entries in its scripts); the
  # listing is here so a port of its own code could. Ruby's Dir.entries includes
  # "." and ".." first; TTM.dir_entries is the raw listing.
  def self.glob(*a); []; end
  def self.entries(path); [".", ".."] + TTMFiles.entries(path); end
end

# ---- instructions written for a mouse, told with the 3DS's buttons -----------------
# The game teaches its controls in one message (map 22: the timeline is "moving
# the mouse toward the top of the screen") and two pictures with a mouse icon
# (Tut_MenuCancel: right-click; Tut_InteractItem: left-click). On the 3DS those
# are L (Input.mouse_y), B and A. English only: with another language the
# translator patches the text by position afterwards and loads its own
# pictures, which are left alone.
module TTMControls
  TEXT = {
    "moving the mouse toward the top of the screen." => "holding the L button.",
    # the whack-a-mole (map 71) teaches the PC's keys before it starts
    'Move mallet: \c[6]Arrow keys' => 'Move mallet: \c[6]D-pad',
    '\c[0]Hit: \c[6]Enter/Space/C' => '\c[0]Hit: \c[6]A button',
  }
  PICTURES = {
    "Graphics/Pictures/Tut_MenuCancel" => ["B", "Press B to toggle menu or cancel."],
    "Graphics/Pictures/Tut_InteractItem" => ["A", "Press A to interact with an object."],
  }

  # The zombie phase (map 129) is a Plants-vs-Zombies parody that shoots with the
  # keyboard's W A S D -- RGSS's R, X, Y and Z. The 3DS maps only X and R (the
  # A and W keys), so two of the four directions could never fire: up (W), left
  # (A), down (S) and right (D) become the four D-pad directions while the
  # phase's overlay picture is on screen. The arrows are still reported too, so
  # the player can walk as before; nothing changes outside the minigame.
  PVZ_SHOOT = { 8 => 18, 4 => 14, 2 => 15, 6 => 16 }
  PVZ_OVERLAY = "MG PvZ Overlay Instr"

  def self.pvz?
    screen = $game_screen
    return false unless screen && screen.respond_to?(:pictures) && screen.pictures
    pic = screen.pictures[41]
    !pic.nil? && pic.name == PVZ_OVERLAY
  end

  def self.keys_for(keys)
    return keys unless pvz?
    extra = []
    keys.each do |k|
      shot = PVZ_SHOOT[k]
      extra << shot if shot && !keys.include?(shot)
    end
    extra.empty? ? keys : keys + extra
  end

  # The two pictures the zombie phase shows: its text banner and its keyboard.
  PVZ_PICTURES = {
    "Graphics/Pictures/instruction_shootplants" => :pvz_text,
    "Graphics/Pictures/MG PvZ Overlay Instr" => :pvz_pad,
    # the horse ride (map 29) waits on the C button, which is A on the 3DS
    "Graphics/Pictures/Instruction - Horse Gallop" => :gallop,
  }

  # a map just loaded (load_data): its messages' lines, rewritten
  def self.map_text(map, name = nil)
    map.events.each_value do |e|
      e.pages.each do |pg|
        pg.list.each do |c|
          next unless c.code == 101 || c.code == 401
          t = TEXT[c.parameters[0].to_s.strip]
          c.parameters[0] = t if t
        end
      end
    end
    map_flow(map) if name && name =~ %r{Map071\.rxdata\z}
    ending_scripts(map) if name && name =~ %r{Map167\.rxdata\z}
    map
  end

  # Map 167's credit event makes the achievement call from a Script Call:
  #
  #     if defined? _ttm_unlock_achievement
  #     _ttm_unlock_achievement
  #     end
  #
  # mruby has no `defined?` keyword (see the Steam shim above), so what the guard
  # guarded becomes the call -- in every build of this game the method exists,
  # defined by MAIN and patched there (ttm_patches.rb).
  def self.ending_scripts(map)
    map.events.each_value do |e|
      e.pages.each do |pg|
        list = pg.list
        i = 0
        while i < list.size
          c = list[i]
          if c.code == 355 && c.parameters[0].to_s.include?("defined? _ttm_unlock_achievement")
            c.parameters[0] = "_ttm_unlock_achievement"
            j = i + 1
            while j < list.size && list[j].code == 655     # its continuation lines
              list[j].parameters[0] = ""
              j += 1
            end
            i = j
          else
            i += 1
          end
        end
      end
    end
    map
  end

  # Map 71 asks "Use mouse (Recommended)" / "Use keyboard/gamepad" before the
  # whack-a-mole. The mouse answer transfers to map 125, where the mallet follows
  # a pointer the 3DS does not have, so both answers now reach map 82 (the
  # arrow-key game); the second one still shows the controls first.
  MOUSE_CHOICE = "Use mouse (Recommended)"

  def self.map_flow(map)
    map.events.each_value do |e|
      e.pages.each do |pg|
        list = pg.list
        base = nil
        list.each_with_index do |c, i|
          if c.code == 102 && c.parameters[0].is_a?(Array) &&
             c.parameters[0].include?(MOUSE_CHOICE)
            c.parameters[0] = ["Use the gamepad (Recommended)", "Show the controls"]
            base = i
          end
        end
        next unless base
        j = base + 1
        while j < list.size && list[j].code != 404
          c = list[j]
          if c.code == 402
            c.parameters[1] = c.parameters[0] == 0 ? "Use the gamepad (Recommended)" : "Show the controls"
          elsif c.code == 201 && c.parameters[0] == 0 && c.parameters[1] == 125
            c.parameters[1] = 82
          end
          j += 1
        end
      end
    end
    map
  end

  # a picture just loaded (RPG::Cache): the banner's mouse icon and text
  # replaced by a button and its own words, in the banner's colours
  def self.repaint(path, b)
    special = PVZ_PICTURES[path]
    return pvz_text(b) if special == :pvz_text
    return pvz_pad(b) if special == :pvz_pad
    return gallop(b) if special == :gallop
    spec = PICTURES[path]
    return b unless spec && !$translator
    box = banner(b)
    return b unless box
    x0, y0, x1, y1 = box
    bg = b.get_pixel(x1 - 4, y0 + 4)
    ink = text_colour(b, x0 + 40, x1 - 4, (y0 + y1) / 2 - 3, (y0 + y1) / 2 + 3)
    b.fill_rect(x0 + 1, y0 + 1, x1 - x0 - 1, y1 - y0 - 1, bg)
    cx = x0 + 18
    cy = (y0 + y1) / 2
    r = 11
    (-r..r).each do |dy|                    # the button: a filled circle
      w = 0
      w += 1 while (w + 1) * (w + 1) + dy * dy <= r * r
      b.fill_rect(cx - w, cy + dy, 2 * w + 1, 1, Color.new(60, 60, 60))
    end
    b.font.size = 15
    b.font.bold = true
    b.font.color = Color.new(255, 255, 255)
    b.draw_text(cx - r, cy - r, 2 * r + 1, 2 * r + 1, spec[0], 1)
    b.font.size = 16
    b.font.bold = false
    b.font.color = ink
    b.draw_text(x0 + 36, y0, x1 - x0 - 40, y1 - y0, spec[1], 0)
    b
  end

  # "PRESS 'W' 'A' 'S' 'D' ON THE KEYBOARD TO SHOOT." in the zombie phase: the
  # band, the gold and the capitals stay, the PC's keys go
  def self.pvz_text(b)
    b.font.name = Font.default_name
    b.fill_rect(0, 336, b.width, 29, Color.new(0, 0, 0, 235))
    b.font.size = 15
    b.font.bold = true
    b.font.color = Color.new(230, 194, 131)
    b.draw_text(0, 340, b.width, 20, "PRESS A DIRECTION ON THE D-PAD TO SHOOT.", 1)
    b
  end

  # the keyboard beside it: a D-pad, drawn from rectangles so it needs no asset
  def self.pvz_pad(b)
    b.clear
    cx = b.width / 2
    cy = 424
    arm = 46
    half = 15
    edge = Color.new(49, 49, 49)
    face = Color.new(163, 163, 163)
    b.fill_rect(cx - half - 3, cy - arm - 3, 2 * half + 6, 2 * arm + 6, edge)
    b.fill_rect(cx - arm - 3, cy - half - 3, 2 * arm + 6, 2 * half + 6, edge)
    b.fill_rect(cx - half, cy - arm, 2 * half, 2 * arm, face)
    b.fill_rect(cx - arm, cy - half, 2 * arm, 2 * half, face)
    arrow(b, cx, cy - arm + 9, :up, 8, Color.new(255, 255, 255))
    arrow(b, cx, cy + arm - 9, :down, 8, Color.new(255, 255, 255))
    arrow(b, cx - arm + 9, cy, :left, 8, Color.new(255, 255, 255))
    arrow(b, cx + arm - 9, cy, :right, 8, Color.new(255, 255, 255))
    b
  end

  # a filled triangle, one pixel line at a time (Bitmap has no polygon)
  def self.arrow(b, cx, cy, dir, r, colour)
    r.times do |i|
      w = i + 1
      case dir
      when :up then b.fill_rect(cx - w, cy - r + i, 2 * w, 1, colour)
      when :down then b.fill_rect(cx - w, cy + r - i, 2 * w, 1, colour)
      when :left then b.fill_rect(cx - r + i, cy - w, 1, 2 * w, colour)
      when :right then b.fill_rect(cx + r - i, cy - w, 1, 2 * w, colour)
      end
    end
    b
  end

  # "Hold SPACE to gallop:" (map 29). The event waits on the C button, which is
  # A on the 3DS; the sentence and the key cap say so.
  def self.gallop(b)
    b.fill_rect(228, 377, 192, 17, Color.new(0, 0, 0, 0))
    b.fill_rect(228, 377, 192, 17, Color.new(193, 38, 0, 102))
    b.font.size = 12
    b.font.bold = true
    b.font.color = Color.new(0, 0, 0)
    b.draw_text(229, 378, 190, 14, "Hold A to gallop:", 1)
    b.font.color = Color.new(255, 255, 255)
    b.draw_text(228, 377, 190, 14, "Hold A to gallop:", 1)
    b.fill_rect(243, 401, 164, 26, Color.new(155, 155, 155))
    b.font.size = 20
    b.font.bold = false
    b.font.color = Color.new(0, 0, 0)
    b.draw_text(243, 400, 164, 28, "A", 1)
    b
  end

  # the opaque banner in a picture otherwise transparent: found from the
  # middle column up from the bottom, then along its middle row
  def self.banner(b)
    mx = b.width / 2
    yb = (b.height - 1).downto(0).find { |y| b.get_pixel(mx, y).alpha > 0 }
    return nil unless yb
    yt = yb
    yt -= 1 while yt > 0 && b.get_pixel(mx, yt - 1).alpha > 0
    my = (yt + yb) / 2
    xl = mx
    xl -= 1 while xl > 0 && b.get_pixel(xl - 1, my).alpha > 0
    xr = mx
    xr += 1 while xr < b.width - 1 && b.get_pixel(xr + 1, my).alpha > 0
    return nil if xr - xl < 60 || yb - yt < 16
    [xl, yt, xr, yb]
  end

  # the text's colour: the most opaque pixel across its rows, made opaque (the
  # banners are one colour, the box at 30% opacity and the letters solid)
  def self.text_colour(b, xa, xb, y0, y1)
    best = nil
    (y0..y1).each do |y|
      (xa..xb).each do |x|
        c = b.get_pixel(x, y)
        best = c if best.nil? || c.alpha > best.alpha
      end
    end
    best ? Color.new(best.red, best.green, best.blue, 255) : Color.new(205, 196, 169, 255)
  end
end

$KCODE = "u"
$DEBUG = false
$BTEST = false
