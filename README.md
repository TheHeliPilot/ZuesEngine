# ZeusEngine<br><br>

**ZeusEngine** is a 2D game engine and editor built with **OpenGL**.<br>

---

## Features<br><br>

- **2D Rendering** — OpenGL-powered for high-performance graphics.<br>
- **Editor** — Built with ImGui for rapid development workflows.<br>
- **Networking** — Uses ENet for low-latency multiplayer networking.<br>
- **Modular Architecture** — Engine and editor are separated for flexibility.<br><br>

---

## Project Structure<br><br>

ZeusEngine/<br>
├── Engine/ # Core engine library<br>
├── Editor/ # Editor application<br>
├── MyGameProject/ # Example game application (The actual game executable)<br><br>

---

### Prerequisites<br><br>

- **CMake** (Installed and in your system's PATH)<br>
- **OpenGL** development libraries<br>
- A **C++ Compiler** (e.g., Visual Studio on Windows or CLion)

---

### 1. Windows (Visual Studio Workflow) ⚙️<br><br>

This is the standard approach for Windows, generating a Visual Studio solution file (`.sln`).<br><br>

1.  **Generate the Project Files:**<br>
    * Open the **Developer Command Prompt for VS** (or a similar environment).
    * Navigate to the repository root (`ZeusEngine` folder).
    * Tell CMake to generate the Visual Studio solution (e.g., for VS 2022) in a new `build` directory:
        ```bash
        cmake -B build -G "Visual Studio 17 2022"
        ```
        *(Adjust the generator string to match your installed Visual Studio version.)*
2.  **Build the Project:**
    * Use CMake to compile all targets in **Release** mode:
        ```bash
        cmake --build build --config Release
        ```

---

## Dependencies<br><br>

- OpenGL — Rendering backend<br>
- ImGui — Editor UI library<br>
- ENet — Networking library<br><br>

---

## License<br><br>

This project is licensed under a strict **Non-Commercial License**.<br>
See [LICENSE.md](./LICENSE.md) for details.
