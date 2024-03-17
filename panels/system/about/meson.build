print_renderer_deps = [
  dependency('gtk4'),
  epoxy_dep
]

executable(
  'test-info-cleanup',
  [ 'info-cleanup.c', 'test-info-cleanup.c' ],
  include_directories: [ top_inc ],
  dependencies: [ print_renderer_deps ],
  c_args: cflags,
)

executable(
  'gnome-control-center-print-renderer',
  'gnome-control-center-print-renderer.c',
  include_directories: [ top_inc ],
  dependencies: [ print_renderer_deps ],
  c_args: cflags,
  install: false,
  install_dir: control_center_libexecdir,
)

install_data(
  [ 'regolith-logo.png' ],
  install_dir: join_paths(control_center_datadir, 'icons')
)

# subdir('icons')
