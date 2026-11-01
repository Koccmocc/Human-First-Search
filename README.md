# Human-first Search

Local university proof of concept. The current increment is the C++ crawler's
build and CLI scaffold. Crawling, HTML extraction and JSONL export are not
implemented yet. Development is currently independent of the team's Python/API/UI.

## Build and run

Run these commands from the repository root (`Search-Engine`):

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/crawler/hfs-crawler.exe --help
./build/debug/crawler/hfs-crawler.exe --version
```

Release uses the same sequence with the `release` preset. On Linux the executable
has no `.exe` suffix. CTest runs the executable for you on either platform.

Requirements: CMake 3.30+, Ninja, and GCC with C++20 support.
The initial environment uses MinGW-w64 GCC 15.2.0,
CMake 4.2.1 and Ninja 1.13.2. These tools must be on `PATH`; on Windows the GCC
`bin` directory must also remain on `PATH` when running executables so their
runtime DLLs can be found. Do not mix different MinGW installations.

Presets select `g++` from `PATH`, without machine-specific absolute paths.
All project targets use standard C++20 without GNU language extensions.
Git and network access are needed on first configuration to download the pinned
Lexbor HTML parser. It is built from C with `gcc`; the crawler uses `g++`.
See [the parser decision](docs/html-parser.md) for the version, license and offline override.

## Structure and decisions

### VS Code IntelliSense

Open `Search-Engine` as the workspace folder. The committed `.vscode/settings.json`
selects C++20 and reads compiler flags, defines and include paths from
`build/debug/compile_commands.json`. Run `cmake --preset debug` to generate it.
The compiler and public include directory are also specified as fallbacks for
headers that are not yet associated with a translation unit. System include paths
are discovered from `g++`; do not copy standard library headers into the project.

After changing editor settings, run **C/C++: Reset IntelliSense Database** from
the Command Palette. If diagnostics remain stale, run **Developer: Reload Window**.
Use **C/C++: Log Diagnostics** to check the selected compiler and language standard.
When opening a parent folder instead, its workspace settings must point to the
nested project's compilation database; nested `.vscode` settings are not applied.

### Source layout

- `CMakeLists.txt`: project version, required language standard and CTest setup.
- `CMakePresets.json`: separate Debug/Release directories and Ninja/GCC selection.
- `crawler/include/hfs/`: public interfaces of the crawler library.
- `crawler/src/cli.cpp`: argument handling, independent of console output.
- `crawler/src/main.cpp`: process entry point, output and exit codes.
- `crawler/tests/cli_test.cmake`: CLI checks against the built executable.
- `docs/liubomyr-plan.md`: scope, implementation stages and draft corpus contract.

`crawler_core` is a static library so future URL, extraction and export logic can
be tested without going through `main`. `CliResult` uses `std::variant` to hold
either a `CliAction` or a `CliError`, keeping success and failure mutually exclusive
without exceptions for ordinary argument errors. Console output uses standard
streams, requiring no additional terminal-support library.
Exit code `0` means success and `2` means invalid arguments;
no arguments display help. Diagnostics go to stderr, successful output to stdout.
Unsupported arguments fail rather than starting an unintended operation.

Warnings are enabled on our targets only. They are not treated as errors yet:
diagnostic changes between GCC versions should not prevent a fresh build.
CTest is available in both Debug and Release; checks do not rely on `assert`,
which is normally disabled in Release. `compile_commands.json` is generated in
each build directory for editor tooling. Local overrides belong in the ignored
`CMakeUserPresets.json`; generated builds and corpus data are also ignored.

Lexbor is currently exercised by a C++ compatibility test; the CLI still only
supports help and version. The next increment is a small HTML document wrapper,
followed by local extraction and JSONL export in separate changes.
