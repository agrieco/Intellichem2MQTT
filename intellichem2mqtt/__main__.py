"""Entry point for running Intellichem2MQTT as a module.

Usage:
    python -m intellichem2mqtt
    python -m intellichem2mqtt -c /path/to/config.yaml
    python -m intellichem2mqtt --help
"""

import argparse
import asyncio
import sys
from pathlib import Path

from . import __version__
from .app import run_app
from .config import create_default_config


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
  intellichem2mqtt                     Run with default config
  intellichem2mqtt -c /etc/intellichem2mqtt/config.yaml
  intellichem2mqtt --generate-config   Print default config

For more information, see: https://github.com/yourusername/intellichem2mqtt
        """,
    )

    parser.add_argument(
        "-c", "--config",
        default="/etc/intellichem2mqtt/config.yaml",
        help="Path to configuration file (default: /etc/intellichem2mqtt/config.yaml)",
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

    args = parser.parse_args()

    # Handle --generate-config
    if args.generate_config:
        print(create_default_config())
        return 0

    # Check config file exists
    config_path = Path(args.config)
    if not config_path.exists():
        print(f"Error: Configuration file not found: {config_path}", file=sys.stderr)
        print(f"\nTo create a default configuration file:", file=sys.stderr)
        print(f"  mkdir -p {config_path.parent}", file=sys.stderr)
        print(f"  intellichem2mqtt --generate-config > {config_path}", file=sys.stderr)
        return 1

    # Run the application
    try:
        asyncio.run(run_app(str(config_path)))
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
