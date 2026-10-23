# check_game_scripts.rb -- can mruby compile the game's own scripts?
#
# Runs every work/scripts/*.rb through
#   * mruby 4.0's compiler (mrbc -c, built by build_mruby.ps1), and
#   * CRuby's parser (Prism) as a reference,
# so each failure can be classed as "Ruby 1.8 syntax" (both reject it) or "mruby
# parser gap" (only mruby rejects it). Also counts the Regexp and Marshal uses
# the engine will have to supply.
#
# Run with the private CRuby (it has Prism):
#   C:\Users\davao\devkitpro\tools\rubyinstaller-3.4.11-1-x64\bin\ruby.exe ruby-spike\check_game_scripts.rb
#
# Read-only on work/scripts. Prints a report; nothing from the scripts is copied
# anywhere except a temp file per script for mrbc.

require "prism"
require "tmpdir"
require "open3"

ROOT    = File.expand_path("..", __dir__)
SCRIPTS = File.join(ROOT, "work", "scripts")
MRBC    = "C:/Users/davao/devkitpro/src/mruby-4.0.0/build/host/bin/mrbc.exe"

# Count the features the engine must supply, from CRuby's AST.
class Uses < Prism::Visitor
  attr_reader :regexp, :marshal
  def initialize; @regexp = 0; @marshal = 0; super; end
  def visit_regular_expression_node(n); @regexp += 1; super; end
  def visit_interpolated_regular_expression_node(n); @regexp += 1; super; end
  def visit_match_last_line_node(n); @regexp += 1; super; end
  def visit_constant_read_node(n); @marshal += 1 if n.name == :Marshal; super; end
end

files = Dir.glob(File.join(SCRIPTS, "*.rb")).sort
abort "no scripts in #{SCRIPTS}" if files.empty?

rows = []
total_bytes = 0
mrbc_ms = 0.0
Dir.mktmpdir do |tmp|
  files.each do |path|
    # rxdata_dump.py wrote in text mode, turning the original \r\n into \r\r\n
    src = File.binread(path).gsub("\r\r\n", "\r\n")
    total_bytes += src.bytesize
    name = File.basename(path, ".rb")

    tmpfile = File.join(tmp, "s.rb")
    File.binwrite(tmpfile, src)
    t = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    out, status = Open3.capture2e(MRBC, "-c", tmpfile)
    mrbc_ms += (Process.clock_gettime(Process::CLOCK_MONOTONIC) - t) * 1000
    m_err = status.success? ? nil : out.lines.first.to_s.strip.sub(/^.*?s\.rb:/, "line ")

    utf8 = src.dup.force_encoding("UTF-8")
    parsed = Prism.parse(utf8)
    c_err = parsed.errors.first && "line #{parsed.errors.first.location.start_line}: #{parsed.errors.first.message}"
    uses = Uses.new
    parsed.value.accept(uses)

    rows << { name: name, bytes: src.bytesize, mruby: m_err, cruby: c_err,
              regexp: uses.regexp, marshal: uses.marshal }
  end
end

both    = rows.select { |r| r[:mruby] && r[:cruby] }
m_only  = rows.select { |r| r[:mruby] && !r[:cruby] }
c_only  = rows.select { |r| !r[:mruby] && r[:cruby] }
ok      = rows.count { |r| !r[:mruby] }

puts "game scripts: #{rows.size} files, #{total_bytes / 1024} KB"
puts "mruby 4.0 compiles: #{ok}/#{rows.size}"
puts "CRuby 3.4 parses:   #{rows.count { |r| !r[:cruby] }}/#{rows.size}"
puts
puts "rejected by both (Ruby 1.8 syntax): #{both.size}"
both.each { |r| puts "  #{r[:name]}: mruby #{r[:mruby]}"; puts "    CRuby #{r[:cruby]}" }
puts "rejected by mruby only (parser gap): #{m_only.size}"
m_only.each { |r| puts "  #{r[:name]}: #{r[:mruby]}" }
puts "rejected by CRuby only: #{c_only.size}"
c_only.each { |r| puts "  #{r[:name]}: #{r[:cruby]}" }
puts
re = rows.select { |r| r[:regexp] > 0 }
puts "Regexp literals: #{rows.sum { |r| r[:regexp] }} in #{re.size} scripts"
re.sort_by { |r| -r[:regexp] }.first(8).each { |r| puts format("  %4d  %s", r[:regexp], r[:name]) }
ma = rows.select { |r| r[:marshal] > 0 }
puts "Marshal references: #{rows.sum { |r| r[:marshal] }} in #{ma.size} scripts"
ma.each { |r| puts format("  %4d  %s", r[:marshal], r[:name]) }
puts
puts format("mrbc -c over all scripts on this PC: %.0f ms (includes process start-up)", mrbc_ms)
