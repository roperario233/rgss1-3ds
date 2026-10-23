# rgss_headless.rb -- RPG Maker XP's built-in classes, without graphics or sound.
#
# Enough of RGSS1 (plus the few mkxp-z extras the game uses) for the game's own
# scripts to load data and run their scenes on mruby: every object keeps its
# state (positions, rects, opacity, bitmaps' sizes) but nothing is drawn or
# played. Graphics.update calls $ttm_frame_hook, which is where the harness
# measures each frame. Written from the RGSS1 reference, not copied from any
# implementation.
#
# Needs mruby-ttm-core (TTM.archive_*, Marshal.load) and mruby-onig-regexp.

# ---- logging that works on the 3DS (Spike, from C) and on the PC -------------
module Kernel
  alias_method :__ttm_print, :print
  def ttm_log(s)
    if Object.const_defined?(:Spike) && Spike.respond_to?(:log)
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

# old mkxp's module: the game asks it for the folder its settings and saves go in
module MKXP
  def self.data_directory; "save/"; end
end

# The game's settings and saves are small files it creates and reads back.
# Headless, they live in memory. The language setting reads as "en", so the
# game skips its language screen.
module TTMFiles
  @files = {}
  @dirs = {}
  LANG_SETTING = /lang\z/
  def self.norm(path); path.to_s.tr("\\", "/"); end
  def self.exist?(path)
    p = norm(path)
    @files.key?(p) || @dirs.key?(p) || (p =~ LANG_SETTING ? true : false)
  end
  def self.read(path)
    p = norm(path)
    return @files[p] if @files.key?(p)
    return "en" if p =~ LANG_SETTING
    raise Errno::ENOENT, path
  end
  def self.write(path, data); @files[norm(path)] = data.to_s; end
  def self.mkdir(path); @dirs[norm(path)] = true; end
  def self.directory?(path); @dirs.key?(norm(path)); end
  def self.delete(path); @files.delete(norm(path)); end
end

class File
  def self.exist?(path); TTMFiles.exist?(path); end
  def self.exists?(path); TTMFiles.exist?(path); end
  def self.file?(path); TTMFiles.exist?(path) && !TTMFiles.directory?(path); end
  def self.directory?(path); TTMFiles.directory?(path); end
  def self.read(path); TTMFiles.read(path); end
  def self.binread(path); TTMFiles.read(path); end
  def self.write(path, data); TTMFiles.write(path, data); data.to_s.size; end
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

# a file handle on TTMFiles; written content is stored on close
class MemIO
  def initialize(path, content, read_only = false)
    @path = path; @buf = content.dup; @pos = 0; @ro = read_only; @closed = false
  end
  def write(*a); s = a.join; @buf << s; s.size; end
  def print(*a); @buf << a.join; nil; end
  def puts(*a); a.each { |x| @buf << x.to_s << "\n" }; @buf << "\n" if a.empty?; nil; end
  def <<(s); @buf << s.to_s; self; end
  def read(*a); s = @buf[@pos..-1].to_s; @pos = @buf.size; s; end
  def gets
    return nil if @pos >= @buf.size
    i = @buf.index("\n", @pos)
    line = i ? @buf[@pos..i] : @buf[@pos..-1]
    @pos += line.size
    line
  end
  def each_line; while (l = gets); yield l; end; end
  def eof?; @pos >= @buf.size; end
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
  def self.glob(*a); []; end
  def self.entries(*a); []; end
end

$KCODE = "u"
$DEBUG = false
$BTEST = false

# ---- value types ----------------------------------------------------------------
# Color, Tone, Rect and Table are engine classes in C (gems/mruby-ttm-rgss).

class Font
  class << self
    attr_accessor :default_name, :default_size, :default_bold, :default_italic,
                  :default_color, :default_shadow, :default_outline
  end
  self.default_name = "Arial"
  self.default_size = 22
  self.default_bold = false
  self.default_italic = false
  self.default_color = Color.new(255, 255, 255, 255)
  def self.exist?(name); true; end
  attr_accessor :name, :size, :bold, :italic, :color, :shadow, :outline
  def initialize(name = Font.default_name, size = Font.default_size)
    @name = name; @size = size
    @bold = Font.default_bold; @italic = Font.default_italic
    @color = Color.new(255, 255, 255, 255)
  end
end

# ---- graphics objects: state only ---------------------------------------------
module TTMDisposable
  def dispose; @disposed = true; nil; end
  def disposed?; @disposed ? true : false; end
end

class Bitmap
  include TTMDisposable
  attr_reader :width, :height
  attr_accessor :font
  EXTENSIONS = ["", ".png", ".jpg", ".bmp"]
  @missing = {}
  class << self; attr_reader :missing; end

  def initialize(*args)
    @font = Font.new
    if args.size >= 2
      @width = args[0]; @height = args[1]
    else
      name = args[0].to_s
      size = nil
      EXTENSIONS.each do |ext|
        break if (size = TTM.archive_png_size(name + ext))
        break if TTM.archive_has?(name + ext) && (size = [32, 32])
      end
      unless size
        Bitmap.missing[name] = true
        size = [32, 32]
      end
      @width, @height = size
    end
  end
  def rect; Rect.new(0, 0, @width, @height); end
  def blt(*a); nil; end
  def stretch_blt(*a); nil; end
  def fill_rect(*a); nil; end
  def gradient_fill_rect(*a); nil; end
  def clear; nil; end
  def clear_rect(*a); nil; end
  def get_pixel(x, y); Color.new(0, 0, 0, 0); end
  def set_pixel(x, y, c); nil; end
  def hue_change(h); nil; end
  def blur; nil; end
  def draw_text(*a); nil; end
  def text_size(str)
    Rect.new(0, 0, str.to_s.size * @font.size / 2, @font.size)
  end
  def dup; b = Bitmap.new(@width, @height); b.font = @font; b; end
  alias_method :clone, :dup
end

class Viewport
  include TTMDisposable
  attr_accessor :rect, :visible, :z, :ox, :oy
  attr_writer :color, :tone
  def initialize(*args)
    @rect = args[0].is_a?(Rect) ? args[0] : Rect.new(*args)
    @visible = true; @z = 0; @ox = 0; @oy = 0
  end
  def color; @color ||= Color.new(0, 0, 0, 0); end
  def tone; @tone ||= Tone.new; end
  def flash(color, duration); nil; end
  def update; nil; end
end

class Sprite
  include TTMDisposable
  attr_accessor :bitmap, :src_rect, :visible, :x, :y, :z, :ox, :oy, :zoom_x, :zoom_y,
                :angle, :mirror, :bush_depth, :opacity, :blend_type,
                :viewport, :wave_amp, :wave_length, :wave_speed, :wave_phase
  attr_writer :color, :tone
  def color; @color ||= Color.new(0, 0, 0, 0); end
  def tone; @tone ||= Tone.new; end
  def initialize(viewport = nil)
    @viewport = viewport
    @src_rect = Rect.new
    @visible = true
    @x = @y = @z = @ox = @oy = 0
    @zoom_x = @zoom_y = 1.0
    @angle = 0; @mirror = false; @bush_depth = 0
    @opacity = 255; @blend_type = 0
  end
  def bitmap=(b)
    @bitmap = b
    @src_rect.set(0, 0, b.width, b.height) if b
  end
  def flash(color, duration); nil; end
  def update; nil; end
  def width; @src_rect.width; end
  def height; @src_rect.height; end
end

class Plane
  include TTMDisposable
  attr_accessor :bitmap, :viewport, :visible, :z, :ox, :oy, :zoom_x, :zoom_y,
                :opacity, :blend_type
  attr_writer :color, :tone
  def initialize(viewport = nil)
    @viewport = viewport; @visible = true
    @z = @ox = @oy = 0; @zoom_x = @zoom_y = 1.0
    @opacity = 255; @blend_type = 0
  end
  def color; @color ||= Color.new(0, 0, 0, 0); end
  def tone; @tone ||= Tone.new; end
end

class Window
  include TTMDisposable
  attr_accessor :windowskin, :contents, :stretch, :cursor_rect, :active, :visible,
                :pause, :x, :y, :width, :height, :z, :ox, :oy, :opacity,
                :back_opacity, :contents_opacity, :viewport
  def initialize(viewport = nil)
    @viewport = viewport
    @cursor_rect = Rect.new
    @stretch = true; @active = true; @visible = true; @pause = false
    @x = @y = @width = @height = @z = @ox = @oy = 0
    @opacity = 255; @back_opacity = 255; @contents_opacity = 255
  end
  def update; nil; end
end

class Tilemap
  include TTMDisposable
  attr_accessor :tileset, :autotiles, :map_data, :flash_data, :priorities,
                :visible, :ox, :oy, :viewport
  def initialize(viewport = nil)
    @viewport = viewport; @autotiles = Array.new(7); @visible = true
    @ox = @oy = 0
  end
  def update; nil; end
end

# ---- Graphics / Input / Audio ------------------------------------------------------
module Graphics
  @frame_count = 0
  @frame_rate = 40
  class << self
    attr_accessor :frame_count, :frame_rate, :fullscreen, :show_cursor
  end
  def self.update
    @frame_count += 1
    $ttm_frame_hook.call if $ttm_frame_hook
    nil
  end
  def self.freeze; nil; end
  def self.transition(*a); nil; end
  def self.frame_reset; nil; end
  def self.width; 640; end
  def self.height; 480; end
  def self.snap_to_bitmap; Bitmap.new(640, 480); end
end

module Input
  DOWN = 2; LEFT = 4; RIGHT = 6; UP = 8
  A = 11; B = 12; C = 13; X = 14; Y = 15; Z = 16; L = 17; R = 18
  SHIFT = 21; CTRL = 22; ALT = 23
  F5 = 25; F6 = 26; F7 = 27; F8 = 28; F9 = 29
  # mkxp extensions the game's mouse scripts and pause key use
  MOUSELEFT = 38; MOUSEMIDDLE = 39; MOUSERIGHT = 40; K = 41
  # Headless player: confirms (C) every 20 frames so messages and choices
  # advance; never walks. $ttm_input_frame is bumped by Input.update.
  $ttm_input_frame = 0
  def self.update; $ttm_input_frame += 1; nil; end
  def self.trigger?(k); k == C && $ttm_input_frame % ($ttm_confirm_every || 20) == 0; end
  def self.repeat?(k); trigger?(k); end
  def self.press?(k); false; end
  def self.dir4; 0; end
  def self.dir8; 0; end
  def self.mouse_x; 0; end
  def self.mouse_y; 0; end
end

module Audio
  %w[bgm_play bgm_stop bgm_fade bgs_play bgs_stop bgs_fade me_play me_stop me_fade
     se_play se_stop].each { |m| define_singleton_method(m) { |*a| nil } }
end

# ---- RPG module: the data classes, from the data's own ivars --------------------
module RPG
  def self.data_class(path, ivars, &init)
    # const_defined?(x, false): RPG::Class must not resolve to ::Class.
    # (mruby's const_get takes no inherit flag; a direct constant wins anyway.)
    # ::Class, because inside RPG a bare Class means RPG::Class once it exists.
    klass = path.split("::").inject(RPG) do |mod, part|
      mod.const_defined?(part, false) ? mod.const_get(part) : mod.const_set(part, ::Class.new)
    end
    klass.send(:attr_accessor, *ivars)
    klass.send(:define_method, :initialize, &init) if init
    klass
  end

  data_class("AudioFile", %i[name volume pitch]) { |name = "", volume = 100, pitch = 100|
    @name = name; @volume = volume; @pitch = pitch }
  data_class("EventCommand", %i[code indent parameters]) { |code = 0, indent = 0, parameters = []|
    @code = code; @indent = indent; @parameters = parameters }
  data_class("MoveCommand", %i[code parameters]) { |code = 0, parameters = []|
    @code = code; @parameters = parameters }
  data_class("MoveRoute", %i[repeat skippable list]) {
    @repeat = true; @skippable = false; @list = [RPG::MoveCommand.new] }
  data_class("Map", %i[tileset_id width height autoplay_bgm bgm autoplay_bgs bgs
                       encounter_list encounter_step data events]) { |width = 20, height = 15|
    @tileset_id = 1; @width = width; @height = height; @autoplay_bgm = false
    @bgm = RPG::AudioFile.new; @autoplay_bgs = false; @bgs = RPG::AudioFile.new("", 80)
    @encounter_list = []; @encounter_step = 30; @data = Table.new(width, height, 3); @events = {} }
  data_class("MapInfo", %i[name parent_id order expanded scroll_x scroll_y]) {
    @name = ""; @parent_id = 0; @order = 0; @expanded = false; @scroll_x = 0; @scroll_y = 0 }
  data_class("Event", %i[id name x y pages]) { |x = 0, y = 0|
    @id = 0; @name = ""; @x = x; @y = y; @pages = [RPG::Event::Page.new] }
  data_class("Event::Page", %i[condition graphic move_type move_speed move_frequency move_route
                               walk_anime step_anime direction_fix through always_on_top
                               trigger list]) {
    @condition = RPG::Event::Page::Condition.new; @graphic = RPG::Event::Page::Graphic.new
    @move_type = 0; @move_speed = 3; @move_frequency = 3; @move_route = RPG::MoveRoute.new
    @walk_anime = true; @step_anime = false; @direction_fix = false; @through = false
    @always_on_top = false; @trigger = 0; @list = [RPG::EventCommand.new] }
  data_class("Event::Page::Condition", %i[switch1_valid switch2_valid variable_valid
                                          self_switch_valid switch1_id switch2_id variable_id
                                          variable_value self_switch_ch]) {
    @switch1_valid = @switch2_valid = @variable_valid = @self_switch_valid = false
    @switch1_id = @switch2_id = @variable_id = 1; @variable_value = 0; @self_switch_ch = "A" }
  data_class("Event::Page::Graphic", %i[tile_id character_name character_hue direction pattern
                                        opacity blend_type]) {
    @tile_id = 0; @character_name = ""; @character_hue = 0; @direction = 2; @pattern = 0
    @opacity = 255; @blend_type = 0 }
  data_class("CommonEvent", %i[id name trigger switch_id list]) {
    @id = 0; @name = ""; @trigger = 0; @switch_id = 1; @list = [RPG::EventCommand.new] }
  data_class("Actor", %i[id name class_id initial_level final_level exp_basis exp_inflation
                         character_name character_hue battler_name battler_hue parameters
                         weapon_id armor1_id armor2_id armor3_id armor4_id weapon_fix
                         armor1_fix armor2_fix armor3_fix armor4_fix])
  data_class("Class", %i[id name position weapon_set armor_set element_ranks state_ranks learnings])
  data_class("Class::Learning", %i[level skill_id])
  data_class("Skill", %i[id name icon_name description scope occasion animation1_id animation2_id
                         menu_se common_event_id sp_cost power atk_f eva_f str_f dex_f agi_f
                         int_f hit pdef_f mdef_f variance element_set plus_state_set
                         minus_state_set])
  data_class("Item", %i[id name icon_name description scope occasion animation1_id animation2_id
                        menu_se common_event_id price consumable parameter_type parameter_points
                        recover_hp_rate recover_hp recover_sp_rate recover_sp hit pdef_f mdef_f
                        variance element_set plus_state_set minus_state_set])
  data_class("Weapon", %i[id name icon_name description animation1_id animation2_id price atk
                          pdef mdef str_plus dex_plus agi_plus int_plus element_set
                          plus_state_set minus_state_set])
  data_class("Armor", %i[id name icon_name description kind auto_state_id price pdef mdef eva
                         str_plus dex_plus agi_plus int_plus guard_element_set guard_state_set])
  data_class("Enemy", %i[id name battler_name battler_hue maxhp maxsp str dex agi int atk pdef
                         mdef eva animation1_id animation2_id element_ranks state_ranks actions
                         exp gold item_id weapon_id armor_id treasure_prob])
  data_class("Enemy::Action", %i[kind basic skill_id condition_turn_a condition_turn_b
                                 condition_hp condition_level condition_switch_id rating])
  data_class("Troop", %i[id name members pages])
  data_class("Troop::Member", %i[enemy_id x y hidden immortal])
  data_class("Troop::Page", %i[condition span list])
  data_class("Troop::Page::Condition", %i[turn_valid enemy_valid actor_valid switch_valid turn_a
                                          turn_b enemy_index enemy_hp actor_id actor_hp switch_id])
  data_class("State", %i[id name animation_id restriction nonresistance zero_hp cant_get_exp
                         cant_evade slip_damage rating hit_rate maxhp_rate maxsp_rate str_rate
                         dex_rate agi_rate int_rate atk_rate pdef_rate mdef_rate eva battle_only
                         hold_turn auto_release_prob shock_release_prob guard_element_set
                         plus_state_set minus_state_set])
  data_class("Animation", %i[id name animation_name animation_hue position frame_max frames timings])
  data_class("Animation::Frame", %i[cell_max cell_data])
  data_class("Animation::Timing", %i[frame se flash_scope flash_color flash_duration condition])
  data_class("Tileset", %i[id name tileset_name autotile_names panorama_name panorama_hue fog_name
                           fog_hue fog_opacity fog_blend_type fog_zoom fog_sx fog_sy
                           battleback_name passages priorities terrain_tags])
  data_class("System", %i[magic_number party_members elements switches variables windowskin_name
                          title_name gameover_name battle_transition title_bgm battle_bgm
                          battle_end_me gameover_me cursor_se decision_se cancel_se buzzer_se
                          equip_se shop_se save_se load_se battle_start_se escape_se
                          actor_collapse_se enemy_collapse_se words test_battlers test_troop_id
                          start_map_id start_x start_y battleback_name battler_name battler_hue
                          edit_map_id _])
  data_class("System::Words", %i[gold hp sp str dex agi int atk pdef mdef weapon armor1 armor2
                                 armor3 armor4 attack skill guard item equip])
  data_class("System::TestBattler", %i[actor_id level weapon_id armor1_id armor2_id armor3_id
                                       armor4_id])

  # Bitmaps by folder, cached like RGSS's RPG::Cache
  module Cache
    @cache = {}
    def self.load_bitmap(folder, filename, hue = 0)
      key = [folder, filename, hue]
      b = @cache[key]
      return b if b && !b.disposed?
      @cache[key] = filename.to_s.empty? ? Bitmap.new(32, 32) : Bitmap.new(folder + filename.to_s)
    end
    { animation: "Animations", autotile: "Autotiles", battleback: "Battlebacks",
      battler: "Battlers", character: "Characters", fog: "Fogs", gameover: "Gameovers",
      icon: "Icons", panorama: "Panoramas", picture: "Pictures", tileset: "Tilesets",
      title: "Titles", windowskin: "Windowskins" }.each do |m, dir|
      define_singleton_method(m) { |f, hue = 0| load_bitmap("Graphics/#{dir}/", f, hue) }
    end
    def self.tile(filename, tile_id, hue)
      key = [filename, tile_id, hue]
      @cache[key] ||= Bitmap.new(32, 32)
    end
    def self.clear; @cache = {}; GC.start; end
  end

  # RPG::Sprite: Sprite plus battle effects (damage pop-ups, animations)
  class Sprite < ::Sprite
    def initialize(viewport = nil)
      super(viewport)
      @_whiten_duration = @_appear_duration = @_escape_duration = 0
      @_collapse_duration = @_damage_duration = @_animation_duration = 0
      @_blink = false
    end
    def whiten; @_whiten_duration = 16; end
    def appear; @_appear_duration = 16; end
    def escape; @_escape_duration = 32; end
    def collapse; @_collapse_duration = 48; end
    def damage(value, critical); @_damage_duration = 40; end
    def animation(animation, hit); @_animation = animation; @_animation_duration = animation ? animation.frame_max : 0; end
    def loop_animation(animation); @_loop_animation = animation; end
    def blink_on; @_blink = true; end
    def blink_off; @_blink = false; end
    def effect?
      @_whiten_duration > 0 || @_appear_duration > 0 || @_escape_duration > 0 ||
        @_collapse_duration > 0 || @_damage_duration > 0 || @_animation_duration > 0
    end
    def update
      super
      @_whiten_duration -= 1 if @_whiten_duration > 0
      @_appear_duration -= 1 if @_appear_duration > 0
      @_escape_duration -= 1 if @_escape_duration > 0
      @_collapse_duration -= 1 if @_collapse_duration > 0
      @_damage_duration -= 1 if @_damage_duration > 0
      @_animation_duration -= 1 if @_animation_duration > 0
    end
    def x=(v); super; end
    def y=(v); super; end
  end

  class Weather
    attr_accessor :type, :max, :ox, :oy
    def initialize(viewport = nil); @type = 0; @max = 0; @ox = 0; @oy = 0; end
    def update; nil; end
    def dispose; nil; end
  end
end

# ---- data files: the archive first, then plain files ----------------------------
module Kernel
  # lang/ comes from lang.dat (mkxp's extra archive); headless runs in English,
  # so its index can be empty.
  TTM_LANG_INDEX = { langs: [], trans_images: [] }

  def load_data(filename)
    name = filename.to_s
    data = TTM.archive_read(name) || TTM.read_file(name)
    if data.nil?
      return TTM_LANG_INDEX if name =~ %r{\Alang[/\\]index\.rxdata\z}
      raise Errno::ENOENT, name
    end
    Marshal.load(data)
  end

  def save_data(obj, filename); nil; end
end
