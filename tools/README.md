# client_probe

Scope a RemoteInput port to a native game client before writing any of it.

The probe inspects a running client and answers the three questions that
decide the schedule:

1. **Which graphics API does it present frames with?** That names the function
   to hook for screen capture, and it is the largest single cost item.
2. **How does it read mouse and keyboard input?** That decides whether input
   takes days or weeks. It is the main schedule risk.
3. **Can a library be injected into it at all?** On macOS the hardened runtime
   can refuse the `dlopen` outright, which changes the whole design.

Standard library only. No install step. Python 3.8 or newer.

## Use

```sh
cd tools

python3 -m client_probe --list                 # find candidate processes
python3 -m client_probe --name runescape       # probe by name
python3 -m client_probe --pid 4242             # probe by process id
python3 -m client_probe --pid 4242 --json      # machine readable output
```

Run the client and let it draw a few frames first. A process that has not
rendered yet may not have loaded its graphics driver.

On macOS and Linux, `sudo` gives a fuller module list:

```sh
sudo python3 -m client_probe --pid 4242
```

## How it reads the target

The probe collects three signals and cross-checks them.

| Signal | Source | Strength |
|---|---|---|
| Loaded modules | `vmmap`, `/proc/<pid>/maps`, PowerShell | strongest — proves the API is in use |
| Imported symbols | `nm -u`, `otool -L`, `objdump -T` | strong |
| ASCII byte scan | the binary and any module shipped with the game | weakest — a lead, not proof |

It also reports the target architecture and whether the process runs under
Rosetta. The injected library must match the target, not the host: an x86_64
client on an Apple Silicon Mac needs `libRemoteInput-x86_64.dylib`.

The byte scan exists to catch a statically linked toolkit, which exports no
symbols and loads no module. It also produces false positives: a Chromium
client names every Vulkan entry point in its binary while it presents through
Metal. This is why a loaded module outranks a byte scan hit when the probe
picks a verdict.

## Reading the report

`confirmed` means a module and a symbol agree. `likely` means only one of the
two signals fired. `unknown` means nothing matched, and you should re-run with
`sudo` while the client is drawing.

The `WHAT TO DO NEXT` block turns the three verdicts into an ordered plan and
names the files in this repository that each step touches.

## Teaching it about a new client

Everything the probe looks for lives in `client_probe/signatures.py`. Add the
symbol names and the module name fragments for a new toolkit or graphics API
there. No other file needs to change.

## Layout

| File | Role |
|---|---|
| `signatures.py` | what to look for — the only file you normally edit |
| `model.py` | frozen value types |
| `parse.py` | pure parsers for the platform tool output |
| `classify.py` | pure verdict logic |
| `collect.py` | the impure layer that runs the platform tools |
| `report.py` | text rendering |
| `__main__.py` | command line entry point |

`parse.py` and `classify.py` hold every decision and are pure, so
`test_client_probe.py` covers them without touching a real process.

## Tests

```sh
cd tools && python3 -m unittest test_client_probe -v
```
