# png_check.rb -- the row-by-row PNG decoder (gems/mruby-ttm-gfx/src/png.c)
# against stb_image, on every image in the game's archive.
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\png_check.rb
#
# png.c exists so that no decode needs a large block of RAM (console run 8).
# It must give exactly stb's pixels: each image is loaded twice, once through
# png.c and once with $ttm_png_stb = true, and the two compared by size and a
# checksum over every pixel. Every image that is not a JPEG must have gone
# through png.c (none falling back to stb); JPEGs take stb both times, and are
# loaded too, since their pixels now go into bands as well.

$ttm_game_dir = "C:/Users/davao/OneDrive/Desktop/3ds project/work/game/"
%w[rgss/compat.rb rgss/rgss.rb].each do |f|
  TTM.load_script(TTM.read_file("romfs/" + f), f)
end
TTM.archive_open($ttm_game_dir + "To the Moon.rgssad")

manifest = TTM.read_file("../work/rgssad_manifest.json")
names = manifest.scan(/"name": "(Graphics[^"]+\.(?:png|PNG|jpg|JPG|jpeg))"/).map { |m| m[0].gsub("\\\\", "/") }
names = names.map { |n| n.sub(/\.[A-Za-z]+\z/, "") }.uniq

same = 0
differ = []
failed = []
fell_back = []    # not a JPEG, yet not decoded by png.c
pixels = 0
t0 = TTM.us
names.each do |n|
  begin
    $ttm_png_stb = false
    before = Bitmap.ttm_decoded[0]
    a = Bitmap.new(n)
    data = TTM.image_data(n)
    jpeg = data.getbyte(0) == 0xff && data.getbyte(1) == 0xd8
    fell_back << n if !jpeg && Bitmap.ttm_decoded[0] != before + 1
    $ttm_png_stb = true
    b = Bitmap.new(n)
  rescue => e
    failed << "#{n}: #{e.message}"
    next
  end
  if a.width == b.width && a.height == b.height && a.ttm_checksum == b.ttm_checksum
    same += 1
    pixels += a.width * a.height
  else
    differ << format("%s (%dx%d vs %dx%d)", n, a.width, a.height, b.width, b.height)
  end
  a.dispose
  b.dispose
end
$ttm_png_stb = false

__ttm_print format("%d images, %d identical to stb (%d pixels), %d differ, %d failed, %.1f s\n",
                   names.size, same, pixels, differ.size, failed.size, (TTM.us - t0) / 1e6)
differ.first(20).each { |d| __ttm_print "  DIFFER  #{d}\n" }
failed.first(20).each { |f| __ttm_print "  FAILED  #{f}\n" }
fell_back.first(20).each { |f| __ttm_print "  STB     #{f} (not decoded by png.c)\n" }
__ttm_print format("decoded by png.c: %d, by stb: %d; non-JPEGs that fell back to stb: %d\n",
                   *Bitmap.ttm_decoded, fell_back.size)
ok = differ.empty? && failed.empty? && fell_back.empty? && same > 0
__ttm_print(ok ? "png_check: PASS\n" : "png_check: FAIL\n")
