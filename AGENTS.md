# AGENTS.md

## Purpose
- This repository contains three main parts: `firmware/` (ESP-IDF C firmware), `mqtt_service/` (Python MQTT consumer), and `dashboard/` (Streamlit UI).
- Use the smallest change that solves the task; keep service boundaries intact.
- Prefer repo-local commands and files over global assumptions.
- No Cursor rules were found in `.cursor/rules/` or `.cursorrules`.
- No Copilot instructions were found in `.github/copilot-instructions.md`.

## Repository Layout
- `firmware/`: ESP-IDF project for Heltec WiFi LoRa 32 V2.
- `firmware/main/`: entrypoint with `app_main()`.
- `firmware/components/board/`: board pins, I2C, OLED power/reset helpers.
- `firmware/components/display/`: SSD1306 display driver and rendering.
- `firmware/components/dht11/`: DHT11 driver.
- `firmware/components/app_core/`: top-level runtime flow.
- `mqtt_service/main.py`: subscribes to TTN MQTT and writes CSV files into `data/`.
- `dashboard/app.py`: Streamlit dashboard that reads CSV files from `data/`.
- `docs/`: diagrams and history.
- `data/`: generated runtime data; treat as output, not source.

## Build And Run Commands

### Firmware
- Work from `firmware/`.
- First-time target selection: `idf.py set-target esp32`
- Build: `idf.py build`
- Flash and monitor: `idf.py flash monitor`
- Monitor only: `idf.py monitor`
- Clean build artifacts: `idf.py fullclean`
- Reconfigure after sdkconfig or component changes: `idf.py reconfigure`

### Dashboard
- Work from `dashboard/`.
- Create venv: `python -m venv .venv`
- Install deps: `.venv\Scripts\python -m pip install -r requirements.txt`
- Run app: `.venv\Scripts\python -m streamlit run app.py`

### MQTT Service
- Work from `mqtt_service/`.
- Create venv: `python -m venv .venv`
- Install deps: `.venv\Scripts\python -m pip install -r requirements.txt`
- Run service: `.venv\Scripts\python main.py`

## Test And Verification Commands
- There is no repo-configured automated test suite yet.
- There is no `pytest.ini`, `pyproject.toml`, `tests/`, or firmware Unity test target in this repo.
- There is no true single-test command today because no automated tests are checked in.
- For firmware changes, verify with `idf.py build`; use `idf.py flash monitor` when hardware behavior matters.
- For dashboard changes, do a syntax check with `.venv\Scripts\python -m py_compile app.py`.
- For MQTT service changes, do a syntax check with `.venv\Scripts\python -m py_compile main.py`.
- For broader Python syntax verification, run `.venv\Scripts\python -m compileall .` inside the relevant service directory.

## If You Add Tests Later
- Preferred Python test framework: `pytest`.
- Preferred single-file test command: `pytest path/to/test_file.py`
- Preferred single-test command: `pytest path/to/test_file.py -k test_name`
- Prefer deterministic unit tests around CSV parsing, payload decoding, and file-path handling.
- For firmware, keep hardware-dependent verification separate from any future host-side tests.

## Linting And Formatting Status
- No formatter or linter is configured in the repository.
- Do not invent new tooling in routine changes unless the task asks for it.
- Match the existing formatting style in the touched area.
- If you add a formatter or linter, document the command here and keep scope narrow.

## General Editing Rules
- Keep changes localized; avoid cross-service rewrites.
- Do not rename directories or move generated data paths unless the task requires it.
- Preserve relative paths between `mqtt_service/`, `dashboard/`, and `data/`.
- Prefer fixing root causes over adding wrappers or duplicate code.
- Avoid speculative abstractions; this codebase is still small.

## Python Conventions
- Follow PEP 8 unless the surrounding file clearly uses a different local pattern.
- Use 4-space indentation.
- Prefer UTF-8 source text, but keep new content ASCII unless Spanish text or existing content requires accents.
- Keep functions small and side effects obvious.
- Use `snake_case` for functions, variables, and module-level helpers.
- Use `UPPER_SNAKE_CASE` for module constants like paths, broker settings, and flags.
- Use descriptive names such as `data_folder`, `received_at`, `temperature_c`.
- Prefer f-strings over string concatenation for new code.
- Prefer `pathlib.Path` for new filesystem work, but do not churn stable code only to switch APIs.
- Group imports as standard library, third-party, then local imports.
- Within each import group, keep imports one per line unless the module already uses another pattern.
- Avoid unused imports; remove them when touching a file.
- Avoid wildcard imports.
- Add docstrings only when behavior is non-obvious or the function is reused across modules.
- Prefer explicit return values over implicit `None` in non-trivial helpers.
- Handle expected failures close to the source and surface actionable messages.
- For missing keys or optional payload fields, use guarded access patterns consistently.
- Use `with open(...)` for file I/O.
- Keep CSV column order stable once data is written to disk.
- Keep timezone handling explicit when writing timestamps.

## Python Error Handling
- Do not swallow exceptions silently.
- Catch specific exceptions such as `KeyError`, `FileNotFoundError`, or `json.JSONDecodeError` where relevant.
- Use Streamlit user-facing errors in `dashboard/` for recoverable UI problems.
- In service code, prefer logging plus clean exits over deep exception suppression.
- Never hardcode new secrets; prefer environment variables for credentials.
- Treat TTN credentials and broker settings as sensitive even if legacy code currently hardcodes them.

## Firmware C Conventions
- Follow the existing ESP-IDF style in `firmware/components/`.
- Use 4-space indentation and braces on the same line as the function or control statement.
- Keep header include order consistent: local header first, then C standard headers, then ESP-IDF/project headers.
- Use include guards in headers, matching existing names like `BOARD_CONFIG_H`.
- Use `snake_case` for functions and variables.
- Use short, descriptive prefixes by component, such as `board_`, `display_`, `dht11_`, `app_core_`.
- Use `UPPER_SNAKE_CASE` for macros and compile-time constants.
- Mark file-local symbols `static`.
- Prefer `const` where data should not change.
- Keep component APIs narrow and centered on `esp_err_t` return values.
- Validate pointer arguments in public functions.
- Use fixed-width integer types where width matters.
- Prefer small helpers over deeply nested logic.

## Firmware Error Handling
- Use `ESP_ERROR_CHECK(...)` for failures that should abort startup or cannot be recovered safely.
- Use `ESP_RETURN_ON_ERROR(...)` in helpers that should propagate `esp_err_t` upward.
- Log recoverable runtime failures with `ESP_LOGW` or `ESP_LOGE`.
- Use a file-local `static const char *TAG` for logging.
- Keep log messages compact and hardware-oriented.
- Preserve current behavior where boot failures fail fast and sensor read failures degrade gracefully.

## Naming And Architecture Notes
- `app_core` orchestrates startup and the main polling loop; keep business flow there.
- `board` owns pin definitions and board-level initialization.
- `display` owns SSD1306 buffer management and screen rendering.
- `dht11` owns protocol timing and sensor parsing.
- `mqtt_service` should remain the writer of CSV telemetry files.
- `dashboard` should remain a reader/presenter of stored data, not the MQTT subscriber.

## What To Verify Before Finishing
- If you changed firmware, run `idf.py build` from `firmware/`.
- If you changed Python, run `python -m py_compile` on the touched file from its service directory.
- If you changed paths or CSV schema, verify both `mqtt_service/` and `dashboard/` still agree on them.
- If you touched credentials or broker config, make sure no new secrets were introduced.

## Notes For Future Agents
- The repo mixes Spanish user-facing text with English identifiers; preserve the local context.
- Prefer incremental cleanup over repo-wide style passes.
- Document any newly introduced commands or rules by updating `AGENTS.md` in the same change.
