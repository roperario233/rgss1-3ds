# profile_pc.rb -- run M2 part 3 on the PC with the per-phase profile on.
#   (from ruby-spike/)  mruby.exe profile_pc.rb
# The wrappers add their own cost, so use this for the breakdown only; the
# frame times to trust are from romfs/part3.rb without the profile.
$ttm_profile = true
TTM.load_script(TTM.read_file("romfs/part3.rb"), "part3.rb")
