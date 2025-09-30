# ZeusEngine

**ZeusEngine** is a 2D game engine and editor built with **OpenGL**.
It features a fast renderer, an intuitive editor UI, and networking support for multiplayer games.

---

## Features

- **2D Rendering** — OpenGL-powered for high-performance graphics.
- **Editor** — Built with ImGui for rapid development workflows.
- **Networking** — Uses ENet for low-latency multiplayer networking.
- **Modular Architecture** — Engine and editor are separated for flexibility.

---

## Project Structure

ZeusEngine/
├── Engine/ # Core engine library
├── Editor/ # Editor application
├── MyGameProject/ # Example game application (The actual game executable)

---

## Build Instructions (Cross-Platform)

These instructions use **CMake** to handle project generation for various operating systems.

### Prerequisites

- **CMake** (Installed and in your system's PATH)
- **OpenGL** development libraries
- A **C++ Compiler** (e.g., Visual Studio on Windows, GCC/Clang on Unix-like systems)

---

### 1. Windows (Visual Studio Workflow) ⚙️

This is the standard approach for Windows, generating a Visual Studio solution file (`.sln`).

1.  **Generate the Project Files:**
    * Open the **Developer Command Prompt for VS** (or a similar environment).
    * Navigate to the repository root (`ZeusEngine` folder).
    * Tell CMake to generate the Visual Studio solution (e.g., for VS 2022) in a new `build` directory:
        ```bash
        cmake -B build -G "Visual Studio 17 2022"
        ```
        *(Adjust the generator string to match your installed Visual Studio version).*
2.  **Build the Project:**
    * Use CMake to compile all targets in **Release** mode:
        ```bash
        cmake --build build --config Release
        ```
3.  **Run the Editor:**
    * The executable is located in the build output folder:
        ```bash
        .\build\Editor\Release\Editor.exe
        ```
4.  **Run Game App:**
    * The game executable is located here:
        ```bash
        .\build\MyGameProject\Release\MyGameProject.exe
        ```

---

### 2. Linux / macOS (Unix Makefiles Workflow) 🐧

These instructions use the default Unix Makefiles generator.

1.  **Build the Engine**
    ```bash
    cd Engine
    cmake .
    cmake --build .
    ```
2.  **Build the Editor**
    ```bash
    cd ../Editor
    cmake .
    cmake --build .
    ```
3.  **Run the Editor**
    ```bash
    ./Editor
    ```
4.  **Build & Run Game App**
    ```bash
    cd ../MyGameProject 
    cmake .
    cmake --build .
    ./MyGameProject
    ```
    *(Note: The game folder is named `MyGameProject` in the repository.)*

---

## Dependencies

- OpenGL — Rendering backend
- ImGui — Editor UI library
- ENet — Networking library

---

## License

This project is licensed under a strict **Non-Commercial License**.
See [LICENSE.md](./LICENSE.md) for details.
