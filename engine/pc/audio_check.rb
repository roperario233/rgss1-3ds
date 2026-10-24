# audio_check.rb -- the Audio module (gems/mruby-ttm-audio) and the audio pack
# (tools/pack_audio.py), on the PC's silent backend.
#
#   cd engine
#   C:\Users\davao\devkitpro\build\engine\host\bin\mruby.exe pc\audio_check.rb
#
# Needs ../work/audio/audio.ttma (python tools\pack_audio.py <the game's zip or
# folder>) and ../work/audio/refs.txt: every sound the game's data names, and whether
# the game ships a file of that name, one "kind/name <tab> 0|1 <tab> where" line each.
# refs.txt came from a one-off script that walked the extracted Data/ and the game's
# zip; it is not part of this repository. Without both files, tools\run_checks.ps1
# skips this check rather than failing it.
#
#  1. the pack: every stream decodes to the end with stb_vorbis (the decoder
#     the console runs), with the channels, rate and length in its index
#  2. the game's sounds: every name the maps, common events, system data,
#     animations and scripts ask for, and that the game ships, is in the pack;
#     the ones it does not ship are logged and silent, never an error
#  3. RGSS's behaviour, on the backend's clock (Audio.ttm_advance): the same
#     BGM again only changes its volume, a new pitch restarts it, BGS loops,
#     fades end on time, the BGM goes quiet under an ME and comes back, the
#     oldest SE gives way when all voices are busy, se_stop, the SE cache

TTM.load_script(TTM.read_file("pc/game_dir.rb"), "pc/game_dir.rb")
$ttm_game_dir = ttm_check_game_dir
$ttm_audio_pack = "../work/audio/audio.ttma"
%w[rgss/compat.rb rgss/rgss.rb].each do |f|
  TTM.load_script(TTM.read_file("romfs/" + f), f)
end

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

def bgm; Audio.ttm_state[0]; end
def bgs; Audio.ttm_state[1]; end
def me; Audio.ttm_state[2]; end
def voices; Audio.ttm_state[3]; end
def near(a, b, slack); (a - b).abs <= slack; end
# advance in game frames (25 ms at 40 fps)
def frames(n); n.times { Audio.ttm_advance(25) }; end

__ttm_print "the pack\n"
pack = Audio.ttm_pack
check("opened", pack.size > 0, "#{pack.size} sounds")
# The index's length is ffmpeg's decode of the packed stream. The two
# decoders trim the last Vorbis page differently, and only ever by silent
# padding: on 004-System04 (6183 frames in the game's own file) stb_vorbis
# gives exactly 6183 and ffmpeg 6528; on 002-System02 (2944) ffmpeg is exact
# and stb gives 3200. So the lengths may differ by up to half a long block
# (1024 frames), never more.
bad = []
secs_total = 0.0
exact = 0
pack.each do |(key, ch, rate, n)|
  d = Audio.ttm_decode("Audio/" + key)
  secs_total += n / rate.to_f
  exact += 1 if d && d[2] == n
  bad << "#{key}: #{d.inspect} vs #{[ch, rate, n].inspect}" unless d && d[0] == ch && d[1] == rate && (d[2] - n).abs <= 1024
end
check("every stream decodes to its end, as indexed", bad.empty?,
      bad.empty? ? format("%d streams, %.1f minutes; %d exactly the index's length, the rest within 1024 frames of end padding",
                          pack.size, secs_total / 60, exact) : "#{bad.size} differ: #{bad.first(3).join('; ')}")

__ttm_print "the game's sounds\n"
keys = {}
pack.each { |e| keys[e[0]] = e }
refs = TTM.read_file("../work/audio/refs.txt").split("\n").collect { |l| l.split("\t") }
shipped = refs.select { |r| r[1] == "1" }
absent = refs.select { |r| r[1] == "0" }
lost = shipped.reject { |r| keys[r[0]] }
check("every sound the game uses and ships is in the pack", lost.empty?,
      lost.empty? ? "#{shipped.size} of #{refs.size} names" : lost.first(5).collect(&:first).join(", "))
missing_before = Audio.ttm_stats[2]
errors = 0
absent.each do |(key, _, _)|
  kind, name = key.split("/", 2)
  begin
    case kind
    when "bgm" then Audio.bgm_play("Audio/BGM/" + name)
    when "bgs" then Audio.bgs_play("Audio/BGS/" + name)
    when "me" then Audio.me_play("Audio/ME/" + name)
    else Audio.se_play("Audio/SE/" + name)
    end
  rescue => e
    errors += 1
  end
end
check("the #{absent.size} the game does not ship: logged, silent, no error",
      errors == 0 && Audio.ttm_stats[2] - missing_before == absent.size && !bgm[:playing] && voices.empty?,
      absent.first(3).collect(&:first).join(", ") + " ...")

__ttm_print "BGM\n"
by_kind = Hash.new { |h, k| h[k] = [] }
pack.each { |e| by_kind[e[0].split("/").first] << e }
b1 = by_kind["bgm"].first
b2 = by_kind["bgm"].last
name1 = "Audio/BGM/" + b1[0].split("/", 2)[1]
Audio.bgm_play(name1, 100, 100)
check("bgm_play starts it", bgm[:playing] && bgm[:name] == b1[0], b1[0])
frames(40)
check("one second plays one second of it", near(bgm[:frames], b1[2], 2), "#{bgm[:frames]} frames at #{b1[2]} Hz")
was = bgm[:frames]
Audio.bgm_play(name1, 50, 100)
check("the same BGM again: no restart, new volume", bgm[:frames] == was && near(bgm[:volume], 0.5, 0.001),
      "#{bgm[:frames]} frames, volume #{bgm[:volume]}")
Audio.bgm_play(name1, 50, 120)
check("a new pitch restarts it", bgm[:frames] == 0 && bgm[:pitch] == 120)
frames(40)
check("pitch 120 plays 1.2 s of it a second", near(bgm[:frames], b1[2] * 1.2, 2), "#{bgm[:frames]} frames")
Audio.bgm_play("Audio\\BGM\\" + b1[0].split("/", 2)[1].upcase + ".ogg", 100, 100)
check("names: backslashes, any case, an extension", bgm[:playing] && bgm[:name] == b1[0])
Audio.bgm_fade(1000)
frames(20)
check("bgm_fade(1000): half way, half the volume", bgm[:playing] && near(bgm[:volume], 0.5, 0.03), bgm[:volume].to_s)
frames(20)
check("bgm_fade(1000): stopped after 40 frames", !bgm[:playing])
Audio.bgm_play("Audio/BGM/" + b2[0].split("/", 2)[1])
Audio.bgm_stop
check("bgm_stop", !bgm[:playing] && bgm[:name] == "")

__ttm_print "BGS\n"
s = by_kind["bgs"].min_by { |e| e[3] }
Audio.bgs_play("Audio/BGS/" + s[0].split("/", 2)[1], 80, 100)
check("bgs_play: RGSS's volume as given", bgs[:playing] && near(bgs[:volume], 0.8, 0.001))
secs = s[3] / s[2].to_f
frames((secs * 40).ceil + 80)
check("loops past its end", bgs[:playing] && bgs[:frames] > s[3], format("%.2f s long, %d frames played", secs, bgs[:frames]))
Audio.bgs_fade(0)
check("bgs_fade(0) stops at once", !bgs[:playing])

__ttm_print "ME over BGM\n"
m = by_kind["me"].min_by { |e| e[3] }
Audio.bgm_play(name1, 100, 100)
frames(4)
Audio.me_play("Audio/ME/" + m[0].split("/", 2)[1])
frames(4)
check("the BGM goes quiet and waits", bgm[:paused] && bgm[:volume] == 0, "volume #{bgm[:volume]}")
held = bgm[:frames]
frames(40)
check("...without moving on", bgm[:frames] == held)
mlen = m[3] / m[2].to_f
frames((mlen * 40).ceil - 44 + 2)     # 44 frames of it have played already
check("the ME plays once and ends", !me[:playing], format("%s, %.1f s", m[0], mlen))
check("the BGM carries on from where it was, fading in", !bgm[:paused] && bgm[:frames] > held && bgm[:volume] < 0.2,
      format("%d frames on, volume %.2f", bgm[:frames] - held, bgm[:volume]))
frames(40)
check("...back to full volume within a second", near(bgm[:volume], 1, 0.001))
Audio.me_play("Audio/ME/" + m[0].split("/", 2)[1])
frames(8)
Audio.me_stop
frames(1)
check("me_stop brings the BGM back too", !me[:playing] && !bgm[:paused])
Audio.bgm_stop

__ttm_print "SE\n"
long = by_kind["se"].max_by { |e| e[3] }
short = by_kind["se"].min_by { |e| e[3] }
Audio.se_stop
loads, hits = Audio.ttm_stats
12.times { Audio.se_play("Audio/SE/" + long[0].split("/", 2)[1], 80, 100) }
check("12 voices at once", voices.size == 12, long[0])
first = voices.collect { |v| v[3] }.min
Audio.se_play("Audio/SE/" + long[0].split("/", 2)[1], 80, 100)
check("a 13th: the oldest gives way", voices.size == 12 && voices.collect { |v| v[3] }.min > first)
l2, h2 = Audio.ttm_stats
check("decoded once, then from the cache", l2 - loads <= 1 && h2 - hits >= 12, "#{l2 - loads} decoded, #{h2 - hits} from the cache")
Audio.se_stop
check("se_stop", voices.empty?)
Audio.se_play("Audio/SE/" + short[0].split("/", 2)[1], 80, 150)
frames(((short[3] / short[2].to_f) / 1.5 * 40).ceil + 1)
check("a voice ends with its sound (pitch 150: sooner)", voices.empty?, short[0])
# every SE, one at a time: each fits once least recently used ones give way
silent = by_kind["se"].reject do |e|
  Audio.se_play("Audio/SE/" + e[0].split("/", 2)[1])
  playing = voices.size == 1
  Audio.se_stop
  playing
end
st = Audio.ttm_stats
check("every SE plays, one at a time, in 4 MB of SE memory", silent.empty?,
      silent.empty? ? "#{by_kind['se'].size} sounds; #{st[3]} KB kept at the end" : silent.first(3).collect(&:first).join(", "))
# a pile-up (all of them 25 ms apart, 12 long ones still sounding): sounds
# still playing are never freed, so some get no room -- logged, not fatal
by_kind["se"].each { |e| Audio.se_play("Audio/SE/" + e[0].split("/", 2)[1]); frames(1) }
st = Audio.ttm_stats
check("a pile-up stays within the 4 MB", st[4] >= 0 && st[3] <= 4096, "#{st[3]} KB kept, #{st[4]} KB free")
Audio.se_stop

__ttm_print format("%d checks passed, %d failed%s\n", $ttm_passed, $ttm_failed.size,
                   $ttm_failed.empty? ? "" : ": " + $ttm_failed.join(", "))
