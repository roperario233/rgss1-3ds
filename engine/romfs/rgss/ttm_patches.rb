# ttm_patches.rb -- To the Moon-specific speed patches.
#
# Applied to the player's own scripts as they load: each patch finds one short,
# exact piece of a named script and swaps in a call to a helper below (the way
# mkxp-z ports patch scripts at load). The engine carries no copy of the
# game's code. A patch whose text is not found is skipped and logged, so a
# different release of the game is left alone.
#
# Every patch must leave the game's behaviour unchanged. The harness checks
# that: a deterministic run's state digests are identical with and without
# the patches ($ttm_no_patches = true), on the PC and on the 3DS.

module TTMPatch
  PATCHES = [
    # mruby has no `defined?`: `defined? x` is parsed as a call to a method named
    # `defined?` with x as its argument, so the ending's guard failed twice over
    # -- first on _steam_achievement_unlock, which Steamworks supplies on the PC
    # and nothing supplied here (console run 16: "undefined method
    # '_steam_achievement_unlock'"), and then on `defined?` itself once that name
    # existed. This is the game's only `defined?`; the body is unchanged but for
    # the guard, and compat.rb's shim makes the call a logged no-op. Map 167's
    # Script Call carries the same guard and is rewritten as it loads
    # (compat.rb, TTMControls.ending_scripts).
    { script: "MAIN",
      find: "def _ttm_unlock_achievement\n  if defined? _steam_achievement_unlock\n" \
            "    _steam_achievement_unlock(\"ACH_DEBUG_MOON\")\n  end\nend",
      replace: "def _ttm_unlock_achievement\n" \
               "  _steam_achievement_unlock(\"ACH_DEBUG_MOON\")\nend" },
    # Caterpillar re-scans every event's name with a regex on each refresh
    # (switch 1 changes, party refresh, map setup). Names do not change, so the
    # scan result is cached per name; the rest of check_caterpillar runs as before.
    { script: "Caterpillar",
      find: '@event.name.gsub(/\\\\cat_actor\\[([0-9]+)\\]/i) {@caterpillar_actor = $1 }',
      replace: 'ttm_cat = TTMPatch.cat_actor_scan(@event.name); @caterpillar_actor = ttm_cat.dup if ttm_cat' },
    # Both Sigmund minisodes (run 16) read the party's leader whenever the player
    # is centred, and their data starts the game with no party at all
    # ($data_system.party_members is []), so the boot-time moveto raised
    # "undefined method 'id' for nil" before a single frame was drawn. The main
    # game's own later build guards exactly this lookup -- its line is commented
    # out and a guarded one follows -- so this is the author's fix, not ours, and
    # it can only change the one state that used to abort. The main game's copy
    # is prefixed with '#', so the leading newline in `find` leaves it alone.
    # The minisodes' older mouse script reads the cursor through user32
    # (Cursor_Pos, Scr2cli) and takes a nil on every failed call, so
    # Mouse.pos raised "nil cannot be converted to Integer" on the first frame.
    # The main game's own later build replaced exactly these two lines with
    # Input.mouse_x / Input.mouse_y (compat.rb reports the pointer outside the
    # window, as mkxp does, and the D-pad walks), which is what they become
    # here. The main game's copies are commented out, so it is left alone.
    { script: "Mouse 1",
      find: "gx, gy = Cursor_Pos.call(global_pos) == 0 ? nil : global_pos.unpack('ll')",
      replace: "gx, gy = Input.mouse_x, Input.mouse_y" },
    { script: "Mouse 1",
      find: "x, y = Scr2cli.call(self.hwnd, local_pos) == 0 ? nil : local_pos.unpack('ll')",
      replace: "x, y = Input.mouse_x, Input.mouse_y" },

    { script: "Caterpillar",
      find: "\n      event = @actor_id_to_event[$game_party.actors[0].id]\n",
      replace: "\n      leader = $game_party.actors[0]\n      event = @actor_id_to_event[leader.id] if leader\n" },

    # The last definition of command_101 in all three games is in "Mouse 3", so
    # the log line for each message goes after it (a patch on an earlier script
    # would be replaced by the later one). Up to 20 lines a run: it is how a
    # console run says whether the game is producing dialogue at all, and it
    # changes nothing. The class is Interpreter, RMXP's name (not RMVX's
    # Game_Interpreter).
    { script: "Mouse 3",
      append: <<~'RUBY' },
        class Interpreter
          alias_method :ttm_command_101, :command_101
          def command_101
            $ttm_messages = ($ttm_messages || 0) + 1
            if $ttm_messages <= 20
              ttm_log("msg: " + @list[@index].parameters[0].to_s[0, 60])
            end
            ttm_command_101
          end
        end
      RUBY

    # "Rach checker": events 3-6 loop over every event on the map, every frame,
    # comparing names against six constants; events with other names fall
    # through every branch. Loop over just the matching events (same order),
    # found once per map.
    { script: "Rach checker",
      find: "      # All event loops\n      for event in $game_map.events.values\n",
      replace: "      # All event loops\n      for event in TTMPatch.events_named(TRIGGER_NAME, TRIGGER_NAME2, " \
               "GOUP_NAME, GODOWN_NAME, GORIGHT_NAME, GOLEFT_NAME)\n" },
    # ccoa's weather makes 500 sprites on every map load, weather or not, and
    # moves all 500 each time the map scrolls (ox=/oy=); the game asks for at
    # most 52 (falling ash, power 12). Inside `module RPG` its Sprite.new is
    # RPG::Sprite, so each also runs Ruby on creation and dispose: ~10 % of a
    # map change (pc/mapload_profile.rb). The sprites are now plain ::Sprite,
    # made as max= asks for them. The random numbers each sprite draws are
    # still drawn here, all 1,000 in the same order, so everything else the
    # game does at random stays the same.
    { script: "Weather Script",
      find: "    for i in 1..500\n      sprite = Sprite.new(viewport)\n      sprite.z = 1000\n" \
            "      sprite.visible = false\n      sprite.opacity = 0\n      @sprites.push(sprite)\n" \
            "      @current_pose.push(0)\n",
      replace: "    @ttm_lazy = true\n    @ttm_viewport = viewport\n    for i in 1..500\n      @current_pose.push(0)\n" },
    { script: "Weather Script",
      append: <<~'RUBY' },
        module RPG
          class Weather
            alias_method :ttm_max_set, :max=
            def max=(max)
              n = [[max, 0].max, 500].min
              ttm_grow(n) if @ttm_lazy && n >= @sprites.size
              ttm_max_set(max)
            end
            # sprites 0..n, as the original made them, with the current
            # weather's bitmap (type= hands them out) and scroll
            def ttm_grow(n)
              while @sprites.size <= n
                sprite = ::Sprite.new(@ttm_viewport)
                sprite.z = 1000
                sprite.visible = false
                sprite.opacity = 0
                sprite.ox = @ox
                sprite.oy = @oy
                @sprites.push(sprite)
              end
              t = @type
              @type = -1
              self.type = t
            end
          end
        end
      RUBY
    # UMS lays its message window out again on every frame an event's text
    # is up (Window_Message#update -> reset_window, kept there for event
    # following), and that redraws the name and choice windows each time: a
    # new Bitmap each, the choices one character at a time through regexps.
    # On the 3DS a choice ran at 4 fps (console run 12: rb 238 ms) and the
    # Bitmaps left behind filled texture memory. Each is now redrawn only
    # when what it would draw changes. (The first set_choices draws the raw
    # strings and then rewrites them in place, so the second draws again;
    # from the third on, nothing changes.)
    { script: "UMS new",
      append: <<~'RUBY' },
        class Window_Choice
          alias_method :ttm_set_choices, :set_choices
          def set_choices(choices)
            s = $game_system
            key = [choices.map { |c| c.dup }, s.font, s.font_color, s.windowskin, s.shadowed_text, s.shadow_color]
            return if key == @ttm_key && contents && !contents.disposed?
            @ttm_key = key
            ttm_set_choices(choices)
          end
        end
        class Window_Name
          alias_method :ttm_set_name, :set_name
          def set_name(name)
            key = [name.dup, $game_system.font, $game_system.name_window]
            if key == @ttm_key && contents && !contents.disposed?
              if $game_system.name_window       # as refresh places it
                @dummy_window.x = self.x
                @dummy_window.y = self.y + 12
              end
              return
            end
            @ttm_key = key
            ttm_set_name(name)
          end
        end
      RUBY
    # The game's menu on the bottom screen (TTMBottomMenu, below), and the
    # map-load time in log.txt. After the game's last script, so that no
    # later one replaces them.
    { script: "Achievements",
      append: <<~'RUBY' },
        class Scene_Map
          alias_method :ttm_bottom_update, :update
          def update
            ttm_bottom_update
            return unless $scene.equal?(self)
            $ttm_bottom_return = false
            menu_ok = !$game_system.map_interpreter.running? && !$game_system.menu_disabled
            TTMBottomMenu.update(menu_ok, menu_ok && !$game_system.save_disabled)
          end
          alias_method :ttm_timed_transfer_player, :transfer_player
          def transfer_player
            from = $game_map.map_id
            t = TTM.us
            ttm_timed_transfer_player
            ms = ((TTM.us - t) & 0x3fffffff) / 1000
            ttm_log("map load: #{from} -> #{$game_map.map_id} in #{ms} ms") if $game_map.map_id != from
          end
        end
        # Load and Exit Game opened from the bottom screen go back to the map
        # when cancelled, not to the top screen's menu ("GUBID-TTM Menu"
        # sends them there because that is where they were opened from)
        class Scene_Load
          alias_method :ttm_bottom_on_cancel, :on_cancel
          def on_cancel
            ttm_bottom_on_cancel
            TTMBottomMenu.back_to_map
          end
        end
        class Scene_End
          alias_method :ttm_bottom_command_cancel, :command_cancel
          def command_cancel
            ttm_bottom_command_cancel
            TTMBottomMenu.back_to_map
          end
          alias_method :ttm_bottom_end_update, :update
          def update
            ttm_bottom_end_update
            TTMBottomMenu.back_to_map if $scene.is_a?(Scene_Menu)
          end
        end
        # the title screen has no party: no menu below it
        class Scene_Title
          alias_method :ttm_bottom_title_main, :main
          def main
            TTMBottomMenu.dispose
            ttm_bottom_title_main
          end
        end
      RUBY
    # The 3DS has no mouse: the game's cursor sprite (Sprite_Mouse, made at
    # the end of "Mouse 3") stays hidden. The mouse scripts show it whenever
    # the pointer moves, and holding L moves it (Input.mouse_y). With it hidden
    # the windows skip their mouse hover code, as on a PC with the mouse unused.
    # (This one is a platform change, not a speed patch.)
    { script: "Mouse 3",
      append: "class Sprite_Mouse\n  def visible=(v); super(false); end\nend\n$mouse_sprite.visible = false\n" },
  ]

  @applied = []
  @skipped = []
  class << self; attr_reader :applied, :skipped; end

  # script name is "NNN_Title"; patches name the title
  def self.apply(name, code)
    PATCHES.each do |pt|
      next unless name.include?(pt[:script])
      next if $ttm_skip_patches && $ttm_skip_patches.include?(pt[:script])    # pc/choice_check.rb
      if pt[:append]                       # code run after the script's own
        code = code + "\n" + pt[:append]
        @applied << pt[:script]
        next
      end
      find = pt[:find]
      repl = pt[:replace]
      if code.include?("\r\n")
        find = find.gsub("\n", "\r\n")
        repl = repl.gsub("\n", "\r\n")
      end
      if code.include?(find)
        code = code.sub(find) { repl }
        @applied << pt[:script]
      else
        @skipped << pt[:script]
      end
    end
    code
  end

  # ---- loading the game's scripts ----------------------------------------------
  # Ruby 1.8 source the minisodes still carry. RMXP ran Ruby 1.8, whose parser
  # took two forms mruby refuses: a hash literal with the arrows left out
  # ({'a', 'b', 'c', 'd'} meant {'a'=>'b', 'c'=>'d'}), and a space between a
  # method name and its argument list (x.foo (a, b)). The main game's scripts
  # have neither; both minisodes do. A script that fails to compile is rewritten
  # and compiled again -- nothing has run at that point, so a fixup can only turn
  # "will not load" into "loads as it did on the PC".
  def self.legacy(code)
    out = code.gsub(/\.([a-zA-Z_]\w*[?!]?)[ \t]+\(/, '.\\1(')
    # a bare call with a space (blt (x, y, ...)), but never a keyword:
    # `if (x)` and friends are the same in both rubies and must stay put
    out = out.gsub(/(?<![.\w])(?!(?:if|unless|while|until|case|when|return|and|or|not|then|do|elsif|else|begin|end|for|in|def|class|module|super|yield|raise|break|next|redo|retry|rescue|ensure)\b)([a-z_]\w*[?!]?)[ \t]+\(/) { "#{$1}(" }
    lines = out.split("\n", -1)
    i = 0
    while i < lines.size
      line = lines[i]
      if line =~ /=\s*\{\s*['"]/ && !line.include?("=>")
        j = i + 1
        j += 1 while j < lines.size && lines[j].strip != "}" && lines[j].strip != "},"
        if j < lines.size
          lines[i] = line.sub(/\{(\s*['"])/, 'Hash[\1')
          lines[j] = lines[j].sub("}", "]")
        end
      end
      i += 1
    end
    lines.join("\n")
  end

  # compile and run one script, through its bytecode cache when there is one;
  # a script the parser refuses gets one retry with the 1.8 forms rewritten (and
  # is then not cached, since the cache names carry the source's CRC)
  def self.load_script(name, code, cache)
    begin
      cache ? TTM.load_script_cached(code, name, cache) : TTM.load_script(code, name)
    rescue SyntaxError => e
      fixed = legacy(code)
      raise e if fixed == code
      ttm_log("legacy syntax: #{name} (Ruby 1.8 forms rewritten, not cached)")
      TTM.load_script(fixed, name)
    end
  end

  # ---- helpers used by the patched scripts ------------------------------------
  @cat = {}
  def self.cat_actor_scan(name)
    return @cat[name] if @cat.key?(name)
    found = nil
    name.gsub(/\\cat_actor\[([0-9]+)\]/i) { found = $1 }
    @cat[name.dup] = found
  end

  # events whose name is one of `names`, in $game_map.events order; rebuilt
  # whenever the map or its event set changes
  def self.events_named(*names)
    evs = $game_map.events
    c = @named
    unless c && c[0].equal?(evs) && c[1] == evs.size && c[2] == $game_map.map_id && c[3] == names
      c = @named = [evs, evs.size, $game_map.map_id, names,
                    evs.values.select { |e| names.include?(e.name) }]
    end
    c[4]
  end
end

# ---- the game's menu on the bottom screen ---------------------------------------
# While the map plays on the top screen, the bottom one shows the game's own
# menu ("GUBID-TTM Menu": the Characters / Notes / Items tabs and lists and the
# Save / Load / Exit Game row, on its Sigmund Corp. background), made from the
# game's own window classes inside Graphics.ttm_on_bottom and shown at exactly
# half size (VIEW: the whole 640x480 menu). Its heart-monitor strip at the top
# is the game's own Window_MenuHeartMonitor: the pictures heart<v72>,
# heartoverlay<v72> and heartoveralygraphic<v72>, with the overlay scrolling by
# variable 73. Its windows are never active, so the buttons keep driving the
# map; the stylus works the menu:
#  - a tab shows its list; a row of Notes or Items shows its description;
#    dragging a list scrolls it
#  - Save opens the game's save screen as its event command does
#    ($game_temp.save_calling: Scene_Map#call_save waits for the player to
#    stand still); Load and Exit Game open theirs, and cancelling there comes
#    back to the map (back_to_map, and the Scene_Load / Scene_End patches)
#  - Save, Load and Exit Game are veiled, and buzz, whenever the game's own
#    menu could not open (an event running, the menu disabled; Save also
#    when saving is disabled)
# Scene_Map calls TTMBottomMenu.update each frame. Off the map nothing does,
# and graphics.c veils the whole screen. A list is made again when what it
# shows changes (the notes are items); everything, for a new game, a loaded
# one or another language.
class TTMBottomMenu
  VIEW = [0, 0, 640, 480]         # the game's whole menu, at half size (0.5, 0, 0)

  def self.update(menu_ok, save_ok)
    if @menu && !@menu.same_game?
      @menu.dispose
      @menu = nil
    end
    @menu ||= new
    @menu.update(menu_ok, save_ok)
    Graphics.ttm_bottom_dim = 0
  end

  def self.dispose
    @menu.dispose if @menu
    @menu = nil
    $ttm_bottom_return = false
  end

  # a screen opened from here, cancelled: back to the map, not the top menu
  def self.back_to_map
    return unless $ttm_bottom_return
    $ttm_bottom_return = false
    $scene = Scene_Map.new if $scene.is_a?(Scene_Menu)
  end

  def self.menu; @menu; end    # pc/bottom_menu_check.rb

  attr_reader :tab, :status, :notes, :items, :tabs, :options, :veils, :monitor, :heart

  def initialize
    @scale, @dx, @dy = Graphics.ttm_bottom_view(*VIEW)
    @party = $game_party
    @lang = txt(:menu_commands1)
    Graphics.ttm_on_bottom do
      @back = Window_BackImg.new
      @tabs = Window_TabDisplay.new(100, 95, 105 * 3, 35, txt(:menu_commands1), 1)
      @options = Window_TabDisplay.new(115, 390, 345, 36, txt(:menu_commands2), 1, 33)
      # over each of Save, Load, Exit Game while it cannot be used
      @veils = @options.instance_variable_get(:@tab_wins).map do |w|
        v = Sprite.new
        v.bitmap = Bitmap.new(w.width, w.height)
        v.bitmap.fill_rect(v.bitmap.rect, Color.new(0, 0, 0, 150))
        v.x = w.x
        v.y = w.y
        v.z = 9000
        v.visible = false
        v
      end
    end
    @tabs.active = @options.active = false
    @options.index = -1
    make_status
    make_lists
    make_monitor
    show(0)
  end

  def make_status
    @actors = @party.actors.map { |a| a.id }
    Graphics.ttm_on_bottom { @status = Window_MenuStatus.new }
    @status.active = false
    @status.index = -1
  end

  def make_lists
    @item_counts = @party.instance_variable_get(:@items).dup
    Graphics.ttm_on_bottom do
      @notes = Window_Notes.new
      @items = Window_MenuItem.new
    end
    [@notes, @items].each do |w|
      w.active = false
      w.index = -1
    end
  end

  # The game's heart-monitor strip: its own Window_MenuHeartMonitor, 640x72 at
  # the top of its menu. The story changes game variable 72 (the era), which
  # picks the heart<v72>, heartoverlay<v72> and heartoveralygraphic<v72>
  # pictures, so a change disposes the strip and makes it again. Calling the
  # game's refresh instead would leave its old sprites registered (it makes new
  # ones without freeing them). If the class or a picture cannot be made, log
  # it once and let the menu go on without the strip.
  def make_monitor
    @heart = $game_variables[72]
    Graphics.ttm_on_bottom { @monitor = Window_MenuHeartMonitor.new }
  rescue => e
    @monitor = nil
    ttm_log("bottom menu: no heart monitor (#{e.message})")
  end

  def same_game?
    @party.equal?($game_party) && txt(:menu_commands1) == @lang
  end

  def party_changed?
    acts = @party.actors
    return true if acts.size != @actors.size
    acts.size.times { |i| return true if acts[i].id != @actors[i] }
    false
  end

  def update(menu_ok, save_ok)
    if @monitor && $game_variables[72] != @heart
      @monitor.dispose
      make_monitor
    end
    if party_changed?
      @status.dispose
      make_status
      show(@tab)
    end
    if @party.instance_variable_get(:@items) != @item_counts
      @notes.dispose
      @items.dispose
      make_lists
      show(@tab)
    end
    @back.update
    @monitor.update if @monitor
    @tabs.update
    @options.update
    @status.update
    @notes.update
    @items.update
    @veils[0].visible = !save_ok
    @veils[1].visible = @veils[2].visible = !menu_ok
    stylus(menu_ok, save_ok)
  end

  def dispose
    @monitor.dispose if @monitor
    @monitor = nil
    [@back, @tabs, @options, @status, @notes, @items].each { |w| w.dispose }
    @veils.each do |v|
      v.bitmap.dispose
      v.dispose
    end
  end

  # tab i and its list, as Scene_Menu#update_input shows them
  def show(i)
    @tab = i
    @tabs.index = i
    @status.visible = i == 0
    @notes.visible = i == 1
    @items.visible = i == 2
  end

  def list
    @tab == 1 ? @notes : @tab == 2 ? @items : @status
  end

  private

  def stylus(menu_ok, save_ok)
    t = Input.ttm_touch
    unless t
      @drag = nil
      return
    end
    pos = [(t[0] - @dx) / @scale, (t[1] - @dy) / @scale]
    if Input.ttm_touch_trigger?
      on_tap(pos, menu_ok, save_ok)
    elsif @drag
      drag(pos)
    end
  end

  # which of a Window_TabDisplay's tabs is under pos (nil: none)
  def tab_at(display, pos)
    display.instance_variable_get(:@tab_wins).index { |w| w.rect.over?(pos) }
  end

  def on_tap(pos, menu_ok, save_ok)
    @drag = nil
    if (i = tab_at(@tabs, pos))
      return if i == @tab
      $game_system.se_play($data_system.decision_se)
      show(i)
      w = list
      w.index = 0 if w != @status && w.instance_variable_get(:@item_max) > 0
    elsif (i = tab_at(@options, pos))
      unless i == 0 ? save_ok : menu_ok
        $game_system.se_play($data_system.buzzer_se)
        return
      end
      $game_system.se_play($data_system.decision_se)
      case i
      when 0 then $game_temp.save_calling = true
      when 1 then open_scene(Scene_Load.new(true))
      when 2 then open_scene(Scene_End.new)
      end
    else
      w = list
      return unless w.rect.over?(pos)
      rows = w == @status ? 64 : 32
      # Notes and Items: the list is the left half, the description the right
      if w == @status || pos[0] < w.x + w.width / 2
        row = ((pos[1] - w.y - 16 + w.oy) / rows).to_i
        if row >= 0 && row < w.instance_variable_get(:@item_max) && row != w.index
          $game_system.se_play($data_system.cursor_se)
          w.index = row
        end
      end
      @drag = [pos[1], w.top_row, w] if w != @status
    end
  end

  def open_scene(scene)
    $ttm_bottom_return = true
    $scene = scene
  end

  # a list follows the stylus a row at a time; the chosen row stays in view
  def drag(pos)
    y0, top0, w = @drag
    n = w.instance_variable_get(:@item_max)
    page = w.page_row_max
    top = top0 + ((y0 - pos[1]) / 32).to_i
    top = n - page if top > n - page
    top = 0 if top < 0
    return if top == w.top_row
    w.top_row = top
    if w.index >= 0
      w.index = top if w.index < top
      w.index = top + page - 1 if w.index > top + page - 1
    end
  end
end
