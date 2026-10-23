# rgss.rb -- the parts of RGSS1 that stay in Ruby in this engine.
#
# Bitmap, Viewport, Sprite, Window, Tilemap, Plane, Graphics, Color, Tone, Rect and Table are C
# (gems/mruby-ttm-gfx, gems/mruby-ttm-rgss), and so is Audio (gems/mruby-ttm-audio).
# Here: Font (a data holder the C text code reads), Input, where Audio's sounds
# come from, the RPG module, and load_data.

class Font
  class << self
    attr_accessor :default_name, :default_size, :default_bold, :default_italic,
                  :default_color, :default_shadow, :default_outline
  end
  def self.exist?(name); true; end

  # ---- faces (font.c) ---------------------------------------------------------
  # The game names four fonts: Arial and Times New Roman (the PC version of To
  # the Moon substitutes Open Sans for both, which is the engine's default face)
  # and, for Korean and Chinese, the two files below from the game's Fonts/
  # folder. One face is loaded the first time a Font asks for it -- drawing a
  # language's own name on the language screen needs that language's glyphs --
  # and a name with no file of its own draws in the default face.
  TTM_FACE_FILES = {
    "NanumBarunGothic"    => "Fonts/NanumBarunGothic.ttf",
    "WenQuanYi Zen Hei"   => "Fonts/WenQuanYi_Zen_Hei.ttf",
  }
  TTM_FACES_TRIED = {}

  def self.ttm_face_for(name)
    s = name.to_s
    file = TTM_FACE_FILES[s] || TTM_FACE_FILES[s.downcase]
    return false unless file
    return true if TTM_FACES_TRIED[s]
    TTM_FACES_TRIED[s] = true
    data = TTM.read_file(($ttm_game_dir || "") + file)
    unless data
      ttm_log("font: #{$ttm_game_dir}#{file} not found -- #{s} draws in the default face")
      return false
    end
    t0 = TTM.us
    Font.ttm_face(s, data)
    ttm_log(format("font: %s, %d KB, in %d ms", s, data.bytesize / 1024,
                   (TTM.us - t0) / 1000))
    true
  end

  def self.default_name=(name)
    @default_name = name
    ttm_face_for(name)
  end

  self.default_name = "Arial"
  self.default_size = 22
  self.default_bold = false
  self.default_italic = false
  self.default_color = Color.new(255, 255, 255, 255)

  attr_accessor :size, :bold, :italic, :color, :shadow, :outline
  def name; @name; end
  def name=(name)
    @name = name
    Font.ttm_face_for(name)
  end
  def initialize(name = Font.default_name, size = Font.default_size)
    @name = name; @size = size
    @bold = Font.default_bold; @italic = Font.default_italic
    @color = Color.new(255, 255, 255, 255)
    Font.ttm_face_for(name)
  end
end

# ---- Input ---------------------------------------------------------------------
# Key state comes from $ttm_keys_source, a proc returning the RGSS keys held this
# frame (an Array of key numbers): the 3DS app maps its buttons, the PC runner
# scripts presses. Input.update turns that into press / trigger / repeat, with
# RGSS1's timing (repeat after 15 frames, then every 4).
module Input
  DOWN = 2; LEFT = 4; RIGHT = 6; UP = 8
  A = 11; B = 12; C = 13; X = 14; Y = 15; Z = 16; L = 17; R = 18
  SHIFT = 21; CTRL = 22; ALT = 23
  F5 = 25; F6 = 26; F7 = 27; F8 = 28; F9 = 29
  # mkxp extensions the game's mouse scripts and pause key use
  MOUSELEFT = 38; MOUSEMIDDLE = 39; MOUSERIGHT = 40; K = 41
  # the engine's own: the 3DS's L button, the timeline. Not RGSS's L: the
  # game's common event 85 ("FAST GFX UPDT") sets Graphics.frame_rate = 160
  # while L is held and only puts 40 back on A, so L as RGSS L ran the game
  # at fast-forward after every look at the timeline (console run 10)
  TTM_TIMELINE = 99

  @held = {}
  @time = Hash.new(0)
  def self.update
    keys = $ttm_keys_source ? $ttm_keys_source.call : []
    now = {}
    keys.each { |k| now[k] = true }
    @time.keys.each { |k| @time.delete(k) unless now[k] }
    now.each_key { |k| @time[k] += 1 }
    @held = now
    # the stylus, after the keys (the 3DS reads both from one scan)
    @touch_was = @touch
    @touch = $ttm_touch_source ? $ttm_touch_source.call : nil
    nil
  end
  # The bottom screen's stylus ([x, y], 320x240), or nil: held now, and just
  # put down this frame (romfs/rgss/ttm_patches.rb, TTMBottomMenu).
  def self.ttm_touch; @touch; end
  def self.ttm_touch_trigger?; !@touch.nil? && @touch_was.nil?; end
  def self.press?(k); @held[k] ? true : false; end
  def self.trigger?(k); @time[k] == 1; end
  def self.repeat?(k)
    t = @time[k]
    t == 1 || (t > 15 && (t - 15) % 4 == 0)
  end
  def self.dir4
    return 2 if @held[DOWN]
    return 4 if @held[LEFT]
    return 6 if @held[RIGHT]
    return 8 if @held[UP]
    0
  end
  def self.dir8
    d, l, r, u = @held[DOWN], @held[LEFT], @held[RIGHT], @held[UP]
    return 1 if d && l
    return 3 if d && r
    return 7 if u && l
    return 9 if u && r
    dir4
  end
  # mkxp's pointer, which the game's mouse scripts read. The 3DS has none: it
  # is where mkxp puts a pointer outside the window, -20,-20, so nothing is
  # hovered (at 0,0 the "Show time line" common event, which opens the
  # timeline while the pointer is in the top 64 rows, was always on). Holding
  # the 3DS's L puts it at the top of the screen instead: the timeline on a
  # button.
  def self.mouse_x; @held[TTM_TIMELINE] ? 320 : -20; end
  def self.mouse_y; @held[TTM_TIMELINE] ? 16 : -20; end
end

# The bottom screen (graphics.c): every Sprite, Window and Viewport made inside
# the block is drawn there, not on the game's screen.
module Graphics
  def self.ttm_on_bottom
    was = ttm_screen
    self.ttm_screen = 1
    yield
  ensure
    self.ttm_screen = was
  end
end

# ---- Audio ---------------------------------------------------------------------
# Audio is C (gems/mruby-ttm-audio). Sounds come from the audio pack
# ($ttm_audio_pack, made by tools/pack_audio.py from the player's own Audio/),
# else from Audio/ in the game folder.
if $ttm_audio_pack
  n = Audio.ttm_open($ttm_audio_pack, $ttm_game_dir || "")
  ttm_log(n > 0 ? "audio: #{n} sounds in #{$ttm_audio_pack}" :
                  "audio: no pack at #{$ttm_audio_pack}; sounds only from Audio/ in the game folder")
else
  Audio.ttm_open(nil, $ttm_game_dir || "")
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

  # Bitmaps by folder, cached like RGSS's RPG::Cache.
  #
  # An asset the engine cannot produce no longer ends the game: the failure is
  # logged (once per asset) and RGSS's stand-in for an image that is not there,
  # a blank 32x32 bitmap, is cached in its place. On the 3DS the usual cause is
  # the PICA200's 1024 px texture limit -- tilesets, some vehicles -- which M6
  # addresses by slicing; a decode that runs out of RAM lands here too. Set
  # $ttm_strict_assets = true to get the error back while chasing an asset.
  module Cache
    @cache = {}
    def self.load_bitmap(folder, filename, hue = 0)
      key = [folder, filename, hue]
      b = @cache[key]
      return b if b && !b.disposed?
      return @cache[key] = Bitmap.new(32, 32) if filename.to_s.empty?
      begin
        @cache[key] = TTMControls.repaint(folder + filename.to_s, Bitmap.new(folder + filename.to_s))
      rescue RGSSError, Errno::ENOENT => e
        # raise e, not a bare raise: mruby's bare raise inside a rescue does not
        # re-raise the caught exception the way CRuby's does.
        raise e if $ttm_strict_assets
        ttm_log("cache: #{folder}#{filename} could not be built (#{e.message}); using a blank bitmap")
        @cache[key] = Bitmap.new(32, 32)
      end
    end
    { animation: "Animations", autotile: "Autotiles", battleback: "Battlebacks",
      battler: "Battlers", character: "Characters", fog: "Fogs", gameover: "Gameovers",
      icon: "Icons", panorama: "Panoramas", picture: "Pictures", tileset: "Tilesets",
      title: "Titles", windowskin: "Windowskins" }.each do |m, dir|
      define_singleton_method(m) { |f, hue = 0| load_bitmap("Graphics/#{dir}/", f, hue) }
    end
    # An event whose graphic is a tile (books, vases, the furniture events
    # move around): its 32x32 cell of the tileset, as RGSS builds it. 488
    # event pages on 69 maps use one; until run 11 this was a blank bitmap.
    # Hue is ignored (the game never changes one).
    def self.tile(filename, tile_id, hue)
      key = [filename, tile_id, hue]
      b = @cache[key]
      return b if b && !b.disposed?
      b = Bitmap.new(32, 32)
      id = tile_id - 384
      b.blt(0, 0, tileset(filename), Rect.new(id % 8 * 32, id / 8 * 32, 32, 32))
      @cache[key] = b
    end
    def self.clear; @cache = {}; GC.start; end
  end

  # RPG::Sprite: Sprite plus RGSS's effects -- above all the animations of
  # event command 207, Show Animation (841 uses: the emotion balloons over
  # characters, flashes, "Tile Flip", "MEMENTO DETECTED"). As RGSS1's own:
  # 16 cell sprites showing 192x192 cells of the animation's sheet, a frame
  # every 2 game frames, each timing's SE and flash on its frame; position 3
  # is the screen, the others the sprite (top, middle, bottom).
  #
  # Three differences from RGSS, for the 3DS:
  #  - @_busy: every character sprite is updated every frame and almost none
  #    has an effect running, so update returns at once unless one does
  #    (console run 5);
  #  - the cell sprites follow the sprite through TTMAnimFollow, which only a
  #    sprite that has shown an animation carries (RGSS overrides x= and y= on
  #    every sprite);
  #  - a sheet stays in RPG::Cache when its last animation ends (RGSS disposes
  #    it): the next balloon needs no decode, and bitmap.c sheds its pixels if
  #    RAM runs short.
  module TTMAnimFollow
    def x=(x)
      sx = x - self.x
      if sx != 0
        @_animation_sprites.each { |s| s.x += sx } if @_animation_sprites
        @_loop_animation_sprites.each { |s| s.x += sx } if @_loop_animation_sprites
      end
      super
    end
    def y=(y)
      sy = y - self.y
      if sy != 0
        @_animation_sprites.each { |s| s.y += sy } if @_animation_sprites
        @_loop_animation_sprites.each { |s| s.y += sy } if @_loop_animation_sprites
      end
      super
    end
  end

  class Sprite < ::Sprite
    @@_animations = []
    def initialize(viewport = nil)
      super(viewport)
      @_whiten_duration = @_appear_duration = @_escape_duration = 0
      @_collapse_duration = @_damage_duration = @_animation_duration = 0
      @_blink = false
    end
    def dispose
      dispose_animation
      dispose_loop_animation
      super
    end
    def whiten
      self.blend_type = 0
      self.color.set(255, 255, 255, 128)
      self.opacity = 255
      @_whiten_duration = 16
      @_busy = true
    end
    def appear
      self.blend_type = 0
      self.color.set(0, 0, 0, 0)
      self.opacity = 0
      @_appear_duration = 16
      @_busy = true
    end
    def escape
      self.blend_type = 0
      self.color.set(0, 0, 0, 0)
      self.opacity = 255
      @_escape_duration = 32
      @_busy = true
    end
    def collapse
      self.blend_type = 1
      self.color.set(255, 64, 64, 255)
      self.opacity = 255
      @_collapse_duration = 48
      @_busy = true
    end
    # battle damage numbers: the game has no battles; kept as a duration only
    def damage(value, critical); @_damage_duration = 40; @_busy = true; end
    def dispose_damage; end

    def make_cell_sprites(bitmap)
      extend(TTMAnimFollow) unless is_a?(TTMAnimFollow)
      (0..15).collect do
        s = ::Sprite.new(self.viewport)
        s.bitmap = bitmap
        s.visible = false
        s
      end
    end

    def animation(animation, hit)
      dispose_animation
      @_animation = animation
      return if @_animation.nil?
      @_animation_hit = hit
      @_animation_duration = @_animation.frame_max
      bitmap = RPG::Cache.animation(@_animation.animation_name, @_animation.animation_hue)
      @_animation_sprites = []
      if @_animation.position != 3 || !@@_animations.include?(animation)
        @_animation_sprites = make_cell_sprites(bitmap)
        @@_animations.push(animation) unless @@_animations.include?(animation)
      end
      @_busy = true
      update_animation
    end

    def loop_animation(animation)
      return if animation == @_loop_animation
      dispose_loop_animation
      @_loop_animation = animation
      return if @_loop_animation.nil?
      @_loop_animation_index = 0
      bitmap = RPG::Cache.animation(@_loop_animation.animation_name, @_loop_animation.animation_hue)
      @_loop_animation_sprites = make_cell_sprites(bitmap)
      @_busy = true
      update_loop_animation
    end

    def dispose_animation
      return if @_animation_sprites.nil?
      @_animation_sprites.each(&:dispose)
      @_animation_sprites = nil
      @_animation = nil
    end

    def dispose_loop_animation
      return if @_loop_animation_sprites.nil?
      @_loop_animation_sprites.each(&:dispose)
      @_loop_animation_sprites = nil
      @_loop_animation = nil
    end

    def blink_on
      return if @_blink
      @_blink = true
      @_blink_count = 0
      @_busy = true
    end
    def blink_off
      return unless @_blink
      @_blink = false
      self.color.set(0, 0, 0, 0)
    end
    def blink?; @_blink; end

    def effect?
      @_whiten_duration > 0 || @_appear_duration > 0 || @_escape_duration > 0 ||
        @_collapse_duration > 0 || @_damage_duration > 0 || @_animation_duration > 0
    end

    def update
      super
      return unless @_busy
      if @_whiten_duration > 0
        @_whiten_duration -= 1
        self.color.alpha = 128 - (16 - @_whiten_duration) * 10
      end
      if @_appear_duration > 0
        @_appear_duration -= 1
        self.opacity = (16 - @_appear_duration) * 16
      end
      if @_escape_duration > 0
        @_escape_duration -= 1
        self.opacity = 256 - (32 - @_escape_duration) * 10
      end
      if @_collapse_duration > 0
        @_collapse_duration -= 1
        self.opacity = 256 - (48 - @_collapse_duration) * 6
      end
      @_damage_duration -= 1 if @_damage_duration > 0
      if @_animation && Graphics.frame_count % 2 == 0
        @_animation_duration -= 1
        update_animation
      end
      if @_loop_animation && Graphics.frame_count % 2 == 0
        update_loop_animation
        @_loop_animation_index = (@_loop_animation_index + 1) % @_loop_animation.frame_max
      end
      if @_blink
        @_blink_count = (@_blink_count + 1) % 32
        alpha = @_blink_count < 16 ? (16 - @_blink_count) * 6 : (@_blink_count - 16) * 6
        self.color.set(255, 255, 255, alpha)
      end
      @@_animations.clear
      @_busy = effect? || @_animation || @_loop_animation || @_blink
    end

    def update_animation
      if @_animation_duration > 0
        frame_index = @_animation.frame_max - @_animation_duration
        animation_set_sprites(@_animation_sprites, @_animation.frames[frame_index].cell_data, @_animation.position)
        @_animation.timings.each do |timing|
          animation_process_timing(timing, @_animation_hit) if timing.frame == frame_index
        end
      else
        dispose_animation
      end
    end

    def update_loop_animation
      frame_index = @_loop_animation_index
      animation_set_sprites(@_loop_animation_sprites, @_loop_animation.frames[frame_index].cell_data,
                            @_loop_animation.position)
      @_loop_animation.timings.each do |timing|
        animation_process_timing(timing, true) if timing.frame == frame_index
      end
    end

    def animation_set_sprites(sprites, cell_data, position)
      for i in 0..15
        sprite = sprites[i]
        pattern = cell_data[i, 0]
        if sprite.nil? || pattern.nil? || pattern == -1
          sprite.visible = false if sprite
          next
        end
        sprite.visible = true
        sprite.src_rect.set(pattern % 5 * 192, pattern / 5 * 192, 192, 192)
        if position == 3
          if self.viewport
            sprite.x = self.viewport.rect.width / 2
            sprite.y = self.viewport.rect.height - 160
          else
            sprite.x = 320
            sprite.y = 240
          end
        else
          sprite.x = self.x - self.ox + self.src_rect.width / 2
          sprite.y = self.y - self.oy + self.src_rect.height / 2
          sprite.y -= self.src_rect.height / 4 if position == 0
          sprite.y += self.src_rect.height / 4 if position == 2
        end
        sprite.x += cell_data[i, 1]
        sprite.y += cell_data[i, 2]
        sprite.z = 2000
        sprite.ox = 96
        sprite.oy = 96
        sprite.zoom_x = cell_data[i, 3] / 100.0
        sprite.zoom_y = cell_data[i, 3] / 100.0
        sprite.angle = cell_data[i, 4]
        sprite.mirror = (cell_data[i, 5] == 1)
        sprite.opacity = cell_data[i, 6] * self.opacity / 255.0
        sprite.blend_type = cell_data[i, 7]
      end
    end

    def animation_process_timing(timing, hit)
      return unless timing.condition == 0 || (timing.condition == 1 && hit == true) ||
                    (timing.condition == 2 && hit == false)
      se = timing.se
      Audio.se_play("Audio/SE/" + se.name, se.volume, se.pitch) if se && se.name != ""
      case timing.flash_scope
      when 1 then self.flash(timing.flash_color, timing.flash_duration * 2)
      when 2 then self.viewport.flash(timing.flash_color, timing.flash_duration * 2) if self.viewport
      when 3 then self.flash(nil, timing.flash_duration * 2)
      end
    end
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
  # A language pack (lang.dat) that will not open is a ZIP problem, not a game
  # one: the loader must not be the thing that ends the run.
  TTM_LANG_INDEX = { langs: [], trans_images: [] }

  def load_data(filename)
    name = filename.to_s
    # lang/... lives in the pack (langzip.c); everything else in the archive,
    # then as a plain file
    data = TTM.lang_read(name) || TTM.archive_read(name) || TTM.read_file(name)
    if data.nil?
      return TTM_LANG_INDEX if name =~ %r{\Alang[/\\]index\.rxdata\z}
      raise Errno::ENOENT, name
    end
    obj = Marshal.load(data)
    obj = TTMControls.map_text(obj, name) if name =~ %r{Data[/\\]Map\d+\.rxdata\z}
    obj
  end

  def save_data(obj, filename); nil; end
end

# ---- images: what Bitmap.new(name) reads (the C side decodes) --------------------
module TTM
  IMAGE_EXTS = ["", ".png", ".jpg", ".bmp"]
  def self.image_data(name)
    # a localised picture (118_TTMTranslator asks for "lang/<code>/...") comes
    # out of the pack; everything else from the archive, then as a plain file
    if name =~ %r{\Alang[/\\]}
      IMAGE_EXTS.each do |ext|
        data = TTM.lang_read(name + ext)
        return data if data
      end
    end
    IMAGE_EXTS.each do |ext|
      data = TTM.archive_read(name + ext)
      return data if data
    end
    IMAGE_EXTS.each do |ext|
      data = TTM.read_file(($ttm_game_dir || "") + name + ext)
      return data if data
    end
    nil
  end
end
