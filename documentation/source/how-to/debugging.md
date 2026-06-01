# Debugging and Tracing

The OPC UA Device Support module provides several tools for debugging and tracing.

## IOC Shell Commands

Several commands are available for inspecting the state of the module:

*   `opcuaShow`:
    Prints information about sessions, subscriptions, items, and data elements.
    Supports glob patterns.
*   `opcuaShowSecurity`:
    Prints information about the security setup and discovered endpoints.
*   `opcuaConnect` / `opcuaDisconnect`:
    Manually manage session connections.

Example:
```
opcuaShow "mySession*" 1
```

## EPICS Record Tracing

You can enable tracing for specific EPICS records by setting the `TPRO` field to a value `>= 1`.

*   In `.db` file:
    `field(TPRO, "1")`
*   In IOC shell:
    `caput <PV_NAME>.TPRO 1`

## Using UaExpert (External Tool)

UaExpert is a powerful OPC UA client tool from Unified Automation that can be used to:
1.  Test connection to the OPC UA server.
2.  Browse the address space.
3.  Identify `NamespaceIndex` and `Identifier` (NodeID) for EPICS record configuration.

## Adaptive Concurrency Monitoring

```{versionadded} 0.11.0
Adaptive concurrency was introduced to automatically throttle requests based on server response times.
```

If adaptive concurrency is enabled, you can monitor its state using `opcuaShow`.
It will display current limits and performance metrics.
