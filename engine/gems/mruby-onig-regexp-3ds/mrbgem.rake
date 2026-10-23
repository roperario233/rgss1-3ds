# mruby-onig-regexp-3ds -- Regexp for mruby on the 3DS (and the PC test build).
#
# Wraps two upstream projects without modifying them:
#   * mattn/mruby-onig-regexp (MIT): the Regexp / MatchData classes, String#=~,
#     sub/gsub/scan/split with patterns, $~ and $1..$9.
#   * Onigmo 6.2.0 (BSD-2, bundled in that repo): the regex engine CRuby uses.
#
# Upstream builds Onigmo with ./configure (Unix) or Makefile.mingw (Windows);
# neither fits a devkitARM cross build driven from Windows. Instead this gem
# compiles the Onigmo sources it needs directly: each src/onigmo_*.c here just
# #includes one upstream file, include/config.h replaces ./configure, and only
# the ASCII / UTF-8 encodings the binding uses are built.
#
# Upstream locations (fetched by build_mruby.ps1):
#   TTM_ONIG_BINDING  mruby-onig-regexp checkout (pinned commit)
#   TTM_ONIGMO        onigmo-6.2.0 source tree

# Named like upstream: the binding defines mrb_mruby_onig_regexp_gem_init, and
# mruby derives that name from the gem name (the directory name is free).
MRuby::Gem::Specification.new('mruby-onig-regexp') do |spec|
  spec.license = 'MIT (binding), BSD-2-Clause (Onigmo)'
  spec.authors = ['mattn (binding)', 'K.Kosako, K.Takata (Onigmo)']
  spec.summary = 'Regexp via Onigmo, built without configure'
  spec.add_dependency 'mruby-string-ext', core: 'mruby-string-ext'

  binding = (ENV['TTM_ONIG_BINDING'] || 'C:/Users/davao/devkitpro/src/mruby-onig-regexp-4962d7eb').tr('\\', '/')
  onigmo  = (ENV['TTM_ONIGMO'] || 'C:/Users/davao/devkitpro/src/onigmo/onigmo-6.2.0').tr('\\', '/')
  [binding, onigmo].each { |d| fail "mruby-onig-regexp-3ds: missing #{d}" unless File.directory?(d) }

  spec.rbfiles = Dir.glob("#{binding}/mrblib/*.rb").sort
  spec.cc.include_paths += ["#{dir}/include", onigmo, "#{onigmo}/enc/unicode", "#{binding}/src"]
  spec.cc.defines += %w[HAVE_ONIGMO_H]
  # Onigmo 6.2 predates C23, which gcc 15+ uses by default
  spec.cc.flags << '-std=gnu11'
end
