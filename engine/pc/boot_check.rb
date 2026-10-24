# boot_check.rb -- the two start-up caches (romfs/boot.rb): the archive's index
# (gems/mruby-ttm-core/src/rgssad.c) and compiled scripts (ttm_core.c,
# TTM.load_script_cached).
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\boot_check.rb
#
#  - the index read from the cache is the index built from the archive, entry
#    for entry, and files read through it are the same bytes
#  - a cache that does not match (other size or content, cut short) is
#    rebuilt, never trusted
#  - a cached script runs as its source does, from bytecode the second time,
#    and its backtraces still name the script and line; a damaged file
#    compiles from source again
# Whether the game itself behaves the same from bytecode is pc/run_pc.rb's
# screenshots, compared between a cold and a warm cache.

TTM.load_script(TTM.read_file("pc/game_dir.rb"), "pc/game_dir.rb")
$ttm_game_dir = ttm_check_game_dir
ARC = ttm_check_archive($ttm_game_dir)
DIR = "../work/pc_data/scratch/bootcheck/"
%w[rgss/compat.rb].each { |f| TTM.load_script(TTM.read_file("romfs/" + f), f) }
TTM.dir_mkdir(DIR)
(TTM.dir_entries(DIR) || []).each { |f| TTM.file_delete(DIR + f) }

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

def timed
  t = TTM.us
  r = yield
  [r, (TTM.us - t) / 1000.0]
end

__ttm_print "the archive's index\n"
idx = DIR + "archive.idx"
n0, ms0 = timed { TTM.archive_open(ARC) }
fresh = TTM.archive_entries
check("without a cache: indexed", !TTM.archive_cached? && n0 > 0, "#{n0} files, #{ms0} ms")
n1, ms1 = timed { TTM.archive_open(ARC, idx) }
check("first start: built, and the cache written", !TTM.archive_cached? && TTM.file_size(idx).to_i > 0,
      "#{TTM.file_size(idx)} bytes")
n2, ms2 = timed { TTM.archive_open(ARC, idx) }
# (on the PC the cache is no faster: its disk makes 979 small seeks nearly
# free. The 3DS's SD card took 1.9 s for them; that is what the cache is for.)
check("next start: from the cache", TTM.archive_cached? && n2 == n0, "#{ms2} ms; #{ms0} ms without (PC)")
check("the same index, entry for entry", TTM.archive_entries == fresh)
same = %w[Data/Scripts.rxdata Data/Map001.rxdata Graphics/Titlescreen/titlescreen.png Data/System.rxdata].all? do |f|
  a = TTM.archive_read(f)
  TTM.archive_open(ARC)
  b = TTM.archive_read(f)
  TTM.archive_open(ARC, idx)
  a == b
end
check("files read through it are the same bytes", same)

bytes = TTM.read_file(idx)
TTM.file_write(idx, bytes.byteslice(0, 8) + [1].pack("V") + bytes.byteslice(12, bytes.bytesize - 12))
TTM.archive_open(ARC, idx)
check("a cache for an archive of another size: rebuilt", !TTM.archive_cached? && TTM.archive_entries == fresh)
bytes = TTM.read_file(idx)
TTM.file_write(idx, bytes.byteslice(0, 12) + [0].pack("V") + bytes.byteslice(16, bytes.bytesize - 16))
TTM.archive_open(ARC, idx)
check("a cache for other content (fingerprint): rebuilt", !TTM.archive_cached? && TTM.archive_entries == fresh)
bytes = TTM.read_file(idx)
TTM.file_write(idx, bytes.byteslice(0, bytes.bytesize / 2))
TTM.archive_open(ARC, idx)
check("a cache cut short: rebuilt", !TTM.archive_cached? && TTM.archive_entries == fresh)
TTM.archive_open(ARC, idx)
check("...and good again afterwards", TTM.archive_cached?)

__ttm_print "compiled scripts\n"
src = "$boot_check_v = (1..10).inject(:+)\n" \
      "class BootCheckThing; def boom; raise ArgumentError, 'boom'; end; end\n"
bin = DIR + "test.mrb"
h0, c0, b0 = TTM.script_cache_stats
TTM.load_script_cached(src, "boot_check_script", bin)
h1, c1, b1 = TTM.script_cache_stats
check("first run: compiled, ran, saved", c1 == c0 + 1 && $boot_check_v == 55 && TTM.file_size(bin).to_i > 0,
      "#{TTM.file_size(bin)} bytes")
$boot_check_v = nil
TTM.load_script_cached(src, "boot_check_script", bin)
h2, c2, b2 = TTM.script_cache_stats
check("second run: from bytecode, same result", h2 == h1 + 1 && c2 == c1 && $boot_check_v == 55)
where = begin
  BootCheckThing.new.boom
rescue ArgumentError => e
  (e.backtrace || []).first.to_s
end
check("a backtrace from bytecode names the script and line", where.include?("boot_check_script:2"), where)
check("defs go where the source's would (top level, Object)", Object.const_defined?(:BootCheckThing))
TTM.file_write(bin, "RITE garbage")
TTM.load_script_cached(src, "boot_check_script", bin)
h3, c3, b3 = TTM.script_cache_stats
check("a damaged file: compiled from source again", b3 == b2 + 1 && c3 == c2 + 1 && $boot_check_v == 55)
TTM.load_script_cached(src, "boot_check_script", bin)
check("...and rewritten", TTM.script_cache_stats[0] == h3 + 1)
err = begin
  TTM.load_script_cached("def broken(", "boot_check_bad", DIR + "bad.mrb")
  nil
rescue SyntaxError => e
  e.message
end
check("a syntax error is still a SyntaxError", err && !TTM.file_size(DIR + "bad.mrb"), err.to_s)

(TTM.dir_entries(DIR) || []).each { |f| TTM.file_delete(DIR + f) }
__ttm_print format("%d checks passed, %d failed%s\n", $ttm_passed, $ttm_failed.size,
                   $ttm_failed.empty? ? "" : ": " + $ttm_failed.join(", "))
