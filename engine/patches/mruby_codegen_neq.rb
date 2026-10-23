# mruby_codegen_neq.rb -- compile `a != b` inline in mruby 4.0.0's code
# generator, applied by build_mruby.ps1. Idempotent.
#
# mruby defines BasicObject#!= as a tiny bytecode method (EQ, JMPNOT,
# LOADFALSE, JMP, LOADTRUE), so every `x != nil` / `name != ""` in the game's
# scripts paid for a full method call: ~4% of a busy map's frame (M2 part 3
# profile). This patch emits the same five instructions inline wherever the
# compiler already inlines `==` (one plain argument, no block, splat or
# keywords). Same result as BasicObject#!=; the only difference would be a
# class that defines its own `!=`, and the game defines none.
#
# Usage: ruby mruby_codegen_neq.rb <mruby dir>

dir = ARGV[0] or abort "usage: ruby mruby_codegen_neq.rb <mruby dir>"
path = File.join(dir, "mrbgems", "mruby-compiler", "core", "codegen.c")
src = File.binread(path)
MARK = "ttm3ds patch"
if src.include?(MARK)
  puts "already patched: #{path}"
  exit
end

edits = [
  ["  enum mrb_insn opt_op = OP_NOP;\n  int safe = call->safe_call;\n",
   "  enum mrb_insn opt_op = OP_NOP;\n  int neq = 0;   /* #{MARK}: `!=` compiled as !(==) */\n" \
   "  int safe = call->safe_call;\n"],
  ["    else if (sym == MRB_OPSYM(eq)) opt_op = OP_EQ;\n",
   "    else if (sym == MRB_OPSYM(eq)) opt_op = OP_EQ;\n" \
   "    else if (sym == MRB_OPSYM(neq)) { opt_op = OP_EQ; neq = 1; }   /* #{MARK} */\n"],
  ["  else if (opt_op == OP_EQ && n == 1) {\n    genop_1(s, OP_EQ, cursp());\n  }\n",
   "  else if (opt_op == OP_EQ && n == 1) {\n    genop_1(s, OP_EQ, cursp());\n" \
   "    if (neq) {   /* #{MARK}: the same code as BasicObject#!=, inline */\n" \
   "      uint32_t pos1 = genjmp2_0(s, OP_JMPNOT, cursp(), VAL);\n" \
   "      genop_1(s, OP_LOADFALSE, cursp());\n" \
   "      uint32_t pos2 = genjmp_0(s, OP_JMP);\n" \
   "      dispatch(s, pos1);\n" \
   "      genop_1(s, OP_LOADTRUE, cursp());\n" \
   "      dispatch(s, pos2);\n" \
   "    }\n  }\n"],
]
edits.each do |old, _|
  abort "patch anchor not found or not unique (mruby changed?): #{old.inspect}" unless src.scan(old).size == 1
end
edits.each { |old, new| src = src.sub(old, new) }
File.binwrite(path, src)
puts "patched: #{path}"
