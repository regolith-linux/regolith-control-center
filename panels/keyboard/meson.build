panels_list += cappletname
desktop = 'regolith-@0@-panel.desktop'.format(cappletname)

desktop_in = configure_file(
  input: desktop + '.in.in',
  output: desktop + '.in',
  configuration: desktop_conf
)

i18n.merge_file(
  desktop,
  type: 'desktop',
  input: desktop_in,
  output: desktop,
  po_dir: po_dir,
  install: true,
  install_dir: control_center_desktopdir
)

pc_conf = configuration_data()
pc_conf.set('prefix', control_center_prefix)
pc_conf.set('datarootdir', control_center_datadir)
pc_conf.set('datadir', control_center_datadir)
pc_conf.set('PACKAGE', meson.project_name())
pc_conf.set('VERSION', meson.project_version())

pc = 'regolith-keybindings.pc'

configure_file(
  input: pc + '.in',
  output: pc,
  install: true,
  install_dir: join_paths(control_center_datadir, 'pkgconfig'),
  configuration: pc_conf
)

xml_files = [
  '00-multimedia.xml',
  '01-input-sources.xml',
  '01-launchers.xml',
  '01-screenshot.xml',
  '01-system.xml',
  '50-accessibility.xml'
]

foreach file: xml_files
  i18n.merge_file(
    file,
    input: file + '.in',
    output: file,
    po_dir: po_dir,
    data_dirs: its_dir,
    install: true,
    install_dir: join_paths(control_center_pkgdatadir, 'keybindings')
  )
endforeach

sources = files(
  'cc-alt-chars-key-dialog.c',
  'cc-keyboard-panel.c',
  'cc-keyboard-item.c',
  'cc-keyboard-manager.c',
  'cc-keyboard-option.c',
  'cc-keyboard-shortcut-editor.c',
  'wm-common.c',
  'keyboard-shortcuts.c'
)

resource_data = files(
  'enter-keyboard-shortcut.svg',
  'cc-keyboard-panel.ui',
  'cc-keyboard-shortcut-editor.ui',
)

sources += gnome.compile_resources(
  'cc-' + cappletname + '-resources',
  cappletname + '.gresource.xml',
  c_name: 'cc_' + cappletname,
  dependencies: resource_data,
  export: true
)

deps = common_deps + [
  gnome_desktop_dep,
  x11_dep
]

panels_libs += static_library(
  cappletname,
  sources: sources,
  include_directories: [top_inc, common_inc],
  dependencies: deps,
  c_args: cflags
)
