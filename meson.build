project(
  'sd-inhibit-bridge',
  'c',
  version: '0.1.1',
  default_options: [
    'warning_level=3',
    'c_std=c2x',
  ],
)

add_project_arguments([
  '-D_DEFAULT_SOURCE',
  '-Wno-pedantic',
  '-Wno-error=unused-function',
  '-Wno-error=unused-parameter',
  '-Wno-error=unused-but-set-parameter',
  '-Wno-error=unused-but-set-variable',
  '-Wno-error=unused-variable',
  '-Wno-error=unused-value',
  '-Wno-error=unused-label',
  '-Wno-error=unused-const-variable',
  '-Wno-error=unused-result',
  '-include', meson.source_root() / 'extensions.h',
  # HACK: stop vscode's intellisense from complaining about missing c2x features
  '-include', meson.source_root() / 'c2xcompat.h',
], language: 'c')

DEP_SYSTEMD = dependency(
  'systemd',
  include_type: 'system',
  required: false,
)

if get_option('sd-bus-provider') == 'auto'
  DEP_LIBSYSTEMD = dependency(
    ['libsystemd', 'libelogind'],
    include_type: 'system',
    required: true,
  )
else
  DEP_LIBSYSTEMD = dependency(
    get_option('sd-bus-provider'),
    include_type: 'system',
    required: true,
  )
endif

docdir = get_option('docdir')
if docdir == ''
  docdir = join_paths(
    get_option('prefix'),
    get_option('datadir'),
    'doc',
    meson.project_name(),
  )
endif
install_data('README.md', install_dir: docdir)

licensedir = get_option('licensedir')
if licensedir == ''
  licensedir = join_paths(
    get_option('prefix'),
    get_option('datadir'),
    'licenses',
    meson.project_name(),
  )
endif
install_data('LICENSE', install_dir: licensedir)

subdir('src')
