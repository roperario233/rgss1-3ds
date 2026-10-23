# patch_upstream.rb -- one fix to mruby-onig-regexp (pinned 4962d7eb), applied
# by build_mruby.ps1 after fetching. Idempotent.
#
# Bug: String#gsub (and scan) reuse ONE MatchData across matches, and
# MatchData#[] / #to_a cache the capture array in @cache on first use. $1..$9
# are set through MatchData#[], so from the second match on they return the
# FIRST match's groups: "\C[2]x\C[0]".gsub(/\\C\[(\d)\]/) { $1 } gives "2x2".
# The game's message system does exactly this (gsub! with $1 in the block).
#
# Fix: drop the cache whenever onig_match_common() stores a new match.
#
# Usage: ruby patch_upstream.rb <mruby-onig-regexp dir>

dir = ARGV[0] or abort "usage: ruby patch_upstream.rb <mruby-onig-regexp dir>"
path = File.join(dir, "src", "mruby_onig_regexp.c")
src = File.binread(path)

MARK = "ttm3ds patch"
if src.include?(MARK)
  puts "already patched: #{path}"
  exit
end

anchor = "  mrb_obj_iv_set(mrb, (struct RObject*)ONIG_REGEXP_CLASS(mrb), MRB_IVSYM(last_match), MISMATCH_NIL_OR(match_value));\n"
abort "patch anchor not found (upstream changed?): #{path}" unless src.scan(anchor).size == 1

fix = "  /* #{MARK}: gsub/scan reuse one MatchData; drop its cached captures so $1.. follow each match */\n" \
      "  mrb_iv_set(mrb, match_value, MRB_SYM(cache), mrb_nil_value());\n"
File.binwrite(path, src.sub(anchor, fix + anchor))
puts "patched: #{path}"
