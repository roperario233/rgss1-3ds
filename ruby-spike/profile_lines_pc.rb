# profile_lines_pc.rb -- part 3 on the PC with the line-level sampling profiler:
# for each map, the hottest methods and source lines of the game's scripts.
#   (from ruby-spike/)  mruby.exe profile_lines_pc.rb
# Needs the host build's MRB_USE_DEBUG_HOOK (build_config_3ds.rb).
$ttm_profile_lines = true
TTM.load_script(TTM.read_file("romfs/part3.rb"), "part3.rb")
