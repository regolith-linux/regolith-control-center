fs = import('fs')

panels_list += cappletname
desktop = 'regolith-@0@-panel.desktop'.format(cappletname)

i18n.merge_file(
  type: 'desktop',
  input: desktop + '.in',
  output: desktop,
  po_dir: po_dir,
  install: true,
  install_dir: control_center_desktopdir
)

sources = files(
  'cc-ubuntu-colors-row.c',
  'cc-ubuntu-panel.c',
  'cc-ubuntu-dock-dialog.c',
)

colors = {
  'default': '#E95420',
  'bark': '#787859',
  'sage': '#657B69',
  'olive': '#4B8501',
  'viridian': '#03875B',
  'prussiangreen': '#308280',
  'blue': '#0073E5',
  'purple': '#7764D8',
  'magenta': '#B34CB3',
  'red': '#DA3450',
}

gresource_xml_contents = []
resource_generated_data = []
resource_data = files(
  'assets/theme-dark.svg',
  'assets/theme-light.svg',
  'cc-ubuntu-colors-row.css',
  'cc-ubuntu-colors-row.ui',
  'cc-ubuntu-panel.ui',
  'cc-ubuntu-dock-dialog.ui',
)

foreach file: resource_data
  file_path = '@0@'.format(file)
  gresource_xml_contents += '<file alias="@0@" preprocess="@1@">@2@</file>'.format(
    (meson.project_source_root() / file_path).replace(meson.current_source_dir() + '/', ''),
    fs.name(file).endswith('.ui') or fs.name(file).endswith('.svg') ? 'xml-stripblanks' : '',
    meson.project_source_root() / file_path,
  )
endforeach

foreach color, code: colors
  asset_name = color + '.svg'
  asset_path = 'assets' / 'colors'
  color_asset = custom_target(
    asset_name,
    input: asset_path / 'color.svg.in',
    depfile: '@INPUT@',
    output: asset_name,
    command: [
      'sed',
      's/#00ff00/@0@/g'.format(code),
      '@INPUT@',
    ],
    capture: true,
  )

  gresource_xml_contents += (
    '<file alias="@0@" preprocess="xml-stripblanks">@1@</file>'.format(
    asset_path / asset_name, color_asset.full_path()))
  resource_generated_data += color_asset
endforeach

gresource_xml = configure_file(
  configuration: {
    'GENERATED_FILES': '\n'.join(gresource_xml_contents)
  },
  input: cappletname + '.gresource.xml',
  output: cappletname + '.generated.gresource.xml',
)

sources += gnome.compile_resources(
  'cc-' + cappletname + '-resources',
  gresource_xml,
  c_name: 'cc_' + cappletname,
  dependencies: resource_data + resource_generated_data,
  export: true
)

cflags += '-DGNOMELOCALEDIR="@0@"'.format(control_center_localedir)

panels_libs += static_library(
  cappletname,
  sources: sources,
  include_directories: [top_inc, common_inc],
  dependencies: common_deps,
  c_args: cflags
)
