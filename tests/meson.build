gtest_ver = '>=1.14.0'
gtest_dep = dependency('gtest', 
                       fallback : ['gtest', 'gtest_dep'], 
                       version : gtest_ver)
gmock_dep = dependency('gmock', 
                       fallback : ['gtest', 'gmock_dep'], 
                       version : gtest_ver)

tests_deps = [
    gtest_dep,
    gmock_dep,
]

tests_src = [
    'support/main.cpp',
    'support/ass_file_test_stubs.cpp',
    'support/util.cpp',

    'tests/access.cpp',
    'tests/ass_file_roundtrip.cpp',
    'tests/ass_override_ast.cpp',
    'tests/audio.cpp',
    'tests/cajun.cpp',
    'tests/calltip_provider.cpp',
    'tests/character_count.cpp',
    'tests/charset.cpp',
    'tests/color.cpp',
    'tests/colour_picker_model.cpp',
    'tests/dialogue_lexer.cpp',
    'tests/format.cpp',
    'tests/fs.cpp',
    'tests/hotkey.cpp',
    'tests/iconv.cpp',
    'tests/ifind.cpp',
    'tests/inline_string_encoding.cpp',
    'tests/karaoke_matcher.cpp',
    'tests/keyframe.cpp',
    'tests/line_iterator.cpp',
    'tests/line_wrap.cpp',
    'tests/mru.cpp',
    'tests/option.cpp',
    'tests/path.cpp',
    'tests/signals.cpp',
    'tests/split.cpp',
    'tests/syntax_highlight.cpp',
    'tests/thesaurus.cpp',
    'tests/theme.cpp',
    'tests/time.cpp',
    'tests/type_name.cpp',
    'tests/util.cpp',
    'tests/uuencode.cpp',
    'tests/vfr.cpp',
    'tests/word_split.cpp',
    'tests/ycbcr.cpp',
]

tests_src += files(
    '../src/ass_attachment.cpp',
    '../src/ass_dialogue.cpp',
    '../src/ass_entry.cpp',
    '../src/ass_file.cpp',
    '../src/ass_override.cpp',
    '../src/ass_override_ast.cpp',
    '../src/ass_parser.cpp',
    '../src/ass_style.cpp',
    '../src/colour_picker_model.cpp',
    '../src/subtitle_format_ass.cpp',
    '../src/text_file_reader.cpp',
    '../src/text_file_writer.cpp',
    '../src/theme_palette.cpp',
    '../src/version.cpp',
)

tests_inc = include_directories('support', '../src')

tests_args = []

# force UTF8 for MSVC
if host_machine.system() == 'windows'
    tests_args += ['/utf-8']
elif host_machine.system() == 'darwin'
    tests_args += ['-DGTEST_HAS_POSIX_RE=0']
endif

runner = executable(
    'gtest-main',
    tests_src,
    version_h,
    include_directories: [tests_inc, libaegisub_inc],
    dependencies: [tests_deps, deps],
    cpp_args: tests_args + conf_defines,
    link_with: libaegisub,
)

# setup test env
if host_machine.system() == 'windows'
    setup_sh = find_program('setup.bat')
else
    setup_sh = find_program('setup.sh')
endif

test_setup = custom_target('setup-test-data',
    output: 'applied_test_setup',
    command: setup_sh,
)

test('gtest main', runner, env: {'AEGISUB_TEST_DATA_DIR': meson.current_source_dir()}, depends: test_setup)
test('luajit-52', executable('luajit-52', 'tests/luajit_52.c', dependencies: luajit))
