<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/sessionflow-lockup-dark.png">
    <img src="docs/assets/sessionflow-lockup-light.png" alt="sessionflow" width="420">
  </picture>
</p>

# Sessionflow

A free, open-source DAW for music production.

Early development. Stack: [docs/stack.md](docs/stack.md). Modules and contracts: [docs/architecture.md](docs/architecture.md).

## Build

Requires CMake 3.25+, Ninja and a C++20 compiler. On Windows, run from a Visual Studio developer shell.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

## License

[GPLv3](LICENSE)
