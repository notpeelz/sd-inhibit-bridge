conf_data = configuration_data()
conf_data.set_quoted('SDIB_VERSION', meson.project_version())

file_buildconf = configure_file(
  output: 'buildconf.h',
  configuration: conf_data
)

EXE_SDIB_NAME = meson.project_name()
EXE_SDIB_PATH = get_option('prefix') / get_option('bindir') / EXE_SDIB_NAME

executable(
  EXE_SDIB_NAME,
  [
    'main.c',
    'htable.c',
    'inhibitman.c',
  ],
  install: true,
  install_dir: get_option('bindir'),
  dependencies: [
    DEP_LIBSYSTEMD,
  ],
  include_directories: [
    include_directories('.'),
  ],
  link_args: [
    '-Wl,--version-script=@0@'.format(meson.current_source_dir() / 'version.map'),
  ],
  c_args: [
    '-include', file_buildconf.full_path(),
  ],
)

subdir('systemd')
