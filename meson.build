project(
  'sd-inhibit-bridge',
  'c',
  version: '0.1.0',
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

DEP_SYSTEMD = dependency('systemd', required: false)

if get_option('sd-bus-provider') == 'auto'
  DEP_LIBSYSTEMD = dependency(
    ['libsystemd', 'libelogind'],
    required: true,
  )
else
  DEP_LIBSYSTEMD = dependency(get_option('sd-bus-provider'), required: true)
endif

subdir('src')
