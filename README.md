# 3D Game Engine (Raylib & Lua)

A lightweight, modular 3D game engine and scene editor built in **C++20** using **Raylib 5.5** and **Lua 5.5** scripting. It features an integrated in-engine script editor, real-time debugging tools, and a standalone project export pipeline (.exe).

---

## Architecture & Project Structure

The Visual Studio solution (`3d_engine_cpp.slnx`) is organized into three main modules:

* **`EngineCore`**: Static library (`.lib`) containing the core engine systems, entity logic (`Player`, `Enemy`, `Coin`), camera controller, Lua scripting integration, and debug overlays.
* **`3d_engine-v1.0` (`cpp_3d`)**: The main 3D editor and runtime environment featuring block-based level editing, project serialization, and a built-in Lua script editor.
* **`Template_cpp_3d`**: Standalone runtime template executable (`Template.exe`) used by the editor's export pipeline to package playable builds.

---

## Features

* **3D Scene & Block Editor**: Create, arrange, and save 3D level layouts directly inside the engine.
* **Embedded Lua Scripting**: Write, edit, and execute Lua 5.5 scripts in real time with live context binding to game entities (`Player`, `Enemy`, `Coin`, `Camera`).
* **Standalone Game Export**: Automatically bundles level data (`assets.dat`), resources, and the compiled `Template.exe` runtime into a playable standalone build.
* **Built-in Debug Overlay**: Toggle real-time performance metrics, bounding box visualizations, and engine state diagnostics.

---

## Requirements & Dependencies

* **OS**: Windows 10 / 11 (x64)
* **IDE**: Visual Studio 2026 (Toolset `v145`, C++20 standard)
* **Libraries**:
  * [Raylib 5.5.0](https://raylib.com) (included via NuGet `packages.config`)
  * [Lua 5.5](https://lua.org) (included locally in `external/lua`)

---

## How to Build & Run

1. **Clone the repository:**
   ```bash
   git clone https://github.com
   cd Game-Engine-Raylib
   ```

2. **Open the solution:**
   Double-click `3d_engine_cpp.slnx` to open it in **Visual Studio 2026**.

3. **Select Configuration:**
   Set the solution configuration to **`Release` | `x64`** (or **`Debug` | `x64`**) in the top toolbar.

4. **Build the entire solution:**
   Press **`Ctrl + Shift + B`** (or go to `Build > Build Solution`).
   * *Note:* Building `Template_cpp_3d` automatically generates and copies `Template.exe` into `cpp_3d/template/` via a post-build event so the export feature works out of the box.

5. **Run the Engine:**
   Set **`3d_engine-v1.0`** as the Startup Project and press **`F5`** (or **`Ctrl + F5`**) to launch the editor.

---

## License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.
