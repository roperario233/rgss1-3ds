# mapload_profile.rb -- where a map change's time goes (PC, line profiler).
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\mapload_profile.rb
#
# Runs the game as pc/run_pc.rb does (4,000 frames) and times every
# Scene_Map#transfer_player: in total and in its parts ($game_map.setup, the
# old spriteset's dispose, Spriteset_Map.new), with the line profiler on during
# each transfer. Prints each transfer, then the 25 heaviest lines over all of
# them. The 3DS runs this Ruby some 25-30x slower; its own "map load" log
# lines (romfs/rgss/ttm_patches.rb) give the console's times.

$ttm_max_frames = 4000
$ttm_max_tex_dim = 1024
$ttm_tex_budget_kb = 44 * 1024
$ttm_shots = []
$ml_samples = Hash.new(0)
$ml_rows = []

$ttm_extra_frame_hook = proc do
  if !$ml_installed && Object.const_defined?(:Scene_Map) && Object.const_defined?(:Spriteset_Map)
    $ml_installed = true
    Game_Map.class_eval do
      alias_method :ml_setup, :setup
      def setup(id)
        t = TTM.us
        ml_setup(id)
        $ml_part[:setup] += TTM.us - t if $ml_part
      end
    end
    Spriteset_Map.class_eval do
      alias_method :ml_init, :initialize
      alias_method :ml_dispose, :dispose
      def initialize(*a)
        t = TTM.us
        ml_init(*a)
        $ml_part[:spriteset_new] += TTM.us - t if $ml_part
      end
      def dispose
        t = TTM.us
        ml_dispose
        $ml_part[:spriteset_dispose] += TTM.us - t if $ml_part
      end
    end
    Scene_Map.class_eval do
      alias_method :ml_transfer, :transfer_player
      def transfer_player
        $ml_part = Hash.new(0)
        from = $game_map.map_id
        TTM.profile_start(50)
        t = TTM.us
        ml_transfer
        total = TTM.us - t
        TTM.profile_stop.each { |k, v| $ml_samples[k] += v }
        $ml_rows << [from, $game_map.map_id, total, $ml_part]
        $ml_part = nil
      end
    end
  end
end

TTM.load_script(TTM.read_file("pc/run_pc.rb"), "pc/run_pc.rb")

__ttm_print "map changes (ms, PC):\n"
__ttm_print "  from  to   total   setup  dispose  spriteset\n"
$ml_rows.each do |(from, to, total, part)|
  __ttm_print format("  %3d  %3d  %6.1f  %6.1f  %7.1f  %9.1f\n", from, to, total / 1000.0,
                     part[:setup] / 1000.0, part[:spriteset_dispose] / 1000.0, part[:spriteset_new] / 1000.0)
end
all = $ml_samples.values.inject(0, :+)
__ttm_print format("heaviest lines over %d transfers (%d samples):\n", $ml_rows.size, all)
$ml_samples.sort_by { |_, v| -v }.first(25).each do |(k, v)|
  __ttm_print format("  %5.1f%%  %s\n", 100.0 * v / [all, 1].max, k)
end
