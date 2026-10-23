# mruby_gc_alloc_counter.rb -- add an object-allocation counter to mruby 4.0.0's
# gc.c, applied by build_mruby.ps1. Idempotent.
#
# The engine's frame budget depends on how many short-lived objects the game's
# scripts create per frame (M2 part 2: ~9.5 us each with GC on the 3DS), and
# mruby exposes no such count. The patch adds one global, bumped in
# mrb_obj_alloc() next to gc->live++. mruby-ttm-core reads it as TTM.obj_allocs.
#
# Usage: ruby mruby_gc_alloc_counter.rb <mruby dir>

dir = ARGV[0] or abort "usage: ruby mruby_gc_alloc_counter.rb <mruby dir>"
path = File.join(dir, "src", "gc.c")
src = File.binread(path)
MARK = "ttm3ds patch"
DEFINITION = "size_t mrb_ttm_obj_allocs = 0;\n"
if src.include?(MARK)
  # first version used MRB_API (= extern) on an initialised definition
  if src.include?("MRB_API " + DEFINITION)
    File.binwrite(path, src.sub("MRB_API " + DEFINITION, DEFINITION))
    puts "updated: #{path}"
  else
    puts "already patched: #{path}"
  end
  exit
end

decl_anchor = "MRB_API struct RBasic*\nmrb_obj_alloc(mrb_state *mrb, enum mrb_vtype ttype, struct RClass *cls)\n"
count_anchor = "  gc->live++;\n"
[decl_anchor, count_anchor].each do |a|
  abort "patch anchor not found or not unique (mruby changed?): #{a.inspect}" unless src.scan(a).size == 1
end

src = src.sub(decl_anchor, "/* #{MARK}: objects allocated since start, read by mruby-ttm-core */\n" +
                           DEFINITION + "\n" + decl_anchor)
src = src.sub(count_anchor, count_anchor + "  mrb_ttm_obj_allocs++;    /* #{MARK} */\n")
File.binwrite(path, src)
puts "patched: #{path}"
