
# FeatherForge (v1.0)
> **A C++ IDE for simple library management for beginners**

FeatherForge is a native C++ integrated development environment built using OpenGL3 and Dear ImGui. I made it because i was tired of using Cmake and didn't even understand Cmake well when i was a begginer. I didn't know how to use it i couldnt comprehend WHY i needed it , might sound dumb but that's usually how beginners are.. i learned C++ to the point i made actual games that ran in the terminal but i didn't know how to use libraries.
Getting VS Code configured for C++ projects was a hurdle for me when I was starting out. i just couldn't get it running.I'm sure thats the case with most beginners. 

FeatherForge is a lightweight native C++ IDE for Linux focused on rapid prototyping and small projects. It is not intended to replace CMake or full-featured IDEs like CLion; instead, it is used when you just want to test a code or make smaller projects where you wouldn't need makefiles but are forced to make them because it includes libraries.

---

## Why FeatherForge?

| Feature | FeatherForge | Traditional Desktop Workflows |
| :--- | :--- | :--- |
| **Library Management** | Libraries placed directly inside lib/ or vendor/lib/ are detected automatically. | Requires manual target linking and configuration lines. |
| **Include Paths** | Discovers headers in include/ or vendor/ subdirectories automatically. | Manually map search paths and target directories. |
| **Error Diagnostics** | Runs background Clang syntax checks with inline markers. | Parsing raw terminal diagnostic outputs manually. |
| **Resource Footprint** | Native lightweight graphics framework. | Heavy multi-gigabyte platform ecosystems. |

![Main FeatherForge IDE interface showcasing a clean, dual-panel workspace layout](/compiletarget.png)

---

## Installation & Deployment

FeatherForge v1.0 is packaged natively for Linux systems as an AMD64 Debian binary. You can download and install the package directly through your terminal using either of the automated methods below.

### Option 1: Download via curl
```
curl -L [https://github.com/KinetiNode/Featherforge/raw/main/featherforge_1.0_amd64.deb](https://github.com/KinetiNode/Featherforge/raw/main/featherforge_1.0_amd64.deb) -o featherforge_1.0_amd64.deb

```

### Option 2: Download via wget

```
wget [https://github.com/KinetiNode/Featherforge/raw/main/featherforge_1.0_amd64.deb](https://github.com/KinetiNode/Featherforge/raw/main/featherforge_1.0_amd64.deb)

```

### Local Package Installation

Once downloaded, install the .deb package along with its necessary system dependencies using your package manager:

```
sudo apt update && sudo apt install ./featherforge_1.0_amd64.deb

```

---

## Building from Source

If you prefer to build the IDE directly from source, install the required development packages:

```
sudo apt install build-essential libglfw3-dev

```

Then compile the executable using the provided build script(you first need to clone the files using git clone):

```
chmod +x build.sh
./build.sh

```

The compiled binary will be generated in your local path directory:

```
./FeatherForge

```

---

## Environment Prerequisites

FeatherForge relies on industry-grade compiler pipelines natively present on your machine. On its initial initialization block, the Automated Onboarding Wizard will run system diagnostics to verify:

1. **GCC/G++** (Supporting C++17 or higher) - *Required for binary building.*
2. **Clang / Clang++** - *Optional, drives the on-demand smart linter.*

If any component is missing, the setup wizard will give you a command for linux to download the essentials.

> Note: GDB is not currently checked by the onboarding wizard or invoked anywhere in the build pipeline. The "Debug Build" toggle in Preferences compiles with `-g` debug symbols only — see Debug Builds below for what it does today, and the Roadmap for planned GDB integration.

---

## Project Directory Structure

FeatherForge replaces build scripts by utilizing strict directory conventions, resolved relative to whichever project folder you have open (not wherever the app happened to launch from). Organize your custom workspace folder exactly like this, and the IDE will infer everything dynamically:

```
your_project/
├── src/
│   ├── main.cpp            <-- Main entry point file
│   └── physics_core.cpp    <-- Additional source files (not supported yet,will not be compiled will be fixed in v2.0)
├── include/
│   └── raylib.h            <-- Drop external library headers here
├── lib/
│   └── libraylib.a         <-- Drop precompiled libraries (.a / .so) here
└── vendor/                 <-- Optional: header-only libraries or bundled SDKs
    └── some_lib/
        └── include/...

```

* **Includes:** `include/` is passed to the compiler and linter as a single `-I` search path. Headers directly inside `include/` can be included by bare filename; a header nested inside a subfolder needs its path written relative to `include/` in your `#include` statement (e.g. `#include "json/json.h"` for a file at `include/json/json.h`).
* **Vendor Includes (recursive):** Every subdirectory under `vendor/` is automatically discovered and added as its own `-I` path — handy for header-only libraries or SDKs that ship with their own internal folder structure.
* **Smart Auto-Linking:** Libraries placed directly inside `lib/` or `vendor/lib/` are detected automatically. The engine loops through these files and extracts their base configurations. If both a static (`.a`) and shared (`.so`) build of the same library are present, the static build is linked automatically, so your executable does not depend on an external `.so` file at runtime. Versioned shared libraries (e.g. `libraylib.so.6.0.0`) are also detected and linked directly by path if no unversioned asset is available. Library files that do not follow the traditional `lib`-prefix naming convention are still linked by full path rather than being skipped.

---

## Core Feature Guides

### Compile Target Locking

Currently, FeatherForge builds a single compile target. Projects with multiple `.cpp` files are planned for v2.0. To prevent the development environment from getting confused when navigating your workspace, you explicitly lock your program's entry point:

1. Locate the file containing your `int main()` function within the left Workspace Explorer sidebar.
2. Right-click the file and choose **Set as Compile Target** from the context layout.
3. A `[*]` visual signature will pin next to the filename.
4. You can now edit deeply nested headers or helper files, and hitting `F5` will reliably compile the correct entry target every time.

![ Right-clicking a file in the sidebar explorer and setting it as the locked compilation target](/compiletarget.png)

### Preferences Panel

FeatherForge keeps its core interface extremely minimal. Configuration preferences are isolated within a modular settings suite, which can be opened via **Settings > Preferences** or the `Ctrl + ,` shortcut.

* **Opt-In Clang Smart Linter:** Offloads real-time syntax checking to background worker threads using `std::async` and `std::future` to prevent editor lag.
* **Debug Builds (current behavior):** Toggling this and building with F5 compiles your target with `-g` debug symbols instead of `-O2` optimizations, so the resulting binary is ready to be attached to an external debugger (e.g. running `gdb ./build/yourprogram` yourself). It does not currently launch or wrap GDB automatically — see Roadmap.
* **Settings Persistence:** Checkbox changes take effect immediately in the running session, but are only written to a localized `featherforge.cfg` configuration file when you click **Save & Close**. Clicking **Cancel** discards unsaved changes and reloads the last saved values.

![Preferences popup modal displaying modular feature toggles for Clangd and GDB](/pref.png)

---

## Step-by-Step Workspace Tutorial

### 1. Opening a Project Environment

Launch FeatherForge from your system applications menu or your terminal. Go to the top file layout bar and select **File > Open Project...** The IDE calls a native `zenity` configuration portal allowing you to pick your project folder. The left sidebar explorer will dynamically generate a clean folder tree.

![ Native directory portal opening a codebase workspace into the tree hierarchy sidebar](/open.png)

### 2. Setting Your Execution Target

To prevent the editor from getting confused when compiling project code, right-click your entry class (the file containing `int main()`) in the workspace explorer sidebar and click **Set as Compile Target**. A `[*]` visual indicator will lock next to the name, designating it as the active build pipeline anchor.

### 3. Compilation & Error Routing

Press **F5** or click the **Compile & Run** action button on the main toolbar context. The IDE compiles your codebase and outputs logs to the base console.

If a compiler break occurs, the console window parses raw compiler streams using a regex layout filter:


```regex
^([^:\s]+):(\d+):(\d+):\s*(error|warning|note):\s*(.*)$
```

Any compiler break turns into a clickable, red UI selection object. **Double-click the red error line**, and the editor will automatically adjust viewport visibility, shift focuses, and snap your cursor exactly to the calculated row and character column where the bug lives.



### 4. Background Threaded Diagnostics

To activate real-time typing alerts without inducing lag into your interface frame loop, go to **Settings > Preferences** and activate the **Opt-In: Clang Smart Linter**. The engine will safely instantiate non-blocking background workers using `std::async` and `std::future` to track syntax markers dynamically as you write.

---

## Keyboard Shortcuts

| Key Shortcut | Operation Scope | System Output Behavior |
| --- | --- | --- |
| `Ctrl + S` | Editor Framework | Writes current text modifications to disk and triggers an asynchronous linter loop. |
| `Ctrl + ,` | Preferences Modal | Toggles system preferences and the linter module. |
| `F5` | Build Pipeline | Automates project saving, collects auto-linking binaries, and executes g++ compilation. |
New Folder creation is available via **File > New Folder** or right-clicking inside the sidebar — it does not currently have a dedicated keyboard shortcut.

if you put a non existing path in new file(/n/a/test.cpp) it will create those directories too.
---

## Current Limitations (v1.0)

FeatherForge is designed primarily for small C++ projects and rapid prototyping. Current limitations include:

### Build System

* Builds a single compile target instead of automatically compiling all source files.
* Full rebuild on every compilation loop; no incremental compilation checks.
* No CMake, Makefile, Meson, or Ninja configuration file imports.
* No custom compiler flag editor interface.
* Libraries are automatically detected only directly inside `lib/` and `vendor/lib/` directories.
* No code completion (LSP/clangd).
* Single editor view layout (no tabs or split workspace windows).
* Generates debug symbols only (`-g`).
* No integrated GDB debugging frontend console.
* No visual breakpoints manager.
* No variable inspector block.
* No call stack viewer tracking.
* Basic Git file staging and tracking support only.
* No branch management interface.
* No commit history log browser.
  
### Platform

* Linux support only.
* Windows and macOS variants are planned for subsequent cycles.

---

## Roadmap for v2.x

FeatherForge minor updates will be rolled out incrementally across minor semantic versions (v1.1, v1.2, etc.) rather than a single monolith release. The project target timeline plans to introduce:

* **Automatic Multi-File Builds:** System tracking to parse and link multiple project source blocks without manual definitions.
* **clangd/LSP Integration:** Adding intelligent code auto-completion, hover definitions, and structural formatting options.
* **GDB-Backed Crash Catcher:** Real GDB integration — automatically running builds under GDB, intercepting segmentation faults, and piping a formatted backtrace directly into the editor.
* **Project-Wide Search:** Global multi-file search and replace tooling filters.
* **Windows Toolchain Support:** Native structural support for Windows development architectures.
* **Better Git Integration:** Branch management controls and visual historical timelines.
* **Package/Dependency Management:** Integrated tools to fetch external libraries directly from source code repositories.
* **Manual Link-Type Override:** A per-library menu option to explicitly force static versus dynamic linking patterns for custom system paths.

---

## License

FeatherForge is open source under the MIT License.

The project bundles several third-party libraries (including Dear ImGui and TextEditor), which remain under their respective licenses within the `vendor/` directory.

---

## Technical Support

For technical bug tracking, ecosystem inquiries, or development ticket support, please submit a clear log export package to our dedicated product queue:

* **Official Developer Context Portal:** KinetiNode+support@proton.me
* **Official Website:** KinetiNode.pages.dev *(Note: The dedicated FeatherForge product page is currently under active development)*

*FeatherForge is curated and maintained under the KinetiNode development pipeline.*

