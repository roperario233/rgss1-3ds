# game_dir.rb -- where the PC checks find the game they replay.
#
# Every check in pc/ drives a real RGSS1 game: it opens the archive, boots the
# scripts and compares what the engine draws against what the game draws. This
# repository carries no game data and never will (see LICENSE), so the checks need
# your own copy. Two files decide which copy:
#
#   work/game_dir.txt   one line: the game's folder, relative to engine/ (where the
#                       checks run -- or absolute). Missing, the checks use
#                       ../work/game/ (which is what docs/PORTING.md says to use).
#   the archive         the folder's To the Moon.rgssad, or else its first
#                       *.rgssad -- so any RGSS1 game's folder works, not just the
#                       one these checks were written against.
#
# Neither file is required to build the engine or the CIA.
#
# Needs TTM.read_file and TTM.dir_entries (C, in the host's mruby and on the
# console), so load it where the checks load their scripts from romfs/.

def ttm_check_game_dir
  line = (TTM.read_file("../work/game_dir.txt") || "").split("\n").first.to_s.strip
  line = "../work/game" if line.empty?
  line += "/" unless line.end_with?("/")
  line
end

def ttm_check_archive(dir)
  names = (TTM.dir_entries(dir) || []).map { |n| n.to_s }
  pick = names.find { |n| n == "To the Moon.rgssad" } ||
         names.find { |n| n.downcase.end_with?(".rgssad") }
  dir + (pick || "To the Moon.rgssad")
end
