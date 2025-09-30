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

ZeusEngine/<br>
├── Engine/      # Core engine library  
├── Editor/      # Editor application  

---

## Build Instructions

### Prerequisites

- CMake  
- OpenGL development libraries  
- ENet library  
- ImGui library  

### Steps

1. **Build the Engine**  
   cd ZeusEngine/Engine  
   cmake .  
   cmake --build .  

2. **Build the Editor**  
   cd ZeusEngine/Editor  
   cmake .  
   cmake --build .  

3. **Run the Editor**  
   ./Editor  

4. **Build & Run Game App**  
   cd ZeusEngine/GameApp  
   cmake .  
   cmake --build .  
   ./GameApp  

---

## Dependencies

- OpenGL — Rendering backend  
- ImGui — Editor UI library  
- ENet — Networking library  

---

## License

This project is licensed under a strict **Non-Commercial License**.  
See [LICENSE.md](./LICENSE.md) for details.
