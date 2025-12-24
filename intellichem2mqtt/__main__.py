"""Entry point for running Intellichem2MQTT as a module.

Usage:
    python -m intellichem2mqtt                    # Use env vars or defaults
    python -m intellichem2mqtt -c /path/to/config.yaml
    python -m intellichem2mqtt --help
"""

import argparse
import asyncio
import os
import sys
from pathlib import Path

from . import __version__
from .app import run_app
from .config import create_default_config, print_env_help, get_config


def main() -> int:
    """Main entry point.

    Returns:
        Exit code (0 for success, 1 for error)
    """
    parser = argparse.ArgumentParser(
        prog="intellichem2mqtt",
        description="Pentair IntelliChem to MQTT Bridge",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Docker/Environment variables (no config file needed):
  MQTT_HOST=192.168.1.100 MQTT_USERNAME=user MQTT_PASSWORD=pass intellichem2mqtt

  # Config file:
  intellichem2mqtt -c /etc/intellichem2mqtt/config.yaml
  intellichem2mqtt --generate-config > config.yaml

For more information, see: https://github.com/yourusername/intellichem2mqtt
        """,
    )

    parser.add_argument(
        "-c", "--config",
        default=None,
        help="Path to configuration file (optional if using env vars)",
    )
    parser.add_argument(
        "-v", "--version",
        action="version",
        version=f"%(prog)s {__version__}",
    )
    parser.add_argument(
        "--generate-config",
        action="store_true",
        help="Print default configuration and exit",
    )
    parser.add_argument(
        "--env-help",
        action="store_true",
        help="Print environment variable help and exit",
    )

    args = parser.parse_args()

    # Handle --generate-config
    if args.generate_config:
        print(create_default_config())
        return 0

    # Handle --env-help
    if args.env_help:
        print(print_env_help())
        return 0

    # Determine configuration source
    config_path = args.config
    using_env = os.environ.get("MQTT_HOST") is not None

    # If no config file specified and no env vars, check default location
    if not config_path and not using_env:
        default_paths = [
            "/etc/intellichem2mqtt/config.yaml",
            "/config/config.yaml",  # Docker default
            "config.yaml",
        ]
        for path in default_paths:
            if Path(path).exists():
                config_path = path
                break

    # Validate we have some configuration
    if not config_path and not using_env:
        print("Error: No configuration found.", file=sys.stderr)
        print("\nOptions:", file=sys.stderr)
        print("  1. Set MQTT_HOST environment variable (and other env vars)", file=sys.stderr)
        print("  2. Create a config file: intellichem2mqtt --generate-config > config.yaml", file=sys.stderr)
        print("  3. Specify config path: intellichem2mqtt -c /path/to/config.yaml", file=sys.stderr)
        print("\nFor environment variable help: intellichem2mqtt --env-help", file=sys.stderr)
        return 1

    # Log configuration source
    if using_env:
        print(f"Using environment variable configuration (MQTT_HOST={os.environ.get('MQTT_HOST')})")
    elif config_path:
        print(f"Using configuration file: {config_path}")

    # Run the application
    try:
        asyncio.run(run_app(config_path))
        return 0
    except KeyboardInterrupt:
        print("\nInterrupted by user")
        return 0
    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
