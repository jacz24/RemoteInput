"""client_probe — scope a RemoteInput port to a native game client.

The probe answers three questions about a running client:

1. Which graphics API does it present frames with?
2. How does it read mouse and keyboard input?
3. Can a library be injected into it at all?

Run it with:  python3 -m client_probe --name <process name>
"""

__version__ = "1.0.0"
