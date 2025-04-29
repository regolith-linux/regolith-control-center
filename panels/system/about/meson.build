desktop = 'regolith-about-panel.desktop'
i18n.merge_file(
  type: 'desktop',
  input: desktop + '.in',
  output: desktop,
  po_dir: po_dir,
  install: true,
  install_dir: control_center_desktopdir
)

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
  'regolith-control-center-print-renderer',
  'regolith-control-center-print-renderer.c',
  include_directories: [ top_inc ],
  dependencies: [ print_renderer_deps ],
  c_args: cflags,
  install: true,
  install_dir: control_center_libexecdir,
)

