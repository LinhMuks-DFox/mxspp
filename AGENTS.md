# Repository Guidelines

## Project Structure & Module Organization
C++ sources sit in `src/` (frontend, core, backend, jit, runtime, driver). Public headers mirror this in `include/mxspp/`. Third-party payloads live in `lib/`, CMake outputs land in `build/bin/`, and runnable language samples are under `example/examples/`. See `docs/Architecture.md` for a pipeline overview before touching cross-module flows.

## Build, Test, and Development Commands
- `python3 download_dep.py` – vendor the pinned LLVM toolchain and PEGTL into `lib/`.
- `python3 rebuild.py --clean` – remove `build/` + `bin/`, reconfigure with Ninja, then compile from scratch.
- `python3 rebuild.py` – incremental rebuild; regenerates `compile_commands.json` if the tree lacks `build.ninja`.
- `python3 before_commit.py --staged` – run formatters, linters, and auxiliary checks, writing the summary to `check_result.md`.
- `build/bin/mxspp example/examples/hello_world.mxs` – smoke-test the runtime after changes.

## Coding Style & Naming Conventions
C++ uses the root `.clang-format` (WebKit base, 4 spaces, 90 columns, right-aligned pointers). Keep exported types prefixed with `MX` (`MXObject`, `MXType`) and apply camelCase for methods and functions inside namespaces. Python tooling must satisfy `black`, `ruff`, and `mypy --strict`; maintain snake_case names and avoid side-effects at import time.

## Testing Guidelines
There is no dedicated unit harness yet, so lean on integration checks. Extend `example/examples/` with focused `.mxs` snippets that exercise new semantics and run them through the built `mxspp` binary. When touching runtime or JIT code, add defensive assertions in `src/core` and use a clean rebuild to refresh `compile_commands.json` for tooling and clang-tidy runs.

## Commit & Pull Request Guidelines
Keep commits scoped and use imperative subjects (`Add driver hook`, `Update build config`); add a short body when context is non-obvious. Reference the touched modules in the subject where it helps reviewers. Pull requests should outline intent, link issues, list executed commands, and attach relevant logs or screenshots for behavioral deltas. Always run `before_commit.py` before requesting review.

## Development Environment Tips
Use the Docker workflow in `docker-dev/` (`docker-compose up -d`, then `docker-compose exec mxspp-dev /bin/bash`) when local dependencies are unavailable. Ensure `ninja` is on PATH ahead of any build; the helper scripts hard-require it. Do not commit vendor artifacts from `lib/`; only curated metadata belongs in version control.
