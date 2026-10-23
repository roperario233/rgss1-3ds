# choice_check.rb -- the choice and name windows of a message (UMS), redrawn
# only when they change (romfs/rgss/ttm_patches.rb, "UMS new").
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\choice_check.rb          (patched)
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\choice_check.rb unpatched
#
# Runs the game as pc/run_pc.rb does until the player is free on the map, then
# shows a message with a name and two choices over the player (message_event
# 0, as the game's own scenes do -- that is what makes UMS lay it out again
# every frame) and holds it for HOLD frames. Prints, for the frames the choice
# is up: Bitmaps made per frame, the Ruby time per frame, and a checksum of
# the choice and name windows' contents at the end. Run both ways: the
# checksums must be the same.

HOLD = 80
$cc = { step: :wait, frames: 0, bitmaps: 0, ruby_us: 0, per_frame: [] }
$ttm_skip_patches = ["UMS new"] if ARGV.include?("unpatched")
$ttm_max_frames = 3000
$ttm_shots = []

class << Bitmap
  alias_method :cc_new, :new
  def new(*a)
    $cc[:bitmaps] += 1
    cc_new(*a)
  end
end

$ttm_extra_frame_hook = proc do
  next unless Object.const_defined?(:Scene_Map)
  s = $cc
  now = TTM.us
  case s[:step]
  when :wait
    if $scene.is_a?(Scene_Map) && !$game_system.map_interpreter.running? && $ttm_frame > 200 &&
       $game_temp.message_text.nil?
      $ttm_keys_source = proc { [] }          # no presses from here on
      $game_system.message_event = 0
      $game_system.name = "Dr. Watts"
      $game_temp.message_text = "So, what do you say?"
      $game_temp.choice_start = 1
      $game_temp.choice_max = 2
      $game_temp.choices = ["Sure, I'll go get it for you!", "No. Cut the crap and help."]
      $game_temp.choice_cancel_type = 0
      $game_temp.choice_proc = proc { |n| s[:chosen] = n }
      s[:step] = :up
      s[:t] = now
    end
  when :up
    mw = $scene.instance_variable_get(:@message_window)
    cw = mw && mw.instance_variable_get(:@choice_window)
    if cw && cw.visible
      s[:frames] += 1
      # this frame's Ruby: the time since the last hook, less its drawing
      s[:ruby_us] += now - s[:t] - Graphics.ttm_render_us
      s[:per_frame] << s[:bitmaps]
      s[:bitmaps] = 0
      if s[:frames] == HOLD
        nw = mw.instance_variable_get(:@name_window)
        s[:choice_sum] = cw.contents.ttm_checksum
        s[:name_sum] = nw.contents.ttm_checksum
        s[:choice_size] = [cw.width, cw.height]
        Graphics.ttm_screenshot("pc/shots/choice_#{$ttm_skip_patches ? 'unpatched' : 'patched'}.png")
        s[:step] = :done
        $ttm_frame = $ttm_max_frames - 1      # stop the run
      end
    else
      s[:bitmaps] = 0
    end
    s[:t] = now
  end
end

def ttm_log(s)
  __ttm_print(s.to_s + "\n") unless s.to_s.start_with?("gfx:", "audio:", "cache:", "frame ", "            audio", "map load")
end

TTM.load_script(TTM.read_file("pc/run_pc.rb"), "pc/run_pc.rb")

s = $cc
__ttm_print format("choice window, %s\n", $ttm_skip_patches ? "UNPATCHED" : "patched")
__ttm_print format("  frames with the choice up: %d\n", s[:frames])
if s[:frames] > 0
  pf = s[:per_frame]
  __ttm_print format("  Bitmaps made, frames 1-5: %s; frames 6-%d: %d in all\n",
                     pf[0, 5].inspect, pf.size, pf[5..-1].to_a.inject(0) { |a, b| a + b })
  __ttm_print format("  Ruby per frame: %.2f ms\n", s[:ruby_us] / 1000.0 / s[:frames])
  __ttm_print format("  choice window %s, contents checksum %08x; name window contents %08x\n",
                     s[:choice_size].inspect, s[:choice_sum], s[:name_sum])
end
