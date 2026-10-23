# fileio_check.rb -- RGSS's File/Dir on real files: the C layer in
# gems/mruby-ttm-core/src/fileio.c plus the path mapping in rgss/compat.rb. This
# is where the game's saves and its settings files land, so it gets checked on
# its own as well as through a real save (pc/save_roundtrip.rb).
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\fileio_check.rb
#
# Writes only under $ttm_data_dir + "scratch/" and deletes every file it writes
# (empty folders are left behind), so it can also be run on the console, where
# $ttm_data_dir is the app's folder on the SD card.

$ttm_data_dir = $ttm_data_dir || "../work/pc_data/"
TTM.load_script(TTM.read_file("romfs/rgss/compat.rb"), "rgss/compat.rb")
# compat.rb sends Kernel#print to the log, and the checks read better raw

FILE = "scratch/dir/blob.bin"
# a name that cannot have been left by an earlier run (the empty folders are)
DEEP = "scratch/a/b/c#{TTM.us}"
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

# the mapping: the game's own paths, under one root, with nothing that can climb
mapped = TTMFiles.norm("save/save4.rxdata")
check("path maps under the root", mapped == $ttm_data_dir + "save/save4.rxdata", mapped)
check("parent segments are dropped", TTMFiles.norm("../../etc/passwd") == $ttm_data_dir + "etc/passwd")
check("backslashes and drive letters", TTMFiles.norm("C:\\save\\x.rxdata") == $ttm_data_dir + "save/x.rxdata")

# bytes, not characters: every byte value, four times over
blob = (0..255).to_a.pack("C*") * 4
check("write returns the byte count", File.binwrite(FILE, blob) == blob.bytesize)
check("read gives the bytes back", File.binread(FILE) == blob, "#{blob.bytesize} bytes")
check("File.size agrees", File.size(FILE) == blob.bytesize)
check("file? and directory?", File.file?(FILE) && File.directory?("scratch/dir"))

# a Time, which is what Window_SaveFile shows under a slot (FAT time on the 3DS,
# so only "it is a Time" is asserted here -- the value is printed)
mt = File.open(FILE, "rb").mtime
check("mtime is a Time", mt.is_a?(Time), mt.to_s)

# mkdir makes the parents the game's "save/" needs, and does not mind repeats
check("the deep folder is not there yet", TTMFiles.directory?(DEEP) == false)
Dir.mkdir(DEEP)
check("Dir.mkdir creates parents", Dir.exist?(DEEP))
check("Dir.mkdir again is harmless", Dir.mkdir(DEEP) == 0)

names = Dir.entries("scratch/dir")
check("Dir.entries lists the file", names.include?("blob.bin"), names.join(","))

# reading what is not there: ENOENT for the game, "en" for the language setting
begin
  File.read("scratch/not-there.txt")
  check("File.read raises ENOENT", false)
rescue Errno::ENOENT
  check("File.read raises ENOENT", true)
end
check("the language setting reads as en", TTMFiles.read("save/lang") == "en")
check("the language setting exists", File.exist?("save/lang") == true)

# delete, and delete again (the game checks with FileTest first, but must not die)
check("delete removes it", File.delete(FILE) == 1)
check("it is gone", File.exist?(FILE) == false)
check("deleting a missing file does not raise", File.delete(FILE) == 1)
check("deleting a file in a missing folder does not raise", File.delete("scratch/none/x") == 1)

__ttm_print format("\n%d checks passed", $ttm_passed)
if $ttm_failed.empty?
  __ttm_print ", 0 failed\n"
else
  __ttm_print format(", %d FAILED: %s\n", $ttm_failed.size, $ttm_failed.join(", "))
end
