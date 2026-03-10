from pathlib import Path
import re


COMMAND_DATA_PATH = Path(__file__).with_name("default_harmony_commands.h")
COMMAND_PATTERN = re.compile(
    r'^HARMONY_COMMAND\(\s*(0x[0-9A-Fa-f]+)\s*,\s*([0-2])\s*,\s*"([^"]+)"\s*\)\s*$'
)


def load_default_commands() -> list[dict]:
    commands: list[dict] = []
    for line_number, raw_line in enumerate(
        COMMAND_DATA_PATH.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = raw_line.strip()
        if not line or line.startswith("//") or line.startswith("#"):
            continue
        match = COMMAND_PATTERN.fullmatch(line)
        if match is None:
            raise ValueError(
                f"Invalid Harmony command definition in {COMMAND_DATA_PATH.name}:{line_number}"
            )
        command_id, command_type, name = match.groups()
        commands.append(
            {
                "id": int(command_id, 16),
                "type": int(command_type),
                "name": name,
            }
        )
    return commands


DEFAULT_COMMANDS = load_default_commands()

DEFAULT_COMMANDS_BY_ID = {entry["id"]: entry for entry in DEFAULT_COMMANDS}
VALID_HARMONY_CHANNELS = [5, 8, 14, 17, 32, 35, 41, 44, 62, 65, 71, 74]


def expand_payloads(name: str, command_type: int) -> list[str]:
    if command_type <= 1:
        return [name]
    return [
        f"{name}_clicked",
        f"{name}_double",
        f"{name}_multiple",
        f"{name}_long",
    ]


def normalize_override(command_id: int, override: dict) -> dict:
    base = DEFAULT_COMMANDS_BY_ID.get(command_id, {})
    name = override.get("name", base.get("name"))
    command_type = override.get("type", base.get("type"))
    if name is None or command_type is None:
        raise ValueError(
            "Overrides for unknown command IDs must provide both name and type"
        )
    return {"id": command_id, "name": name, "type": command_type}


def build_resolved_commands(overrides: list[dict]) -> list[dict]:
    resolved_by_id = {entry["id"]: dict(entry) for entry in DEFAULT_COMMANDS}
    ordered_ids = [entry["id"] for entry in DEFAULT_COMMANDS]

    for override in overrides:
        command_id = override["id"]
        normalized = normalize_override(command_id, override)
        if command_id not in resolved_by_id:
            ordered_ids.append(command_id)
        resolved_by_id[command_id] = normalized

    return [resolved_by_id[command_id] for command_id in ordered_ids]


def build_event_types(overrides: list[dict]) -> list[str]:
    event_types: list[str] = []
    for command in build_resolved_commands(overrides):
        for event_type in expand_payloads(command["name"], command["type"]):
            if event_type not in event_types:
                event_types.append(event_type)
    if "sleep" not in event_types:
        event_types.append("sleep")
    return event_types
