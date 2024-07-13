"""Represents the 'disconnect' DAP request."""

from __future__ import annotations

from typing import Any

import mqt.debug

from .dap_message import DAPMessage


class DisconnectDAPMessage(DAPMessage):
    """Represents the 'disconnect' DAP request."""

    message_type_name: str = "disconnect"

    def __init__(self, message: dict[str, Any]) -> None:
        """Initializes the 'DisconnectDAPMessage' instance.

        Args:
            message (dict[str, Any]): The object representing the 'disconnect' request.
        """
        super().__init__(message)

    def validate(self) -> None:
        """Validates the 'DisconnectDAPMessage' instance."""

    def handle(self, server: mqt.debug.DAPServer) -> dict[str, Any]:
        """Performs the action requested by the 'disconnect' DAP request.

        Args:
            server (mqt.debug.DAPServer): The DAP server that received the request.

        Returns:
            dict[str, Any]: The response to the request.
        """
        mqt.debug.destroy_ddsim_simulation_state(server.simulation_state)
        return super().handle(server)
