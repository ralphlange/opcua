# Configuring an OPC UA Server (Siemens S7-1500 PLC)

This guide details the steps to configure a Siemens S7-1500 PLC as an OPC UA server using TIA Portal.

## Activate OPC UA Server

1.  Open your project in TIA Portal.
2.  Navigate to your PLC's properties (e.g., `PLC_1 [CPU 1518-4 PN/DP]`).
3.  Go to `General` > `OPC UA` > `Server` > `General`.
4.  Check the `Activate OPC UA server` box.
5.  Ensure that one of the `Server addresses` (`opc.tcp://...`) matches the IP address of your PLC on the Process Operator Network (PON).
    The default OPC UA port is 4840.

    **Note:**
    The OPC UA server must be accessible via one of the PLC's IPs on the PON.
    It cannot be accessed via a separate PLC Communications Processor (CP) module.

## Configure OPC UA Server Options

1.  In the PLC properties, navigate to `General` > `OPC UA` > `Server` > `Options`.
2.  **Port:**
    Adjust the `Port` number if a different port than the default 4840 is required.
3.  **Minimum sampling interval:**
    Defines the minimum rate (in ms) at which the server checks data sources for changes.
4.  **Minimum publishing interval:**
    Defines the minimum rate (in ms) at which the server sends updated data to clients.

    **Note:**
    For S7-1500 PLCs, especially mid-size ones (e.g., 1516), starting with 250ms intervals and grouping variables into structures is a good practice for performance.

## Configure OPC UA Server Security

1.  In the PLC properties, navigate to `General` > `OPC UA` > `Server` > `Security`.
2.  **Security Policies:**
    Select the desired security policy.
    For basic testing and development, you might select `No security`.
    For production environments, stronger options like `Basic128Rsa15-Sign & Encrypt` are recommended.
3.  **Guest Authentication:**
    If no specific user authentication is required, check `Enable guest authentication`.

    **Warning:**
    OPC UA Security is enabled by default.
    To connect to any OPC UA server *without* security, you must set the option `sec-mode=None` for the concerned session(s) in your EPICS IOC configuration.

## Configure OPC UA Runtime License

1.  In the PLC properties, navigate to `General` > `Runtime licenses` > `OPC UA`.
2.  Ensure the `Type of purchased license` is at least the same level as the `Type of required license` (e.g., `SIMATIC OPC UA S7-1500 large`).

## Define Variables in PLC Data Structures

Organize your PLC variables into data blocks that will be exposed as OPC UA nodes.

1.  **Create Data Blocks:**
    In TIA Portal, create new data blocks (e.g., `DB1`, `DB2`, `DB3`, `DB4`).
2.  **Define Variables:**
    Inside each data block, define the variables you want to expose.
    *   **CFGU (Configuration, Unmonitored):**
        Variables transmitted from IOC to PLC without readback.
        No subscription needed.
    *   **CFGM (Configuration, Monitored):**
        Variables transmitted from IOC to PLC with readback if values change.
        Requires a subscription.
    *   **STSV (State, Single Value):**
        Variables transmitted from PLC to IOC, each independently.
        Requires a subscription.
    *   **STST (State, Structured):**
        Variables transmitted from PLC to IOC as a whole block if any element changes.
        Requires a subscription.

### Example PLC Data Types and EPICS Record Type Compatibility

| PLC data type         | EPICS Record type           |
| :-------------------- | :-------------------------- |
| REAL                  | ai/ao                       |
| BOOL                  | bi/bo                       |
| BYTE/WORD/DWORD/INT/DINT | longin/longout, mbbi/mbbo, mbbiDirect/mbboDirect, ai/ao |
| STRUCT                | opcuaItem                   |

**Note:**
Structured items can only be handled by the IOC using the `opcuaItem` record type.
