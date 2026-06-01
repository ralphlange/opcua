# Record Types and Options

## Compatible Record Types

The following standard EPICS record types are supported:

| Record Type | Support |
| :--- | :--- |
| `ai` / `ao` | Analog input/output |
| `bi` / `bo` | Binary input/output |
| `longin` / `longout` | 32-bit integer |
| `int64in` / `int64out` | 64-bit integer (added in v0.4.0) |
| `mbbi` / `mbbo` | Multi-bit binary input/output |
| `mbbiDirect` / `mbboDirect` | Multi-bit binary direct |
| `stringin` / `stringout` | String data |
| `waveform` | Array data |

### Custom Record Types
*   `opcuaItem`:
    Used for structured data (added in v0.3.0).

## Record Link Options

Options can be added to the `INP` or `OUT` fields of records using the OPC UA DTYP.

| Option | Default | Description |
| :--- | :--- | :--- |
| `sampling` | `-1` | Sampling interval in ms (-1 = use publishing interval). |
| `qsize` | `1` | Server-side queue size. |
| `cqsize` | `1.5 * qsize` | Client-side queue size (min 3). |
| `discard` | `old` | Discard policy: `old` (drop oldest), `new` (drop newest). |
| `register` | `n` | Register item with server for performance (`y`/`n`). |
| `timestamp`| `server` | Source of timestamp: `server`, `source`. |
| `monitor` | `y` | Enable monitoring. For outputs, enables readback. |
| `bini` | `read` | Behavior at init: `read`, `ignore`, `write`. |
| `element` | | Path to element inside an `opcuaItem` structure. |
