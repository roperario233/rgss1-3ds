# bottom_menu_check.rb -- the game's menu on the bottom screen, in the running
# game (romfs/rgss/ttm_patches.rb, TTMBottomMenu; graphics.c's bottom pass).
# Replaces run 12's touch_check.rb (the four touch buttons are gone).
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\bottom_menu_check.rb
#
# Runs the game as pc/run_pc.rb does, with the stylus scripted ($ttm_touch, in
# bottom-screen pixels, worked out from the menu's own windows):
#  - in a cutscene: the menu is there, Save / Load / Exit Game are veiled,
#    and tapping Save opens nothing
#  - free on the map: the menu is the game's whole 640x480 screen at half size,
#    with its heart-monitor strip at the top (heart0/overlay0/graphic0);
#    variable 72 = 2 remakes the strip with the heart2 pictures, and putting it
#    back brings heart0 back
#  - tapping Notes shows the notes; tapping Save opens the game's save screen,
#    where the bottom screen is veiled; cancelling comes back to the map; Load
#    and Exit Game likewise, back to the map (not the top screen's menu)
# and saves the bottom screen to pc/shots/bottom_*.png.

$ttm_max_frames = 9000
$ttm_shots = []
$ttm_touch = nil
$ttm_touch_source = proc { $ttm_touch }
$bc = { step: :cutscene, log: [] }

# game coordinates -> bottom-screen pixels
def bc_screen(gx, gy)
  s, dx, dy = Graphics.ttm_bottom_view(*TTMBottomMenu::VIEW)
  [(gx * s + dx).to_i, (gy * s + dy).to_i]
end

def bc_centre(w)
  bc_screen(w.x + w.width / 2, w.y + w.height / 2)
end

def bc_tab(display, i)
  bc_centre(display.instance_variable_get(:@tab_wins)[i])
end

# start a tap now: the stylus down for two frames
def bc_tap(pt)
  $bc[:tap] = [pt, 2]
end

def bc_shot(name)
  Graphics.ttm_screenshot_bottom("pc/shots/bottom_#{name}.png")
end

$ttm_extra_frame_hook = proc do
  next unless Object.const_defined?(:TTMBottomMenu)
  s = $bc
  # the stylus
  if s[:tap]
    $ttm_touch = s[:tap][0]
    s[:tap][1] -= 1
    s[:tap] = nil if s[:tap][1] < 0
    $ttm_touch = nil unless s[:tap]
  end
  $ttm_extra_keys = nil
  m = TTMBottomMenu.menu
  on_map = $scene.is_a?(Scene_Map)
  busy = on_map && $game_system.map_interpreter.running?
  s[:at] ||= 0
  case s[:step]
  when :cutscene               # an event running, the menu up
    if busy && m && $ttm_frame > 300
      s[:cut_menu] = true
      s[:cut_veiled] = m.veils.all? { |v| v.visible }
      bc_shot("cutscene")
      bc_tap(bc_tab(m.options, 0))
      s[:at] = $ttm_frame
      s[:step] = :cutscene_wait
    end
  when :cutscene_wait
    if $ttm_frame == s[:at] + 30
      s[:cut_nothing] = !$scene.is_a?(Scene_Save)
      s[:step] = :free
    end
  when :free                   # wait to be free on the map, then stop pressing
    if on_map && !busy && m && !m.veils[0].visible
      $ttm_keys_source = proc { $ttm_extra_keys || [] }
      s[:free_at] = $ttm_frame
      bc_shot("characters")
      # the heart-monitor strip: what it is now, then a story era change
      mon = m.monitor
      s[:monitor] = mon
      s[:era] = m.heart.to_i
      s[:era_new] = s[:era] == 2 ? 3 : 2      # both eras have their pictures
      s[:strip_rect] = mon && [mon.x, mon.y, mon.width, mon.height]
      s[:beat0] = mon && mon.instance_variable_get(:@heart_beat).bitmap
      s[:overlay0] = mon && mon.instance_variable_get(:@heart_overlay).bitmap
      s[:graphic0] = mon && mon.instance_variable_get(:@heart_graphic_overlay).bitmap
      $game_variables[72] = s[:era_new]
      s[:at] = $ttm_frame
      s[:step] = :heart_change
    end
  when :heart_change
    if $ttm_frame > s[:at] + 2
      mon = m.monitor
      s[:heart_new] = m.heart
      s[:heart_changed] = mon && !mon.equal?(s[:monitor]) &&
        mon.instance_variable_get(:@heart_beat).bitmap.equal?(RPG::Cache.picture("heart#{s[:era_new]}"))
      $game_variables[72] = s[:era]
      s[:at] = $ttm_frame
      s[:step] = :heart_restore
    end
  when :heart_restore
    if $ttm_frame > s[:at] + 2
      mon = m.monitor
      s[:heart_restored] = mon && m.heart == s[:era] &&
        mon.instance_variable_get(:@heart_beat).bitmap.equal?(RPG::Cache.picture("heart#{s[:era]}"))
      bc_shot("strip")
      bc_tap(bc_tab(m.tabs, 1))
      s[:at] = $ttm_frame
      s[:step] = :notes
    end
  when :notes
    if $ttm_frame == s[:at] + 10
      s[:notes_shown] = m.tab == 1 && m.notes.visible && !m.status.visible
      s[:notes_index] = m.notes.index
      bc_shot("notes")
      bc_tap(bc_tab(m.options, 0))
      s[:at] = $ttm_frame
      s[:step] = :save
    end
  when :save
    if $scene.is_a?(Scene_Save)
      s[:save_opened] = $ttm_frame - s[:at]
      s[:step] = :save_dim
      s[:at] = $ttm_frame
    elsif $ttm_frame - s[:at] > 200
      s[:step] = :done
    end
  when :save_dim
    if $ttm_frame == s[:at] + 5
      s[:save_dim] = Graphics.ttm_bottom_dim
      bc_shot("save_screen")
      s[:step] = :save_cancel
    end
  when :save_cancel
    $ttm_extra_keys = [12] if $ttm_frame % 2 == 0          # B, pressed afresh
    if on_map
      s[:save_back] = true
      s[:step] = :load
      s[:at] = $ttm_frame
    end
  when :load
    if $ttm_frame == s[:at] + 20
      bc_tap(bc_tab(TTMBottomMenu.menu.options, 1))
    elsif $scene.is_a?(Scene_Load)
      s[:load_opened] = true
      s[:step] = :load_cancel
    elsif $ttm_frame - s[:at] > 200
      s[:step] = :done
    end
  when :load_cancel
    $ttm_extra_keys = [12] if $ttm_frame % 2 == 0
    s[:load_to_menu] = true if $scene.is_a?(Scene_Menu)
    if on_map
      s[:load_back] = true
      s[:step] = :exit
      s[:at] = $ttm_frame
    end
  when :exit
    if $ttm_frame == s[:at] + 20
      bc_tap(bc_tab(TTMBottomMenu.menu.options, 2))
    elsif $scene.is_a?(Scene_End)
      s[:exit_opened] = true
      s[:step] = :exit_cancel
    elsif $ttm_frame - s[:at] > 200
      s[:step] = :done
    end
  when :exit_cancel
    $ttm_extra_keys = [12] if $ttm_frame % 2 == 0
    s[:exit_to_menu] = true if $scene.is_a?(Scene_Menu)
    if on_map
      s[:exit_back] = true
      s[:step] = :done
      bc_shot("back_on_map")
    end
  when :done
    $ttm_frame = $ttm_max_frames - 1          # run_pc.rb stops the game next frame
    s[:step] = :stopping
  end
end

def ttm_log(s)
  __ttm_print(s.to_s + "\n") unless s.to_s.start_with?("gfx:", "audio:", "cache:", "frame ", "            audio")
end

TTM.load_script(TTM.read_file("pc/run_pc.rb"), "pc/run_pc.rb")

$ttm_passed = 0
$ttm_failed = []
def check(name, cond, extra = nil)
  if cond
    $ttm_passed += 1
    __ttm_print format("  ok    %s%s\n", name, extra ? "  (#{extra})" : "")
  else
    $ttm_failed << name
    __ttm_print format("  FAIL  %s%s\n", name, extra ? "  (#{extra})" : "")
  end
end
s = $bc
__ttm_print "bottom-screen menu\n"
check("the menu is on the bottom screen in a cutscene", s[:cut_menu])
check("Save, Load and Exit Game veiled in a cutscene", s[:cut_veiled])
check("Save tapped in a cutscene: nothing opens", s[:cut_nothing])
check("free on the map at some point", s[:free_at], "frame #{s[:free_at]}")
check("the heart-monitor strip is on the menu", s[:monitor].is_a?(Window_MenuHeartMonitor))
check("the strip is the menu's top 640x72, at half size",
      s[:strip_rect] == [0, 0, 640, 72] && Graphics.ttm_bottom_view(*TTMBottomMenu::VIEW) == [0.5, 0.0, 0.0],
      s[:strip_rect].inspect)
check("the strip lies inside 320x240",
      s[:strip_rect] && s[:strip_rect][0] >= 0 && s[:strip_rect][1] >= 0 &&
      s[:strip_rect][0] + s[:strip_rect][2] * 0.5 <= 320 && s[:strip_rect][1] + s[:strip_rect][3] * 0.5 <= 240)
check("the strip shows the era's own pictures",
      s[:beat0].equal?(RPG::Cache.picture("heart#{s[:era]}")) &&
      s[:overlay0].equal?(RPG::Cache.picture("heartoverlay#{s[:era]}")) &&
      s[:graphic0].equal?(RPG::Cache.picture("heartoveralygraphic#{s[:era]}")),
      "era #{s[:era].inspect}")
check("variable 72 = #{s[:era_new]} remakes the strip with heart#{s[:era_new]}", s[:heart_changed],
      "heart variable #{s[:heart_new].inspect}")
check("restoring era #{s[:era]} brings heart#{s[:era]} back", s[:heart_restored])
check("tapping Notes shows the notes", s[:notes_shown], "notes index #{s[:notes_index].inspect}")
check("tapping Save opens the save screen", s[:save_opened], s[:save_opened] ? "#{s[:save_opened]} frames after the tap" : "never")
check("the bottom screen is veiled there", s[:save_dim].to_i > 0, "dim #{s[:save_dim].inspect}")
check("cancelling the save screen returns to the map", s[:save_back])
check("tapping Load opens the load screen", s[:load_opened])
check("cancelling it returns to the map, not the top menu", s[:load_back] && !s[:load_to_menu])
check("tapping Exit Game opens the end screen", s[:exit_opened])
check("cancelling it returns to the map, not the top menu", s[:exit_back] && !s[:exit_to_menu])
# the proof that it is on the bottom screen (not the top) is in the shots:
# pc/shots/bottom_characters.png and bottom_strip.png show the strip; the
# monitor is made inside Graphics.ttm_on_bottom, which is screen 1.
__ttm_print format("%d checks passed, %d failed%s\n", $ttm_passed, $ttm_failed.size,
                   $ttm_failed.empty? ? "" : ": " + $ttm_failed.join(", "))
