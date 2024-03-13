if get_option('systemd-user-service').enabled()
  systemd_config = configuration_data()
  systemd_config.set('SDIB_BIN', EXE_SDIB_PATH)

  if not DEP_SYSTEMD.found()
    error('user service requires systemd')
  endif

  systemd_user_unit_dir = DEP_SYSTEMD.get_variable(
    pkgconfig: 'systemduserunitdir',
    pkgconfig_define: ['prefix', get_option('prefix')],
  )

  configure_file(
    input: 'sd-inhibit-bridge.service.in',
    output: 'sd-inhibit-bridge.service',
    configuration: systemd_config,
    install_dir: systemd_user_unit_dir,
  )
endif
